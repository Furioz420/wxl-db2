// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "FdidResolver.hpp"

#include "../decode/DB2File.hpp"
#include "../ExtensionApi.hpp"

#include <mutex>
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
        // TextureFileData.db2 -> {FileDataID, MaterialResourcesID, textureType, relMRID}.
        struct TexDataRow { uint32_t fileDataId; uint32_t materialResId; uint32_t textureType; uint32_t rel; };

        wxl::features::db2::DB2Table<PathRow>    g_tex;
        wxl::features::db2::DB2Table<PathRow>    g_model;
        wxl::features::db2::DB2Table<TexDataRow> g_texData;
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> g_mridIndex;

        std::once_flag g_once;
        bool           g_ready = false;

        void LoadTables()
        {
            g_tex.Load("TextureFilePath.db2");
            g_model.Load("ModelFilePath.db2");
            g_texData.Load("TextureFileData.db2");

            for (uint32_t i = 0; i < g_texData.RowCount(); ++i)
            {
                const TexDataRow* r = g_texData.At(i);
                if (r) g_mridIndex[r->materialResId].push_back({ r->textureType, r->fileDataId });
            }

            WLOG_INFO("db2-fdid: loaded texpath=%u model=%u texdata=%u (MRID index=%zu)",
                g_tex.RowCount(), g_model.RowCount(), g_texData.RowCount(), g_mridIndex.size());

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
}
