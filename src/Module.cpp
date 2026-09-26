// wxl-db2: retail DB2 table reading, published as three services other extensions consume through
// WXL_Api::GetInterface -- "wxl.db2" (declarative table loading, see include/wxl/Db2Api.h),
// "wxl.fdid" (FileDataID -> path resolution, see include/wxl/FdidApi.h), and "wxl.light" (the
// retail lighting tables, see include/wxl/LightApi.h).
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

#include "api/Db2.hpp"
#include "ExtensionApi.hpp"
#include "api/FdidResolver.hpp"
#include "decode/Wdc5.hpp"
#include "schemas/AppearanceStore.hpp"
#include "schemas/LightStore.hpp"
#include "schemas/ModelStore.hpp"

#include "wxl/AppearanceApi.h"
#include "wxl/Db2Api.h"
#include "wxl/Db2StringApi.h"
#include "wxl/FdidApi.h"
#include "wxl/LightApi.h"
#include "wxl/ModelDataApi.h"
#include "wxl/PluginApi.h"
#include "wxl/RetailSpellDb2Api.h"

#include <cstring>
#include <vector>

namespace
{
    namespace db2 = wxl::runtime::db2;

    // --- vtable glue: Table and Row cross the boundary as opaque handles only this binary
    // dereferences (see the reasoning at the top of Db2Api.h). ---------------------------------

    void* __cdecl Db2Load(const WXL_Db2Definition* definition, char* errorBuf, size_t errorBufSize)
    {
        if (!definition)
        {
            if (errorBuf && errorBufSize) std::snprintf(errorBuf, errorBufSize, "DB2: null definition");
            return nullptr;
        }

        std::vector<db2::Field> fields(definition->fieldCount);
        for (uint32_t i = 0; i < definition->fieldCount; ++i)
        {
            const char* name = definition->fields[i].name ? definition->fields[i].name : "";
            const std::string_view fieldName(name);
            fields[i] = db2::Field{
                fieldName, definition->fields[i].elements, 1, fieldName.ends_with("_lang")
            };
        }

        std::vector<db2::Relation> relations(definition->relationCount);
        for (uint32_t i = 0; i < definition->relationCount; ++i)
        {
            const WXL_Db2Relation& src = definition->relations[i];
            relations[i] = db2::Relation{
                src.name,
                static_cast<db2::RelationSource>(src.source),
                src.sourceField ? src.sourceField : "",
                src.sourceElement,
                src.targetTable ? src.targetTable : "",
                src.targetField ? src.targetField : "@id",
            };
        }

        db2::Definition def{
            definition->name ? definition->name : "",
            definition->filename ? definition->filename : "",
            definition->layoutHash,
            fields,
            relations,
            definition->required != 0,
        };

        auto* table = new db2::Table();
        std::string error;
        if (!table->Load(def, &error))
        {
            delete table;
            if (errorBuf && errorBufSize)
            {
                std::strncpy(errorBuf, error.c_str(), errorBufSize - 1);
                errorBuf[errorBufSize - 1] = '\0';
            }
            return nullptr;
        }
        return table;
    }

    void __cdecl Db2Release(void* table)
    {
        delete static_cast<db2::Table*>(table);
    }

    uint32_t __cdecl Db2RowCount(void* table)
    {
        return table ? static_cast<uint32_t>(static_cast<db2::Table*>(table)->Rows().size()) : 0;
    }

    const void* __cdecl Db2RowAt(void* table, uint32_t index)
    {
        if (!table) return nullptr;
        const auto& rows = static_cast<db2::Table*>(table)->Rows();
        return index < rows.size() ? &rows[index] : nullptr;
    }

    const void* __cdecl Db2FindRow(void* table, uint32_t id)
    {
        return table ? static_cast<db2::Table*>(table)->Find(id) : nullptr;
    }

    uint32_t __cdecl Db2RowId(const void* row)
    {
        return row ? static_cast<const db2::wdc5::Row*>(row)->id : 0;
    }

    uint32_t __cdecl Db2RowParentId(const void* row)
    {
        return row ? static_cast<const db2::wdc5::Row*>(row)->parentId : 0;
    }

    uint32_t __cdecl Db2Value(void* table, const void* row, const char* field, uint32_t element)
    {
        if (!table || !row || !field) return 0;
        return static_cast<db2::Table*>(table)->Value(*static_cast<const db2::wdc5::Row*>(row), field, element);
    }

    size_t __cdecl Db2FieldIndex(void* table, const char* field)
    {
        return table && field ? static_cast<db2::Table*>(table)->FieldIndex(field) : static_cast<size_t>(-1);
    }

    size_t __cdecl Db2ElementCount(void* table, const char* field)
    {
        return table && field ? static_cast<db2::Table*>(table)->ElementCount(field) : 0;
    }

    uint32_t __cdecl Db2RelationKey(void* table, const void* row, const char* relation)
    {
        if (!table || !row || !relation) return 0;
        return static_cast<db2::Table*>(table)->RelationKey(*static_cast<const db2::wdc5::Row*>(row), relation);
    }

    uint32_t __cdecl Db2LayoutHash(void* table)
    {
        return table ? static_cast<db2::Table*>(table)->LayoutHash() : 0;
    }

    uint32_t __cdecl Db2TableHash(void* table)
    {
        return table ? static_cast<db2::Table*>(table)->TableHash() : 0;
    }

    const WXL_Db2Api g_db2Api = {
        sizeof(WXL_Db2Api),
        WXL_DB2_API_VERSION,
        &Db2Load,
        &Db2Release,
        &Db2RowCount,
        &Db2RowAt,
        &Db2FindRow,
        &Db2RowId,
        &Db2RowParentId,
        &Db2Value,
        &Db2FieldIndex,
        &Db2ElementCount,
        &Db2RelationKey,
        &Db2LayoutHash,
        &Db2TableHash,
    };

    const char* __cdecl Db2String(void* table, const void* row,
                                  const char* field, uint32_t element)
    {
        if (!table || !row || !field) return "";
        const std::string_view value = static_cast<db2::Table*>(table)->String(
            *static_cast<const db2::wdc5::Row*>(row), field, element);
        return value.data() ? value.data() : "";
    }

    const WXL_Db2StringApi g_db2StringApi = {
        sizeof(WXL_Db2StringApi),
        WXL_DB2_STRING_API_VERSION,
        &Db2String,
    };

    // --- vtable glue: the FDID resolver (TextureFilePath/ModelFilePath, see FdidResolver.cpp) ------

    const WXL_FdidApi g_fdidApi = {
        sizeof(WXL_FdidApi),
        WXL_FDID_API_VERSION,
        &wxl::db2::fdid::ResolveTexture,
        &wxl::db2::fdid::ResolveModel,
        &wxl::db2::fdid::ResolveMaterialTexture,
    };

    namespace modeldata = wxl::runtime::db2::model;

    int __cdecl ModelDataLoaded() { return modeldata::Status().loaded ? 1 : 0; }
    const char* __cdecl ModelDataError() { return modeldata::Status().error.c_str(); }
    uint32_t __cdecl ModelDataRowCount() { return modeldata::Status().rows; }

    uint32_t __cdecl ModelDataFileDataIdForPath(const char* modelPath)
    {
        return wxl::db2::fdid::ResolveModelId(modelPath);
    }

    int __cdecl ModelDataLookup(uint32_t fileDataId, WXL_ModelFileData* out)
    {
        if (!out) return 0;
        modeldata::ModelFileData row;
        if (!modeldata::Lookup(fileDataId, row)) return 0;

        out->fileDataId       = row.fileDataId;
        out->flags            = row.flags;
        out->lodCount         = row.lodCount;
        out->modelResourcesId = row.modelResourcesId;
        for (int i = 0; i < 6; ++i) out->geoBox[i] = row.geoBox[i];
        return 1;
    }

    int __cdecl ModelDataLookupByPath(const char* modelPath, WXL_ModelFileData* out)
    {
        const uint32_t fdid = wxl::db2::fdid::ResolveModelId(modelPath);
        return fdid ? ModelDataLookup(fdid, out) : 0;
    }

    const WXL_ModelDataApi g_modelDataApi = {
        sizeof(WXL_ModelDataApi),
        WXL_MODEL_DATA_API_VERSION,
        &ModelDataLoaded,
        &ModelDataError,
        &ModelDataRowCount,
        &ModelDataFileDataIdForPath,
        &ModelDataLookup,
        &ModelDataLookupByPath,
    };

    namespace appearance = wxl::runtime::db2::appearance;

    thread_local appearance::Recipe t_recipe;

    void PublishRecipe(const appearance::Recipe& recipe, WXL_Recipe* out)
    {
        out->modelFileDataId    = recipe.modelFileDataId;
        out->skeletonFileDataId = recipe.skeletonFileDataId;
        out->textureLayoutId    = recipe.textureLayoutId;
        out->geosets            = recipe.geosets.data();
        out->geosetCount        = static_cast<uint32_t>(recipe.geosets.size());
        out->layers             = recipe.layers.data();
        out->layerCount         = static_cast<uint32_t>(recipe.layers.size());
        out->attached           = recipe.attached.data();
        out->attachedCount      = static_cast<uint32_t>(recipe.attached.size());
    }

    int __cdecl AppearanceForCharacter(uint32_t chrRaceId, uint32_t sex, const uint32_t* choiceIds,
                                       uint32_t choiceCount, WXL_Recipe* out)
    {
        if (!out) return 0;
        if (!appearance::BuildForCharacter(chrRaceId, sex, choiceIds, choiceCount, t_recipe))
            return 0;
        PublishRecipe(t_recipe, out);
        return 1;
    }

    int __cdecl AppearanceForCreature(uint32_t creatureDisplayInfoId, WXL_Recipe* out)
    {
        if (!out) return 0;
        if (!appearance::BuildForCreature(creatureDisplayInfoId, t_recipe)) return 0;
        PublishRecipe(t_recipe, out);
        return 1;
    }

    uint32_t __cdecl AppearanceChrModelForRace(uint32_t chrRaceId, uint32_t sex)
    {
        return appearance::ChrModelForRace(chrRaceId, sex);
    }

    uint32_t __cdecl AppearanceOptionCount(uint32_t chrModelId)
    {
        return appearance::OptionCount(chrModelId);
    }

    int __cdecl AppearanceOptionAt(uint32_t chrModelId, uint32_t index, WXL_ChrOption* out)
    {
        return out && appearance::OptionAt(chrModelId, index, *out) ? 1 : 0;
    }

    uint32_t __cdecl AppearanceChoiceCount(uint32_t optionId)
    {
        return appearance::ChoiceCount(optionId);
    }

    int __cdecl AppearanceChoiceAt(uint32_t optionId, uint32_t index, WXL_ChrChoice* out)
    {
        return out && appearance::ChoiceAt(optionId, index, *out) ? 1 : 0;
    }

    const char* __cdecl AppearanceOptionName(uint32_t optionId)
    {
        return appearance::OptionName(optionId);
    }

    const char* __cdecl AppearanceChoiceName(uint32_t choiceId)
    {
        return appearance::ChoiceName(choiceId);
    }

    uint32_t __cdecl AppearanceChoiceSwatchColor2(uint32_t choiceId)
    {
        return appearance::ChoiceSwatchColor2(choiceId);
    }

    uint32_t __cdecl AppearanceOptionSecondaryOrderIndex(uint32_t optionId)
    {
        return appearance::OptionSecondaryOrderIndex(optionId);
    }

    uint32_t __cdecl AppearanceLayoutForModel(uint32_t chrModelId)
    {
        return appearance::LayoutForModel(chrModelId);
    }

    int __cdecl AppearanceLayoutSize(uint32_t layoutId, uint32_t* outWidth, uint32_t* outHeight)
    {
        uint32_t width = 0, height = 0;
        if (!appearance::LayoutSize(layoutId, width, height)) return 0;
        if (outWidth) *outWidth = width;
        if (outHeight) *outHeight = height;
        return 1;
    }

    uint32_t __cdecl AppearanceSectionCount(uint32_t layoutId)
    {
        return appearance::SectionCount(layoutId);
    }

    int __cdecl AppearanceSectionAt(uint32_t layoutId, uint32_t index, WXL_TextureSection* out)
    {
        return out && appearance::SectionAt(layoutId, index, *out) ? 1 : 0;
    }

    const WXL_AppearanceApi g_appearanceApi = {
        sizeof(WXL_AppearanceApi),
        WXL_APPEARANCE_API_VERSION,
        &AppearanceForCharacter,
        &AppearanceForCreature,
        &AppearanceChrModelForRace,
        &AppearanceOptionCount,
        &AppearanceOptionAt,
        &AppearanceChoiceCount,
        &AppearanceChoiceAt,
        &AppearanceLayoutForModel,
        &AppearanceLayoutSize,
        &AppearanceSectionCount,
        &AppearanceSectionAt,
        &AppearanceOptionName,
        &AppearanceChoiceName,
        &AppearanceChoiceSwatchColor2,
        &AppearanceOptionSecondaryOrderIndex,
    };

    // --- vtable glue: the retail lighting tables (LightState is POD, copied by value across the
    // boundary, see LightApi.h) -------------------------------------------------------------------

    namespace light = wxl::runtime::db2::light;

    void FillLightState(const light::LightState& in, WXL_LightState& out)
    {
        auto rgb = [](const light::Rgb& c) { return WXL_LightRgb{ c.r, c.g, c.b }; };
        out = WXL_LightState{};
        out.valid    = in.valid ? 1 : 0;
        out.lightId  = in.lightId;
        out.paramsId = in.paramsId;
        out.direct = rgb(in.direct); out.ambient = rgb(in.ambient);
        out.skyTop = rgb(in.skyTop); out.skyMiddle = rgb(in.skyMiddle);
        out.skyBand1 = rgb(in.skyBand1); out.skyBand2 = rgb(in.skyBand2);
        out.skySmog = rgb(in.skySmog); out.skyFog = rgb(in.skyFog);
        out.sunColor = rgb(in.sunColor); out.cloudSunColor = rgb(in.cloudSunColor);
        out.cloudEmissive = rgb(in.cloudEmissive);
        out.cloudLayer1Ambient = rgb(in.cloudLayer1Ambient); out.cloudLayer2Ambient = rgb(in.cloudLayer2Ambient);
        out.oceanClose = rgb(in.oceanClose); out.oceanFar = rgb(in.oceanFar);
        out.riverClose = rgb(in.riverClose); out.riverFar = rgb(in.riverFar);
        out.horizonAmbient = rgb(in.horizonAmbient); out.groundAmbient = rgb(in.groundAmbient);
        out.endFogColor = rgb(in.endFogColor); out.sunFogColor = rgb(in.sunFogColor);
        out.fogHeightColor = rgb(in.fogHeightColor); out.endFogHeightColor = rgb(in.endFogHeightColor);
        out.shadowOpacity = in.shadowOpacity;
        out.fogEnd = in.fogEnd; out.fogScaler = in.fogScaler; out.fogDensity = in.fogDensity;
        out.fogHeight = in.fogHeight; out.fogHeightScaler = in.fogHeightScaler; out.fogHeightDensity = in.fogHeightDensity;
        out.fogZScalar = in.fogZScalar; out.mainFogStartDist = in.mainFogStartDist; out.mainFogEndDist = in.mainFogEndDist;
        out.sunFogAngle = in.sunFogAngle; out.sunFogStrength = in.sunFogStrength; out.cloudDensity = in.cloudDensity;
        out.endFogColorDistance = in.endFogColorDistance; out.fogStartOffset = in.fogStartOffset;
        for (int i = 0; i < 4; ++i)
        {
            out.fogHeightCoefficients[i] = in.fogHeightCoefficients[i];
            out.mainFogCoefficients[i]   = in.mainFogCoefficients[i];
            out.heightDensityFogCoeff[i] = in.heightDensityFogCoeff[i];
        }
        out.colorGradingFdid = in.colorGradingFdid; out.darkerColorGradingFdid = in.darkerColorGradingFdid;
        out.lightSkyboxId = in.lightSkyboxId; out.cloudTypeId = in.cloudTypeId; out.paramsFlags = in.paramsFlags;
        out.glow = in.glow; out.highlightSky = in.highlightSky;
        out.waterShallowAlpha = in.waterShallowAlpha; out.waterDeepAlpha = in.waterDeepAlpha;
        out.oceanShallowAlpha = in.oceanShallowAlpha; out.oceanDeepAlpha = in.oceanDeepAlpha;
        out.sunPolar = in.sunPolar; out.sunAzimuth = in.sunAzimuth;
        out.sunAttenuationStart = in.sunAttenuationStart; out.sunAttenuationEnd = in.sunAttenuationEnd;
        for (int i = 0; i < 3; ++i)
        {
            out.overrideSunPosition[i]     = in.overrideSunPosition[i];
            out.overrideCelestialSphere[i] = in.overrideCelestialSphere[i];
        }
        out.skyboxFdid = in.skyboxFdid; out.celestialSkyboxFdid = in.celestialSkyboxFdid; out.skyboxFlags = in.skyboxFlags;
    }

    int __cdecl LightStatusLoaded() { return light::Status().loaded ? 1 : 0; }
    int __cdecl LightStatusFailed() { return light::Status().failed ? 1 : 0; }
    const char* __cdecl LightStatusError() { return light::Status().error.c_str(); }
    void __cdecl LightStatusCounts(uint32_t* lights, uint32_t* data, uint32_t* params, uint32_t* skyboxes)
    {
        const light::StoreStatus& s = light::Status();
        if (lights) *lights = s.lights;
        if (data) *data = s.data;
        if (params) *params = s.params;
        if (skyboxes) *skyboxes = s.skyboxes;
    }

    int __cdecl LightEvaluate(int mapId, const float worldPos[3], float timeHalfMinutes, WXL_LightState* out)
    {
        if (!out) return 0;
        const light::LightState s = light::Evaluate(mapId, worldPos, timeHalfMinutes);
        FillLightState(s, *out);
        return s.valid ? 1 : 0;
    }

    int __cdecl LightCurrent(WXL_LightState* out)
    {
        if (!out) return 0;
        const light::LightState& s = light::Current();
        FillLightState(s, *out);
        return s.valid ? 1 : 0;
    }

    void __cdecl LightSetMapOverride(int mapId) { light::SetMapOverride(mapId); }
    int __cdecl LightMapOverride() { return light::MapOverride(); }
    void __cdecl LightSetTimeOverride(float t) { light::SetTimeOverride(t); }
    float __cdecl LightTimeOverride() { return light::TimeOverride(); }
    float __cdecl LightWorldTimeHalfMinutes() { return light::WorldTimeHalfMinutes(); }

    const WXL_LightApi g_lightApi = {
        sizeof(WXL_LightApi),
        WXL_LIGHT_API_VERSION,
        &LightStatusLoaded,
        &LightStatusFailed,
        &LightStatusError,
        &LightStatusCounts,
        &LightEvaluate,
        &LightCurrent,
        &LightSetMapOverride,
        &LightMapOverride,
        &LightSetTimeOverride,
        &LightTimeOverride,
        &LightWorldTimeHalfMinutes,
    };
}

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info = {
        sizeof(WXL_PluginInfo),
        WXL_API_VERSION,
        "wxl-db2",
        1,
        WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;

    wxl_db2::g_api = api;

    api->PublishInterface("wxl.db2", WXL_DB2_API_VERSION, const_cast<WXL_Db2Api*>(&g_db2Api));
    api->PublishInterface("wxl.db2.strings", WXL_DB2_STRING_API_VERSION,
                          const_cast<WXL_Db2StringApi*>(&g_db2StringApi));
    api->PublishInterface("wxl.db2.filtered", WXL_DB2_FILTER_API_VERSION,
                          const_cast<WXL_Db2FilterApi*>(wxl_db2::FilterApi()));

    if (wxl_db2::ConfigBool("WXL_DB2_FDID", true))
        api->PublishInterface("wxl.fdid", WXL_FDID_API_VERSION,
                              const_cast<WXL_FdidApi*>(&g_fdidApi));
    else
        api->Log(WXL_LOG_INFO, "wxl-db2", "FileDataID resolver disabled by configuration");

    api->PublishInterface("wxl.modeldata", WXL_MODEL_DATA_API_VERSION,
                          const_cast<WXL_ModelDataApi*>(&g_modelDataApi));
    api->PublishInterface("wxl.appearance", WXL_APPEARANCE_API_VERSION,
                          const_cast<WXL_AppearanceApi*>(&g_appearanceApi));

    if (wxl_db2::ConfigBool("WXL_DB2_LIGHTS", true))
    {
        api->PublishInterface("wxl.light", WXL_LIGHT_API_VERSION,
                              const_cast<WXL_LightApi*>(&g_lightApi));
        wxl_db2::InstallLightStore();
    }
    else
        api->Log(WXL_LOG_INFO, "wxl-db2", "retail lighting tables disabled by configuration");

    api->PublishInterface("wxl.retail-db2", WXL_RETAIL_DB2_API_VERSION,
                          const_cast<WXL_RetailDb2Api*>(wxl_db2::RetailApi()));
    if (wxl_db2::ConfigBool("WXL_DB2_RETAIL_ITEMS", true))
        wxl_db2::InstallRetailItemIndex();
    else
        api->Log(WXL_LOG_INFO, "wxl-db2", "retail item/collection graph disabled by configuration");

    api->PublishInterface("wxl.retail-spell-db2", WXL_RETAIL_SPELL_DB2_API_VERSION,
                          const_cast<WXL_RetailSpellDb2Api*>(wxl_db2::RetailSpellApi()));
    if (!wxl_db2::ConfigBool("WXL_DB2_RETAIL_SPELLS", true))
        api->Log(WXL_LOG_INFO, "wxl-db2", "retail spell-visual graph disabled by configuration");

    api->Log(WXL_LOG_INFO, "wxl-db2",
             "DB2 v1 + filtered DB2 + ModelFileData + appearance recipes"
             " + optional retail data services published");
    return 1;
}
