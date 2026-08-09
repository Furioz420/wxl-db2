// FileDataID -> path resolution, backed by the client's own TextureFilePath/ModelFilePath/
// TextureFileData DB2 tables (same WDC1/2/3 decode as DB2File.hpp/Db2Decode.hpp), read through the
// client's own storage seam -- every table this needs is already in the client's own archive set
// (see DB2File::Load).
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

    // MaterialResourcesID (+ texture-type hint) -> .blp path, via TextureFileData. Not part of the
    // published ABI surface today (nothing outside wxl-db2 needs it yet); kept for parity with the
    // host resolver it replaces.
    bool ResolveMaterial(uint32_t mrid, uint32_t typeHint, std::string& outPath);
}
