// Focused PTR 12.1 item/appearance and liquid DB2 declarations.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "Db2.hpp"

#include <array>

namespace wxl::runtime::db2::retail
{
    inline constexpr std::array kItemFields{
        Field{"ClassID"}, Field{"SubclassID"}, Field{"Material"}, Field{"InventoryType"},
        Field{"SheatheType"}, Field{"Sound_override_subclassID"}, Field{"IconFileDataID"},
        Field{"ItemGroupSoundsID"}, Field{"ContentTuningID"}, Field{"ModifiedCraftingReagentItemID"},
        Field{"Field_12_0_0_63534_010"}, Field{"CraftingQualityID"}, Field{"ItemSquishEraID"},
        Field{"RecraftReagentCountPercentage"}, Field{"OrderSource"},
    };
    inline constexpr Definition Item{"Item", "item.db2", 0x996192AAu, kItemFields};

    inline constexpr std::array kItemAppearanceFields{
        Field{"DisplayType"}, Field{"ItemDisplayInfoID"}, Field{"DefaultIconFileDataID"},
        Field{"UiOrder"}, Field{"TransmogPlayerConditionID"},
    };
    inline constexpr std::array kItemAppearanceRelations{
        Relation{"display", RelationSource::Field, "ItemDisplayInfoID", 0, "ItemDisplayInfo", "@id"},
    };
    inline constexpr Definition ItemAppearance{
        "ItemAppearance", "itemappearance.db2", 0x481C4281u,
        kItemAppearanceFields, kItemAppearanceRelations,
    };

    inline constexpr std::array kItemModifiedAppearanceFields{
        Field{"ID"}, Field{"ItemID"}, Field{"ItemAppearanceModifierID"}, Field{"ItemAppearanceID"},
        Field{"OrderIndex"}, Field{"TransmogSourceTypeEnum"}, Field{"Flags"},
    };
    inline constexpr std::array kItemModifiedAppearanceRelations{
        Relation{"item", RelationSource::Field, "ItemID", 0, "Item", "@id"},
        Relation{"appearance", RelationSource::Field, "ItemAppearanceID", 0, "ItemAppearance", "@id"},
    };
    inline constexpr Definition ItemModifiedAppearance{
        "ItemModifiedAppearance", "itemmodifiedappearance.db2", 0x03A6C979u,
        kItemModifiedAppearanceFields, kItemModifiedAppearanceRelations,
    };

    inline constexpr std::array kItemDisplayInfoFields{
        Field{"GeosetGroupOverride"}, Field{"ItemVisual"}, Field{"ParticleColorID"},
        Field{"ItemRangedDisplayInfoID"}, Field{"OverrideSwooshSoundKitID"},
        Field{"SheatheTransformMatrixID"}, Field{"StateSpellVisualKitID"},
        Field{"SheathedSpellVisualKitID"}, Field{"UnsheathedSpellVisualKitID"}, Field{"Flags"},
        Field{"ModelResourcesID", 2}, Field{"ModelMaterialResourcesID", 2}, Field{"ModelType", 2},
        Field{"GeosetGroup", 6}, Field{"AttachmentGeosetGroup", 6}, Field{"HelmetGeosetVis", 2},
    };
    inline constexpr Definition ItemDisplayInfo{
        "ItemDisplayInfo", "itemdisplayinfo.db2", 0x9F3AB8A9u, kItemDisplayInfoFields,
    };

    inline constexpr std::array kItemDisplayInfoMaterialResFields{
        Field{"ComponentSection"}, Field{"MaterialResourcesID"},
    };
    inline constexpr Definition ItemDisplayInfoMaterialRes{
        "ItemDisplayInfoMaterialRes", "itemdisplayinfomaterialres.db2", 0xAA462C0Eu,
        kItemDisplayInfoMaterialResFields,
    };

    inline constexpr std::array kItemDisplayInfoModelMatResFields{
        Field{"MaterialResourcesID"}, Field{"TextureType"}, Field{"ModelIndex"},
    };
    inline constexpr Definition ItemDisplayInfoModelMatRes{
        "ItemDisplayInfoModelMatRes", "itemdisplayinfomodelmatres.db2", 0x52510D63u,
        kItemDisplayInfoModelMatResFields,
    };

    inline constexpr std::array kModelFileDataFields{
        Field{"GeoBox", 6}, Field{"FileDataID"}, Field{"Flags"}, Field{"LodCount"},
        Field{"ModelResourcesID"},
    };
    inline constexpr Definition ModelFileData{
        "ModelFileData", "modelfiledata.db2", 0x2AE4E788u, kModelFileDataFields,
    };

    inline constexpr std::array kTextureFileDataFields{
        Field{"FileDataID"}, Field{"UsageType"}, Field{"MaterialResourcesID"},
    };
    inline constexpr Definition TextureFileData{
        "TextureFileData", "texturefiledata.db2", 0xBD7C74C2u, kTextureFileDataFields,
    };

    inline constexpr std::array kComponentModelFileDataFields{
        Field{"GenderIndex"}, Field{"ClassID"}, Field{"RaceID"}, Field{"PositionIndex"},
    };
    inline constexpr Definition ComponentModelFileData{
        "ComponentModelFileData", "componentmodelfiledata.db2", 0xAD90D87Au,
        kComponentModelFileDataFields, {}, false,
    };

    inline constexpr std::array kComponentTextureFileDataFields{
        Field{"GenderIndex"}, Field{"ClassID"}, Field{"RaceID"},
    };
    inline constexpr Definition ComponentTextureFileData{
        "ComponentTextureFileData", "componenttexturefiledata.db2", 0xB32B030Au,
        kComponentTextureFileDataFields, {}, false,
    };

    inline constexpr std::array kHelmetGeosetDataFields{
        Field{"RaceID"}, Field{"HideGeosetGroup"}, Field{"RaceBitSelection"},
        Field{"Flags"},
    };
    inline constexpr Definition HelmetGeosetData{
        "HelmetGeosetData", "helmetgeosetdata.db2", 0x103B3B37u,
        kHelmetGeosetDataFields,
    };

    inline constexpr std::array kHelmetAnimScalingFields{
        Field{"RaceID"}, Field{"Amount"},
    };
    inline constexpr Definition HelmetAnimScaling{
        "HelmetAnimScaling", "helmetanimscaling.db2", 0xFF7E3A8Au,
        kHelmetAnimScalingFields,
    };

    inline constexpr std::array All{
        Item, ItemAppearance, ItemModifiedAppearance, ItemDisplayInfo,
        ItemDisplayInfoMaterialRes, ItemDisplayInfoModelMatRes, ModelFileData, TextureFileData,
        ComponentModelFileData, ComponentTextureFileData,
        HelmetGeosetData, HelmetAnimScaling,
    };

    inline constexpr std::array kSpellXSpellVisualFields{
        // This table keeps its row ID inline (unlike the other visual graph
        // tables below), so ID is part of the physical 12-field WDC5 layout.
        Field{"ID"}, Field{"DifficultyID"}, Field{"SpellVisualID"},
        Field{"Probability"}, Field{"Flags2"}, Field{"Priority"},
        Field{"SpellIconFileID"}, Field{"ActiveIconFileID"},
        Field{"ViewerUnitConditionID"}, Field{"ViewerPlayerConditionID"},
        Field{"CasterUnitConditionID"}, Field{"CasterPlayerConditionID"},
    };
    inline constexpr Definition SpellXSpellVisual{
        "SpellXSpellVisual", "spellxspellvisual.db2", 0x7994A890u,
        kSpellXSpellVisualFields,
    };

    inline constexpr std::array kSpellVisualFields{
        Field{"MissileCastOffset", 3}, Field{"MissileImpactOffset", 3},
        Field{"StateKit"}, Field{"AnimEventSoundID"}, Field{"Flags"},
        Field{"MissileAttachment"}, Field{"MissileDestinationAttachment"},
        Field{"MissileCastPositionerID"}, Field{"MissileImpactPositionerID"},
        Field{"MissileTargetingKit"}, Field{"HostileSpellVisualID"},
        Field{"CasterSpellVisualID"}, Field{"SpellVisualMissileSetID"},
        Field{"DamageNumberDelay"}, Field{"LowViolenceSpellVisualID"},
        Field{"RaidSpellVisualMissileSetID"},
        Field{"ReducedUnexpectedCameraMovementSpellVisualID"},
    };
    inline constexpr Definition SpellVisual{
        "SpellVisual", "spellvisual.db2", 0x4B85C90Fu,
        kSpellVisualFields,
    };

    inline constexpr std::array kSpellVisualMissileFields{
        Field{"CastOffset", 3}, Field{"ImpactOffset", 3}, Field{"ID"},
        Field{"SpellVisualEffectNameID"}, Field{"SoundEntriesID"},
        Field{"Attachment"}, Field{"DestinationAttachment"},
        Field{"CastPositionerID"}, Field{"ImpactPositionerID"},
        Field{"FollowGroundHeight"}, Field{"FollowGroundDropSpeed"},
        Field{"FollowGroundApproach"}, Field{"Flags"},
        Field{"SpellMissileMotionID"}, Field{"AnimKitID"},
        Field{"ClutterLevel"}, Field{"DecayTimeAfterImpact"},
        Field{"Field_11_0_0_54210_017"},
    };
    inline constexpr std::array kSpellVisualMissileRelations{
        Relation{"missileSet", RelationSource::ParentId, {}, 0,
                 "SpellVisual", "SpellVisualMissileSetID"},
    };
    inline constexpr Definition SpellVisualMissile{
        "SpellVisualMissile", "spellvisualmissile.db2", 0xAE389078u,
        kSpellVisualMissileFields, kSpellVisualMissileRelations,
    };

    inline constexpr std::array kSpellVisualEventFields{
        Field{"StartEvent"}, Field{"EndEvent"}, Field{"StartMinOffsetMs"},
        Field{"StartMaxOffsetMs"}, Field{"EndMinOffsetMs"},
        Field{"EndMaxOffsetMs"}, Field{"TargetType"},
        Field{"SpellVisualKitID"}, Field{"Field_10_0_0_44649_008"},
        Field{"Field_10_0_0_44649_009"},
    };
    inline constexpr Definition SpellVisualEvent{
        "SpellVisualEvent", "spellvisualevent.db2", 0x865F512Eu,
        kSpellVisualEventFields,
    };

    inline constexpr std::array kSpellVisualKitEffectFields{
        Field{"EffectType"}, Field{"Effect"},
    };
    inline constexpr Definition SpellVisualKitEffect{
        "SpellVisualKitEffect", "spellvisualkiteffect.db2", 0xE3206CA2u,
        kSpellVisualKitEffectFields,
    };

    inline constexpr std::array kSpellVisualKitModelAttachFields{
        Field{"Offset", 3}, Field{"OffsetVariation", 3},
        Field{"SpellVisualEffectNameID"}, Field{"AttachmentID"},
        Field{"PositionerID"}, Field{"Yaw"}, Field{"Pitch"},
        Field{"Roll"}, Field{"YawVariation"}, Field{"PitchVariation"},
        Field{"RollVariation"}, Field{"Scale"}, Field{"ScaleVariation"},
        Field{"StartAnimID"}, Field{"AnimID"}, Field{"EndAnimID"},
        Field{"AnimKitID"}, Field{"Flags"}, Field{"LowDefModelAttachID"},
        Field{"StartDelay"}, Field{"Field_9_0_1_33978_021"},
        Field{"Field_11_0_0_54210_022"},
    };
    inline constexpr Definition SpellVisualKitModelAttach{
        "SpellVisualKitModelAttach", "spellvisualkitmodelattach.db2",
        0x02CF8554u, kSpellVisualKitModelAttachFields,
    };

    inline constexpr std::array kSpellVisualEffectNameFields{
        Field{"ModelFileDataID"}, Field{"BaseMissileSpeed"},
        Field{"Scale"}, Field{"MinAllowedScale"},
        Field{"MaxAllowedScale"}, Field{"Alpha"}, Field{"Flags"},
        Field{"TextureFileDataID"}, Field{"EffectRadius"}, Field{"Type"},
        Field{"GenericID"}, Field{"RibbonQualityID"},
        Field{"DissolveEffectID"}, Field{"ModelPosition"},
        Field{"Field_9_1_0_38549_014"}, Field{"Field_11_0_0_54210_015"},
    };
    inline constexpr Definition SpellVisualEffectName{
        "SpellVisualEffectName", "spellvisualeffectname.db2", 0x2245CEE6u,
        kSpellVisualEffectNameFields,
    };

    inline constexpr std::array kLiquidTypeFields{
        Field{"Name", 1, 1, true}, Field{"Texture", 6, 1, true},
        Field{"Flags"}, Field{"SoundBank"}, Field{"SoundID"}, Field{"SpellID"},
        Field{"MaxDarkenDepth"}, Field{"FogDarkenIntensity"}, Field{"AmbDarkenIntensity"},
        Field{"DirDarkenIntensity"}, Field{"LightID"}, Field{"ParticleScale"},
        Field{"ParticleMovement"}, Field{"ParticleTexSlots"}, Field{"MaterialID"},
        Field{"MinimapStaticCol"}, Field{"FrameCountTexture", 6}, Field{"Color", 3},
        Field{"Float", 38}, Field{"Int", 4}, Field{"Coefficient", 4},
    };
    inline constexpr Definition LiquidType{
        "LiquidType", "liquidtype.db2", 0xD1ECEEC9u, kLiquidTypeFields,
    };

    inline constexpr std::array kLiquidMaterialFields{
        Field{"Flags"}, Field{"LVF"},
    };
    inline constexpr Definition LiquidMaterial{
        "LiquidMaterial", "liquidmaterial.db2", 0x98E5D7AAu, kLiquidMaterialFields,
    };

    inline constexpr std::array kLiquidTypeXTextureFields{
        Field{"FileDataID"}, Field{"OrderIndex"}, Field{"Type"},
    };
    inline constexpr std::array kLiquidTypeXTextureRelations{
        Relation{"liquidType", RelationSource::ParentId, {}, 0, "LiquidType", "@id"},
    };
    inline constexpr Definition LiquidTypeXTexture{
        "LiquidTypeXTexture", "liquidtypextexture.db2", 0x7BEECC7Fu,
        kLiquidTypeXTextureFields, kLiquidTypeXTextureRelations,
    };

    inline constexpr std::array LiquidAll{
        LiquidType, LiquidMaterial, LiquidTypeXTexture,
    };
}
