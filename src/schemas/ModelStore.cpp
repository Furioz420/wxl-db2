// The ModelFileData table, loaded whole on first use and answered by FileDataID.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// EVERY COLUMN IS DECLARED, the same rule LightStore.cpp follows: a column nobody consumes yet is
// still data, and dropping it from the declaration would silently shift every field after it.
//
// FileDataID is this table's own id column, stored inline among the fields rather than in a separate
// id list, so it is declared here like any other field AND is what Find() keys on. The decoder
// cross-checks the declared field count against the file, so a build whose layout no longer matches
// this list refuses to load instead of mis-reading it.

#include "ModelStore.hpp"
#include "../ExtensionApi.hpp"
#include "../api/Db2.hpp"

#include <bit>
#include <mutex>

namespace
{
    using namespace wxl::runtime::db2;

    constexpr Field kModelFileDataFields[] = {
        { "GeoBox", 6 }, { "FileDataID" }, { "Flags" }, { "LodCount" }, { "ModelResourcesID" },
    };

    constexpr Definition kDefinition{
        "modelfiledata", "ModelFileData.db2", 0, kModelFileDataFields,
    };

    model::StoreStatus g_status;
    Table              g_table;
    std::once_flag     g_loadOnce;

    void LoadOnce()
    {
        std::string error;
        if (!g_table.Load(kDefinition, &error))
        {
            g_status.failed = true;
            g_status.error  = error;
            WLOG_WARN("db2-modeldata: %s", error.c_str());
            return;
        }
        g_status.loaded = true;
        g_status.rows   = static_cast<uint32_t>(g_table.Rows().size());
        WLOG_INFO("db2-modeldata: ModelFileData loaded (%u rows, layout=0x%08x)",
                  g_status.rows, g_table.LayoutHash());
    }

    void EnsureLoaded() { std::call_once(g_loadOnce, LoadOnce); }
}

namespace wxl::runtime::db2::model
{
    const StoreStatus& Status()
    {
        EnsureLoaded();
        return g_status;
    }

    bool Lookup(uint32_t fileDataId, ModelFileData& out)
    {
        EnsureLoaded();
        if (!g_status.loaded || fileDataId == 0) return false;

        const wdc5::Row* row = g_table.Find(fileDataId);
        if (!row) return false;

        out.fileDataId       = g_table.Value(*row, "FileDataID");
        out.flags            = g_table.Value(*row, "Flags");
        out.lodCount         = g_table.Value(*row, "LodCount");
        out.modelResourcesId = g_table.Value(*row, "ModelResourcesID");
        for (size_t i = 0; i < 6; ++i)
            out.geoBox[i] = std::bit_cast<float>(g_table.Value(*row, "GeoBox", i));
        return true;
    }

    uint32_t LodCount(uint32_t fileDataId)
    {
        ModelFileData row;
        return Lookup(fileDataId, row) ? row.lodCount : 0;
    }

    const Table* Get()
    {
        EnsureLoaded();
        return g_status.loaded ? &g_table : nullptr;
    }
}
