// Optional localized-string overlays for WXL custom WDC5 tables.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "LocalizationCsv.hpp"

#include "game/Io.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wxl::runtime::db2::localization
{
    namespace
    {
        struct Change
        {
            uint32_t rowId = 0;
            size_t field = 0;
            std::string value;
        };

        Result Invalid(Result result, std::string message)
        {
            result.status = Status::Invalid;
            result.error = std::move(message);
            return result;
        }

        bool ReadArchiveFile(const std::string& path, std::vector<uint8_t>& bytes)
        {
            void* handle = nullptr;
            if (!wxl::game::io::FileOpen(path.c_str(), wxl::game::io::kOpenWholeFile, &handle) ||
                !handle)
                return false;
            struct Lease
            {
                void* handle = nullptr;
                ~Lease() { if (handle) wxl::game::io::FileClose(handle); }
            } lease{handle};
            uint32_t high = 0;
            const uint32_t size = wxl::game::io::FileSize(handle, &high);
            if (!size || high) return false;
            bytes.resize(size);
            uint32_t read = 0;
            if (!wxl::game::io::FileRead(handle, bytes.data(), size, &read) || read != size)
            {
                bytes.clear();
                return false;
            }
            return true;
        }

        bool ValidUtf8(std::string_view value)
        {
            for (size_t i = 0; i < value.size();)
            {
                const uint8_t first = static_cast<uint8_t>(value[i]);
                if (first < 0x80) { ++i; continue; }
                size_t count = 0;
                uint32_t codepoint = 0;
                if ((first & 0xE0) == 0xC0) { count = 2; codepoint = first & 0x1F; }
                else if ((first & 0xF0) == 0xE0) { count = 3; codepoint = first & 0x0F; }
                else if ((first & 0xF8) == 0xF0) { count = 4; codepoint = first & 0x07; }
                else return false;
                if (i + count > value.size()) return false;
                for (size_t j = 1; j < count; ++j)
                {
                    const uint8_t next = static_cast<uint8_t>(value[i + j]);
                    if ((next & 0xC0) != 0x80) return false;
                    codepoint = (codepoint << 6) | (next & 0x3F);
                }
                if ((count == 2 && codepoint < 0x80) ||
                    (count == 3 && codepoint < 0x800) ||
                    (count == 4 && codepoint < 0x10000) ||
                    codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                    return false;
                i += count;
            }
            return true;
        }

        bool ParseCsv(std::string_view text,
                      const std::function<bool(std::vector<std::string>&)>& consume,
                      std::string& error)
        {
            std::vector<std::string> row;
            std::string field;
            bool quoted = false;
            bool quoteClosed = false;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char ch = text[i];
                if (quoted)
                {
                    if (ch == '"')
                    {
                        if (i + 1 < text.size() && text[i + 1] == '"')
                        {
                            field.push_back('"');
                            ++i;
                        }
                        else
                        {
                            quoted = false;
                            quoteClosed = true;
                        }
                    }
                    else field.push_back(ch);
                    continue;
                }
                if (ch == '"')
                {
                    if (!field.empty() || quoteClosed)
                    {
                        error = "quote appears inside an unquoted CSV field";
                        return false;
                    }
                    quoted = true;
                }
                else if (ch == ',')
                {
                    row.push_back(std::move(field));
                    field.clear();
                    quoteClosed = false;
                }
                else if (ch == '\r' || ch == '\n')
                {
                    if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                    row.push_back(std::move(field));
                    field.clear();
                    quoteClosed = false;
                    if (!consume(row)) return false;
                    row.clear();
                }
                else
                {
                    if (quoteClosed)
                    {
                        error = "characters follow a closing CSV quote";
                        return false;
                    }
                    field.push_back(ch);
                }
            }
            if (quoted)
            {
                error = "unterminated quoted CSV field";
                return false;
            }
            if (!field.empty() || !row.empty())
            {
                row.push_back(std::move(field));
                if (!consume(row)) return false;
            }
            return true;
        }

        bool ParseU32(std::string_view value, uint32_t& output, int base = 10)
        {
            if (base == 16 && value.starts_with("0x")) value.remove_prefix(2);
            if (value.empty()) return false;
            const char* begin = value.data();
            const char* end = begin + value.size();
            const auto result = std::from_chars(begin, end, output, base);
            return result.ec == std::errc{} && result.ptr == end;
        }

        std::unordered_map<std::string, std::string> ParseMetadata(std::string_view line)
        {
            constexpr std::string_view prefix = "# wxl-db2-localization ";
            std::unordered_map<std::string, std::string> values;
            if (!line.starts_with(prefix)) return values;
            line.remove_prefix(prefix.size());
            while (!line.empty())
            {
                const size_t space = line.find(' ');
                const std::string_view token = line.substr(0, space);
                const size_t equals = token.find('=');
                if (equals && equals != std::string_view::npos)
                    values.emplace(std::string(token.substr(0, equals)),
                                   std::string(token.substr(equals + 1)));
                if (space == std::string_view::npos) break;
                line.remove_prefix(space + 1);
            }
            return values;
        }
    }

    Result Apply(const Definition& definition, wdc5::Table& table)
    {
        Result result;
        result.archivePath = "DBFilesClient\\WXL\\Localization\\Active\\" +
            std::string(definition.name) + ".localized.csv";
        if (!std::ranges::any_of(definition.fields, [](const Field& field) { return field.string; }))
            return result;

        std::vector<uint8_t> bytes;
        if (!ReadArchiveFile(result.archivePath, bytes)) return result;
        if (std::ranges::find(bytes, uint8_t{0}) != bytes.end())
            return Invalid(std::move(result), "localized CSV contains a NUL byte");
        std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
        if (!ValidUtf8(text)) return Invalid(std::move(result), "localized CSV is not valid UTF-8");

        const size_t metadataEnd = text.find_first_of("\r\n");
        if (metadataEnd == std::string_view::npos)
            return Invalid(std::move(result), "localized CSV metadata line is missing");
        const auto metadata = ParseMetadata(text.substr(0, metadataEnd));
        const auto require = [&](const char* key) -> const std::string* {
            const auto found = metadata.find(key);
            return found == metadata.end() ? nullptr : &found->second;
        };
        const std::string* version = require("schema_version");
        const std::string* name = require("table");
        const std::string* locale = require("locale");
        const std::string* build = require("build");
        const std::string* layout = require("layout_hash");
        const std::string* tableHash = require("table_hash");
        const std::string* schema = require("embedded_schema");
        if (!version || *version != "1" || !name || *name != definition.name || !locale ||
            locale->empty() || !build || build->empty() || !layout || !tableHash || !schema)
            return Invalid(std::move(result), "localized CSV metadata is incomplete or names another table");
        result.locale = *locale;
        result.build = *build;
        uint32_t layoutValue = 0, tableHashValue = 0;
        if (!ParseU32(*layout, layoutValue, 16) || layoutValue != table.LayoutHash() ||
            !ParseU32(*tableHash, tableHashValue, 16) || tableHashValue != table.TableHash() ||
            *schema != table.Schema())
            return Invalid(std::move(result), "localized CSV does not match the loaded WDC5 schema/hashes");

        size_t csvStart = metadataEnd;
        if (text[csvStart] == '\r') ++csvStart;
        if (csvStart < text.size() && text[csvStart] == '\n') ++csvStart;
        std::string csvError;
        std::vector<std::string> header;
        std::vector<size_t> fields;
        std::unordered_set<std::string> headerNames;
        std::unordered_set<uint32_t> ids;
        std::vector<Change> changes;
        bool sawHeader = false;
        const auto consume = [&](std::vector<std::string>& row) -> bool {
            if (!sawHeader)
            {
                sawHeader = true;
                header = row;
                if (header.empty() || header.front() != "ID")
                {
                    csvError = "first column must be ID";
                    return false;
                }
                if (header.size() < 2)
                {
                    csvError = "no string columns are present";
                    return false;
                }
                fields.assign(header.size(), (std::numeric_limits<size_t>::max)());
                for (size_t column = 0; column < header.size(); ++column)
                {
                    if (!headerNames.emplace(header[column]).second)
                    {
                        csvError = "duplicate header " + header[column];
                        return false;
                    }
                    if (!column) continue;
                    const auto found = std::ranges::find_if(definition.fields,
                        [&](const Field& field) {
                            return field.name == header[column] && field.string;
                        });
                    if (found == definition.fields.end())
                    {
                        csvError = "undeclared string field " + header[column];
                        return false;
                    }
                    fields[column] = static_cast<size_t>(found - definition.fields.begin());
                }
                return true;
            }
            if (row.size() == 1 && row.front().empty()) return true;
            if (row.size() > header.size())
            {
                csvError = "row has too many columns";
                return false;
            }
            row.resize(header.size());
            uint32_t rowId = 0;
            if (!ParseU32(row.front(), rowId) || !ids.emplace(rowId).second)
            {
                csvError = "invalid or duplicate ID";
                return false;
            }
            if (!table.Find(rowId)) return true;
            ++result.matchedRows;
            for (size_t column = 1; column < header.size(); ++column)
                if (!row[column].empty())
                    changes.push_back(Change{rowId, fields[column], std::move(row[column])});
            return true;
        };
        if (!ParseCsv(text.substr(csvStart), consume, csvError) || !sawHeader)
            return Invalid(std::move(result), "localized CSV parse failed: " + csvError);

        for (const Change& change : changes)
        {
            std::string error;
            if (!table.SetString(change.rowId, change.field, 0, change.value, &error))
                return Invalid(std::move(result), "localized CSV apply failed: " + error);
        }
        result.status = Status::Applied;
        result.appliedCells = static_cast<uint32_t>(changes.size());
        return result;
    }
}
