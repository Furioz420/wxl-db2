// Model appearance recipes: which parts of a model are visible, what is painted on it, what is worn.
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

#pragma once

// The lists are built directly as the published types rather than as private twins copied out at the
// boundary: this store IS what publishes them, so a second shape would only be a chance to disagree.
#include "wxl/AppearanceApi.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wxl::runtime::db2 { class Table; }

namespace wxl::runtime::db2::appearance
{
    /// Everything needed to draw one appearance, in the model side's vocabulary rather than the
    /// tables'. Both producers below fill this same shape: a creature's parts and a character's are
    /// the same question asked of different tables, and the consumer should not have to know which.
    struct Recipe
    {
        uint32_t modelFileDataId = 0;
        uint32_t skeletonFileDataId = 0; ///< 0 when the model is not split-skeleton
        uint32_t textureLayoutId = 0;    ///< the sheet layout `layers` address

        /// Final geoset ids, as the model's own submeshes carry them (group * 100 + value).
        std::vector<uint16_t>          geosets;
        std::vector<WXL_TextureLayer>  layers;
        std::vector<WXL_AttachedModel> attached;

        /// Empties the lists without giving their storage back: a recipe is asked for repeatedly and
        /// the answers are all about the same size, so the buffers are worth keeping.
        void Clear()
        {
            modelFileDataId = skeletonFileDataId = textureLayoutId = 0;
            geosets.clear();
            layers.clear();
            attached.clear();
        }
    };

    /// How the load went, for the panel and the log.
    struct StoreStatus
    {
        bool        loaded = false; ///< a load was attempted and every required table decoded
        bool        failed = false;
        std::string error;
        uint32_t    chrModels = 0, raceModels = 0, options = 0, choices = 0;
        uint32_t    elements = 0, geosets = 0;
        uint32_t    materials = 0, skinnedModels = 0, textureLayers = 0, textureLayouts = 0;
        uint32_t    textureSections = 0;
        uint32_t    displayInfos = 0, modelData = 0, creatureGeosets = 0;
    };

    /// Triggers the load on first use.
    const StoreStatus& Status();

    /**
     * @brief The appearance of a player character.
     * @param chrRaceId   ChrRaces id.
     * @param sex         0 or 1, as ChrRaceXChrModel states it.
     * @param choiceIds   ChrCustomizationChoice ids, in any order.
     * @param choiceCount how many.
     * @param out         cleared, then filled.
     * @return false when the tables hold no model for that race and sex.
     */
    bool BuildForCharacter(uint32_t chrRaceId, uint32_t sex, const uint32_t* choiceIds,
                           uint32_t choiceCount, Recipe& out);

    /**
     * @brief The appearance of a creature or NPC, entirely from its display entry.
     * @param creatureDisplayInfoId CreatureDisplayInfo id.
     * @param out                   cleared, then filled.
     * @return false when the tables hold no such display entry, or it names no model.
     */
    bool BuildForCreature(uint32_t creatureDisplayInfoId, Recipe& out);

    /// The ChrModel a race and sex resolve to, 0 when the tables pair them with none. The unit a
    /// model's customization hangs off is the ChrModel, not the race, so this is where an enumeration
    /// starts.
    uint32_t ChrModelForRace(uint32_t chrRaceId, uint32_t sex);

    /// The options a ChrModel offers, in the tables' own order. Enumerated rather than reduced to
    /// fixed slots: the table that used to map the stock client's five customization bytes onto
    /// modern choices ships zero rows now, so that mapping belongs outside this resolver, and it
    /// cannot be made without seeing what the options are.
    uint32_t OptionCount(uint32_t chrModelId);
    bool     OptionAt(uint32_t chrModelId, uint32_t index, WXL_ChrOption& out);

    /// The choices one option offers, likewise ordered.
    uint32_t ChoiceCount(uint32_t chrCustomizationOptionId);
    bool     ChoiceAt(uint32_t chrCustomizationOptionId, uint32_t index, WXL_ChrChoice& out);

    /// The layout a ChrModel's textures are addressed in, 0 when it names none. Separate from the
    /// recipe because the sheet has to be sized before a single choice is known.
    uint32_t LayoutForModel(uint32_t chrModelId);

    /// The sheet one layout describes. False when no layout carries that id, or it states no size.
    /// A model's UVs normalise against this, so composing a sheet of any other size puts every part
    /// of the model on the wrong part of the picture, however correctly each part was painted.
    bool LayoutSize(uint32_t layoutId, uint32_t& outWidth, uint32_t& outHeight);

    /// The regions tiling one layout, in the table's own order.
    uint32_t SectionCount(uint32_t layoutId);
    bool     SectionAt(uint32_t layoutId, uint32_t index, WXL_TextureSection& out);

    /// Raw table access for consumers needing columns beyond Recipe. Null until loaded, or for a
    /// name never declared. Names are the lowercase file stems ("chrmodel", "creaturedisplayinfo").
    const Table* Get(std::string_view table);
}
