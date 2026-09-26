// Optional localized-string overlays for WXL custom WDC5 tables.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "Db2.hpp"

#include <cstdint>
#include <string>

namespace wxl::runtime::db2::localization
{
    enum class Status
    {
        Missing,
        Applied,
        Invalid,
    };

    struct Result
    {
        Status status = Status::Missing;
        uint32_t matchedRows = 0;
        uint32_t appliedCells = 0;
        std::string archivePath;
        std::string locale;
        std::string build;
        std::string error;
    };

    Result Apply(const Definition& definition, wdc5::Table& table);
}
