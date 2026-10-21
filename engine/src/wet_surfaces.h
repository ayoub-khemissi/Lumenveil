#pragma once

#include "config.h"
#include "engine.h"

constexpr float kWettingSeconds = 20.0f;
constexpr float kPuddleFillSeconds = 90.0f;
constexpr float kDryingSecondsByDay = 240.0f;
constexpr float kDryingSecondsAtNight = 720.0f;
constexpr float kPuddleDrainSlowdown = 1.5f;
constexpr float kSandDryingSpeedup = 3.0f;
constexpr float kRainForFullWetness = 0.4f;
constexpr float kPuddleRainFrom = 0.15f;
constexpr float kPuddleRainFull = 0.8f;
constexpr float kSnowWetness = 0.3f;
constexpr double kWetnessResyncSeconds = 30.0;
constexpr float kPuddleShareAtFullFill = 0.3f;
constexpr float kMaxPuddleShare = 0.8f;
constexpr float kWetWeatherSunLoss = 0.8f;
constexpr float kWetLightRiseFull = 0.05f;
constexpr float kPuddleTileYards = 20.0f;
constexpr float kPuddleSecondTileYards = 31.0f;
constexpr float kPuddleSecondTurnDegrees = 37.0f;

struct SurfaceWetnessTarget
{
    float wetness;
    float puddles;
};

SurfaceWetnessTarget WetnessTarget(WeatherKind weather, float intensity);
float WetnessDryingSeconds(WeatherKind weather, float sunElevation);

class SurfaceWetness
{
public:
    void Update(const FrameInputs& in);
    void Reset();
    bool Valid() const { return m_valid; }
    float Wetness() const { return m_wetness; }
    float Puddles() const { return m_puddles; }

private:
    bool m_valid = false;
    double m_seconds = 0.0;
    float m_wetness = 0.0f;
    float m_puddles = 0.0f;
};

struct WetSurfaceParams
{
    float sheen;
    float wetness;
    float puddleShare;
    float light;
    float lightColor[3];
};

bool WetSurfacesWanted(const Config& cfg);
WetSurfaceParams BuildWetSurfaceParams(const FrameInputs& in, const Config& cfg, const SurfaceWetness& state);
bool WetSurfacesShown(const WetSurfaceParams& p);
float PuddleThreshold(float share);
