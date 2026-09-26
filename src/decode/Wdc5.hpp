// Shared decoded WDC5 table representation and host snapshot wire format.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wxl::runtime::db2::wdc5
{
    struct FieldShape
    {
        uint16_t elements = 1;
        uint8_t words = 1;
        bool string = false;
    };

    struct Row
    {
        uint32_t id = 0;
        uint32_t parentId = 0;
        // Physical fields are flattened to one allocation. WDC5 item tables contain hundreds of
        // thousands of rows; a vector per field would add millions of client-heap allocations.
        std::vector<uint32_t> values;
    };

    enum class SnapshotFilterSource : uint8_t
    {
        RowId,
        ParentId,
        Field,
    };

    /** Host-side row filter applied before a decoded table crosses into the 32-bit client. */
    struct SnapshotFilter
    {
        SnapshotFilterSource source = SnapshotFilterSource::RowId;
        uint16_t field = 0;
        uint16_t element = 0;
        std::span<const uint32_t> values;
    };

    class Table
    {
    public:
        // Host-only format decoder. The implementation lives under wxl-host-extension/host.
        bool Load(const void* bytes, size_t size, const std::vector<FieldShape>& shapes,
                  uint32_t expectedLayoutHash, std::string* error = nullptr);

        // Architecture-neutral snapshot used to transfer decoded rows from the 64-bit host to the DLL.
        bool LoadSnapshot(const void* bytes, size_t size, uint32_t expectedLayoutHash,
                          std::string* error = nullptr);
        bool SaveSnapshot(std::vector<uint8_t>& bytes, std::string* error = nullptr) const;
        bool SaveSnapshotFiltered(const SnapshotFilter& filter, std::vector<uint8_t>& bytes,
                                  std::string* error = nullptr) const;

        const Row* Find(uint32_t id) const noexcept;
        uint32_t Value(const Row& row, size_t field, size_t element = 0) const noexcept;
        uint64_t Value64(const Row& row, size_t field, size_t element = 0) const noexcept;
        std::string_view String(const Row& row, size_t field, size_t element = 0) const noexcept;
        bool SetString(uint32_t rowId, size_t field, size_t element,
                       std::string_view value, std::string* error = nullptr);
        size_t ElementCount(size_t field) const noexcept;
        const std::vector<Row>& Rows() const noexcept { return rows_; }
        uint32_t LayoutHash() const noexcept { return layoutHash_; }
        uint32_t TableHash() const noexcept { return tableHash_; }
        const std::string& Schema() const noexcept { return schema_; }

    private:
        uint32_t layoutHash_ = 0;
        uint32_t tableHash_ = 0;
        std::string schema_;
        std::vector<Row> rows_;
        std::unordered_map<uint32_t, size_t> byId_;
        std::vector<size_t> fieldOffsets_;
        std::vector<uint16_t> fieldElements_;
        std::vector<uint8_t> fieldWords_;
        std::vector<uint8_t> fieldStrings_;
        std::vector<std::string> strings_;

        bool SaveSnapshotImpl(const SnapshotFilter* filter, std::vector<uint8_t>& bytes,
                              std::string* error) const;
    };
}
