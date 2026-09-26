// FileDataID -> path resolution, backed by the client's TextureFilePath/ModelFilePath tables and
// TextureFileData material graph, all read through the mounted archive set.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <cstdint>
#include <string>

namespace wxl::db2::fdid
{
    // Loads the resolution tables. Lazy: the first call to Resolve*() triggers it. Safe to call
    // more than once; only the first does the work.
    void EnsureLoaded();

    // FileDataID -> file path, cached (a node's c_str() is stable for the process once resolved,
    // which is what lets this cross the WXL_Db2Api/WXL_FdidApi boundary as a raw pointer). Textures
    // and models share one FileDataID space, so either kind resolves through the same call.
    const char* ResolveTexture(uint32_t fileDataId);
    const char* ResolveModel(uint32_t fileDataId);

    uint32_t ResolveModelId(const char* modelPath);

    // MaterialResourcesID (+ texture-type hint) -> .blp path, via TextureFileData.
    bool ResolveMaterial(uint32_t mrid, uint32_t typeHint, std::string& outPath);

    const char* ResolveMaterialTexture(uint32_t mrid, uint32_t typeHint);
}
