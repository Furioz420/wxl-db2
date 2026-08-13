// The ModelFileData table: the per-model facts recorded beside a model rather than inside it.
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

#pragma once

#include <cstdint>
#include <string>

namespace wxl::runtime::db2 { class Table; }

namespace wxl::runtime::db2::model
{
    /// One decoded ModelFileData row, every column named.
    struct ModelFileData
    {
        uint32_t fileDataId       = 0;
        uint32_t flags            = 0;
        uint32_t lodCount         = 0; ///< reduced skin profiles beyond the base one; 0 = none
        uint32_t modelResourcesId = 0;
        float    geoBox[6]{};
    };

    /// How the load went, for the log and the published status calls.
    struct StoreStatus
    {
        bool        loaded = false;
        bool        failed = false;
        std::string error;
        uint32_t    rows = 0;
    };

    /// Triggers the load on first use.
    const StoreStatus& Status();

    /// @return false when the table is absent from this deployment or holds no row for this id.
    bool Lookup(uint32_t fileDataId, ModelFileData& out);

    /// Convenience over Lookup for the one column most consumers want. 0 on any miss, which reads
    /// the same as "this model ships no reduced profiles" on purpose: both mean "do not degrade it".
    uint32_t LodCount(uint32_t fileDataId);

    /// Raw table access for columns beyond ModelFileData. Null until loaded.
    const Table* Get();
}
