// Plain-C facade over the immutable retail item/catalog snapshots owned by wxl-db2.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ItemDisplayIndex.hpp"
#include "RetailItemCatalog.hpp"
#include "../ExtensionApi.hpp"

#include "wxl/RetailDb2Api.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>

namespace
{
    namespace display = wxl::runtime::db2::itemdisplay;
    namespace items = wxl::runtime::db2::retailitems;

    using CatalogLease = std::shared_ptr<const items::Catalog>;
    using IndexLease = std::shared_ptr<const display::Index>;

    int __cdecl Enabled()
    {
        return wxl_db2::ConfigBool("WXL_DB2_RETAIL_ITEMS", true) ? 1 : 0;
    }

    void* __cdecl AcquireCatalog()
    {
        CatalogLease current = items::Current();
        return current ? new CatalogLease(std::move(current)) : nullptr;
    }

    void __cdecl ReleaseCatalog(void* handle)
    {
        delete static_cast<CatalogLease*>(handle);
    }

    uint32_t __cdecl CatalogItemCount(void* handle)
    {
        return handle ? static_cast<uint32_t>((*static_cast<CatalogLease*>(handle))->itemOrder.size()) : 0;
    }

    int __cdecl CatalogItemAt(void* handle, uint32_t position, uint32_t* itemId,
                              WXL_RetailItemInfo* out)
    {
        if (!handle || !itemId || !out) return 0;
        const auto& catalog = **static_cast<CatalogLease*>(handle);
        if (position >= catalog.itemOrder.size()) return 0;
        const auto it = catalog.items.find(catalog.itemOrder[position]);
        if (it == catalog.items.end()) return 0;
        *itemId = it->first;
        const items::Item& value = it->second;
        *out = WXL_RetailItemInfo{
            value.classId, value.subclassId, value.soundOverride, value.itemGroupSoundsId,
            value.material, value.inventoryType, value.sheatheType, value.displayId,
            value.appearanceId, value.iconFileDataId,
        };
        return 1;
    }

    uint32_t __cdecl CatalogVariantCount(void* handle)
    {
        return handle ? static_cast<uint32_t>((*static_cast<CatalogLease*>(handle))->variantOrder.size()) : 0;
    }

    int __cdecl CatalogVariantAt(void* handle, uint32_t position, uint64_t* key,
                                 WXL_RetailItemVariant* out)
    {
        if (!handle || !key || !out) return 0;
        const auto& catalog = **static_cast<CatalogLease*>(handle);
        if (position >= catalog.variantOrder.size()) return 0;
        const auto it = catalog.variants.find(catalog.variantOrder[position]);
        if (it == catalog.variants.end()) return 0;
        *key = it->first;
        *out = WXL_RetailItemVariant{
            it->second.displayId, it->second.appearanceId, it->second.iconFileDataId,
        };
        return 1;
    }

    uint32_t __cdecl CatalogIconCount(void* handle)
    {
        return handle ? static_cast<uint32_t>((*static_cast<CatalogLease*>(handle))->iconOrder.size()) : 0;
    }

    int __cdecl CatalogIconAt(void* handle, uint32_t position, uint32_t* displayId,
                              uint32_t* fileDataId)
    {
        if (!handle || !displayId || !fileDataId) return 0;
        const auto& catalog = **static_cast<CatalogLease*>(handle);
        if (position >= catalog.iconOrder.size()) return 0;
        const auto it = catalog.iconByDisplay.find(catalog.iconOrder[position]);
        if (it == catalog.iconByDisplay.end()) return 0;
        *displayId = it->first;
        *fileDataId = it->second;
        return 1;
    }

    uint32_t __cdecl CatalogSoundCount(void* handle)
    {
        return handle ? static_cast<uint32_t>((*static_cast<CatalogLease*>(handle))->soundOrder.size()) : 0;
    }

    int __cdecl CatalogSoundAt(void* handle, uint32_t position, uint32_t* displayId,
                               uint32_t* soundId)
    {
        if (!handle || !displayId || !soundId) return 0;
        const auto& catalog = **static_cast<CatalogLease*>(handle);
        if (position >= catalog.soundOrder.size()) return 0;
        const auto it = catalog.soundByDisplay.find(catalog.soundOrder[position]);
        if (it == catalog.soundByDisplay.end()) return 0;
        *displayId = it->first;
        *soundId = it->second;
        return 1;
    }

    uint32_t __cdecl CatalogDisplayCount(void* handle)
    {
        return handle ? static_cast<uint32_t>((*static_cast<CatalogLease*>(handle))->displayOrder.size()) : 0;
    }

    int __cdecl CatalogDisplayAt(void* handle, uint32_t position, uint32_t* displayId)
    {
        if (!handle || !displayId) return 0;
        const auto& catalog = **static_cast<CatalogLease*>(handle);
        if (position >= catalog.displayOrder.size()) return 0;
        *displayId = catalog.displayOrder[position];
        return 1;
    }

    void __cdecl RequestDisplays(const uint32_t* displayIds, uint32_t count)
    {
        if (!displayIds || !count || !Enabled()) return;
        display::RequestBatch(std::span<const uint32_t>(displayIds, count));
    }

    uint64_t __cdecl IndexGeneration() { return display::Generation(); }

    void* __cdecl AcquireIndex()
    {
        IndexLease current = display::Current();
        return current ? new IndexLease(std::move(current)) : nullptr;
    }

    void __cdecl ReleaseIndex(void* handle) { delete static_cast<IndexLease*>(handle); }

    const display::Index* Index(void* handle)
    {
        return handle ? static_cast<IndexLease*>(handle)->get() : nullptr;
    }

    int __cdecl IndexModelsReady(void* handle)
    {
        const display::Index* index = Index(handle);
        return index && index->modelsReady ? 1 : 0;
    }

    int __cdecl IndexMaterialsReady(void* handle)
    {
        const display::Index* index = Index(handle);
        return index && index->materialsReady ? 1 : 0;
    }

    uint32_t __cdecl IndexResolvedDisplayCount(void* handle)
    {
        const display::Index* index = Index(handle);
        return index ? static_cast<uint32_t>(index->resolvedDisplayOrder.size()) : 0;
    }

    int __cdecl IndexResolvedDisplayAt(void* handle, uint32_t position, uint32_t* displayId)
    {
        const display::Index* index = Index(handle);
        if (!index || !displayId) return 0;
        if (position >= index->resolvedDisplayOrder.size()) return 0;
        *displayId = index->resolvedDisplayOrder[position];
        return 1;
    }

    int __cdecl IndexDisplayRecord(void* handle, uint32_t displayId,
                                   WXL_RetailDisplayRecord* out)
    {
        const display::Index* index = Index(handle);
        if (!index || !out) return 0;
        const auto found = index->displayRecords.find(displayId);
        if (found == index->displayRecords.end()) return 0;
        const display::DisplayRecord& value = found->second;
        *out = WXL_RetailDisplayRecord{};
        out->inventoryType = value.inventoryType;
        out->flags = value.flags;
        out->itemVisual = value.itemVisual;
        out->particleColor = value.particleColor;
        std::copy(value.nativeModelNames.begin(), value.nativeModelNames.end(), out->nativeModelNames);
        std::copy(value.nativeModelTextures.begin(), value.nativeModelTextures.end(), out->nativeModelTextures);
        std::copy(value.geosets.begin(), value.geosets.end(), out->geosets);
        std::copy(value.helmetVis.begin(), value.helmetVis.end(), out->helmetVis);
        std::copy(value.componentTextures.begin(), value.componentTextures.end(), out->componentTextures);
        return 1;
    }

    uint32_t __cdecl IndexModelCount(void* handle, uint32_t displayId)
    {
        const display::Index* index = Index(handle);
        const auto found = index ? index->models.find(displayId) : display::Index::ModelMap::const_iterator{};
        return !index || found == index->models.end() ? 0 : static_cast<uint32_t>(found->second.size());
    }

    int __cdecl IndexModelAt(void* handle, uint32_t displayId, uint32_t position,
                             WXL_RetailModelEntry* out)
    {
        const display::Index* index = Index(handle);
        if (!index || !out) return 0;
        const auto found = index->models.find(displayId);
        if (found == index->models.end() || position >= found->second.size()) return 0;
        const display::ModelEntry& value = found->second[position];
        *out = WXL_RetailModelEntry{
            value.modelSlot, value.attachId, value.modelIndex, value.raceId, value.genderId,
            value.modelFlags, value.textureFlags, value.folder, value.model, value.texture, {},
        };
        std::copy(std::begin(value.geoFilter.ids), std::end(value.geoFilter.ids), out->geoFilter.ids);
        out->geoFilter.count = value.geoFilter.count;
        return 1;
    }

    uint32_t __cdecl IndexMaterialCount(void* handle, uint32_t displayId)
    {
        const display::Index* index = Index(handle);
        if (!index) return 0;
        const auto found = index->materials.find(displayId);
        return found == index->materials.end() ? 0 : static_cast<uint32_t>(found->second.size());
    }

    int __cdecl IndexMaterialAt(void* handle, uint32_t displayId, uint32_t position,
                                WXL_RetailMaterialEntry* out)
    {
        const display::Index* index = Index(handle);
        if (!index || !out) return 0;
        const auto found = index->materials.find(displayId);
        if (found == index->materials.end() || position >= found->second.size()) return 0;
        const display::MaterialEntry& value = found->second[position];
        *out = WXL_RetailMaterialEntry{
            value.modelIndex, value.modelColumn, value.layer, value.textureType,
            value.raceId, value.genderId, value.folder, value.model, value.texture,
            value.skinSectionIds, value.batchIndexes, value.targetSkinSectionIds,
            value.targetBatchIndexes, value.targetMode,
        };
        return 1;
    }

    uint32_t __cdecl IndexHelmetRuleCount(void* handle, uint32_t visibilityId)
    {
        const display::Index* index = Index(handle);
        if (!index || !index->helmetData) return 0;
        const auto found = index->helmetData->geosetsByVis.find(visibilityId);
        return found == index->helmetData->geosetsByVis.end()
            ? 0 : static_cast<uint32_t>(found->second.size());
    }

    int __cdecl IndexHelmetRuleAt(void* handle, uint32_t visibilityId, uint32_t position,
                                  WXL_RetailHelmetGeosetRule* out)
    {
        const display::Index* index = Index(handle);
        if (!index || !index->helmetData || !out) return 0;
        const auto found = index->helmetData->geosetsByVis.find(visibilityId);
        if (found == index->helmetData->geosetsByVis.end() || position >= found->second.size()) return 0;
        const display::HelmetGeosetRule& value = found->second[position];
        *out = WXL_RetailHelmetGeosetRule{
            value.raceId, value.hideGroup, value.raceBitSelection, value.flags,
        };
        return 1;
    }

    int __cdecl IndexHelmetAnimScale(void* handle, uint32_t visibilityId, uint32_t raceId,
                                     float* out)
    {
        const display::Index* index = Index(handle);
        if (!index || !index->helmetData || !out) return 0;
        const uint64_t key = (static_cast<uint64_t>(visibilityId) << 32) | raceId;
        const auto found = index->helmetData->animScaleByVisRace.find(key);
        if (found == index->helmetData->animScaleByVisRace.end()) return 0;
        *out = found->second;
        return 1;
    }

    const WXL_RetailDb2Api g_retailApi = {
        sizeof(WXL_RetailDb2Api), WXL_RETAIL_DB2_API_VERSION,
        &Enabled,
        &AcquireCatalog, &ReleaseCatalog, &CatalogItemCount, &CatalogItemAt,
        &CatalogVariantCount, &CatalogVariantAt, &CatalogIconCount, &CatalogIconAt,
        &CatalogSoundCount, &CatalogSoundAt, &CatalogDisplayCount, &CatalogDisplayAt,
        &RequestDisplays, &IndexGeneration, &AcquireIndex, &ReleaseIndex,
        &IndexModelsReady, &IndexMaterialsReady, &IndexResolvedDisplayCount,
        &IndexResolvedDisplayAt, &IndexDisplayRecord, &IndexModelCount, &IndexModelAt,
        &IndexMaterialCount, &IndexMaterialAt, &IndexHelmetRuleCount, &IndexHelmetRuleAt,
        &IndexHelmetAnimScale,
    };
}

const WXL_RetailDb2Api* wxl_db2::RetailApi()
{
    return &g_retailApi;
}
