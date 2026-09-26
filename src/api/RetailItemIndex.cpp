// Demand-driven native retail item-display graph backed by host-decoded WDC5 snapshots.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ItemDisplayIndex.hpp"
#include "RetailItemCatalog.hpp"
#include "RetailSchemas.hpp"

#include "../ExtensionApi.hpp"
#include "FdidResolver.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace db2 = wxl::runtime::db2;
    namespace itemdisplay = wxl::runtime::db2::itemdisplay;
    namespace retail = wxl::runtime::db2::retail;
    namespace retailitems = wxl::runtime::db2::retailitems;
    using Row = db2::wdc5::Row;

    constexpr size_t kMaxFilterValues = 4096;
    constexpr uint32_t kMissing = static_cast<uint32_t>(-1);
    constexpr std::string_view kObjectMarker = "item\\objectcomponents\\";
    constexpr std::string_view kCollectionMarker = "item\\objectcomponents\\collection\\";
    constexpr std::string_view kCollectionsMarker = "item\\objectcomponents\\collections\\";

    std::unordered_map<uint32_t, uint32_t> g_inventoryByDisplay;

    bool ItemDetailLog()
    {
        static const bool enabled = wxl_db2::ConfigBool(
            "WXL_DB2_RETAIL_ITEM_DETAIL_LOG", false);
        return enabled;
    }

    bool Load(const db2::Definition& definition, db2::Table& table)
    {
        std::string error;
        if (!table.Load(definition, &error))
        {
            WLOG_ERROR("retail-item-index: %.*s: %s",
                       static_cast<int>(definition.name.size()), definition.name.data(), error.c_str());
            return false;
        }
        WLOG_INFO("retail-item-index: decoded %.*s rows=%zu",
                  static_cast<int>(definition.name.size()), definition.name.data(), table.Rows().size());
        return true;
    }

    bool LoadFiltered(const db2::Definition& definition, db2::wdc5::SnapshotFilterSource source,
                      uint16_t field, std::span<const uint32_t> values, db2::Table& table)
    {
        db2::wdc5::SnapshotFilter filter{source, field, 0, values};
        std::string error;
        if (!table.LoadFiltered(definition, filter, &error))
        {
            WLOG_ERROR("retail-item-index: filtered %.*s: %s",
                       static_cast<int>(definition.name.size()), definition.name.data(), error.c_str());
            return false;
        }
        return true;
    }

    uint32_t InventoryTypeFromDisplayType(uint32_t displayType)
    {
        switch (displayType)
        {
            case 0: return 1; case 1: return 3; case 3: return 5; case 4: return 6;
            case 5: return 7; case 6: return 8; case 7: return 9; case 8: return 10;
            case 9: return 16; default: return 0;
        }
    }

    int InventoryTypeRank(uint32_t inventoryType)
    {
        static constexpr std::array<uint32_t, 12> order{20, 5, 10, 7, 8, 6, 9, 4, 19, 3, 1, 16};
        const auto found = std::ranges::find(order, inventoryType);
        return found == order.end() ? static_cast<int>(order.size())
                                    : static_cast<int>(found - order.begin());
    }

    void PreferInventoryType(uint32_t displayId, uint32_t inventoryType)
    {
        if (!displayId || !inventoryType) return;
        auto [found, inserted] = g_inventoryByDisplay.try_emplace(displayId, inventoryType);
        if (!inserted && InventoryTypeRank(inventoryType) < InventoryTypeRank(found->second))
            found->second = inventoryType;
    }

    bool UsesNativeWeaponOwner(uint32_t inventoryType)
    {
        switch (inventoryType)
        {
            case 13: // one-hand
            case 14: // shield
            case 15: // ranged
            case 17: // two-hand
            case 21: // main hand
            case 22: // off hand
            case 23: // held in off hand
            case 25: // thrown
            case 26: // ranged right
            case 28: // relic
                return true;
            default:
                return false;
        }
    }

    uint32_t WotlkItemGroupSound(const retailitems::Item& item)
    {
        // Retail ItemGroupSounds IDs are keys in the modern table, not WotLK's
        // material-family row IDs. Translate by the stable item class/subclass
        // contract used by both clients.
        if (item.classId == 4) // Armor
        {
            switch (item.subclassId)
            {
                case 1: // Cloth
                case 2: // Leather
                    return 7;
                case 3: // Mail
                    return 10;
                case 4: // Plate
                    return 11;
                case 6: // Shield
                    return 9;
                default:
                    break;
            }

            // Cosmetic/misc armor frequently uses subclass zero. Its material
            // still preserves the original armor family.
            switch (item.material)
            {
                case 5: return 10; // Chain
                case 6: return 11; // Plate
                case 7: // Cloth
                case 8: // Leather
                    return 7;
                default:
                    return 7;
            }
        }

        if (item.classId == 2) // Weapon
        {
            switch (item.subclassId)
            {
                case 0:  return 8;  // One-handed axe
                case 1:  return 9;  // Two-handed axe
                case 2:  return 12; // Bow
                case 3:  return 8;  // Gun
                case 4:  return 8;  // One-handed mace
                case 5:  return 9;  // Two-handed mace
                case 6:  return 9;  // Polearm
                case 7:  return 8;  // One-handed sword
                case 8:  return 9;  // Two-handed sword
                case 10: return 13; // Staff
                case 13: return 8;  // Fist weapon
                case 15: return 8;  // Dagger
                case 16: return 8;  // Thrown
                case 18: return 12; // Crossbow
                case 19: return 21; // Wand
                default: return item.material == 2 ? 13u : 8u;
            }
        }

        // Equippable non-armor/non-weapon items use WotLK's neutral soft-item
        // family rather than an unrelated modern table key.
        return 7;
    }

    bool BuildRelationshipIndex()
    {
        std::string error;
        if (!db2::ValidateDefinitions(retail::All, &error))
        {
            WLOG_ERROR("retail-item-index: invalid focused schema graph: %s", error.c_str());
            return false;
        }

        db2::Table table;
        if (!Load(retail::Item, table)) return false;
        auto catalog = std::make_shared<retailitems::Catalog>();
        catalog->items.reserve(table.Rows().size());
        size_t normalizedSoundOverrides = 0;
        size_t normalizedCosmeticArmor = 0;
        for (const Row& row : table.Rows())
        {
            retailitems::Item item;
            item.classId = table.Value(row, "ClassID");
            item.subclassId = table.Value(row, "SubclassID");
            // Retail repurposed armor subclass 5 for cosmetic armor. Build
            // 12340 still labels that value Buckler (OBSOLETE) and applies its
            // obsolete proficiency rules, which can reject otherwise valid
            // heritage/cosmetic pieces. WotLK's miscellaneous armor subclass
            // is the compatible unrestricted representation; Material keeps
            // the original cloth/mail/plate presentation family for sounds.
            if (item.classId == 4 && item.subclassId == 5)
            {
                item.subclassId = 0;
                ++normalizedCosmeticArmor;
            }
            item.soundOverride = table.Value(row, "Sound_override_subclassID");
            // Retail stores the unsigned-byte sentinel 255 for "use this item's own subclass".
            // WotLK's Item.dbc accessor expects the 32-bit -1 sentinel. Passing 255 makes the
            // native equip path search a nonexistent override subclass and suppresses its sound.
            if (item.soundOverride == 0xFFu)
            {
                item.soundOverride = kMissing;
                ++normalizedSoundOverrides;
            }
            item.itemGroupSoundsId = table.Value(row, "ItemGroupSoundsID");
            item.material = table.Value(row, "Material");
            item.inventoryType = table.Value(row, "InventoryType");
            item.sheatheType = table.Value(row, "SheatheType");
            item.iconFileDataId = table.Value(row, "IconFileDataID");
            catalog->items.emplace(row.id, item);
        }

        table = {};
        if (!Load(retail::ItemAppearance, table)) return false;
        struct Appearance
        {
            uint32_t displayId = 0;
            uint32_t iconFileDataId = 0;
        };
        std::unordered_map<uint32_t, Appearance> appearances;
        appearances.reserve(table.Rows().size());
        for (const Row& row : table.Rows())
        {
            const uint32_t displayId = table.Value(row, "ItemDisplayInfoID");
            appearances.emplace(row.id, Appearance{
                displayId, table.Value(row, "DefaultIconFileDataID")
            });
            if (displayId) catalog->displayIds.insert(displayId);
            PreferInventoryType(displayId, InventoryTypeFromDisplayType(table.Value(row, "DisplayType")));
        }

        table = {};
        if (!Load(retail::ItemModifiedAppearance, table)) return false;
        std::unordered_map<uint32_t, std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> chosen;
        for (const Row& row : table.Rows())
        {
            const uint32_t itemId = table.Value(row, "ItemID");
            uint32_t modifiedAppearanceId = table.Value(row, "ID");
            if (!modifiedAppearanceId) modifiedAppearanceId = row.id;
            const uint32_t appearanceId = table.Value(row, "ItemAppearanceID");
            const uint32_t modifier = table.Value(row, "ItemAppearanceModifierID");
            const uint32_t order = table.Value(row, "OrderIndex");
            const auto appearance = appearances.find(appearanceId);
            if (appearance == appearances.end() || !appearance->second.displayId) continue;
            const auto rank = std::tuple{
                modifier == 0 ? 0u : 1u, order, appearance->second.displayId, appearanceId
            };
            const auto found = chosen.find(itemId);
            if (found == chosen.end() || rank < found->second) chosen[itemId] = rank;

            const uint64_t variantKey = (static_cast<uint64_t>(itemId) << 32) | modifier;
            retailitems::Catalog::Variant variant{
                modifiedAppearanceId, itemId, modifier, appearance->second.displayId, appearanceId,
                appearance->second.iconFileDataId,
                table.Value(row, "TransmogSourceTypeEnum"), table.Value(row, "Flags")
            };
            catalog->variants.try_emplace(variantKey, variant);
            catalog->variantsByModifiedAppearance.try_emplace(modifiedAppearanceId, variant);
            if (const auto item = catalog->items.find(itemId); item != catalog->items.end())
            {
                PreferInventoryType(appearance->second.displayId, item->second.inventoryType);
                if (item->second.inventoryType)
                    catalog->soundByDisplay.try_emplace(
                        appearance->second.displayId, WotlkItemGroupSound(item->second));
            }
            if (appearance->second.displayId && appearance->second.iconFileDataId)
                catalog->iconByDisplay.try_emplace(
                    appearance->second.displayId, appearance->second.iconFileDataId);
        }
        for (const auto& [itemId, selection] : chosen)
        {
            const auto item = catalog->items.find(itemId);
            if (item == catalog->items.end()) continue;
            item->second.displayId = std::get<2>(selection);
            item->second.appearanceId = std::get<3>(selection);
            const auto appearance = appearances.find(item->second.appearanceId);
            if (appearance != appearances.end() && appearance->second.iconFileDataId)
                item->second.iconFileDataId = appearance->second.iconFileDataId;
            PreferInventoryType(item->second.displayId, item->second.inventoryType);
            if (item->second.displayId && item->second.inventoryType)
                catalog->soundByDisplay.try_emplace(
                    item->second.displayId, WotlkItemGroupSound(item->second));
            if (item->second.displayId && item->second.iconFileDataId)
                catalog->iconByDisplay.try_emplace(
                    item->second.displayId, item->second.iconFileDataId);
        }

        // Build immutable O(1) positional views for the cross-extension C ABI.
        // The lookup maps remain the catalog's source of truth.
        catalog->itemOrder.reserve(catalog->items.size());
        for (const auto& [itemId, _] : catalog->items)
            catalog->itemOrder.push_back(itemId);
        catalog->variantOrder.reserve(catalog->variants.size());
        for (const auto& [key, _] : catalog->variants)
            catalog->variantOrder.push_back(key);
        catalog->iconOrder.reserve(catalog->iconByDisplay.size());
        for (const auto& [displayId, _] : catalog->iconByDisplay)
            catalog->iconOrder.push_back(displayId);
        catalog->soundOrder.reserve(catalog->soundByDisplay.size());
        for (const auto& [displayId, _] : catalog->soundByDisplay)
            catalog->soundOrder.push_back(displayId);
        catalog->displayOrder.reserve(catalog->displayIds.size());
        for (uint32_t displayId : catalog->displayIds)
            catalog->displayOrder.push_back(displayId);

        const size_t catalogItems = catalog->items.size();
        const size_t catalogIcons = catalog->iconByDisplay.size();
        const size_t catalogSounds = catalog->soundByDisplay.size();
        const size_t catalogVariants = catalog->variants.size();
        retailitems::Publish(std::move(catalog));
        auto initial = std::make_shared<itemdisplay::Index>();
        auto helmetData = std::make_shared<itemdisplay::HelmetData>();

        table = {};
        if (!Load(retail::HelmetGeosetData, table)) return false;
        size_t helmetGeosetRules = 0;
        for (const Row& row : table.Rows())
        {
            if (!row.parentId) continue;
            helmetData->geosetsByVis[row.parentId].push_back({
                table.Value(row, "RaceID"),
                table.Value(row, "HideGeosetGroup"),
                table.Value(row, "RaceBitSelection"),
                table.Value(row, "Flags"),
            });
            ++helmetGeosetRules;
        }

        table = {};
        if (!Load(retail::HelmetAnimScaling, table)) return false;
        size_t helmetAnimScales = 0;
        for (const Row& row : table.Rows())
        {
            const uint32_t raceId = table.Value(row, "RaceID");
            if (!row.parentId || !raceId) continue;
            const uint64_t key =
                (static_cast<uint64_t>(row.parentId) << 32) | raceId;
            helmetData->animScaleByVisRace[key] =
                std::bit_cast<float>(table.Value(row, "Amount"));
            ++helmetAnimScales;
        }

        initial->helmetData = std::move(helmetData);
        initial->modelsReady = true;
        itemdisplay::Publish(std::move(initial));
        WLOG_INFO("retail-item-index: relationship graph ready displays=%zu items=%zu icons=%zu sounds=%zu variants=%zu helmetRules=%zu helmetScales=%zu soundSentinels=%zu cosmeticArmor=%zu",
                  g_inventoryByDisplay.size(), catalogItems, catalogIcons, catalogSounds,
                  catalogVariants, helmetGeosetRules, helmetAnimScales,
                  normalizedSoundOverrides, normalizedCosmeticArmor);
        return true;
    }

    std::string Lower(std::string value)
    {
        for (char& ch : value)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    }

    std::string Normalize(std::string value)
    {
        std::replace(value.begin(), value.end(), '/', '\\');
        return Lower(std::move(value));
    }

    bool EndsWith(std::string_view value, std::string_view suffix)
    {
        return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
    }

    bool Contains(std::string_view value, std::string_view needle)
    {
        return value.find(needle) != std::string_view::npos;
    }

    std::string Basename(std::string_view path)
    {
        const size_t slash = path.find_last_of("\\/");
        return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
    }

    std::string WithoutExtension(std::string value)
    {
        const size_t slash = value.find_last_of("\\/");
        const size_t dot = value.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) value.resize(dot);
        return value;
    }

    std::string Capitalize(std::string value)
    {
        if (!value.empty())
            value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
        return value;
    }

    std::string ObjectFolder(std::string_view path)
    {
        const std::string lower = Normalize(std::string(path));
        const size_t marker = lower.find(kObjectMarker);
        if (marker == std::string::npos) return {};
        const size_t begin = marker + kObjectMarker.size();
        const size_t end = lower.find('\\', begin);
        return end == std::string::npos || end == begin
            ? std::string{} : Capitalize(lower.substr(begin, end - begin));
    }

    std::string AfterMarker(std::string_view path, std::string_view marker)
    {
        std::string normalized(path);
        std::replace(normalized.begin(), normalized.end(), '/', '\\');
        const size_t at = Lower(normalized).find(marker);
        return at == std::string::npos ? Basename(normalized)
                                       : normalized.substr(at + marker.size());
    }

    std::string_view CollectionMarker(std::string_view normalizedPath)
    {
        if (Contains(normalizedPath, kCollectionMarker)) return kCollectionMarker;
        if (Contains(normalizedPath, kCollectionsMarker)) return kCollectionsMarker;
        return {};
    }

    std::string ModelName(std::string_view path)
    {
        const std::string normalized = Normalize(std::string(path));
        const std::string_view marker = CollectionMarker(normalized);
        std::string name = marker.empty() ? Basename(path) : AfterMarker(path, marker);
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) name += ".mdx";
        else name.replace(dot, std::string::npos, ".mdx");
        return name;
    }

    std::string TextureName(std::string_view path)
    {
        const std::string normalized = Normalize(std::string(path));
        const std::string_view marker = CollectionMarker(normalized);
        return WithoutExtension(marker.empty() ? Basename(path) : AfterMarker(path, marker));
    }

    std::string ComponentTextureName(std::string_view path)
    {
        std::string name = TextureName(path);
        // Keep TextureFilePath's collision-safe FileDataID suffix. Retail color variants
        // commonly share the same Blizzard stem, so removing "_<fdid>" collapses (for
        // example) the normal and Mythic chest rows onto one host alias. This mirrors the
        // proven retail DB2 owner: only legacy terminal gender suffixes are neutralized.
        const std::string lower = Lower(name);
        if (lower.size() > 2 && lower[lower.size() - 2] == '_' &&
            (lower.back() == 'm' || lower.back() == 'f'))
            name.resize(name.size() - 2);
        return name;
    }

    uint32_t ModelSlot(uint32_t inventoryType)
    {
        switch (inventoryType)
        {
            case 1: return 0; case 3: return 1; case 4: return 2; case 5: case 20: return 3;
            case 6: return 4; case 7: return 5; case 8: return 6; case 9: return 7;
            case 10: return 8; case 16: return 10; case 19: return 9;
            default: return static_cast<uint32_t>(-1);
        }
    }

    uint32_t Attach(uint32_t inventoryType, size_t modelIndex,
                    uint32_t modelType, bool collection,
                    bool raceGenderCollection)
    {
        const uint32_t side = modelIndex == 0 ? 0 : 1;
        switch (inventoryType)
        {
            case 1: return collection && modelType == 1 ? 19 : (side == 0 ? 11 : 55);
            case 3: return side == 0 ? 6 : 5;
            case 4: case 5: case 19: case 20: return collection ? 19 : 34;
            // Dedicated collection-folder belt components have neither a
            // race/gender ComponentModelFileData row nor ModelType 1 and are
            // authored in waist-attachment space. Older full race/gender
            // collections can still report ModelType 0, so component metadata
            // is the authoritative fallback that keeps those character-skinned
            // models on the synthetic root for per-slot SKIN filtering.
            case 6:
                return collection &&
                               (modelType == 1 || raceGenderCollection)
                           ? 19
                           : 53;
            case 7: return collection ? 19 : (side == 0 ? 9 : 10);
            case 8: return collection ? 19 : (side == 0 ? 47 : 48);
            case 9: return collection ? 19 : (side == 0 ? 3 : 4);
            case 10: return collection ? 19 : (side == 0 ? 1 : 2);
            case 16: return modelType == 1 ? 19 : 12;
            default: return collection ? 19 : static_cast<uint32_t>(-1);
        }
    }

    std::shared_ptr<itemdisplay::Index> CloneCurrent()
    {
        auto next = std::make_shared<itemdisplay::Index>();
        if (const auto current = itemdisplay::Current())
        {
            next->modelsReady = current->modelsReady;
            next->materialsReady = current->materialsReady;
            next->resolvedDisplays = current->resolvedDisplays;
            next->resolvedDisplayOrder = current->resolvedDisplayOrder;
            next->helmetData = current->helmetData;
            for (const auto& [displayId, entries] : current->models)
            {
                auto& copies = next->models[displayId];
                copies.reserve(entries.size());
                for (const itemdisplay::ModelEntry& entry : entries)
                {
                    itemdisplay::ModelEntry copy = entry;
                    copy.folder = next->Intern(entry.folder);
                    copy.model = next->Intern(entry.model);
                    copy.texture = next->Intern(entry.texture);
                    copies.push_back(copy);
                }
            }
            for (const auto& [displayId, entries] : current->materials)
            {
                auto& copies = next->materials[displayId];
                copies.reserve(entries.size());
                for (const itemdisplay::MaterialEntry& entry : entries)
                {
                    itemdisplay::MaterialEntry copy = entry;
                    copy.folder = next->Intern(entry.folder);
                    copy.model = next->Intern(entry.model);
                    copy.texture = next->Intern(entry.texture);
                    copy.skinSectionIds = next->Intern(entry.skinSectionIds);
                    copy.batchIndexes = next->Intern(entry.batchIndexes);
                    copy.targetSkinSectionIds = next->Intern(entry.targetSkinSectionIds);
                    copy.targetBatchIndexes = next->Intern(entry.targetBatchIndexes);
                    copy.targetMode = next->Intern(entry.targetMode);
                    copies.push_back(copy);
                }
            }
            for (const auto& [displayId, record] : current->displayRecords)
            {
                itemdisplay::DisplayRecord copy = record;
                for (size_t i = 0; i < copy.nativeModelNames.size(); ++i)
                {
                    copy.nativeModelNames[i] = next->Intern(
                        record.nativeModelNames[i] ? record.nativeModelNames[i] : "");
                    copy.nativeModelTextures[i] = next->Intern(
                        record.nativeModelTextures[i] ? record.nativeModelTextures[i] : "");
                }
                for (size_t i = 0; i < copy.componentTextures.size(); ++i)
                    copy.componentTextures[i] = next->Intern(
                        record.componentTextures[i] ? record.componentTextures[i] : "");
                next->displayRecords.emplace(displayId, copy);
            }
        }
        return next;
    }

    bool ResolveDisplayBatch(std::span<const uint32_t> displayIds)
    {
        db2::Table displays;
        if (!LoadFiltered(retail::ItemDisplayInfo, db2::wdc5::SnapshotFilterSource::RowId,
                          0, displayIds, displays))
            return false;

        struct DisplaySource
        {
            uint32_t id = 0;
            uint32_t flags = 0;
            uint32_t itemVisual = 0;
            uint32_t particleColor = 0;
            std::array<uint32_t, 2> models{};
            std::array<uint32_t, 2> textures{};
            std::array<uint32_t, 2> modelTypes{};
            std::array<uint32_t, 6> geosets{};
            std::array<uint32_t, 2> helmetVis{};
        };
        std::vector<DisplaySource> sources;
        std::vector<uint32_t> modelResources;
        std::vector<uint32_t> textureResources;
        for (const Row& row : displays.Rows())
        {
            DisplaySource source;
            source.id = row.id;
            source.flags = displays.Value(row, "Flags");
            source.itemVisual = displays.Value(row, "ItemVisual");
            source.particleColor = displays.Value(row, "ParticleColorID");
            for (size_t i = 0; i < 2; ++i)
            {
                source.models[i] = displays.Value(row, "ModelResourcesID", i);
                source.textures[i] = displays.Value(row, "ModelMaterialResourcesID", i);
                source.modelTypes[i] = displays.Value(row, "ModelType", i);
                if (source.models[i]) modelResources.push_back(source.models[i]);
                if (source.textures[i]) textureResources.push_back(source.textures[i]);
            }
            for (size_t i = 0; i < source.geosets.size(); ++i)
                source.geosets[i] = displays.Value(row, "GeosetGroup", i);
            for (size_t i = 0; i < source.helmetVis.size(); ++i)
                source.helmetVis[i] = displays.Value(row, "HelmetGeosetVis", i);
            sources.push_back(source);
        }

        struct MaterialSource
        {
            uint32_t displayId = 0;
            uint32_t resourceId = 0;
            uint32_t textureType = kMissing;
            uint32_t modelIndex = kMissing;
            uint32_t layer = kMissing;
        };
        std::vector<MaterialSource> materialSources;

        db2::Table componentMaterials;
        if (!LoadFiltered(retail::ItemDisplayInfoMaterialRes,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          0, displayIds, componentMaterials))
            return false;
        for (const Row& row : componentMaterials.Rows())
        {
            const uint32_t resourceId = componentMaterials.Value(row, "MaterialResourcesID");
            materialSources.push_back({
                row.parentId, resourceId, kMissing, kMissing,
                componentMaterials.Value(row, "ComponentSection"),
            });
            if (resourceId) textureResources.push_back(resourceId);
        }

        db2::Table modelMaterials;
        if (!LoadFiltered(retail::ItemDisplayInfoModelMatRes,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          0, displayIds, modelMaterials))
            return false;
        for (const Row& row : modelMaterials.Rows())
        {
            const uint32_t resourceId = modelMaterials.Value(row, "MaterialResourcesID");
            materialSources.push_back({
                row.parentId, resourceId, modelMaterials.Value(row, "TextureType"),
                modelMaterials.Value(row, "ModelIndex"), kMissing,
            });
            if (resourceId) textureResources.push_back(resourceId);
        }

        std::ranges::sort(modelResources);
        modelResources.erase(std::unique(modelResources.begin(), modelResources.end()), modelResources.end());
        std::ranges::sort(textureResources);
        textureResources.erase(std::unique(textureResources.begin(), textureResources.end()), textureResources.end());

        struct ComponentModel
        {
            uint32_t raceId = 0;
            uint32_t genderId = kMissing;
            uint32_t position = kMissing;
        };
        std::unordered_map<uint32_t, std::vector<uint32_t>> modelFiles;
        std::unordered_map<uint32_t, ComponentModel> componentModels;
        if (!modelResources.empty())
        {
            db2::Table models;
            if (!LoadFiltered(retail::ModelFileData, db2::wdc5::SnapshotFilterSource::Field,
                              4, modelResources, models))
                return false;
            for (const Row& row : models.Rows())
                modelFiles[models.Value(row, "ModelResourcesID")].push_back(
                    models.Value(row, "FileDataID"));

            std::vector<uint32_t> fileIds;
            for (const auto& [resource, ids] : modelFiles)
                fileIds.insert(fileIds.end(), ids.begin(), ids.end());
            std::ranges::sort(fileIds);
            fileIds.erase(std::unique(fileIds.begin(), fileIds.end()), fileIds.end());
            if (!fileIds.empty())
            {
                db2::Table components;
                std::string optionalError;
                db2::wdc5::SnapshotFilter filter{
                    db2::wdc5::SnapshotFilterSource::RowId, 0, 0, fileIds
                };
                if (components.LoadFiltered(retail::ComponentModelFileData, filter, &optionalError))
                    for (const Row& row : components.Rows())
                        componentModels[row.id] = ComponentModel{
                            components.Value(row, "RaceID"),
                            components.Value(row, "GenderIndex") < 2
                                ? components.Value(row, "GenderIndex") : kMissing,
                            components.Value(row, "PositionIndex"),
                        };
                else
                    WLOG_WARN("retail-item-index: optional ComponentModelFileData: %s",
                              optionalError.c_str());
            }
        }

        struct ComponentTexture
        {
            uint32_t raceId = 0;
            uint32_t genderId = kMissing;
        };
        std::unordered_map<uint32_t, uint32_t> textureFiles;
        std::unordered_map<uint32_t, std::vector<uint32_t>> textureCandidates;
        std::unordered_map<uint32_t, ComponentTexture> componentTextures;
        if (!textureResources.empty())
        {
            db2::Table textures;
            if (!LoadFiltered(retail::TextureFileData, db2::wdc5::SnapshotFilterSource::Field,
                              2, textureResources, textures))
                return false;
            for (const Row& row : textures.Rows())
            {
                textureFiles.try_emplace(textures.Value(row, "MaterialResourcesID"),
                                         textures.Value(row, "FileDataID"));
                textureCandidates[textures.Value(row, "MaterialResourcesID")].push_back(
                    textures.Value(row, "FileDataID"));
            }

            std::vector<uint32_t> fileIds;
            for (const auto& [resource, ids] : textureCandidates)
                for (uint32_t id : ids)
                    if (id) fileIds.push_back(id);
            std::ranges::sort(fileIds);
            fileIds.erase(std::unique(fileIds.begin(), fileIds.end()), fileIds.end());
            if (!fileIds.empty())
            {
                db2::Table components;
                std::string optionalError;
                db2::wdc5::SnapshotFilter filter{
                    db2::wdc5::SnapshotFilterSource::RowId, 0, 0, fileIds
                };
                if (components.LoadFiltered(retail::ComponentTextureFileData, filter, &optionalError))
                {
                    for (const Row& row : components.Rows())
                    {
                        const uint32_t gender = components.Value(row, "GenderIndex");
                        componentTextures[row.id] = ComponentTexture{
                            components.Value(row, "RaceID"),
                            gender < 2 ? gender : kMissing,
                        };
                    }
                }
                else
                    WLOG_WARN("retail-item-index: optional ComponentTextureFileData: %s",
                              optionalError.c_str());
            }
        }

        std::unordered_map<uint32_t, std::string> paths;
        for (const auto& [resource, ids] : modelFiles)
            for (uint32_t id : ids)
            {
                if (const char* path = id ? wxl::db2::fdid::ResolveModel(id) : nullptr;
                    path && *path)
                    paths.emplace(id, path);
            }
        for (const auto& [resource, ids] : textureCandidates)
            for (uint32_t id : ids)
            {
                if (const char* path = id ? wxl::db2::fdid::ResolveTexture(id) : nullptr;
                    path && *path)
                    paths.emplace(id, path);
            }

        auto next = CloneCurrent();
        for (uint32_t displayId : displayIds)
            if (next->resolvedDisplays.insert(displayId).second)
                next->resolvedDisplayOrder.push_back(displayId);
        for (uint32_t displayId : displayIds) next->models.erase(displayId);
        for (uint32_t displayId : displayIds) next->materials.erase(displayId);
        for (uint32_t displayId : displayIds) next->displayRecords.erase(displayId);
        for (const DisplaySource& source : sources)
        {
            itemdisplay::DisplayRecord record;
            record.inventoryType = g_inventoryByDisplay.contains(source.id)
                ? g_inventoryByDisplay.at(source.id) : 0;
            record.flags = source.flags;
            record.itemVisual = source.itemVisual;
            record.particleColor = source.particleColor;
            record.geosets = source.geosets;
            record.helmetVis = source.helmetVis;
            next->displayRecords.emplace(source.id, record);
        }
        // Capes are rendered by the native character owner and commonly have
        // a ModelMaterialResourcesID without an accompanying M2 model. Preserve
        // those texture names in the injected ItemDisplayInfo row just as the
        // native DBC does. Restrict this to capes: armor collection models are
        // owned by RetailEquipment and weapons are populated from the selected
        // model candidate below.
        for (const DisplaySource& source : sources)
        {
            const auto display = next->displayRecords.find(source.id);
            if (display == next->displayRecords.end() ||
                display->second.inventoryType != 16)
                continue;
            for (size_t column = 0; column < source.textures.size(); ++column)
            {
                const auto file = textureFiles.find(source.textures[column]);
                if (file == textureFiles.end()) continue;
                const auto path = paths.find(file->second);
                if (path == paths.end()) continue;
                display->second.nativeModelTextures[column] =
                    next->Intern(TextureName(path->second));
            }
        }
        for (const MaterialSource& material : materialSources)
        {
            const auto candidates = textureCandidates.find(material.resourceId);
            if (candidates == textureCandidates.end()) continue;
            for (uint32_t fileId : candidates->second)
            {
                const auto path = paths.find(fileId);
                if (path == paths.end()) continue;
                itemdisplay::MaterialEntry entry;
                entry.modelIndex = material.modelIndex;
                entry.layer = material.layer;
                entry.textureType = material.textureType;
                if (const auto component = componentTextures.find(fileId);
                    component != componentTextures.end())
                {
                    entry.raceId = component->second.raceId;
                    entry.genderId = component->second.genderId;
                }
                entry.folder = next->Intern(ObjectFolder(path->second));
                entry.texture = next->Intern(TextureName(path->second));
                next->materials[material.displayId].push_back(entry);

                if (material.layer < 8)
                {
                    const auto display = next->displayRecords.find(material.displayId);
                    if (display != next->displayRecords.end() &&
                        !display->second.componentTextures[material.layer])
                        display->second.componentTextures[material.layer] =
                            next->Intern(ComponentTextureName(path->second));
                }
            }
        }
        for (const DisplaySource& source : sources)
        {
            const uint32_t inventoryType = g_inventoryByDisplay.contains(source.id)
                ? g_inventoryByDisplay.at(source.id) : 0;
            for (size_t column = 0; column < 2; ++column)
            {
                const auto files = modelFiles.find(source.models[column]);
                if (files == modelFiles.end()) continue;
                bool hasPositionedCandidate = false;
                for (uint32_t fileId : files->second)
                {
                    const auto component = componentModels.find(fileId);
                    if (component != componentModels.end() && component->second.position == column)
                        hasPositionedCandidate = true;
                }
                for (uint32_t fileId : files->second)
                {
                    const auto selectedPath = paths.find(fileId);
                    if (selectedPath == paths.end() ||
                        !EndsWith(Normalize(selectedPath->second), ".m2"))
                        continue;
                    const auto component = componentModels.find(fileId);
                    if (hasPositionedCandidate &&
                        (component == componentModels.end() ||
                         component->second.position != column))
                        continue;

                    const std::string& path = selectedPath->second;
                    const bool collection = !CollectionMarker(Normalize(path)).empty();
                    const bool raceGenderCollection =
                        collection && component != componentModels.end() &&
                        (component->second.raceId ||
                         component->second.genderId != kMissing);
                    itemdisplay::ModelEntry entry;
                    entry.modelSlot = ModelSlot(inventoryType);
                    entry.attachId = Attach(
                        inventoryType, column, source.modelTypes[column],
                        collection, raceGenderCollection);
                    entry.modelIndex = static_cast<uint32_t>(column);
                    if (component != componentModels.end())
                    {
                        entry.raceId = component->second.raceId;
                        entry.genderId = component->second.genderId;
                    }
                    entry.modelFlags = 0xffffffffu;
                    entry.textureFlags = 0xffffffffu;
                    entry.folder = next->Intern(ObjectFolder(path));
                    entry.model = next->Intern(ModelName(path));
                    const auto textureFile = textureFiles.find(source.textures[column]);
                    if (textureFile != textureFiles.end() && paths.contains(textureFile->second))
                        entry.texture = next->Intern(TextureName(paths.at(textureFile->second)));
                    if (UsesNativeWeaponOwner(inventoryType))
                    {
                        const auto display = next->displayRecords.find(source.id);
                        if (display != next->displayRecords.end() &&
                            !display->second.nativeModelNames[column])
                        {
                            display->second.nativeModelNames[column] = entry.model;
                            display->second.nativeModelTextures[column] = entry.texture;
                        }
                    }
                    next->models[source.id].push_back(entry);
                }
            }
        }

        WLOG_INFO("retail-item-index: publishing requested=%zu resolved=%zu modelDisplays=%zu",
                  displayIds.size(), next->resolvedDisplays.size(), next->models.size());
        if (ItemDetailLog())
        {
            for (uint32_t displayId : displayIds)
            {
                const auto models = next->models.find(displayId);
                if (models == next->models.end() || models->second.empty())
                {
                    WLOG_INFO("retail-item-index: display=%u has no model entries", displayId);
                }
                else
                {
                    for (const itemdisplay::ModelEntry& entry : models->second)
                        WLOG_INFO("retail-item-index: display=%u slot=%u attach=%u modelIndex=%u race=%u gender=%u folder=%s model=%s texture=%s",
                                  displayId, entry.modelSlot, entry.attachId, entry.modelIndex,
                                  entry.raceId, entry.genderId,
                                  entry.folder ? entry.folder : "",
                                  entry.model ? entry.model : "",
                                  entry.texture ? entry.texture : "");
                }
                const auto materials = next->materials.find(displayId);
                if (materials == next->materials.end() || materials->second.empty())
                    WLOG_INFO("retail-item-index: display=%u has no material entries", displayId);
                else
                    for (const itemdisplay::MaterialEntry& entry : materials->second)
                        WLOG_INFO("retail-item-index: display=%u material layer=%u type=%u race=%u gender=%u folder=%s texture=%s",
                                  displayId, entry.layer, entry.textureType,
                                  entry.raceId, entry.genderId,
                                  entry.folder ? entry.folder : "",
                                  entry.texture ? entry.texture : "");
            }
        }
        itemdisplay::Publish(std::move(next));
        return true;
    }

    DWORD WINAPI Worker(LPVOID)
    {
        if (!BuildRelationshipIndex())
        {
            WLOG_ERROR("retail-item-index: startup failed");
            return 0;
        }

        std::unordered_map<uint32_t, uint8_t> failureCounts;
        constexpr uint8_t kMaxBatchAttempts = 4;
        for (;;)
        {
            std::vector<uint32_t> requests = itemdisplay::WaitTakeRequests();
            // Glue and world character construction emit the equipped set in one tight burst.
            // Debounce that burst into one WDC5/table-graph pass instead of resolving once per slot
            // and exposing each partially resolved item to the renderer.
            Sleep(10);
            std::vector<uint32_t> trailing = itemdisplay::TakeRequests();
            requests.insert(requests.end(), trailing.begin(), trailing.end());
            std::ranges::sort(requests);
            requests.erase(std::unique(requests.begin(), requests.end()), requests.end());
            for (size_t offset = 0; offset < requests.size(); offset += kMaxFilterValues)
            {
                const size_t count = std::min(kMaxFilterValues, requests.size() - offset);
                const std::span batch(requests.data() + offset, count);
                bool resolved = false;
                try
                {
                    resolved = ResolveDisplayBatch(batch);
                }
                catch (...)
                {
                    WLOG_ERROR(
                        "retail-item-index: display batch raised an exception count=%zu",
                        batch.size());
                }

                // Always release ownership, including allocation/transport
                // failures, so no display remains permanently in flight.
                itemdisplay::FinishRequests(batch);
                if (resolved)
                {
                    for (uint32_t displayId : batch)
                        failureCounts.erase(displayId);
                    continue;
                }

                std::vector<uint32_t> retry;
                uint8_t highestAttempt = 0;
                retry.reserve(batch.size());
                for (uint32_t displayId : batch)
                {
                    const uint8_t attempt =
                        ++failureCounts[displayId];
                    highestAttempt = std::max(highestAttempt, attempt);
                    if (attempt < kMaxBatchAttempts)
                        retry.push_back(displayId);
                    else
                    {
                        failureCounts.erase(displayId);
                        WLOG_ERROR(
                            "retail-item-index: display=%u resolution abandoned after %u attempts",
                            displayId,
                            static_cast<unsigned>(attempt));
                    }
                }
                if (!retry.empty())
                {
                    const uint32_t backoffMs =
                        100u << std::min<uint8_t>(
                            highestAttempt - 1u, 2u);
                    WLOG_WARN(
                        "retail-item-index: retrying failed batch count=%zu attempt=%u delay=%u ms",
                        retry.size(),
                        static_cast<unsigned>(highestAttempt),
                        backoffMs);
                    Sleep(backoffMs);
                    for (uint32_t displayId : retry)
                        itemdisplay::Request(displayId);
                }
            }
        }
    }

    bool InstallRetailItemIndexImpl()
    {
        if (HANDLE thread = CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr))
        {
            CloseHandle(thread);
            return true;
        }
        WLOG_ERROR("retail-item-index: failed to create background worker");
        return false;
    }
}

bool wxl_db2::InstallRetailItemIndex()
{
    return InstallRetailItemIndexImpl();
}
