#include "wet_surfaces.h"

#include "fog_data.h"
#include "fog_model.h"

#include <algorithm>
#include <cmath>

namespace
{
float SmoothStep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Approach(float value, float target, float seconds, float timeConstant)
{
    return target + (value - target) * std::exp(-seconds / timeConstant);
}

float Intensity(const FrameInputs& in)
{
    return in.weather == WeatherKind::Clear ? 0.0f : std::clamp(in.weatherIntensity, 0.0f, 1.0f);
}
}

SurfaceWetnessTarget WetnessTarget(WeatherKind weather, float intensity)
{
    const float i = std::clamp(intensity, 0.0f, 1.0f);
    switch (weather)
    {
    case WeatherKind::Rain:
        return {SmoothStep(0.0f, kRainForFullWetness, i), SmoothStep(kPuddleRainFrom, kPuddleRainFull, i)};
    case WeatherKind::Snow:
        return {kSnowWetness * i, 0.0f};
    default:
        return {0.0f, 0.0f};
    }
}

float WetnessDryingSeconds(WeatherKind weather, float sunElevation)
{
    const float night = LampNightWeight(sunElevation);
    const float seconds = kDryingSecondsByDay + (kDryingSecondsAtNight - kDryingSecondsByDay) * night;
    return weather == WeatherKind::Sand ? seconds / kSandDryingSpeedup : seconds;
}

void SurfaceWetness::Update(const FrameInputs& in)
{
    const SurfaceWetnessTarget target = WetnessTarget(in.weather, Intensity(in));
    const double elapsed = in.seconds - m_seconds;
    m_seconds = in.seconds;
    if (!m_valid || !(elapsed >= 0.0) || elapsed > kWetnessResyncSeconds)
    {
        m_wetness = target.wetness;
        m_puddles = target.puddles;
        m_valid = true;
        return;
    }
    const float seconds = static_cast<float>(elapsed);
    const float drying = WetnessDryingSeconds(in.weather, in.sunElevation);
    m_wetness = Approach(m_wetness, target.wetness, seconds, target.wetness > m_wetness ? kWettingSeconds : drying);
    m_puddles = Approach(m_puddles, target.puddles, seconds,
                         target.puddles > m_puddles ? kPuddleFillSeconds : drying * kPuddleDrainSlowdown);
}

void SurfaceWetness::Reset()
{
    m_valid = false;
    m_seconds = 0.0;
    m_wetness = 0.0f;
    m_puddles = 0.0f;
}

bool WetSurfacesWanted(const Config& cfg)
{
    return cfg.surfaceSheen > 0.0f || cfg.wetSurfaces > 0.0f;
}

WetSurfaceParams BuildWetSurfaceParams(const FrameInputs& in, const Config& cfg, const SurfaceWetness& state)
{
    WetSurfaceParams p = {};
    const float hourly = in.lightParams.screenEffectSlot == FogData::kDeathSlot ? 0.0f : 1.0f;
    const float shown = in.inLiquid ? 0.0f : hourly * (1.0f - std::clamp(in.indoors, 0.0f, 1.0f));
    p.sheen = std::max(cfg.surfaceSheen, 0.0f) * shown;
    if (cfg.wetSurfaces > 0.0f)
    {
        p.wetness = std::min(state.Wetness() * cfg.wetSurfaces, 1.0f) * shown;
        p.puddleShare = std::min(kPuddleShareAtFullFill * std::max(cfg.puddles, 0.0f) * state.Puddles(),
                                 kMaxPuddleShare) *
                        shown;
    }
    p.light = SmoothStep(0.0f, kWetLightRiseFull, in.toLight[2]) * (1.0f - kWetWeatherSunLoss * Intensity(in));
    UnpackColor(in.directColor, p.lightColor);
    for (float& c : p.lightColor)
        c = std::pow(std::max(c, 0.0f), 2.2f);
    return p;
}

bool WetSurfacesShown(const WetSurfaceParams& p)
{
    return p.sheen > 0.0f || p.wetness > 0.0f || p.puddleShare > 0.0f;
}

float PuddleThreshold(float share)
{
    const float s = std::clamp(share, 0.0f, 1.0f);
    return s <= 0.5f ? 1.0f - std::sqrt(0.5f * s) : std::sqrt(0.5f * (1.0f - s));
}
