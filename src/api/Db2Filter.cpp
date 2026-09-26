// Filtered DB2 load companion service. The stock wxl.db2 v1 ABI remains untouched.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Db2.hpp"
#include "../ExtensionApi.hpp"

#include "wxl/Db2FilterApi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace
{
    namespace db2 = wxl::runtime::db2;

    bool BuildDefinition(const WXL_Db2Definition& source,
                         std::vector<db2::Field>& fields,
                         std::vector<db2::Relation>& relations,
                         db2::Definition& out)
    {
        if (!source.name || !source.filename || !source.fields || !source.fieldCount)
            return false;
        fields.resize(source.fieldCount);
        for (uint32_t i = 0; i < source.fieldCount; ++i)
        {
            const char* name = source.fields[i].name ? source.fields[i].name : "";
            const std::string_view fieldName(name);
            fields[i] = db2::Field{
                fieldName, source.fields[i].elements, 1, fieldName.ends_with("_lang")
            };
        }
        relations.resize(source.relationCount);
        for (uint32_t i = 0; i < source.relationCount; ++i)
        {
            const WXL_Db2Relation& relation = source.relations[i];
            relations[i] = db2::Relation{
                relation.name ? relation.name : "",
                static_cast<db2::RelationSource>(relation.source),
                relation.sourceField ? relation.sourceField : "",
                relation.sourceElement,
                relation.targetTable ? relation.targetTable : "",
                relation.targetField ? relation.targetField : "@id",
            };
        }
        out = db2::Definition{
            source.name, source.filename, source.layoutHash, fields, relations,
            source.required != 0,
        };
        return true;
    }

    void CopyError(const std::string& error, char* buffer, size_t capacity)
    {
        if (!buffer || !capacity) return;
        std::strncpy(buffer, error.c_str(), capacity - 1);
        buffer[capacity - 1] = '\0';
    }

    void* __cdecl LoadFiltered(const WXL_Db2Definition* definition,
                               const WXL_Db2Filter* filter,
                               char* errorBuffer,
                               size_t errorCapacity)
    {
        if (!definition || !filter || !filter->values || !filter->valueCount)
        {
            CopyError("DB2: incomplete filtered-load request", errorBuffer, errorCapacity);
            return nullptr;
        }

        std::vector<db2::Field> fields;
        std::vector<db2::Relation> relations;
        db2::Definition converted{};
        if (!BuildDefinition(*definition, fields, relations, converted))
        {
            CopyError("DB2: invalid filtered-load definition", errorBuffer, errorCapacity);
            return nullptr;
        }

        db2::wdc5::SnapshotFilter convertedFilter;
        switch (filter->source)
        {
            case WXL_DB2_FILTER_ROW_ID:
                convertedFilter.source = db2::wdc5::SnapshotFilterSource::RowId;
                break;
            case WXL_DB2_FILTER_PARENT_ID:
                convertedFilter.source = db2::wdc5::SnapshotFilterSource::ParentId;
                break;
            case WXL_DB2_FILTER_FIELD:
            {
                convertedFilter.source = db2::wdc5::SnapshotFilterSource::Field;
                const char* name = filter->field ? filter->field : "";
                const auto found = std::find_if(fields.begin(), fields.end(),
                    [name](const db2::Field& field) { return field.name == name; });
                if (found == fields.end())
                {
                    CopyError("DB2: filtered-load field is not declared", errorBuffer, errorCapacity);
                    return nullptr;
                }
                convertedFilter.field = static_cast<uint16_t>(found - fields.begin());
                convertedFilter.element = filter->element;
                break;
            }
            default:
                CopyError("DB2: invalid filtered-load source", errorBuffer, errorCapacity);
                return nullptr;
        }
        convertedFilter.values = std::span<const uint32_t>(filter->values, filter->valueCount);

        auto* table = new db2::Table();
        std::string error;
        if (!table->LoadFiltered(converted, convertedFilter, &error))
        {
            delete table;
            CopyError(error, errorBuffer, errorCapacity);
            return nullptr;
        }
        return table;
    }

    const WXL_Db2FilterApi g_filterApi = {
        sizeof(WXL_Db2FilterApi),
        WXL_DB2_FILTER_API_VERSION,
        &LoadFiltered,
    };
}

const WXL_Db2FilterApi* wxl_db2::FilterApi()
{
    return &g_filterApi;
}
