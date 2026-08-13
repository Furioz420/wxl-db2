// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "FdidResolver.hpp"

#include "../api/Db2.hpp"
#include "../decode/DB2File.hpp"
#include "../ExtensionApi.hpp"

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxl::db2::fdid
{
    // Forward declaration: defined below (after the cache it feeds), used by Resolve() inside the
    // anonymous namespace above that point.
    bool ResolveFile(uint32_t fileDataId, std::string& outPath);

    namespace
    {
        // Both *FilePath.db2 decode to {uint32 id; int32 path}.
        struct PathRow { uint32_t id; int32_t path; };

        namespace decl = wxl::runtime::db2;

        constexpr decl::Field kTextureFileDataFields[] = {
            { "ID" }, { "UsageType" }, { "MaterialResourcesID" },
        };
        constexpr decl::Definition kTextureFileData{
            "texturefiledata", "DBFilesClient\\texturefiledata.db2", 0xBD7C74C2u,
            kTextureFileDataFields,
        };

        wxl::features::db2::DB2Table<PathRow> g_tex;
        wxl::features::db2::DB2Table<PathRow> g_model;
        decl::Table                          g_texData;
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> g_mridIndex;
        std::unordered_map<std::string, uint32_t> g_modelIdByStem;

        std::once_flag g_once;
        bool           g_ready = false;

        /// Lowercases, folds forward slashes to backslashes and drops any extension, so the one
        /// spelling the path table stores and the many a caller can hold collapse to the same key.
        std::string NormalizeStem(const char* path)
        {
            std::string s;
            for (const char* p = path; *p; ++p)
            {
                const char c = *p;
                s.push_back(c == '/' ? '\\'
                                     : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            const size_t dot = s.find_last_of('.');
            const size_t sep = s.find_last_of('\\');
            if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) s.resize(dot);
            return s;
        }

        void LoadTables()
        {
            g_tex.Load("TextureFilePath.db2");
            g_model.Load("ModelFilePath.db2");

            std::string texDataError;
            if (!g_texData.Load(kTextureFileData, &texDataError))
                WLOG_WARN("db2-fdid: TextureFileData did not decode (%s); every lookup keyed on a"
                          " material resource id will resolve to nothing",
                          texDataError.empty() ? "no reason given" : texDataError.c_str());

            const size_t usage = g_texData.FieldIndex("UsageType");
            const size_t mrid  = g_texData.FieldIndex("MaterialResourcesID");
            for (const decl::wdc5::Row& row : g_texData.Rows())
                g_mridIndex[g_texData.Value(row, mrid)].push_back(
                    { g_texData.Value(row, usage), row.id });

            g_modelIdByStem.reserve(g_model.RowCount());
            for (uint32_t i = 0; i < g_model.RowCount(); ++i)
            {
                const PathRow* r = g_model.At(i);
                if (!r) continue;
                const char* path = g_model.Str(static_cast<uint32_t>(r->path));
                // First spelling wins: two rows collapsing to one stem differ only by container
                // extension, and either id resolves to the same asset for every consumer here.
                if (path && *path) g_modelIdByStem.emplace(NormalizeStem(path), r->id);
            }

            WLOG_INFO("db2-fdid: loaded texpath=%u model=%u texdata=%zu (MRID index=%zu, model stems=%zu)",
                g_tex.RowCount(), g_model.RowCount(), g_texData.Rows().size(), g_mridIndex.size(),
                g_modelIdByStem.size());

            g_ready = g_tex.RowCount() != 0 || g_model.RowCount() != 0;
            if (!g_ready)
                WLOG_WARN("db2-fdid: no path tables loaded; FileDataID resolution disabled");
        }

        uint32_t MridToFdid(uint32_t mrid, uint32_t want)
        {
            auto it = g_mridIndex.find(mrid);
            if (it == g_mridIndex.end()) return 0;
            for (uint32_t target : { want, 2u })
                for (const auto& c : it->second)
                    if (c.first == target) return c.second;
            return it->second[0].second;
        }

        // fdid -> resolved path; an empty string is a confirmed miss (cached too, so a texture shared
        // by hundreds of tiles is only ever looked up once). Node pointers are stable across rehash.
        std::mutex                                g_cacheMutex;
        std::unordered_map<uint32_t, std::string> g_cache;

        const char* Resolve(uint32_t fdid)
        {
            if (fdid == 0) return nullptr;
            EnsureLoaded();

            std::lock_guard<std::mutex> lock(g_cacheMutex);
            auto it = g_cache.find(fdid);
            if (it == g_cache.end())
            {
                std::string path;
                ResolveFile(fdid, path); // leaves path empty on a miss
                it = g_cache.emplace(fdid, std::move(path)).first;
            }
            return it->second.empty() ? nullptr : it->second.c_str();
        }
    }

    void EnsureLoaded()
    {
        std::call_once(g_once, LoadTables);
    }

    const char* ResolveTexture(uint32_t fileDataId) { return Resolve(fileDataId); }
    const char* ResolveModel(uint32_t fileDataId)   { return Resolve(fileDataId); }

    uint32_t ResolveModelId(const char* modelPath)
    {
        if (!modelPath || !*modelPath) return 0;
        EnsureLoaded();
        if (!g_ready) return 0;

        const auto it = g_modelIdByStem.find(NormalizeStem(modelPath));
        return it == g_modelIdByStem.end() ? 0 : it->second;
    }

    bool ResolveFile(uint32_t fileDataId, std::string& outPath)
    {
        EnsureLoaded();
        if (!g_ready) return false;

        const PathRow* r = g_tex.Find(static_cast<int32_t>(fileDataId));
        const char* path = r ? g_tex.Str(static_cast<uint32_t>(r->path)) : nullptr;
        if (!path || !*path)
        {
            r = g_model.Find(static_cast<int32_t>(fileDataId));
            path = r ? g_model.Str(static_cast<uint32_t>(r->path)) : nullptr;
        }
        if (!path || !*path) return false;
        outPath = path;
        return true;
    }

    bool ResolveMaterial(uint32_t mrid, uint32_t typeHint, std::string& outPath)
    {
        EnsureLoaded();
        if (!g_ready) return false;

        const uint32_t fdid = MridToFdid(mrid, typeHint);
        if (!fdid) return false;
        const PathRow* r = g_tex.Find(static_cast<int32_t>(fdid));
        const char* path = r ? g_tex.Str(static_cast<uint32_t>(r->path)) : nullptr;
        if (!path || !*path) return false;
        outPath = path;
        return true;
    }

    const char* ResolveMaterialTexture(uint32_t mrid, uint32_t typeHint)
    {
        EnsureLoaded();
        if (!g_ready) return nullptr;

        const uint32_t fdid = MridToFdid(mrid, typeHint);
        return fdid ? Resolve(fdid) : nullptr;
    }
}
