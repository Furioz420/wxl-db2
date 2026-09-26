// Retail spell visual graph resolver owned by wxl-db2.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Db2.hpp"
#include "RetailSchemas.hpp"
#include "../ExtensionApi.hpp"

#include "wxl/RetailSpellDb2Api.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    namespace db2 = wxl::runtime::db2;
    namespace retail = wxl::runtime::db2::retail;

    struct Catalog
    {
        uint32_t spellId = 0;
        uint32_t visualId = 0;
        std::vector<WXL_RetailSpellModel> models;
        std::string error;
    };

    struct EventMeta
    {
        uint32_t startEvent = 0;
        uint32_t endEvent = 0;
        uint32_t targetType = 0;
    };

    using CatalogPtr = std::shared_ptr<const Catalog>;
    std::mutex g_mutex;
    std::unordered_map<uint32_t, CatalogPtr> g_catalogs;

    float FloatValue(const db2::Table& table, const db2::wdc5::Row& row,
                     std::string_view field) noexcept
    {
        return std::bit_cast<float>(table.Value(row, field));
    }

    bool LoadFiltered(const db2::Definition& definition,
                      db2::wdc5::SnapshotFilterSource source,
                      const std::vector<uint32_t>& values,
                      db2::Table& table, std::string& error)
    {
        db2::wdc5::SnapshotFilter filter;
        filter.source = source;
        filter.values = values;
        return table.LoadFiltered(definition, filter, &error);
    }

    CatalogPtr Build(uint32_t spellId)
    {
        auto result = std::make_shared<Catalog>();
        result->spellId = spellId;
        auto fail = [&](std::string message) -> CatalogPtr {
            result->error = std::move(message);
            WLOG_WARN("retail-spell-db2: spell=%u %s", spellId, result->error.c_str());
            return result;
        };

        std::string error;
        db2::Table bindings;
        if (!LoadFiltered(retail::SpellXSpellVisual,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          {spellId}, bindings, error))
            return fail("SpellXSpellVisual: " + error);

        for (const db2::wdc5::Row& row : bindings.Rows())
        {
            const uint32_t candidate = bindings.Value(row, "SpellVisualID");
            if (!candidate) continue;
            if (!result->visualId || bindings.Value(row, "DifficultyID") == 0)
                result->visualId = candidate;
            if (bindings.Value(row, "DifficultyID") == 0) break;
        }
        if (!result->visualId)
            return fail("SpellXSpellVisual has no default visual");

        const std::vector<uint32_t> visualIds{result->visualId};
        db2::Table visuals;
        if (!LoadFiltered(retail::SpellVisual,
                          db2::wdc5::SnapshotFilterSource::RowId,
                          visualIds, visuals, error) || !visuals.Find(result->visualId))
            return fail("SpellVisual: " +
                (error.empty() ? std::string("default visual is missing") : error));
        const db2::wdc5::Row* const visual = visuals.Find(result->visualId);

        db2::Table events;
        if (!LoadFiltered(retail::SpellVisualEvent,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          visualIds, events, error))
            return fail("SpellVisualEvent: " + error);

        std::vector<uint32_t> kitIds;
        std::unordered_map<uint32_t, EventMeta> eventByKit;
        for (const db2::wdc5::Row& row : events.Rows())
        {
            const uint32_t kit = events.Value(row, "SpellVisualKitID");
            if (!kit) continue;
            kitIds.push_back(kit);
            eventByKit.try_emplace(kit, EventMeta{
                events.Value(row, "StartEvent"),
                events.Value(row, "EndEvent"),
                events.Value(row, "TargetType")});
        }
        const uint32_t stateKit = visuals.Value(*visual, "StateKit");
        if (stateKit) kitIds.push_back(stateKit);
        std::ranges::sort(kitIds);
        kitIds.erase(std::unique(kitIds.begin(), kitIds.end()), kitIds.end());
        if (kitIds.empty()) return fail("SpellVisualEvent has no visual kits");

        struct Attach
        {
            uint32_t effectNameId = 0;
            float scale = 1.0f;
            uint32_t kitId = 0;
            int32_t attachmentId = -1;
            uint32_t positionerId = 0;
        };
        std::unordered_map<uint32_t, Attach> attaches;
        const auto readAttach = [&](const db2::Table& table, const db2::wdc5::Row& row,
                                    uint32_t kitId) {
            float scale = FloatValue(table, row, "Scale");
            if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
            attaches.insert_or_assign(row.id, Attach{
                table.Value(row, "SpellVisualEffectNameID"), scale, kitId,
                static_cast<int32_t>(table.Value(row, "AttachmentID")),
                table.Value(row, "PositionerID")});
        };

        db2::Table directAttaches;
        if (!LoadFiltered(retail::SpellVisualKitModelAttach,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          kitIds, directAttaches, error))
            return fail("SpellVisualKitModelAttach: " + error);
        for (const db2::wdc5::Row& row : directAttaches.Rows())
            readAttach(directAttaches, row, row.parentId);

        db2::Table effects;
        if (!LoadFiltered(retail::SpellVisualKitEffect,
                          db2::wdc5::SnapshotFilterSource::ParentId,
                          kitIds, effects, error))
            return fail("SpellVisualKitEffect: " + error);

        std::vector<uint32_t> indirectAttachIds;
        std::unordered_map<uint32_t, uint32_t> indirectAttachKit;
        for (const db2::wdc5::Row& row : effects.Rows())
            if (effects.Value(row, "EffectType") == 2)
            {
                const uint32_t attachId = effects.Value(row, "Effect");
                if (attachId)
                {
                    indirectAttachIds.push_back(attachId);
                    indirectAttachKit.insert_or_assign(attachId, row.parentId);
                }
            }
        indirectAttachIds.erase(
            std::remove(indirectAttachIds.begin(), indirectAttachIds.end(), 0),
            indirectAttachIds.end());
        if (!indirectAttachIds.empty())
        {
            db2::Table indirectAttaches;
            if (!LoadFiltered(retail::SpellVisualKitModelAttach,
                              db2::wdc5::SnapshotFilterSource::RowId,
                              indirectAttachIds, indirectAttaches, error))
                return fail("indirect SpellVisualKitModelAttach: " + error);
            for (const db2::wdc5::Row& row : indirectAttaches.Rows())
                readAttach(indirectAttaches, row, indirectAttachKit[row.id]);
        }

        struct Missile
        {
            uint32_t effectNameId = 0;
            int32_t attachmentId = -1;
            uint32_t positionerId = 0;
        };
        std::vector<Missile> missiles;
        const uint32_t missileSet = visuals.Value(*visual, "SpellVisualMissileSetID");
        if (missileSet)
        {
            db2::Table missileRows;
            if (!LoadFiltered(retail::SpellVisualMissile,
                              db2::wdc5::SnapshotFilterSource::ParentId,
                              {missileSet}, missileRows, error))
            {
                // Keep caster and target kits usable when a client payload predates the
                // missile table. The warning makes the incomplete payload diagnosable.
                WLOG_WARN("retail-spell-db2: spell=%u missile set=%u unavailable: %s",
                          spellId, missileSet, error.c_str());
                error.clear();
            }
            else
            {
                for (const db2::wdc5::Row& row : missileRows.Rows())
                    missiles.push_back(Missile{
                        missileRows.Value(row, "SpellVisualEffectNameID"),
                        static_cast<int32_t>(missileRows.Value(row, "Attachment")),
                        missileRows.Value(row, "CastPositionerID")});
            }
        }

        std::vector<uint32_t> effectNameIds;
        for (const auto& [_, attach] : attaches)
            if (attach.effectNameId) effectNameIds.push_back(attach.effectNameId);
        for (const Missile& missile : missiles)
            if (missile.effectNameId) effectNameIds.push_back(missile.effectNameId);
        std::ranges::sort(effectNameIds);
        effectNameIds.erase(std::unique(effectNameIds.begin(), effectNameIds.end()),
                            effectNameIds.end());
        if (effectNameIds.empty()) return fail("visual kits contain no model attachments");

        db2::Table effectNames;
        if (!LoadFiltered(retail::SpellVisualEffectName,
                          db2::wdc5::SnapshotFilterSource::RowId,
                          effectNameIds, effectNames, error))
            return fail("SpellVisualEffectName: " + error);

        for (const auto& [_, attach] : attaches)
        {
            const db2::wdc5::Row* effect = effectNames.Find(attach.effectNameId);
            if (!effect) continue;
            const uint32_t fdid = effectNames.Value(*effect, "ModelFileDataID");
            if (!fdid) continue;
            float scale = FloatValue(effectNames, *effect, "Scale");
            if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
            const auto event = eventByKit.find(attach.kitId);
            const EventMeta meta = event != eventByKit.end()
                ? event->second : EventMeta{};
            result->models.push_back(WXL_RetailSpellModel{
                fdid, attach.scale * scale, attach.attachmentId,
                attach.positionerId, attach.kitId, meta.startEvent,
                meta.endEvent, meta.targetType, WXL_RETAIL_SPELL_MODEL_ATTACH});
        }
        for (const Missile& missile : missiles)
        {
            const db2::wdc5::Row* effect = effectNames.Find(missile.effectNameId);
            if (!effect) continue;
            const uint32_t fdid = effectNames.Value(*effect, "ModelFileDataID");
            if (!fdid) continue;
            float scale = FloatValue(effectNames, *effect, "Scale");
            if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
            result->models.push_back(WXL_RetailSpellModel{
                fdid, scale, missile.attachmentId, missile.positionerId,
                0, 0, 0, 0, WXL_RETAIL_SPELL_MODEL_MISSILE});
        }
        if (result->models.empty()) return fail("visual graph contains no model FileDataIDs");

        WLOG_INFO("retail-spell-db2: ready spell=%u visual=%u models=%zu",
                  spellId, result->visualId, result->models.size());
        return result;
    }

    int __cdecl Enabled()
    {
        return wxl_db2::ConfigBool("WXL_DB2_RETAIL_SPELLS", true) ? 1 : 0;
    }

    void* __cdecl Acquire(uint32_t spellId)
    {
        if (!Enabled() || !spellId) return nullptr;
        std::lock_guard lock(g_mutex);
        auto found = g_catalogs.find(spellId);
        if (found == g_catalogs.end())
            found = g_catalogs.emplace(spellId, Build(spellId)).first;
        return new CatalogPtr(found->second);
    }

    void __cdecl Release(void* lease) { delete static_cast<CatalogPtr*>(lease); }

    uint32_t __cdecl ModelCount(void* lease)
    {
        return lease ? static_cast<uint32_t>((*static_cast<CatalogPtr*>(lease))->models.size()) : 0;
    }

    int __cdecl ModelAt(void* lease, uint32_t index, WXL_RetailSpellModel* out)
    {
        if (!lease || !out) return 0;
        const auto& models = (*static_cast<CatalogPtr*>(lease))->models;
        if (index >= models.size()) return 0;
        *out = models[index];
        return 1;
    }

    const char* __cdecl Error(void* lease)
    {
        return lease ? (*static_cast<CatalogPtr*>(lease))->error.c_str() : "service unavailable";
    }

    const WXL_RetailSpellDb2Api g_spellApi{
        sizeof(WXL_RetailSpellDb2Api), WXL_RETAIL_SPELL_DB2_API_VERSION,
        &Enabled, &Acquire, &Release, &ModelCount, &ModelAt, &Error,
    };
}

const WXL_RetailSpellDb2Api* wxl_db2::RetailSpellApi() { return &g_spellApi; }
