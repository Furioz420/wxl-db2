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
        compact::MaterialIndex g_mridIndex;
        compact::StemIndex g_modelIdByStem;

        std::once_flag g_once;
        bool           g_ready = false;

        void LoadTables()
        {
            g_tex.Load("TextureFilePath.db2");
            g_model.Load("ModelFilePath.db2");

            // Only the compact material triples survive initialization.
            decl::Table g_texData;
            std::string texDataError;
            if (!g_texData.Load(kTextureFileData, &texDataError))
                WLOG_WARN("db2-fdid: TextureFileData did not decode (%s); every lookup keyed on a"
                          " material resource id will resolve to nothing",
                          texDataError.empty() ? "no reason given" : texDataError.c_str());

            const size_t usage = g_texData.FieldIndex("UsageType");
            const size_t mrid  = g_texData.FieldIndex("MaterialResourcesID");
            g_mridIndex.Reserve(g_texData.Rows().size());
            for (const decl::wdc5::Row& row : g_texData.Rows())
                g_mridIndex.Add(g_texData.Value(row, mrid), g_texData.Value(row, usage), row.id);
            g_mridIndex.Finish();

            g_modelIdByStem.Reserve(g_model.RowCount());
            for (uint32_t i = 0; i < g_model.RowCount(); ++i)
            {
                const PathRow* r = g_model.At(i);
                if (!r) continue;
                const char* path = g_model.Str(static_cast<uint32_t>(r->path));
                // First spelling wins: two rows collapsing to one stem differ only by container
                // extension, and either id resolves to the same asset for every consumer here.
                if (path && *path) g_modelIdByStem.Add(path, r->id);
            }

            g_modelIdByStem.Finish();
            WLOG_INFO("db2-fdid-compact-v1: stem_bytes=%zu material_bytes=%zu",
                g_modelIdByStem.Bytes(), g_mridIndex.Bytes());

            WLOG_INFO("db2-fdid: loaded texpath=%u model=%u texdata=%zu (MRID rows=%zu, model stems=%zu)",
                g_tex.RowCount(), g_model.RowCount(), g_texData.Rows().size(), g_mridIndex.Size(),
                g_modelIdByStem.Size());

            g_ready = g_tex.RowCount() != 0 || g_model.RowCount() != 0;
            if (!g_ready)
                WLOG_WARN("db2-fdid: no path tables loaded; FileDataID resolution disabled");
        }

        uint32_t MridToFdid(uint32_t mrid, uint32_t want)
        {
            return g_mridIndex.Find(mrid, want);
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

        return g_modelIdByStem.Find(modelPath);
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
