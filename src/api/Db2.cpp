// Declarative retail DB2 loading for runtime scripts.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Db2.hpp"
#include "LocalizationCsv.hpp"
#include "../ExtensionApi.hpp"

#include "game/Io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace wxl::runtime::db2
{
    namespace
    {
        constexpr size_t kMissingField = static_cast<size_t>(-1);

        bool Fail(std::string* error, std::string message)
        {
            if (error) *error = std::move(message);
            return false;
        }

        std::string ArchivePath(std::string_view filename)
        {
            std::string path(filename);
            std::replace(path.begin(), path.end(), '/', '\\');
            if (path.find('\\') == std::string::npos) path.insert(0, "DBFilesClient\\");
            return path;
        }

        bool ReadArchiveFile(std::string_view filename, std::vector<uint8_t>& bytes)
        {
            bytes.clear();
            const std::string path = ArchivePath(filename);
            void* handle = nullptr;
            if (!wxl::game::io::FileOpen(
                    path.c_str(), wxl::game::io::kOpenWholeFile, &handle) ||
                !handle)
                return false;

            struct FileLease
            {
                void* handle = nullptr;
                ~FileLease()
                {
                    if (handle) wxl::game::io::FileClose(handle);
                }
            } lease{handle};

            uint32_t sizeHigh = 0;
            const uint32_t size = wxl::game::io::FileSize(handle, &sizeHigh);
            if (!size || sizeHigh)
                return false;

            bytes.resize(size);
            uint32_t read = 0;
            if (!wxl::game::io::FileRead(handle, bytes.data(), size, &read) ||
                read != size)
            {
                bytes.clear();
                return false;
            }
            return true;
        }

        const Definition* FindDefinition(std::span<const Definition> definitions, std::string_view name)
        {
            for (const Definition& definition : definitions)
                if (definition.name == name) return &definition;
            return nullptr;
        }

        bool HasField(const Definition& definition, std::string_view name)
        {
            if (name == "@id" || name == "@parent") return true;
            return std::ranges::any_of(definition.fields,
                [name](const Field& field) { return field.name == name; });
        }
    }

    bool Table::Load(const Definition& definition, std::string* error)
    {
        return LoadImpl(definition, nullptr, error);
    }

    bool Table::LoadFiltered(const Definition& definition, const wdc5::SnapshotFilter& filter,
                             std::string* error)
    {
        if (filter.values.empty())
            return Fail(error, "DB2: filtered load requires at least one value");
        if (filter.values.size() > 4096)
            return Fail(error, "DB2: filtered load has too many values");
        return LoadImpl(definition, &filter, error);
    }

    bool Table::LoadImpl(const Definition& definition, const wdc5::SnapshotFilter* filter,
                         std::string* error)
    {
        name_.clear();
        filename_.clear();
        fields_.clear();
        relations_.clear();
        table_ = {};
        if (error) error->clear();
        if (definition.name.empty() || definition.filename.empty())
            return Fail(error, "DB2: table name and filename are required");
        if (definition.fields.empty())
            return Fail(error, "DB2: " + std::string(definition.name) + " has no physical fields");

        fields_.reserve(definition.fields.size());
        for (size_t i = 0; i < definition.fields.size(); ++i)
        {
            const Field& field = definition.fields[i];
            if (field.name.empty() || !fields_.emplace(std::string(field.name), i).second)
                return Fail(error, "DB2: " + std::string(definition.name) + " has an empty or duplicate field name");
        }

        for (const Relation& relation : definition.relations)
        {
            RelationBinding binding{
                relation.source, kMissingField, relation.sourceElement,
                std::string(relation.targetTable), std::string(relation.targetField)
            };
            if (relation.source == RelationSource::Field)
            {
                const auto field = fields_.find(relation.sourceField);
                if (field == fields_.end())
                    return Fail(error, "DB2: relation " + std::string(relation.name) + " names an unknown source field");
                binding.field = field->second;
                if (binding.element >= std::max<uint16_t>(1, definition.fields[binding.field].elements))
                    return Fail(error, "DB2: relation " + std::string(relation.name) + " has an invalid array element");
            }
            if (relation.name.empty() || !relations_.emplace(std::string(relation.name), binding).second)
                return Fail(error, "DB2: " + std::string(definition.name) + " has an empty or duplicate relation name");
        }

        std::vector<uint8_t> bytes;
        if (!ReadArchiveFile(definition.filename, bytes))
            return Fail(error, "DB2: could not read " + ArchivePath(definition.filename));

        std::vector<wdc5::FieldShape> shapes;
        shapes.reserve(definition.fields.size());
        for (const Field& field : definition.fields)
            shapes.push_back(wdc5::FieldShape{
                std::max<uint16_t>(1, field.elements), field.words, field.string});

        if (!table_.Load(bytes.data(), bytes.size(), shapes, definition.layoutHash, error))
            return false;

        // The retired 64-bit host used to filter decoded rows before transferring a snapshot.
        // v1.1 now decodes in-process; retain the same bounded resident-table behavior by compacting
        // a requested subset immediately after the one raw decode.
        if (filter)
        {
            std::vector<uint8_t> snapshot;
            if (!table_.SaveSnapshotFiltered(*filter, snapshot, error))
                return false;
            table_ = {};
            if (!table_.LoadSnapshot(
                    snapshot.data(), snapshot.size(), definition.layoutHash, error))
                return false;
        }

        if (wxl_db2::ConfigBool("WXL_DB2_LOCALE_OVERLAYS", true))
        {
            localization::Result overlay = localization::Apply(definition, table_);
            if (overlay.status == localization::Status::Applied)
                WLOG_INFO("localized CSV applied table=%.*s locale=%s build=%s rows=%u cells=%u path=%s",
                          static_cast<int>(definition.name.size()), definition.name.data(),
                          overlay.locale.c_str(), overlay.build.c_str(), overlay.matchedRows,
                          overlay.appliedCells,
                          overlay.archivePath.c_str());
            else if (overlay.status == localization::Status::Invalid)
                WLOG_WARN("localized CSV ignored table=%.*s path=%s error=%s; using binary fallback",
                          static_cast<int>(definition.name.size()), definition.name.data(),
                          overlay.archivePath.c_str(), overlay.error.c_str());
        }

        name_.assign(definition.name);
        filename_.assign(definition.filename);
        return true;
    }

    uint32_t Table::Value(const wdc5::Row& row, size_t field, size_t element) const noexcept
    {
        return table_.Value(row, field, element);
    }

    uint32_t Table::Value(const wdc5::Row& row, std::string_view field, size_t element) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? 0 : table_.Value(row, index, element);
    }

    size_t Table::FieldIndex(std::string_view field) const noexcept
    {
        const auto it = fields_.find(field);
        return it == fields_.end() ? kMissingField : it->second;
    }

    size_t Table::ElementCount(std::string_view field) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? 0 : table_.ElementCount(index);
    }

    uint32_t Table::RelationKey(const wdc5::Row& row, std::string_view relation) const noexcept
    {
        const auto it = relations_.find(relation);
        if (it == relations_.end()) return 0;
        switch (it->second.source)
        {
            case RelationSource::RowId: return row.id;
            case RelationSource::ParentId: return row.parentId;
            case RelationSource::Field: return table_.Value(row, it->second.field, it->second.element);
        }
        return 0;
    }

    uint64_t Table::Value64(const wdc5::Row& row, size_t field, size_t element) const noexcept
    {
        return table_.Value64(row, field, element);
    }

    uint64_t Table::Value64(const wdc5::Row& row, std::string_view field, size_t element) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? 0 : table_.Value64(row, index, element);
    }

    std::string_view Table::String(const wdc5::Row& row, size_t field, size_t element) const noexcept
    {
        return table_.String(row, field, element);
    }

    std::string_view Table::String(const wdc5::Row& row, std::string_view field, size_t element) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? std::string_view{} : table_.String(row, index, element);
    }

    std::pair<std::string_view, std::string_view> Table::RelationTarget(
        std::string_view relation) const noexcept
    {
        const auto it = relations_.find(relation);
        if (it == relations_.end()) return {};
        return {it->second.targetTable, it->second.targetField};
    }

    bool RelationIndex::Build(const Table& source, std::string_view relation,
                              const Table& target, std::string* error)
    {
        relation_.clear();
        rows_.clear();
        if (error) error->clear();
        const auto [targetTable, targetField] = source.RelationTarget(relation);
        if (targetTable.empty())
            return Fail(error, "DB2: unknown relation " + std::string(relation));
        if (targetTable != target.Name())
            return Fail(error, "DB2: relation target " + std::string(targetTable) +
                               " does not match loaded table " + std::string(target.Name()));

        const size_t field = targetField == "@id" || targetField == "@parent"
            ? kMissingField : target.FieldIndex(targetField);
        if (targetField != "@id" && targetField != "@parent" && field == kMissingField)
            return Fail(error, "DB2: relation target field " + std::string(targetField) + " is absent");

        for (const wdc5::Row& row : target.Rows())
        {
            uint32_t key = 0;
            if (targetField == "@id") key = row.id;
            else if (targetField == "@parent") key = row.parentId;
            else key = target.Value(row, field);
            rows_[key].push_back(&row);
        }
        relation_.assign(relation);
        return true;
    }

    const std::vector<const wdc5::Row*>& RelationIndex::Resolve(
        const Table& source, const wdc5::Row& row) const noexcept
    {
        static const std::vector<const wdc5::Row*> empty;
        if (relation_.empty()) return empty;
        const auto it = rows_.find(source.RelationKey(row, relation_));
        return it == rows_.end() ? empty : it->second;
    }

    bool ValidateDefinitions(std::span<const Definition> definitions, std::string* error)
    {
        if (error) error->clear();
        std::unordered_set<std::string> names;
        for (const Definition& definition : definitions)
        {
            if (definition.name.empty() || definition.filename.empty() || definition.fields.empty())
                return Fail(error, "DB2: every definition needs a name, filename, and physical fields");
            if (!names.emplace(definition.name).second)
                return Fail(error, "DB2: duplicate table definition " + std::string(definition.name));
            std::unordered_set<std::string> fields;
            for (const Field& field : definition.fields)
                if (field.name.empty() || !fields.emplace(field.name).second)
                    return Fail(error, "DB2: duplicate or empty field in " + std::string(definition.name));
        }

        for (const Definition& definition : definitions)
        {
            for (const Relation& relation : definition.relations)
            {
                if (relation.name.empty() || relation.targetTable.empty())
                    return Fail(error, "DB2: incomplete relation in " + std::string(definition.name));
                if (relation.source == RelationSource::Field && !HasField(definition, relation.sourceField))
                    return Fail(error, "DB2: relation source field is absent from " + std::string(definition.name));
                const Definition* target = FindDefinition(definitions, relation.targetTable);
                if (!target)
                    return Fail(error, "DB2: relation target table " + std::string(relation.targetTable) + " is not declared");
                if (!HasField(*target, relation.targetField))
                    return Fail(error, "DB2: relation target field is absent from " + std::string(target->name));
            }
        }
        return true;
    }
}
