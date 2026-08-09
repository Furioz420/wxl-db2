// The retail lighting tables, loaded whole and blended on demand.
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
//
// EVERY COLUMN IS DECLARED. The tables below carry all of their fields by name, the unknown
// ones included -- a column nobody understands yet is still data, and discarding it is how a
// loader rots. LightState surfaces the named ones; Get() reaches the rest.
//
// The shape of the system, as the tables tell it: Light places POINTS in the world (a position,
// a falloff, a continent) and each point carries parameter SETS (clear weather first). LightData
// is those sets unrolled over the day in half-minute stamps -- every colour and fog figure the
// engine needs, keyed by set and time. LightParams adds the per-set constants (skybox, water
// alphas, sun placement), LightSkybox names the dome. Evaluating "here, now" is therefore: pick
// the point whose falloff covers you, walk its set's day-curve, and interpolate.

#include "LightStore.hpp"
#include "../ExtensionApi.hpp"
#include "../api/Db2.hpp"

#include "engine/events/Event.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Sky.hpp"
#include "offsets/game/World.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    using namespace wxl::runtime::db2;
    namespace world = wxl::offsets::game::world;
    namespace sky   = wxl::offsets::engine::sky;

    // ---- declarations: the full physical field list of each table, in file order ------------

    constexpr Field kLightFields[] = {
        { "GameCoords", 3 }, { "GameFalloffStart" }, { "GameFalloffEnd" },
        { "ContinentID" }, { "LightParamsID", 8 },
    };

    constexpr Field kLightDataFields[] = {
        { "LightParamID" },   // inline lookup column in this build (the file says so, not the CSV)
        { "Time" },
        { "DirectColor" }, { "AmbientColor" },
        { "SkyTopColor" }, { "SkyMiddleColor" }, { "SkyBand1Color" }, { "SkyBand2Color" },
        { "SkySmogColor" }, { "SkyFogColor" },
        { "SunColor" }, { "CloudSunColor" }, { "CloudEmissiveColor" },
        { "CloudLayer1AmbientColor" }, { "CloudLayer2AmbientColor" },
        { "OceanCloseColor" }, { "OceanFarColor" }, { "RiverCloseColor" }, { "RiverFarColor" },
        { "ShadowOpacity" },
        { "FogEnd" }, { "FogScaler" }, { "FogDensity" },
        { "FogHeight" }, { "FogHeightScaler" }, { "FogHeightDensity" }, { "FogZScalar" },
        { "MainFogStartDist" }, { "MainFogEndDist" },
        { "SunFogAngle" }, { "CloudDensity" },
        { "ColorGradingFileDataID" }, { "DarkerColorGradingFileDataID" },
        { "HorizonAmbientColor" }, { "GroundAmbientColor" },
        { "EndFogColor" }, { "EndFogColorDistance" }, { "FogStartOffset" },
        { "SunFogColor" }, { "SunFogStrength" },
        { "FogHeightColor" }, { "EndFogHeightColor" },
        { "Field_10_0_0_44649_042" }, { "Field_12_0_0_63854_043" },
        { "FogHeightCoefficients", 4 }, { "MainFogCoefficients", 4 },
        { "HeightDensityFogCoeff", 4 },
    };

    constexpr Relation kLightDataRelations[] = {
        { "LightParamID", RelationSource::Field, "LightParamID", 0, "lightparams" },
    };

    constexpr Field kLightParamsFields[] = {
        { "OverrideCelestialSphere", 3 }, { "OverrideSunPosition", 3 },
        { "HighlightSky" }, { "LightSkyboxID" }, { "CloudTypeID" }, { "Glow" },
        { "WaterShallowAlpha" }, { "WaterDeepAlpha" }, { "OceanShallowAlpha" }, { "OceanDeepAlpha" },
        { "Flags" }, { "SsaoSettingsID" },
        { "SunPolar" }, { "SunAzimuth" }, { "SunAttenuationStart" }, { "SunAttenuationEnd" },
        { "Field_12_0_0_63534_016" }, { "Field_12_0_0_63534_017" }, { "Field_12_0_0_63534_018" },
        { "Field_12_0_0_63534_019" }, { "Field_12_0_0_63534_020" }, { "Field_12_0_0_63534_021" },
        { "Field_12_0_0_63534_022" }, { "Field_12_0_0_63534_023" }, { "Field_12_0_0_63534_024" },
        { "Field_12_0_0_63534_025" }, { "Field_12_0_0_63534_026" }, { "Field_12_0_0_63534_027" },
        { "Field_12_0_0_63534_028" }, { "Field_12_0_0_63534_029" },
        { "Field_12_0_1_65617_030" }, { "Field_12_0_1_65617_031" },
    };

    constexpr Field kLightSkyboxFields[] = {
        { "Name" }, { "Flags" }, { "SkyboxFileDataID" }, { "CelestialSkyboxFileDataID" },
    };

    constexpr Definition kDefinitions[] = {
        { "light",       "light.db2",       0, kLightFields },
        { "lightdata",   "lightdata.db2",   0, kLightDataFields, kLightDataRelations },
        { "lightparams", "lightparams.db2", 0, kLightParamsFields },
        { "lightskybox", "lightskybox.db2", 0, kLightSkyboxFields, {}, false },
    };

    // ---- the resident store ------------------------------------------------------------------

    light::StoreStatus g_status;
    Table g_light, g_data, g_params, g_skybox;
    bool  g_skyboxOk = false;
    std::once_flag g_loadOnce;

    /// LightData rows of one parameter set, in day order. Built once: Evaluate walks these
    /// every call and a 2 MB table is not something to re-scan per frame.
    std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_dayCurves;

    float F(uint32_t v) { return std::bit_cast<float>(v); }

    /// Packed table colour -> floats. The tables pack 0xRRGGBB, the same convention the client's
    /// own consumers read it as; if a consumer ever shows swapped channels, this is the single
    /// place where that assumption lives.
    light::Rgb Color(uint32_t v)
    {
        return { static_cast<float>((v >> 16) & 0xFF) / 255.0f,
                 static_cast<float>((v >>  8) & 0xFF) / 255.0f,
                 static_cast<float>( v        & 0xFF) / 255.0f };
    }

    light::Rgb Lerp(const light::Rgb& a, const light::Rgb& b, float t)
    {
        return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    }

    float LerpF(float a, float b, float t) { return a + (b - a) * t; }

    void LoadNow()
    {
        std::string error;
        if (!ValidateDefinitions(kDefinitions, &error))
        {
            g_status.failed = true;
            g_status.error = error;
            WLOG_WARN("lightdb: %s", error.c_str());
            return;
        }
        struct Slot { Table* table; const Definition* def; uint32_t* count; bool* okOut; };
        bool requiredOk = true;
        const Slot slots[] = {
            { &g_light,  &kDefinitions[0], &g_status.lights,   nullptr },
            { &g_data,   &kDefinitions[1], &g_status.data,     nullptr },
            { &g_params, &kDefinitions[2], &g_status.params,   nullptr },
            { &g_skybox, &kDefinitions[3], &g_status.skyboxes, &g_skyboxOk },
        };
        for (const Slot& s : slots)
        {
            std::string tableError;
            const bool ok = s.table->Load(*s.def, &tableError);
            if (ok) *s.count = static_cast<uint32_t>(s.table->Rows().size());
            if (s.okOut) *s.okOut = ok;
            else if (!ok)
            {
                requiredOk = false;
                if (!g_status.error.empty()) g_status.error += " | ";
                g_status.error += std::string(s.def->name) + ": " + tableError;
            }
        }
        g_status.loaded = requiredOk;
        g_status.failed = !requiredOk;
        if (!requiredOk)
        {
            WLOG_WARN("lightdb: %s", g_status.error.c_str());
            return;
        }

        for (const wdc5::Row& row : g_data.Rows())
            g_dayCurves[row.values[0]].push_back(&row);   // field 0 = LightParamID
        for (auto& [id, rows] : g_dayCurves)
            std::sort(rows.begin(), rows.end(), [](const wdc5::Row* a, const wdc5::Row* b) {
                return a->values[1] < b->values[1];   // field 1 = Time
            });
        WLOG_INFO("lightdb: %u lights, %u data rows over %u day-curves, %u param sets, %u skyboxes",
                  g_status.lights, g_status.data, static_cast<unsigned>(g_dayCurves.size()),
                  g_status.params, g_status.skyboxes);
    }

    /// One LightData row poured into the state. Everything LightState names, read by name.
    void FillFromData(light::LightState& s, const wdc5::Row& r)
    {
        const Table& t = g_data;
        s.direct             = Color(t.Value(r, "DirectColor"));
        s.ambient            = Color(t.Value(r, "AmbientColor"));
        s.skyTop             = Color(t.Value(r, "SkyTopColor"));
        s.skyMiddle          = Color(t.Value(r, "SkyMiddleColor"));
        s.skyBand1           = Color(t.Value(r, "SkyBand1Color"));
        s.skyBand2           = Color(t.Value(r, "SkyBand2Color"));
        s.skySmog            = Color(t.Value(r, "SkySmogColor"));
        s.skyFog             = Color(t.Value(r, "SkyFogColor"));
        s.sunColor           = Color(t.Value(r, "SunColor"));
        s.cloudSunColor      = Color(t.Value(r, "CloudSunColor"));
        s.cloudEmissive      = Color(t.Value(r, "CloudEmissiveColor"));
        s.cloudLayer1Ambient = Color(t.Value(r, "CloudLayer1AmbientColor"));
        s.cloudLayer2Ambient = Color(t.Value(r, "CloudLayer2AmbientColor"));
        s.oceanClose         = Color(t.Value(r, "OceanCloseColor"));
        s.oceanFar           = Color(t.Value(r, "OceanFarColor"));
        s.riverClose         = Color(t.Value(r, "RiverCloseColor"));
        s.riverFar           = Color(t.Value(r, "RiverFarColor"));
        s.horizonAmbient     = Color(t.Value(r, "HorizonAmbientColor"));
        s.groundAmbient      = Color(t.Value(r, "GroundAmbientColor"));
        s.endFogColor        = Color(t.Value(r, "EndFogColor"));
        s.sunFogColor        = Color(t.Value(r, "SunFogColor"));
        s.fogHeightColor     = Color(t.Value(r, "FogHeightColor"));
        s.endFogHeightColor  = Color(t.Value(r, "EndFogHeightColor"));

        s.shadowOpacity      = F(t.Value(r, "ShadowOpacity"));
        s.fogEnd             = F(t.Value(r, "FogEnd"));
        s.fogScaler          = F(t.Value(r, "FogScaler"));
        s.fogDensity         = F(t.Value(r, "FogDensity"));
        s.fogHeight          = F(t.Value(r, "FogHeight"));
        s.fogHeightScaler    = F(t.Value(r, "FogHeightScaler"));
        s.fogHeightDensity   = F(t.Value(r, "FogHeightDensity"));
        s.fogZScalar         = F(t.Value(r, "FogZScalar"));
        s.mainFogStartDist   = F(t.Value(r, "MainFogStartDist"));
        s.mainFogEndDist     = F(t.Value(r, "MainFogEndDist"));
        s.sunFogAngle        = F(t.Value(r, "SunFogAngle"));
        s.sunFogStrength     = F(t.Value(r, "SunFogStrength"));
        s.cloudDensity       = F(t.Value(r, "CloudDensity"));
        s.endFogColorDistance = F(t.Value(r, "EndFogColorDistance"));
        s.fogStartOffset     = F(t.Value(r, "FogStartOffset"));
        for (int i = 0; i < 4; ++i)
        {
            s.fogHeightCoefficients[i] = F(t.Value(r, "FogHeightCoefficients", i));
            s.mainFogCoefficients[i]   = F(t.Value(r, "MainFogCoefficients", i));
            s.heightDensityFogCoeff[i] = F(t.Value(r, "HeightDensityFogCoeff", i));
        }
        s.colorGradingFdid       = t.Value(r, "ColorGradingFileDataID");
        s.darkerColorGradingFdid = t.Value(r, "DarkerColorGradingFileDataID");
    }

    void LerpState(light::LightState& s, const light::LightState& b, float t)
    {
        auto C = [&](light::Rgb light::LightState::* m) { s.*m = Lerp(s.*m, b.*m, t); };
        auto V = [&](float light::LightState::* m) { s.*m = LerpF(s.*m, b.*m, t); };
        C(&light::LightState::direct);  C(&light::LightState::ambient);
        C(&light::LightState::skyTop);  C(&light::LightState::skyMiddle);
        C(&light::LightState::skyBand1); C(&light::LightState::skyBand2);
        C(&light::LightState::skySmog); C(&light::LightState::skyFog);
        C(&light::LightState::sunColor); C(&light::LightState::cloudSunColor);
        C(&light::LightState::cloudEmissive);
        C(&light::LightState::cloudLayer1Ambient); C(&light::LightState::cloudLayer2Ambient);
        C(&light::LightState::oceanClose); C(&light::LightState::oceanFar);
        C(&light::LightState::riverClose); C(&light::LightState::riverFar);
        C(&light::LightState::horizonAmbient); C(&light::LightState::groundAmbient);
        C(&light::LightState::endFogColor); C(&light::LightState::sunFogColor);
        C(&light::LightState::fogHeightColor); C(&light::LightState::endFogHeightColor);
        V(&light::LightState::shadowOpacity);
        V(&light::LightState::fogEnd); V(&light::LightState::fogScaler);
        V(&light::LightState::fogDensity); V(&light::LightState::fogHeight);
        V(&light::LightState::fogHeightScaler); V(&light::LightState::fogHeightDensity);
        V(&light::LightState::fogZScalar);
        V(&light::LightState::mainFogStartDist); V(&light::LightState::mainFogEndDist);
        V(&light::LightState::sunFogAngle); V(&light::LightState::sunFogStrength);
        V(&light::LightState::cloudDensity);
        V(&light::LightState::endFogColorDistance); V(&light::LightState::fogStartOffset);
        for (int i = 0; i < 4; ++i)
        {
            s.fogHeightCoefficients[i] = LerpF(s.fogHeightCoefficients[i], b.fogHeightCoefficients[i], t);
            s.mainFogCoefficients[i]   = LerpF(s.mainFogCoefficients[i],   b.mainFogCoefficients[i],   t);
            s.heightDensityFogCoeff[i] = LerpF(s.heightDensityFogCoeff[i], b.heightDensityFogCoeff[i], t);
        }
        // Ids do not interpolate: the nearer band's grading cube wins.
        if (t >= 0.5f)
        {
            s.colorGradingFdid       = b.colorGradingFdid;
            s.darkerColorGradingFdid = b.darkerColorGradingFdid;
        }
    }

    /// A parameter set at a day time: the surrounding stamps of its curve, interpolated. The
    /// day wraps -- 2880 half-minutes -- so midnight blends across the seam.
    bool EvaluateParams(uint32_t paramsId, float time, light::LightState& out)
    {
        const auto it = g_dayCurves.find(paramsId);
        if (it == g_dayCurves.end() || it->second.empty()) return false;
        const std::vector<const wdc5::Row*>& rows = it->second;

        const wdc5::Row* before = rows.back();
        const wdc5::Row* after  = rows.front();
        for (const wdc5::Row* row : rows)
        {
            const float rt = static_cast<float>(row->values[1]);   // field 1 = Time
            if (rt <= time) before = row;
            if (rt > time) { after = row; break; }
        }
        FillFromData(out, *before);
        if (before != after)
        {
            const float t0 = static_cast<float>(before->values[1]);
            const float t1 = static_cast<float>(after->values[1]);
            const float span = (t1 > t0) ? (t1 - t0) : (t1 + 2880.0f - t0);
            const float dt   = (time >= t0) ? (time - t0) : (time + 2880.0f - t0);
            light::LightState b;
            FillFromData(b, *after);
            LerpState(out, b, (span > 0.0f) ? (dt / span) : 0.0f);
        }
        out.paramsId = paramsId;
        return true;
    }

    void FillFromParams(light::LightState& s)
    {
        const wdc5::Row* p = g_params.Find(s.paramsId);
        if (!p) return;
        const Table& t = g_params;
        s.lightSkyboxId       = t.Value(*p, "LightSkyboxID");
        s.cloudTypeId         = t.Value(*p, "CloudTypeID");
        s.paramsFlags         = t.Value(*p, "Flags");
        s.glow                = F(t.Value(*p, "Glow"));
        s.highlightSky        = F(t.Value(*p, "HighlightSky"));
        s.waterShallowAlpha   = F(t.Value(*p, "WaterShallowAlpha"));
        s.waterDeepAlpha      = F(t.Value(*p, "WaterDeepAlpha"));
        s.oceanShallowAlpha   = F(t.Value(*p, "OceanShallowAlpha"));
        s.oceanDeepAlpha      = F(t.Value(*p, "OceanDeepAlpha"));
        s.sunPolar            = F(t.Value(*p, "SunPolar"));
        s.sunAzimuth          = F(t.Value(*p, "SunAzimuth"));
        s.sunAttenuationStart = F(t.Value(*p, "SunAttenuationStart"));
        s.sunAttenuationEnd   = F(t.Value(*p, "SunAttenuationEnd"));
        for (int i = 0; i < 3; ++i)
        {
            s.overrideSunPosition[i]     = F(t.Value(*p, "OverrideSunPosition", i));
            s.overrideCelestialSphere[i] = F(t.Value(*p, "OverrideCelestialSphere", i));
        }
        if (g_skyboxOk && s.lightSkyboxId)
            if (const wdc5::Row* sky = g_skybox.Find(s.lightSkyboxId))
            {
                s.skyboxFdid          = g_skybox.Value(*sky, "SkyboxFileDataID");
                s.celestialSkyboxFdid = g_skybox.Value(*sky, "CelestialSkyboxFileDataID");
                s.skyboxFlags         = g_skybox.Value(*sky, "Flags");
            }
    }
}

namespace wxl::runtime::db2::light
{
    const StoreStatus& Status()
    {
        return g_status;
    }

    LightState Evaluate(int mapId, const float* worldPos, float timeHalfMinutes)
    {
        std::call_once(g_loadOnce, &LoadNow);
        LightState out;
        if (!g_status.loaded || mapId < 0) return out;

        // The winning point light and the map's own global light (all-zero coordinates). Table
        // positions are stored in the archived axis convention -- inverted around the map
        // centre, thirty-six units to the yard -- and are brought into world space before any
        // distance means anything.
        uint32_t bestParams = 0, bestId = 0;
        float bestWeight = 0.0f;
        uint32_t defaultParams = 0, defaultId = 0;
        const float time = std::fmod(std::fmax(timeHalfMinutes, 0.0f), 2880.0f);
        for (const wdc5::Row& row : g_light.Rows())
        {
            if (static_cast<int>(g_light.Value(row, "ContinentID")) != mapId) continue;
            const float rx = F(g_light.Value(row, "GameCoords", 0));
            const float ry = F(g_light.Value(row, "GameCoords", 1));
            const float rz = F(g_light.Value(row, "GameCoords", 2));
            const uint32_t params = g_light.Value(row, "LightParamsID", 0);
            if (!params) continue;
            if (rx == 0.0f && ry == 0.0f && rz == 0.0f)
            {
                defaultParams = params;
                defaultId = row.id;
                continue;
            }
            const float wx = 17066.666f - rx / 36.0f;
            const float wy = 17066.666f - ry / 36.0f;
            const float wz = rz / 36.0f;
            const float start = F(g_light.Value(row, "GameFalloffStart")) / 36.0f;
            const float end   = F(g_light.Value(row, "GameFalloffEnd"))   / 36.0f;
            const float dx = worldPos[0] - wx, dy = worldPos[1] - wy, dz = worldPos[2] - wz;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            float w = 0.0f;
            if (d <= start) w = 1.0f;
            else if (d < end && end > start) w = 1.0f - (d - start) / (end - start);
            if (w > bestWeight)
            {
                bestWeight = w;
                bestParams = params;
                bestId = row.id;
            }
        }

        if (bestWeight >= 1.0f || (bestWeight > 0.0f && !defaultParams))
        {
            if (!EvaluateParams(bestParams, time, out)) return out;
            out.lightId = bestId;
        }
        else if (bestWeight > 0.0f)
        {
            // Inside a falloff ring: the zone light fades over the map's global one, the same
            // crossfade the reference client performs at every zone boundary.
            LightState zone;
            if (!EvaluateParams(defaultParams, time, out)) return out;
            out.lightId = defaultId;
            if (EvaluateParams(bestParams, time, zone))
            {
                LerpState(out, zone, bestWeight);
                if (bestWeight >= 0.5f) { out.lightId = bestId; out.paramsId = bestParams; }
            }
        }
        else if (defaultParams)
        {
            if (!EvaluateParams(defaultParams, time, out)) return out;
            out.lightId = defaultId;
        }
        else
        {
            return out;
        }

        FillFromParams(out);
        out.valid = true;
        return out;
    }

    const Table* Get(std::string_view table)
    {
        if (!g_status.loaded) return nullptr;
        if (table == "light") return &g_light;
        if (table == "lightdata") return &g_data;
        if (table == "lightparams") return &g_params;
        if (table == "lightskybox") return g_skyboxOk ? &g_skybox : nullptr;
        return nullptr;
    }

    // ---- the live state -------------------------------------------------------------------
    //
    // Evaluating walks a day-curve and crossfades two parameter sets, which is work worth doing
    // once a frame rather than once a consumer. Everything downstream -- grading, water ambient,
    // the sky -- wants the same answer for the same frame anyway, and any two of them disagreeing
    // because they asked at different moments would be a bug nobody could see.

    LightState g_current;
    int        g_mapOverride  = -1;
    float      g_timeOverride = -1.0f;

    /// How far the world's own day has run, in the half-minutes the light rows are keyed in.
    float WorldTimeHalfMinutes()
    {
        const auto* info = reinterpret_cast<const uint8_t*>(
            wxl::game::Native<sky::DayNightGetInfoFn>(sky::kDayNightGetInfo)());
        if (!info) return 0.0f;
        float fraction = 0.0f;
        std::memcpy(&fraction, info + sky::kInfoDayFraction, 4);
        return fraction * sky::kDayHalfMinutes;
    }

    void RefreshCurrent(void*, const void*)
    {
        const int liveMap = *reinterpret_cast<const int*>(world::kCurrentMapId);
        if (liveMap < 0) { g_current = LightState{}; return; }

        const float pos[3] = {
            *reinterpret_cast<const float*>(world::kFocusPosX),
            *reinterpret_cast<const float*>(world::kFocusPosY),
            *reinterpret_cast<const float*>(world::kFocusPosZ),
        };
        const float time = (g_timeOverride >= 0.0f) ? g_timeOverride : WorldTimeHalfMinutes();
        g_current = Evaluate(g_mapOverride >= 0 ? g_mapOverride : liveMap, pos, time);
    }

    const LightState& Current() { return g_current; }

    void  SetMapOverride(int mapId)        { g_mapOverride = mapId; }
    int   MapOverride()                    { return g_mapOverride; }
    void  SetTimeOverride(float halfMin)   { g_timeOverride = halfMin; }
    float TimeOverride()                   { return g_timeOverride; }
}

namespace wxl_db2
{
    bool InstallLightStore()
    {
        g_api->Subscribe(static_cast<uint32_t>(wxl::events::Event::OnFrame),
                         &wxl::runtime::db2::light::RefreshCurrent, nullptr);
        return true;
    }
}
