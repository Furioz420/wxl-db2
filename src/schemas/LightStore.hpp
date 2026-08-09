// The retail lighting tables, resident and answerable: what the light is HERE, NOW.
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

#include <cstdint>
#include <string>
#include <string_view>

namespace wxl::runtime::db2 { class Table; }

namespace wxl::runtime::db2::light
{
    struct Rgb { float r = 0.0f, g = 0.0f, b = 0.0f; };

    /// The complete lighting the retail tables prescribe for one place and one moment: every
    /// named column of LightData at the blended time, plus the LightParams and LightSkybox the
    /// winning light points at. Unnamed columns stay reachable by name through Get().
    struct LightState
    {
        bool     valid    = false;
        uint32_t lightId  = 0;   ///< winning Light row
        uint32_t paramsId = 0;   ///< its clear-weather LightParams set

        Rgb direct, ambient;
        Rgb skyTop, skyMiddle, skyBand1, skyBand2, skySmog, skyFog;
        Rgb sunColor, cloudSunColor, cloudEmissive, cloudLayer1Ambient, cloudLayer2Ambient;
        Rgb oceanClose, oceanFar, riverClose, riverFar;
        Rgb horizonAmbient, groundAmbient;
        Rgb endFogColor, sunFogColor, fogHeightColor, endFogHeightColor;

        float shadowOpacity = 0.0f;
        float fogEnd = 0.0f, fogScaler = 0.0f, fogDensity = 0.0f;
        float fogHeight = 0.0f, fogHeightScaler = 0.0f, fogHeightDensity = 0.0f;
        float fogZScalar = 0.0f, mainFogStartDist = 0.0f, mainFogEndDist = 0.0f;
        float sunFogAngle = 0.0f, sunFogStrength = 0.0f, cloudDensity = 0.0f;
        float endFogColorDistance = 0.0f, fogStartOffset = 0.0f;
        float fogHeightCoefficients[4]{};
        float mainFogCoefficients[4]{};
        float heightDensityFogCoeff[4]{};

        uint32_t colorGradingFdid = 0, darkerColorGradingFdid = 0;

        // From LightParams.
        uint32_t lightSkyboxId = 0, cloudTypeId = 0, paramsFlags = 0;
        float glow = 0.0f, highlightSky = 0.0f;
        float waterShallowAlpha = 0.0f, waterDeepAlpha = 0.0f;
        float oceanShallowAlpha = 0.0f, oceanDeepAlpha = 0.0f;
        float sunPolar = 0.0f, sunAzimuth = 0.0f;
        float sunAttenuationStart = 0.0f, sunAttenuationEnd = 0.0f;
        float overrideSunPosition[3]{};
        float overrideCelestialSphere[3]{};

        // From LightSkybox.
        uint32_t skyboxFdid = 0, celestialSkyboxFdid = 0, skyboxFlags = 0;
    };

    /// How the load went, for the panel and the log. `error` carries the decoder's own words
    /// when a table refused -- the schema declarations are validated against the real files.
    struct StoreStatus
    {
        bool loaded = false;   ///< a load was attempted and every required table decoded
        bool failed = false;   ///< a load was attempted and something refused
        std::string error;
        uint32_t lights = 0, data = 0, params = 0, skyboxes = 0;
    };

    const StoreStatus& Status();

    /// The state at (map, world position, day time in half-minutes 0..2880). First use triggers
    /// the load. Invalid when the tables are absent from this deployment or hold nothing for
    /// the map.
    LightState Evaluate(int mapId, const float* worldPos, float timeHalfMinutes);

    /// What the light IS: the state where the camera stands, at the world's own time of day,
    /// recomputed once a frame and handed out unchanged in between. This is what consumers read;
    /// Evaluate() remains for asking about some other place or moment. Invalid before the first
    /// frame of a world, and whenever Evaluate() would be.
    const LightState& Current();

    /// Evaluates as another map id, for a world built from a retail map's terrain: the tables key
    /// on the RETAIL id, which a custom map does not carry. Negative (the default) follows the
    /// live map. Applies to Current(), not to Evaluate().
    void SetMapOverride(int mapId);
    int  MapOverride();

    /// Holds Current() at one moment of the day instead of following the world clock. Negative
    /// (the default) follows it. In half-minutes, same scale as Evaluate().
    void  SetTimeOverride(float timeHalfMinutes);
    float TimeOverride();

    /// How far the world's own day has run, in those same half-minutes. What Current() follows
    /// unless a time override holds it.
    float WorldTimeHalfMinutes();

    /// Raw table access for consumers that need columns beyond LightState ("light", "lightdata",
    /// "lightparams", "lightskybox"). Null until loaded, or for a name never declared.
    const Table* Get(std::string_view table);
}
