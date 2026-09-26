// Shared decoded-table accessors and architecture-neutral host snapshot format.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Wdc5.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_set>

namespace wxl::runtime::db2::wdc5
{
    namespace
    {
        constexpr uint32_t kSnapshotMagic = 0x35425857u; // 'WXB5'
        constexpr uint32_t kSnapshotVersion = 3;

        void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            const size_t offset = bytes.size();
            bytes.resize(offset + sizeof(value));
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }

        bool ReadU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& value)
        {
            if (static_cast<size_t>(end - cursor) < sizeof(value)) return false;
            std::memcpy(&value, cursor, sizeof(value));
            cursor += sizeof(value);
            return true;
        }

        bool Fail(std::string* error, const char* message)
        {
            if (error) *error = message;
            return false;
        }
    }

    const Row* Table::Find(uint32_t id) const noexcept
    {
        const auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : &rows_[it->second];
    }

    uint32_t Table::Value(const Row& row, size_t field, size_t element) const noexcept
    {
        if (field >= fieldElements_.size() || field >= fieldWords_.size() ||
            element >= fieldElements_[field]) return 0;
        const size_t offset = fieldOffsets_[field] + element * fieldWords_[field];
        return offset < row.values.size() ? row.values[offset] : 0;
    }

    uint64_t Table::Value64(const Row& row, size_t field, size_t element) const noexcept
    {
        if (field >= fieldElements_.size() || field >= fieldWords_.size() ||
            element >= fieldElements_[field]) return 0;
        const size_t offset = fieldOffsets_[field] + element * fieldWords_[field];
        if (offset >= row.values.size()) return 0;
        uint64_t value = row.values[offset];
        if (fieldWords_[field] > 1 && offset + 1 < row.values.size())
            value |= static_cast<uint64_t>(row.values[offset + 1]) << 32;
        return value;
    }

    std::string_view Table::String(const Row& row, size_t field, size_t element) const noexcept
    {
        if (field >= fieldStrings_.size() || !fieldStrings_[field]) return {};
        const uint32_t index = Value(row, field, element);
        return index < strings_.size() ? std::string_view(strings_[index]) : std::string_view{};
    }

    bool Table::SetString(uint32_t rowId, size_t field, size_t element,
                          std::string_view value, std::string* error)
    {
        const auto row = byId_.find(rowId);
        if (row == byId_.end()) return Fail(error, "WDC5 localization: row ID is absent");
        if (field >= fieldStrings_.size() || !fieldStrings_[field])
            return Fail(error, "WDC5 localization: target field is not a string");
        if (field >= fieldElements_.size() || field >= fieldWords_.size() ||
            element >= fieldElements_[field] || fieldWords_[field] != 1)
            return Fail(error, "WDC5 localization: target field element is invalid");
        if (strings_.size() >= (std::numeric_limits<uint32_t>::max)())
            return Fail(error, "WDC5 localization: string pool is full");
        Row& target = rows_[row->second];
        const size_t offset = fieldOffsets_[field] + element;
        if (offset >= target.values.size())
            return Fail(error, "WDC5 localization: target row is malformed");
        strings_.emplace_back(value);
        target.values[offset] = static_cast<uint32_t>(strings_.size() - 1);
        return true;
    }

    size_t Table::ElementCount(size_t field) const noexcept
    {
        return field < fieldElements_.size() ? fieldElements_[field] : 0;
    }

    bool Table::SaveSnapshot(std::vector<uint8_t>& bytes, std::string* error) const
    {
        return SaveSnapshotImpl(nullptr, bytes, error);
    }

    bool Table::SaveSnapshotFiltered(const SnapshotFilter& filter, std::vector<uint8_t>& bytes,
                                     std::string* error) const
    {
        return SaveSnapshotImpl(&filter, bytes, error);
    }

    bool Table::SaveSnapshotImpl(const SnapshotFilter* filter, std::vector<uint8_t>& bytes,
                                 std::string* error) const
    {
        bytes.clear();
        if (error) error->clear();
        if (fieldOffsets_.empty() || fieldElements_.size() + 1 != fieldOffsets_.size() ||
            fieldWords_.size() != fieldElements_.size() ||
            fieldStrings_.size() != fieldElements_.size())
            return Fail(error, "WDC5 snapshot: decoded table has no field layout");
        if (fieldOffsets_.size() > (std::numeric_limits<uint32_t>::max)() ||
            schema_.size() > (std::numeric_limits<uint32_t>::max)())
            return Fail(error, "WDC5 snapshot: table is too large");

        const uint32_t fieldCount = static_cast<uint32_t>(fieldOffsets_.size() - 1);
        const uint32_t valuesPerRow = static_cast<uint32_t>(fieldOffsets_.back());
        if (filter && filter->source == SnapshotFilterSource::Field &&
            (filter->field >= fieldCount || filter->element >= fieldElements_[filter->field]))
            return Fail(error, "WDC5 snapshot: row filter field is out of range");

        std::vector<const Row*> selectedRows;
        if (filter)
        {
            if (filter->source == SnapshotFilterSource::RowId)
            {
                // The decoder already owns an ID index. Row-ID snapshots are
                // the most common item-display/component query, so avoid
                // walking a six-figure table to select one or two records.
                selectedRows.reserve(filter->values.size());
                std::unordered_set<uint32_t> selectedIds;
                selectedIds.reserve(filter->values.size());
                for (uint32_t id : filter->values)
                {
                    if (!selectedIds.insert(id).second) continue;
                    const Row* row = Find(id);
                    if (!row) continue;
                    if (row->values.size() != valuesPerRow)
                        return Fail(
                            error,
                            "WDC5 snapshot: inconsistent decoded row width");
                    selectedRows.push_back(row);
                }
                std::ranges::sort(selectedRows, std::less<const Row*>{});
            }
            else
            {
                std::unordered_set<uint32_t> acceptedValues;
                acceptedValues.reserve(filter->values.size());
                acceptedValues.insert(filter->values.begin(),
                                      filter->values.end());
                for (const Row& row : rows_)
                {
                    if (row.values.size() != valuesPerRow)
                        return Fail(
                            error,
                            "WDC5 snapshot: inconsistent decoded row width");
                    const uint32_t value =
                        filter->source == SnapshotFilterSource::ParentId
                            ? row.parentId
                            : Value(row, filter->field, filter->element);
                    if (acceptedValues.contains(value))
                        selectedRows.push_back(&row);
                }
            }
        }
        else
        {
            for (const Row& row : rows_)
                if (row.values.size() != valuesPerRow)
                    return Fail(
                        error,
                        "WDC5 snapshot: inconsistent decoded row width");
        }

        const size_t selectedRowCount =
            filter ? selectedRows.size() : rows_.size();
        if (selectedRowCount > (std::numeric_limits<uint32_t>::max)())
            return Fail(error, "WDC5 snapshot: filtered row count is too large");

        // Full snapshots preserve the decoder's string pool exactly. Filtered snapshots compact it to
        // strings referenced by selected rows, which is critical for large localized Spell/SpellName
        // tables even when only a handful of spell IDs were requested.
        std::vector<uint8_t> stringWords(valuesPerRow, 0);
        for (size_t field = 0; field < fieldStrings_.size(); ++field)
        {
            if (!fieldStrings_[field]) continue;
            for (size_t element = 0; element < fieldElements_[field]; ++element)
                stringWords[fieldOffsets_[field] + element * fieldWords_[field]] = 1;
        }

        std::vector<uint32_t> stringRemap;
        std::vector<uint32_t> selectedStrings;
        if (filter)
        {
            stringRemap.assign(strings_.size(), (std::numeric_limits<uint32_t>::max)());
            for (const Row* selected : selectedRows)
            {
                const Row& row = *selected;
                for (size_t word = 0; word < row.values.size(); ++word)
                {
                    if (!stringWords[word]) continue;
                    const uint32_t oldIndex = row.values[word];
                    if (oldIndex >= strings_.size())
                        return Fail(error, "WDC5 snapshot: selected row has an invalid string index");
                    if (stringRemap[oldIndex] == (std::numeric_limits<uint32_t>::max)())
                    {
                        if (selectedStrings.size() >= (std::numeric_limits<uint32_t>::max)())
                            return Fail(error, "WDC5 snapshot: filtered string pool is too large");
                        stringRemap[oldIndex] = static_cast<uint32_t>(selectedStrings.size());
                        selectedStrings.push_back(oldIndex);
                    }
                }
            }
        }

        const size_t stringCount = filter ? selectedStrings.size() : strings_.size();
        uint64_t stringPayloadSize = 0;
        if (filter)
        {
            for (uint32_t index : selectedStrings)
                stringPayloadSize += sizeof(uint32_t) + strings_[index].size();
        }
        else
        {
            for (const std::string& value : strings_)
                stringPayloadSize += sizeof(uint32_t) + value.size();
        }
        if (stringCount > (std::numeric_limits<uint32_t>::max)() ||
            stringPayloadSize > (std::numeric_limits<uint32_t>::max)())
            return Fail(error, "WDC5 snapshot: string pool is too large");

        const uint64_t words =
            10ull + fieldOffsets_.size() + fieldElements_.size() + fieldWords_.size() +
            fieldStrings_.size() +
            static_cast<uint64_t>(selectedRowCount) * (2ull + valuesPerRow);
        const uint64_t total = words * sizeof(uint32_t) + schema_.size() + stringPayloadSize;
        if (total > (std::numeric_limits<size_t>::max)() ||
            total > (std::numeric_limits<uint32_t>::max)())
            return Fail(error, "WDC5 snapshot: serialized size overflows");
        bytes.reserve(static_cast<size_t>(total));

        AppendU32(bytes, kSnapshotMagic);
        AppendU32(bytes, kSnapshotVersion);
        AppendU32(bytes, layoutHash_);
        AppendU32(bytes, tableHash_);
        AppendU32(bytes, fieldCount);
        AppendU32(bytes, static_cast<uint32_t>(selectedRowCount));
        AppendU32(bytes, valuesPerRow);
        AppendU32(bytes, static_cast<uint32_t>(schema_.size()));
        AppendU32(bytes, static_cast<uint32_t>(stringCount));
        AppendU32(bytes, static_cast<uint32_t>(stringPayloadSize));
        for (size_t offset : fieldOffsets_)
        {
            if (offset > (std::numeric_limits<uint32_t>::max)())
                return Fail(error, "WDC5 snapshot: field offset is too large");
            AppendU32(bytes, static_cast<uint32_t>(offset));
        }
        for (uint16_t elements : fieldElements_) AppendU32(bytes, elements);
        for (uint8_t words : fieldWords_) AppendU32(bytes, words);
        for (uint8_t stringField : fieldStrings_) AppendU32(bytes, stringField ? 1u : 0u);
        bytes.insert(bytes.end(), schema_.begin(), schema_.end());
        const auto appendString = [&](const std::string& value) {
            AppendU32(bytes, static_cast<uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        };
        if (filter)
        {
            for (uint32_t index : selectedStrings) appendString(strings_[index]);
        }
        else
        {
            for (const std::string& value : strings_) appendString(value);
        }

        const auto appendRow = [&](const Row& row) {
            AppendU32(bytes, row.id);
            AppendU32(bytes, row.parentId);
            for (size_t word = 0; word < row.values.size(); ++word)
            {
                uint32_t value = row.values[word];
                if (filter && stringWords[word]) value = stringRemap[value];
                AppendU32(bytes, value);
            }
        };
        if (filter)
        {
            for (const Row* row : selectedRows) appendRow(*row);
        }
        else
        {
            for (const Row& row : rows_) appendRow(row);
        }
        return true;
    }

    bool Table::LoadSnapshot(const void* data, size_t size, uint32_t expectedLayoutHash,
                             std::string* error)
    {
        layoutHash_ = 0;
        tableHash_ = 0;
        schema_.clear();
        rows_.clear();
        byId_.clear();
        fieldOffsets_.clear();
        fieldElements_.clear();
        fieldWords_.clear();
        fieldStrings_.clear();
        strings_.clear();
        if (error) error->clear();
        if (!data || size < 8 * sizeof(uint32_t))
            return Fail(error, "WDC5 snapshot: empty or truncated header");

        const auto* cursor = static_cast<const uint8_t*>(data);
        const uint8_t* end = cursor + size;
        uint32_t magic = 0, version = 0, fieldCount = 0, rowCount = 0;
        uint32_t valuesPerRow = 0, schemaSize = 0;
        if (!ReadU32(cursor, end, magic) || !ReadU32(cursor, end, version) ||
            !ReadU32(cursor, end, layoutHash_) || !ReadU32(cursor, end, tableHash_) ||
            !ReadU32(cursor, end, fieldCount) || !ReadU32(cursor, end, rowCount) ||
            !ReadU32(cursor, end, valuesPerRow) || !ReadU32(cursor, end, schemaSize))
            return Fail(error, "WDC5 snapshot: truncated header");
        if (magic != kSnapshotMagic || (version < 1 || version > kSnapshotVersion))
            return Fail(error, "WDC5 snapshot: unsupported wire version");
        if (expectedLayoutHash && layoutHash_ != expectedLayoutHash)
            return Fail(error, "WDC5 snapshot: layout hash does not match the declaration");

        uint32_t stringCount = 0, stringPayloadSize = 0;
        if (version >= 3 &&
            (!ReadU32(cursor, end, stringCount) || !ReadU32(cursor, end, stringPayloadSize)))
            return Fail(error, "WDC5 snapshot: truncated string-pool header");

        const uint64_t offsetsBytes = (static_cast<uint64_t>(fieldCount) + 1) * sizeof(uint32_t);
        const uint64_t fieldShapeBytes = version >= 2
            ? static_cast<uint64_t>(fieldCount) * (version >= 3 ? 3ull : 2ull) * sizeof(uint32_t) : 0;
        const uint64_t rowWords = 2ull + valuesPerRow;
        const uint64_t rowsBytes = static_cast<uint64_t>(rowCount) * rowWords * sizeof(uint32_t);
        const uint64_t needed = offsetsBytes + fieldShapeBytes + schemaSize + stringPayloadSize + rowsBytes;
        if (needed > static_cast<uint64_t>(end - cursor))
            return Fail(error, "WDC5 snapshot: truncated payload");

        fieldOffsets_.resize(static_cast<size_t>(fieldCount) + 1);
        for (size_t i = 0; i < fieldOffsets_.size(); ++i)
        {
            uint32_t offset = 0;
            if (!ReadU32(cursor, end, offset))
                return Fail(error, "WDC5 snapshot: truncated field layout");
            fieldOffsets_[i] = offset;
            if (i && fieldOffsets_[i] < fieldOffsets_[i - 1])
                return Fail(error, "WDC5 snapshot: invalid field layout");
        }
        if (fieldOffsets_.back() != valuesPerRow)
            return Fail(error, "WDC5 snapshot: row width does not match field layout");

        fieldElements_.resize(fieldCount);
        fieldWords_.resize(fieldCount, 1);
        fieldStrings_.resize(fieldCount, 0);
        if (version >= 2)
        {
            for (uint16_t& elements : fieldElements_)
            {
                uint32_t value = 0;
                if (!ReadU32(cursor, end, value) || !value || value > 0xFFFFu)
                    return Fail(error, "WDC5 snapshot: invalid field element count");
                elements = static_cast<uint16_t>(value);
            }
            for (size_t i = 0; i < fieldWords_.size(); ++i)
            {
                uint32_t value = 0;
                if (!ReadU32(cursor, end, value) || !value || value > 2u)
                    return Fail(error, "WDC5 snapshot: invalid field word count");
                fieldWords_[i] = static_cast<uint8_t>(value);
                const size_t words = static_cast<size_t>(fieldElements_[i]) * fieldWords_[i];
                if (fieldOffsets_[i + 1] - fieldOffsets_[i] != words)
                    return Fail(error, "WDC5 snapshot: field shape does not match its offsets");
            }
            if (version >= 3)
            {
                for (uint8_t& stringField : fieldStrings_)
                {
                    uint32_t value = 0;
                    if (!ReadU32(cursor, end, value) || value > 1u)
                        return Fail(error, "WDC5 snapshot: invalid string-field flag");
                    stringField = static_cast<uint8_t>(value);
                }
            }
        }
        else
        {
            for (size_t i = 0; i < fieldElements_.size(); ++i)
            {
                const size_t elements = fieldOffsets_[i + 1] - fieldOffsets_[i];
                if (!elements || elements > 0xFFFFu)
                    return Fail(error, "WDC5 snapshot: invalid legacy field shape");
                fieldElements_[i] = static_cast<uint16_t>(elements);
            }
        }

        schema_.assign(reinterpret_cast<const char*>(cursor), schemaSize);
        cursor += schemaSize;

        strings_.reserve(stringCount);
        const uint8_t* stringEnd = cursor + stringPayloadSize;
        for (uint32_t i = 0; i < stringCount; ++i)
        {
            uint32_t length = 0;
            if (!ReadU32(cursor, stringEnd, length) || length > static_cast<size_t>(stringEnd - cursor))
                return Fail(error, "WDC5 snapshot: malformed string pool");
            strings_.emplace_back(reinterpret_cast<const char*>(cursor), length);
            cursor += length;
        }
        if (cursor != stringEnd)
            return Fail(error, "WDC5 snapshot: trailing string-pool bytes");
        if (version < 3) strings_.emplace_back();

        rows_.reserve(rowCount);
        for (uint32_t i = 0; i < rowCount; ++i)
        {
            Row row;
            if (!ReadU32(cursor, end, row.id) || !ReadU32(cursor, end, row.parentId))
                return Fail(error, "WDC5 snapshot: truncated row header");
            row.values.resize(valuesPerRow);
            for (uint32_t& value : row.values)
                if (!ReadU32(cursor, end, value))
                    return Fail(error, "WDC5 snapshot: truncated row values");
            rows_.push_back(std::move(row));
        }
        if (cursor != end)
            return Fail(error, "WDC5 snapshot: unexpected trailing bytes");

        byId_.reserve(rows_.size());
        for (size_t i = 0; i < rows_.size(); ++i) byId_[rows_[i].id] = i;
        return true;
    }
}
