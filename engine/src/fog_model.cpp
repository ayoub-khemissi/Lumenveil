#include "fog_model.h"

#include "cloud_noise.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr float kHazeDensity = 0.00035f;
constexpr float kHazeG = 0.75f;
constexpr float kHazeIsotropic = 0.3f;
constexpr float kHazeFalloff = 1.0f / 220.0f;
constexpr float kHazeSun = 1.3f;
constexpr float kHazeAmbient = 0.8f;
constexpr float kHazeShadowDensity = 0.75f;

constexpr float kGroundStart = 12.0f;
constexpr float kGroundOpticalDepth = 1.1f;
constexpr float kGroundG = 0.4f;
constexpr float kGroundIsotropic = 0.5f;
constexpr float kGroundFalloff = 1.0f / 18.0f;
constexpr float kGroundSun = 0.8f;
constexpr float kGroundAmbient = 0.75f;
constexpr float kGroundShadowDensity = 0.85f;

constexpr float kFarOpticalDepth = 3.0f;
constexpr float kFarRamp = 60.0f;
constexpr float kFarStartFraction = 0.25f;
constexpr float kFarG = 0.2f;
constexpr float kFarIsotropic = 0.7f;
constexpr float kFarSun = 0.45f;
constexpr float kFarAmbient = 0.6f;
constexpr float kFarSkyFalloff = 10.0f;
constexpr float kFarExponent = 2.0f;
constexpr float kAerialFarG = 0.6f;
constexpr float kAerialIsotropic = 0.0f;
constexpr float kAerialAmbient = 1.0f;

constexpr float kClassicUnits = 0.01f;
constexpr float kFogRangeMargin = 300.0f;
constexpr uint32_t kFlagShadowed = 0x1;
constexpr uint32_t kFlagRelativeHeights = 0x2;

constexpr float kDerivedLayerLimit = 1500.0f;

constexpr float kDustDensity = 0.0005f;
constexpr float kDustG = 0.72f;
constexpr float kDustIsotropic = 0.2f;
constexpr float kDustFalloff = 1.0f / 140.0f;
constexpr float kDustSun = 1.2f;
constexpr float kDustSky = 0.9f;
constexpr float kDustTint[3] = {1.0f, 0.82f, 0.62f};
constexpr float kDustCalmShare = 0.6f;
constexpr float kDustRisesFrom = 9.0f / 24.0f;
constexpr float kDustFullFrom = 13.0f / 24.0f;
constexpr float kDustSettlesFrom = 18.5f / 24.0f;
constexpr float kDustCalmAt = 22.0f / 24.0f;
constexpr float kDustWindFrom = 2.0f;
constexpr float kDustPerWindYard = 0.08f;
constexpr float kDustMaxWindGain = 1.6f;
constexpr float kHumidDensity = 0.00035f;
constexpr float kHumidG = 0.6f;
constexpr float kHumidIsotropic = 0.3f;
constexpr float kHumidFalloff = 1.0f / 90.0f;
constexpr float kHumidSun = 1.0f;
constexpr float kHumidSky = 1.0f;
constexpr float kHumidTint[3] = {0.95f, 1.0f, 1.06f};
constexpr float kHumidMorningGain = 0.8f;
constexpr float kHumidMorningFrom = 4.0f / 24.0f;
constexpr float kHumidMorningFullFrom = 6.0f / 24.0f;
constexpr float kHumidMorningFadesFrom = 9.0f / 24.0f;
constexpr float kHumidMorningGoneAt = 11.5f / 24.0f;
constexpr float kHumidAfternoonLoss = 0.3f;
constexpr float kHumidAfternoonFrom = 11.5f / 24.0f;
constexpr float kHumidAfternoonFullFrom = 13.5f / 24.0f;
constexpr float kHumidAfternoonFadesFrom = 16.5f / 24.0f;
constexpr float kHumidAfternoonGoneAt = 18.5f / 24.0f;
constexpr float kOpenLandSkyFalloff = 4.0f;
constexpr float kHorizonAboveShare = 0.3f;
constexpr float kOpenLandFarDepth = 0.8f;
constexpr float kOpenLandFarRamp = 6.0f;
constexpr float kOpenLandFarStart = 120.0f;
constexpr float kOpenLandFullAtDensity = 0.1f;
constexpr float kCloudCoverDry = 0.25f;
constexpr float kCloudCoverGreen = 0.45f;
constexpr float kMaxCloudCover = 0.9f;
constexpr float kCloudHeight = 600.0f;
constexpr float kCloudWindScale = 3.0f;
constexpr float kCloudShadowFullAtElevation = 0.08f;
constexpr float kDegreesToRadians = 0.017453293f;

constexpr float kAirShaftDensity = 0.003f;

constexpr float kMoonLight = 0.35f;
constexpr float kLightSettingHalfWidth = 0.02f;
constexpr float kMinClassicDirectLuminance = 1.0e-3f;
constexpr float kLuminanceWeights[3] = {0.2126f, 0.7152f, 0.0722f};
constexpr float kReferenceFarTarget = 200.0f;
constexpr float kNoLimit = 1.0e9f;

constexpr float kLightHandoverMargin = 0.02f;

constexpr float kSurfaceShadowAmbientHue = 0.35f;
constexpr float kSurfaceShadowFullAtElevation = 0.05f;
constexpr float kMinLightLuminance = 1.0e-4f;

constexpr float kWaterRefractiveIndex = 1.33f;
constexpr float kWaterGlitterFrom = -0.02f;
constexpr float kWaterGlitterFullAt = 0.06f;
constexpr float kMoonGlitter = 0.5f;
constexpr float kWeatherGlitterLoss = 0.85f;
constexpr float kSkyZenithTint[3] = {0.62f, 0.78f, 1.0f};
constexpr float kMinGlitterPeak = 1.0e-4f;
constexpr float kOceanDeepestValue = 0.9f;
constexpr float kSkyGlowOwnSunFrom = 0.1f;

constexpr float kHoursPerDay = 24.0f;
constexpr float kMistRisesFrom = 3.5f / kHoursPerDay;
constexpr float kMistFullFrom = 5.0f / kHoursPerDay;
constexpr float kMistBurnsOffFrom = 7.5f / kHoursPerDay;
constexpr float kMistGoneAt = 10.0f / kHoursPerDay;
constexpr float kMistProbeDistance = 100.0f;
constexpr float kMistReferenceDensity = 0.004f;
constexpr float kMaxMistGain = 8.0f;
constexpr float kHazeRisesFrom = 17.5f / kHoursPerDay;
constexpr float kHazeFullFrom = 20.0f / kHoursPerDay;
constexpr float kHazeFadesFrom = 22.0f / kHoursPerDay;
constexpr float kHazeGoneAt = 1.0f;

constexpr float kBankHazeShare = 0.35f;
constexpr float kBankGroundShare = 1.0f;
constexpr float kBankAuthoredShare = 1.0f;
constexpr float kBankOpenLandShare = 0.75f;
constexpr float kBankNoiseMean = 0.5f;
constexpr float kBankEdgeSpreads = 0.5f;
constexpr float kBankRiseSpreads = 1.0f;
constexpr float kBankEvenCover = 0.995f;
constexpr float kBankMinCover = 0.02f;
constexpr float kBankNoThreshold = -10.0f;
constexpr float kBankMaxNorm = 4.0f;
constexpr int kBankNormSamples = 256;
constexpr float kBankNormReachSpreads = 5.0f;
constexpr int kBankCoverSearchSteps = 48;
constexpr float kBankCoverSearchReach = 8.0f;
constexpr float kBankPoolGainPerPool = 0.5f;
constexpr float kBankShadeDepth = 0.6f;
constexpr float kBankShadeShareOfSize = 0.4f;
constexpr float kBankDustTintShare = 0.8f;
constexpr float kBankDawnRisesFrom = 3.5f / kHoursPerDay;
constexpr float kBankDawnFullFrom = 5.0f / kHoursPerDay;
constexpr float kBankDawnFadesFrom = 8.0f / kHoursPerDay;
constexpr float kBankDawnGoneAt = 10.5f / kHoursPerDay;
constexpr float kBankDayRisesFrom = 8.0f / kHoursPerDay;
constexpr float kBankDayFullFrom = 11.0f / kHoursPerDay;
constexpr float kBankDayFadesFrom = 16.0f / kHoursPerDay;
constexpr float kBankDayGoneAt = 18.5f / kHoursPerDay;
constexpr float kBankDuskRisesFrom = 16.0f / kHoursPerDay;
constexpr float kBankDuskFullFrom = 18.5f / kHoursPerDay;
constexpr float kBankDuskFadesFrom = 21.0f / kHoursPerDay;
constexpr float kBankDuskGoneAt = 23.0f / kHoursPerDay;
constexpr float kBankNightFrom = 21.0f / kHoursPerDay;
constexpr float kBankNightFullFrom = 23.0f / kHoursPerDay;
constexpr float kBankNightFadesFrom = 3.5f / kHoursPerDay;
constexpr float kBankNightGoneAt = 5.0f / kHoursPerDay;

enum BankHour
{
    kBankNight,
    kBankDawn,
    kBankDay,
    kBankDusk,
    kBankHours,
};

constexpr BankProfile kBankNeutral = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr BankProfile kBankHumidLand[kBankHours] = {
    {0.95f, 0.6f, 1.1f, 1.4f},
    {1.15f, 0.5f, 1.4f, 1.6f},
    {0.75f, 1.5f, 0.8f, 0.6f},
    {0.9f, 0.8f, 1.0f, 1.2f},
};
constexpr BankProfile kBankDryLand[kBankHours] = {
    {0.55f, 0.35f, 0.5f, 1.0f},
    {0.6f, 0.4f, 0.6f, 1.0f},
    {1.0f, 1.8f, 1.3f, 0.3f},
    {0.85f, 1.1f, 1.0f, 0.6f},
};
constexpr BankProfile kBankLakeWater[kBankHours] = {
    {1.2f, 0.3f, 1.5f, 1.0f},
    {1.35f, 0.3f, 2.0f, 1.0f},
    {0.55f, 0.35f, 0.5f, 1.0f},
    {1.1f, 0.3f, 1.2f, 1.0f},
};
constexpr BankProfile kBankSeaWater[kBankHours] = {
    {1.1f, 1.0f, 1.2f, 1.0f},
    {1.25f, 0.8f, 1.5f, 1.0f},
    {0.9f, 1.3f, 0.9f, 1.0f},
    {1.0f, 1.0f, 1.1f, 1.0f},
};

constexpr float kTwilightRisesBelow = 0.06f;
constexpr float kTwilightFullBelow = -0.02f;
constexpr float kTwilightFadesBelow = -0.12f;
constexpr float kTwilightGoneBelow = -0.30f;
constexpr float kTwilightFadesAsTheMoonPasses = 0.0f;
constexpr float kTwilightGoneWithTheMoonAt = 0.2f;
constexpr float kTwilightTint[3] = {0.911f, 0.986f, 1.393f};
constexpr float kNightFromSunBelow = 0.06f;
constexpr float kFullNightSunBelow = -0.08f;
constexpr float kLampDaylightFrom = -0.02f;
constexpr float kLampDaylightSpan = 0.15f;
constexpr float kLampClearHaze = 0.8f;
constexpr float kLampWeatherHaze = 1.2f;
constexpr float kLampReferenceDensity = 0.003f;
constexpr float kMinLampDensityGain = 0.5f;
constexpr float kMaxLampDensityGain = 2.0f;
constexpr float kNearLampHazeShare = 0.25f;
constexpr float kNearLampFadeFrom = 3.0f;
constexpr float kNearLampFadeTo = 14.0f;
constexpr float kSceneDirectShare = 0.5f;
constexpr float kMinSceneLight = 0.02f;
constexpr float kInteriorDaySceneLight = 0.12f;
constexpr float kMinSunFacingScale = 0.05f;
constexpr float kMinLightBalance = 0.25f;
constexpr float kMaxLightBalanceGain = 4.0f;
constexpr float kWhiteBalanceStops = 0.5f;
constexpr float kDisplayGamma = 2.2f;
constexpr uint32_t kColourBits = 0x00FFFFFFu;
constexpr float kUnknownSky = -1.0f;
constexpr float kMinHazeExposure = 1e-3f;
constexpr float kSelfLitFrom = 0.9f;
constexpr float kSelfLitSpan = 0.9f;

constexpr float kOvercastSkyShare = 0.85f;
constexpr float kSunAndMoonWeatherShare = 0.8f;
constexpr float kLowSunRisesFrom = 0.0f;
constexpr float kLowSunFullFrom = 0.06f;
constexpr float kLowSunFadesFrom = 0.25f;
constexpr float kLowSunGoneAt = 0.5f;
constexpr float kLowSunLift = 0.35f;
constexpr float kLowSunWarmth = 0.6f;

struct WeatherTint
{
    float hue[3];
    float pull;
};

constexpr WeatherTint kRainTint = {{1.0f, 1.0f, 1.0f}, 0.6f};
constexpr WeatherTint kSnowTint = {{1.02f, 1.06f, 1.14f}, 0.7f};
constexpr WeatherTint kSandTint = {{1.30f, 1.00f, 0.62f}, 0.6f};

float SmoothStep(float e0, float e1, float x)
{
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void Scale(const float* rgb, float s, float* out)
{
    out[0] = rgb[0] * s;
    out[1] = rgb[1] * s;
    out[2] = rgb[2] * s;
}

float Mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

float AerialPerspective(const Config& cfg)
{
    return std::clamp(cfg.aerialPerspective, 0.0f, 1.0f);
}

void Encode(float* rgb, bool linear)
{
    if (linear)
        for (int i = 0; i < 3; ++i)
            rgb[i] = std::pow(std::max(rgb[i], 0.0f), 2.2f);
}

float Luminance(const float* rgb)
{
    return rgb[0] * kLuminanceWeights[0] + rgb[1] * kLuminanceWeights[1] + rgb[2] * kLuminanceWeights[2];
}

void SaturateColour(float* rgb, float saturation)
{
    const float luminance = Luminance(rgb);
    for (int i = 0; i < 3; ++i)
        rgb[i] = std::max(luminance + (rgb[i] - luminance) * saturation, 0.0f);
}

void SaturateLightColours(float saturation, FogParams& p)
{
    if (saturation == 1.0f)
        return;
    for (FogLayer& layer : p.layers)
        SaturateColour(layer.diffuse, saturation);
    SaturateColour(p.lightColor, saturation);
    SaturateColour(p.rayColor, saturation);
    SaturateColour(p.airShaftScatter, saturation);
    SaturateColour(p.lowSunLift, saturation);
}

void SaturateHazeColours(float saturation, FogParams& p)
{
    if (saturation == 1.0f)
        return;
    for (FogLayer& layer : p.layers)
    {
        SaturateColour(layer.emissive, saturation);
        SaturateColour(layer.shadowEmissive, saturation);
    }
}

float ReferenceZ(const FrameInputs& in)
{
    float dx = in.camTarget[0] - in.camPos[0];
    float dy = in.camTarget[1] - in.camPos[1];
    float dz = in.camTarget[2] - in.camPos[2];
    if (dx * dx + dy * dy + dz * dz > kReferenceFarTarget * kReferenceFarTarget)
        return in.camPos[2] - 2.0f;
    return std::min(in.camPos[2], in.camTarget[2]) - 1.0f;
}

void Unbounded(FogLayer& l)
{
    l.skyFalloff = 0.0f;
    l.endDistance = kNoLimit;
}

void DerivedLayers(const FrameInputs& in, const Config& cfg, const FogParams& p, float sunScatter, float fogDistance,
                   const float* fogColor, FogLayer* out)
{
    float foggyZone = std::clamp(700.0f / fogDistance, 0.6f, 2.5f);
    float shadowed = cfg.lightShafts ? 1.0f : 0.0f;
    const float aerial = AerialPerspective(cfg);

    FogLayer& haze = out[0];
    haze.start = 0.0f;
    haze.density = kHazeDensity * cfg.haze * cfg.density * foggyZone;
    haze.g = kHazeG;
    haze.isotropic = Mix(kHazeIsotropic, kAerialIsotropic, aerial);
    Scale(fogColor, Mix(kHazeAmbient, kAerialAmbient, aerial) * cfg.ambient, haze.emissive);
    Scale(p.lightColor, kHazeSun * sunScatter, haze.diffuse);
    haze.exponent = 1.0f;
    haze.upperHeight = p.referenceZ;
    haze.upperFalloff = kHazeFalloff;
    std::memcpy(haze.shadowEmissive, haze.emissive, sizeof(haze.emissive));
    haze.shadowDensity = kHazeShadowDensity;
    haze.shadowed = shadowed;
    Unbounded(haze);
    haze.endDistance = kDerivedLayerLimit;
    haze.bankShare = kBankHazeShare;

    FogLayer& ground = out[1];
    ground.start = kGroundStart;
    ground.density = kGroundOpticalDepth * cfg.groundFog * cfg.density / std::max(fogDistance, 250.0f);
    ground.g = kGroundG;
    ground.isotropic = kGroundIsotropic;
    Scale(fogColor, kGroundAmbient * cfg.ambient, ground.emissive);
    Scale(p.lightColor, kGroundSun * sunScatter, ground.diffuse);
    ground.exponent = 1.0f;
    ground.upperHeight = p.referenceZ;
    ground.upperFalloff = kGroundFalloff;
    std::memcpy(ground.shadowEmissive, ground.emissive, sizeof(ground.emissive));
    ground.shadowDensity = kGroundShadowDensity;
    ground.shadowed = shadowed;
    Unbounded(ground);
    ground.endDistance = kDerivedLayerLimit;
    ground.bankShare = kBankGroundShare;

    out[2] = FogLayer{};
    Unbounded(out[2]);
}

void AuthoredLayers(const AuthoredFog& fog, const Config& cfg, const FogParams& p, float sunScatter, FogLayer* out)
{
    for (int i = 0; i < kSceneLayers; ++i)
    {
        FogLayer& l = out[i];
        l = FogLayer{};
        Unbounded(l);
        if (i >= fog.layerCount)
            continue;
        const AuthoredLayer& a = fog.layers[i];
        const float heightBase = a.flags & kFlagRelativeHeights ? p.referenceZ : 0.0f;
        l.start = std::min(p.maxDistance - kFogRangeMargin, a.start);
        l.density = a.density * kClassicUnits * cfg.density;
        l.g = std::clamp(a.g, -0.99f, 0.99f);
        l.isotropic = 0.0f;
        Scale(a.emissive, cfg.ambient, l.emissive);
        l.strength = a.strength;
        Scale(a.diffuse, 1.0f, l.diffuse);
        l.exponent = a.exponent > 0.0f ? a.exponent : 1.0f;
        l.upperHeight = a.upperHeight + heightBase;
        l.upperFalloff = a.upperDensity > 0.0f ? a.upperDensity * kClassicUnits : 0.0f;
        l.lowerHeight = a.lowerHeight + heightBase;
        l.lowerFalloff = a.lowerDensity > 0.0f ? a.lowerDensity * kClassicUnits : 0.0f;
        Scale(a.shadowEmissive, cfg.ambient, l.shadowEmissive);
        l.shadowDensity = a.shadowMultiplier;
        l.shadowed = (a.flags & kFlagShadowed) && cfg.lightShafts ? 1.0f : 0.0f;
        Encode(l.emissive, p.linear);
        Encode(l.diffuse, p.linear);
        Encode(l.shadowEmissive, p.linear);
        Scale(l.diffuse, a.intensity * sunScatter, l.diffuse);
        l.bankShare = a.flags & kFlagRelativeHeights ? kBankAuthoredShare : -kBankAuthoredShare;
    }
}

float FarFogReach(const FrameInputs& in, const Config& cfg)
{
    const float clientCap = in.clientFarClip > 0.0f ? std::min(in.clientFarClip, in.farClip) : in.farClip;
    const float lift = in.farClip / std::max(clientCap, 1.0f);
    return 1.0f + (lift - 1.0f) * std::clamp(cfg.farFogStretch, 0.0f, 1.0f);
}

float DistanceFogOpticalDepthPerDensity(float farStart, float farLimit, float range, float ramp)
{
    const float spanFraction = (farLimit - farStart) / range;
    return range * (spanFraction + ramp * spanFraction * spanFraction * spanFraction / 3.0f);
}

float OpenLandDensityShare(const Config& cfg)
{
    return std::clamp(cfg.density / kOpenLandFullAtDensity, 0.0f, 1.0f);
}

void DistanceLayer(const FrameInputs& in, const Config& cfg, const FogParams& p, float sunScatter,
                   const float* fogColor, const float* horizonColor, float openLand, FogLayer& out)
{
    out = FogLayer{};
    const float clientStart =
        std::min(std::max(in.fogStart * FarFogReach(in, cfg), 0.0f), p.farLimit * kFarStartFraction);
    const float farStart = Mix(clientStart, std::min(kOpenLandFarStart, p.farLimit * kFarStartFraction), openLand);
    const float ramp = Mix(kFarRamp, kOpenLandFarRamp, openLand);
    const float yourDepth = kFarOpticalDepth * cfg.farFog * cfg.density;
    const float depth = Mix(yourDepth, std::max(yourDepth, kOpenLandFarDepth * OpenLandDensityShare(cfg)), openLand);
    float opticalDepthPerDensity = DistanceFogOpticalDepthPerDensity(farStart, p.farLimit, p.maxDistance, ramp);
    out.start = farStart;
    out.density = cfg.stockFog == 1 && opticalDepthPerDensity > 0.0f ? depth / opticalDepthPerDensity : 0.0f;
    const float aerial = AerialPerspective(cfg);
    out.g = Mix(kFarG, kAerialFarG, aerial);
    out.isotropic = Mix(kFarIsotropic, kAerialIsotropic, aerial);
    out.strength = ramp;
    out.exponent = kFarExponent;
    out.shadowDensity = 1.0f;
    out.shadowed = cfg.lightShafts ? 1.0f : 0.0f;
    out.skyFalloff = kFarSkyFalloff;
    out.endDistance = p.farLimit;
    float color[3];
    for (int i = 0; i < 3; ++i)
        color[i] = Mix(fogColor[i], horizonColor[i], openLand);
    Scale(color, Mix(kFarAmbient, kAerialAmbient, aerial) * cfg.ambient, out.emissive);
    Scale(p.lightColor, kFarSun * sunScatter, out.diffuse);
    std::memcpy(out.shadowEmissive, out.emissive, sizeof(out.emissive));
}

float RampedPathLength(const FogLayer& l, float range)
{
    const float length = std::max(range - l.start, 0.0f);
    const float u = length / range;
    return length + l.strength * range * std::pow(u, l.exponent + 1.0f) / (l.exponent + 1.0f);
}

float HeightDensityFactor(const FogLayer& l, float z)
{
    return std::min(std::exp((l.upperHeight - z) * l.upperFalloff), 1.0f) *
           std::min(std::exp((z - l.lowerHeight) * l.lowerFalloff), 1.0f);
}

float ShadowDensityFactor(const FogLayer& l, float shadowedLayerLightScale)
{
    return l.shadowed > 0.0f ? l.shadowDensity + (1.0f - l.shadowDensity) * shadowedLayerLightScale : 1.0f;
}

float SceneLayersLevelRayOpticalDepth(const FogParams& p, float cameraZ)
{
    float opticalDepth = 0.0f;
    for (int i = 0; i < kSceneLayers; ++i)
    {
        const FogLayer& l = p.layers[i];
        const float heightFactor = HeightDensityFactor(l, cameraZ);
        const float shadowFactor = ShadowDensityFactor(l, p.shadowedLayerLightScale);
        opticalDepth += l.density * heightFactor * shadowFactor * RampedPathLength(l, p.maxDistance);
    }
    return opticalDepth;
}

float ClassicFogThinness(const FogParams& p, const AuthoredFog& fog, float cameraZ)
{
    const float coverageEdge = 1.0f - SmoothStep(kMinimumClassicCoverage, 1.0f, fog.coverage);
    const float levelRayOpticalDepth = SceneLayersLevelRayOpticalDepth(p, cameraZ);
    const float levelRayThinness = std::clamp(1.0f - levelRayOpticalDepth / kFarOpticalDepth, 0.0f, 1.0f);
    return std::max(coverageEdge, levelRayThinness);
}

const AuthoredLayer* MostForwardScatteringLayer(const AuthoredFog& fog)
{
    const AuthoredLayer* halo = nullptr;
    for (int i = 0; i < fog.layerCount; ++i)
        if (!halo || fog.layers[i].g > halo->g)
            halo = &fog.layers[i];
    return halo;
}

bool HaloHue(const AuthoredFog& fog, float* displayReferredRgb)
{
    const AuthoredLayer* halo = MostForwardScatteringLayer(fog);
    if (!halo)
        return false;
    const float* c = halo->diffuse;
    float peak = std::max(c[0], std::max(c[1], c[2]));
    if (peak < 1e-4f)
        return false;
    Scale(c, 1.0f / peak, displayReferredRgb);
    return true;
}

float WeatherStormWeight(const LightParamsSelection& selection)
{
    return selection.screenEffectSlot == kNoScreenEffectLightSlot ? std::clamp(selection.stormBlend, 0.0f, 1.0f)
                                                                   : 0.0f;
}

float HourlyLightWeight(const LightParamsSelection& selection)
{
    return selection.screenEffectSlot == FogData::kDeathSlot ? 0.0f : 1.0f;
}

float Plateau(float x, float risesFrom, float fullFrom, float fadesFrom, float goneAt)
{
    return SmoothStep(risesFrom, fullFrom, x) * (1.0f - SmoothStep(fadesFrom, goneAt, x));
}

float WeatherIntensity(const FrameInputs& in)
{
    return in.weather == WeatherKind::Clear ? 0.0f : std::clamp(in.weatherIntensity, 0.0f, 1.0f);
}

const WeatherTint& TintOf(WeatherKind weather)
{
    switch (weather)
    {
    case WeatherKind::Snow:
        return kSnowTint;
    case WeatherKind::Sand:
        return kSandTint;
    default:
        return kRainTint;
    }
}

void PullTowardWeather(float* rgb, const WeatherTint& tint, float amount)
{
    const float luminance = Luminance(rgb);
    for (int i = 0; i < 3; ++i)
        rgb[i] += (luminance * tint.hue[i] - rgb[i]) * tint.pull * amount;
}

void TintForTwilight(float* rgb, float weight)
{
    for (int i = 0; i < 3; ++i)
        rgb[i] *= std::max(1.0f + (kTwilightTint[i] - 1.0f) * weight, 0.0f);
}

float DustHourWeight(float dayFraction)
{
    return kDustCalmShare +
           (1.0f - kDustCalmShare) * Plateau(dayFraction, kDustRisesFrom, kDustFullFrom, kDustSettlesFrom, kDustCalmAt);
}

float HumidHourWeight(float dayFraction)
{
    return 1.0f +
           kHumidMorningGain * Plateau(dayFraction, kHumidMorningFrom, kHumidMorningFullFrom, kHumidMorningFadesFrom,
                                       kHumidMorningGoneAt) -
           kHumidAfternoonLoss * Plateau(dayFraction, kHumidAfternoonFrom, kHumidAfternoonFullFrom,
                                         kHumidAfternoonFadesFrom, kHumidAfternoonGoneAt);
}

float OpenLandShare(const FrameInputs& in)
{
    return std::clamp(std::max(in.dryLand, in.greenLand), 0.0f, 1.0f);
}

void HorizonSkyColor(const FrameInputs& in, bool linear, float* rgb)
{
    float horizon[3];
    float above[3];
    UnpackColor(in.skyHorizonColor, horizon);
    UnpackColor(in.skyAboveHorizonColor, above);
    for (int i = 0; i < 3; ++i)
        rgb[i] = Mix(horizon[i], above[i], kHorizonAboveShare);
    Encode(rgb, linear);
}

float WindAt(const FrameInputs& in, const Config& cfg)
{
    return cfg.windSpeed + (cfg.stormWind - cfg.windSpeed) * WeatherIntensity(in);
}

float DustWindGain(float windSpeed)
{
    return std::min(1.0f + kDustPerWindYard * std::max(windSpeed - kDustWindFrom, 0.0f), kDustMaxWindGain);
}

void OpenLandHaze(const FrameInputs& in, const Config& cfg, const FogParams& p, float sunScatter,
                  const float* skyColor, FogLayer& out)
{
    out = FogLayer{};
    Unbounded(out);
    const float dust = kDustDensity * std::clamp(in.dryLand, 0.0f, 1.0f) * cfg.dustHaze *
                       DustHourWeight(in.dayFraction) * DustWindGain(WindAt(in, cfg));
    const float humid =
        kHumidDensity * std::clamp(in.greenLand, 0.0f, 1.0f) * cfg.humidHaze * HumidHourWeight(in.dayFraction);
    const float total = (dust + humid) * OpenLandDensityShare(cfg);
    if (!(total > 0.0f))
        return;
    const float dustShare = dust / (dust + humid);
    out.density = total;
    out.g = Mix(kHumidG, kDustG, dustShare);
    out.isotropic = Mix(kHumidIsotropic, kDustIsotropic, dustShare);
    const float sky = Mix(kHumidSky, kDustSky, dustShare) * cfg.ambient;
    const float sun = Mix(kHumidSun, kDustSun, dustShare) * sunScatter;
    for (int i = 0; i < 3; ++i)
    {
        const float tint = Mix(kHumidTint[i], kDustTint[i], dustShare);
        out.emissive[i] = skyColor[i] * tint * sky;
        out.diffuse[i] = p.lightColor[i] * tint * sun;
    }
    out.exponent = 1.0f;
    out.upperHeight = p.referenceZ;
    out.upperFalloff = Mix(kHumidFalloff, kDustFalloff, dustShare);
    std::memcpy(out.shadowEmissive, out.emissive, sizeof(out.emissive));
    out.shadowDensity = 1.0f;
    out.shadowed = cfg.lightShafts ? 1.0f : 0.0f;
    out.skyFalloff = kOpenLandSkyFalloff;
    out.endDistance = std::max(kDerivedLayerLimit, p.farClip);
    out.bankShare = kBankOpenLandShare;
}

int FreeSceneLayer(const FogParams& p, const AuthoredFog* authored)
{
    if (!authored)
        return kSceneLayers - 1;
    return authored->layerCount < kSceneLayers ? kSceneLayers - 1 : -1;
}

void CloudShadows(const FrameInputs& in, const Config& cfg, float hourly, FogParams& p)
{
    const float dry = std::clamp(in.dryLand, 0.0f, 1.0f);
    const float green = std::clamp(in.greenLand, 0.0f, 1.0f);
    const float classes = dry + green;
    const float cover = classes > 0.0f ? (dry * kCloudCoverDry + green * kCloudCoverGreen) / classes : 0.0f;
    p.cloudCover = std::min(cover * cfg.cloudCover, kMaxCloudCover);
    p.cloudShadow = cfg.cloudShadows * std::max(dry, green) * hourly * (1.0f - WeatherIntensity(in)) *
                    SmoothStep(0.0f, kCloudShadowFullAtElevation, in.toLight[2]);
    if (!(p.cloudCover > 0.0f))
        p.cloudShadow = 0.0f;
    p.cloudTileYards = cfg.cloudSize * static_cast<float>(kCloudCellsPerTile);
    p.cloudHeight = p.referenceZ + kCloudHeight;
    const float speed = WindAt(in, cfg) * kCloudWindScale;
    const float heading = cfg.windDirection * kDegreesToRadians;
    p.cloudWind[0] = std::cos(heading) * speed;
    p.cloudWind[1] = -std::sin(heading) * speed;
}

void ApplyWeatherAndTimeOfDay(const FrameInputs& in, const Config& cfg, FogParams& p)
{
    const float hazeScale =
        1.0f + cfg.eveningHaze * HourlyLightWeight(in.lightParams) * EveningHazeWeight(in.dayFraction);
    const float densityScale = hazeScale * (1.0f + cfg.weatherFog * p.weather);
    const WeatherTint& tint = TintOf(in.weather);
    const float tintAmount = cfg.weatherTint * p.weather;
    for (FogLayer& l : p.layers)
    {
        l.density *= densityScale;
        PullTowardWeather(l.diffuse, tint, tintAmount);
        PullTowardWeather(l.emissive, tint, tintAmount);
        PullTowardWeather(l.shadowEmissive, tint, tintAmount);
        TintForTwilight(l.emissive, p.twilight);
        TintForTwilight(l.shadowEmissive, p.twilight);
    }
    Scale(p.airShaftScatter, densityScale, p.airShaftScatter);
    PullTowardWeather(p.airShaftScatter, tint, tintAmount);
    p.eveningHaze = hazeScale - 1.0f;
}

float ClientToClassicDirectLight(const AuthoredFog& fog, const float* clientDirectLight, bool linear)
{
    if (!fog.hasClassicDirectLight)
        return 1.0f;
    float classic[3] = {fog.classicDirectLight[0], fog.classicDirectLight[1], fog.classicDirectLight[2]};
    Encode(classic, linear);
    const float classicLuminance = Luminance(classic);
    if (classicLuminance < kMinClassicDirectLuminance)
        return 1.0f;
    return std::clamp(Luminance(clientDirectLight) / classicLuminance, 0.0f, 1.0f);
}
}

ZoneClimateWeights ZoneClimateTarget(int zoneId, const Config& cfg)
{
    ZoneClimateWeights w = {};
    if (zoneId <= 0)
        return w;
    if (ZoneListIncludes(cfg.dryZones, zoneId))
        w.dry = 1.0f;
    else if (ZoneListIncludes(cfg.greenZones, zoneId))
        w.green = 1.0f;
    return w;
}

float ZoneDensityTarget(int zoneId, const Config& cfg)
{
    return zoneId > 0 ? ZoneFactorOf(cfg.zoneDensity, zoneId) : cfg.zoneDensity.otherZones;
}

float FogCeilingGate(float ceiling, float cameraAboveGround)
{
    if (!(ceiling > 0.0f) || !std::isfinite(cameraAboveGround))
        return 0.0f;
    const float t = std::clamp((cameraAboveGround - kFogCeilingGateFrom * ceiling) /
                                   ((kFogCeilingGateFull - kFogCeilingGateFrom) * ceiling),
                               0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float FogCeilingShare(float ceiling, float strength, float aboveGround)
{
    if (!(ceiling > 0.0f))
        return 1.0f;
    const float faded = std::clamp((aboveGround - ceiling) / (kFogCeilingFadeShare * ceiling), 0.0f, 1.0f);
    return 1.0f - std::clamp(strength, 0.0f, 1.0f) * faded;
}

float FogCeilingGateStep(float gate, float target, double seconds)
{
    const float most = static_cast<float>(std::max(seconds, 0.0) / kFogCeilingGateSeconds);
    return gate + std::clamp(target - gate, -most, most);
}

const Config& ZonedLook(const Config& look, float densityFactor, Config& storage)
{
    if (densityFactor == 1.0f)
        return look;
    storage = look;
    storage.density = look.density * std::max(densityFactor, 0.0f);
    return storage;
}

void ZoneClimate::Update(FrameInputs& in, const Config& cfg)
{
    const ZoneClimateWeights target = ZoneClimateTarget(in.zoneId, cfg);
    const float density = ZoneDensityTarget(in.zoneId, cfg);
    if (in.zoneId != m_zoneId)
    {
        m_zoneId = in.zoneId;
        m_densityFrom = m_density;
        m_densityFade = 0.0f;
        VF_LOG_INFO("zone %d: %s, fog density x%.2f", in.zoneId,
                    target.dry > 0.0f ? "dry (dust haze, scattered clouds)"
                    : target.green > 0.0f ? "green (humid haze, more clouds)"
                                          : "neither dry nor green",
                    density);
    }
    if (m_seconds < 0.0 || !(in.seconds >= m_seconds))
    {
        m_dry = target.dry;
        m_green = target.green;
        m_density = density;
        m_densityFade = 1.0f;
    }
    else
    {
        const float step = static_cast<float>(std::min(in.seconds - m_seconds, 0.25) / kZoneClimateFadeSeconds);
        m_dry += std::clamp(target.dry - m_dry, -step, step);
        m_green += std::clamp(target.green - m_green, -step, step);
        if (std::fabs(target.dry - m_dry) < kZoneClimateSnap)
            m_dry = target.dry;
        if (std::fabs(target.green - m_green) < kZoneClimateSnap)
            m_green = target.green;
        m_densityFade = std::min(m_densityFade + step, 1.0f);
        m_density = m_densityFrom + (density - m_densityFrom) * m_densityFade;
    }
    m_seconds = in.seconds;
    in.dryLand = m_dry;
    in.greenLand = m_green;
}

ZoneClimate& GlobalZoneClimate()
{
    static ZoneClimate climate;
    return climate;
}

void UnpackColor(uint32_t argb, float* rgb)
{
    rgb[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    rgb[1] = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    rgb[2] = static_cast<float>(argb & 0xFF) / 255.0f;
}

namespace
{
void WaterBand(uint32_t colour, float alpha, float value, float* band)
{
    UnpackColor(colour, band);
    Scale(band, value, band);
    band[3] = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
}

void BalanceLight(const FrameInputs& in, float strength, float* gains)
{
    for (int i = 0; i < 3; ++i)
        gains[i] = 1.0f;
    if (!(strength > 0.0f))
        return;
    float ambient[3];
    float direct[3];
    UnpackColor(in.ambientColor, ambient);
    UnpackColor(in.directColor, direct);
    float light[3];
    for (int i = 0; i < 3; ++i)
        light[i] = std::pow(std::max(ambient[i] + kSceneDirectShare * direct[i], kMinSceneLight), 2.2f);
    const float grey = Luminance(light);
    float balanced[3];
    for (int i = 0; i < 3; ++i)
    {
        gains[i] = std::clamp(std::pow(grey / light[i], strength), kMinLightBalance, kMaxLightBalanceGain);
        balanced[i] = gains[i] * light[i];
    }
    const float kept = grey / std::max(Luminance(balanced), 1e-6f);
    for (int i = 0; i < 3; ++i)
        gains[i] = std::clamp(gains[i] * kept, kMinLightBalance, kMaxLightBalanceGain);
}

void LandGains(const FrameInputs& in, float strength, float brightness, float* gains)
{
    BalanceLight(in, strength, gains);
    for (int i = 0; i < 3; ++i)
        gains[i] *= brightness;
}

void HazeOverTheSky(const FrameInputs& in, const Config& cfg, float hourly, FogParams& p)
{
    for (int i = 0; i < 3; ++i)
        p.skyGlow[i] = 0.0f;
    p.skyHazeZenith = 1.0f;
    p.skyHazeGlowGrade = std::clamp(cfg.skyHazeGlowGrade, 0.0f, 1.0f);
    const float haze = hourly * std::clamp(cfg.skyHaze, 0.0f, 1.0f);
    if (!(haze > 0.0f))
        return;
    float horizon[3];
    HorizonSkyColor(in, p.linear, horizon);
    float sun[3];
    std::copy(p.rayColor, p.rayColor + 3, sun);
    const float ownSun = SmoothStep(0.0f, kSkyGlowOwnSunFrom, std::max(sun[0], std::max(sun[1], sun[2])));
    Encode(sun, p.linear);
    for (int i = 0; i < 3; ++i)
        sun[i] = Mix(p.lightColor[i], sun[i], ownSun);
    const float weather = p.overcastShare;
    const float total = 1.0f - (1.0f - weather) * (1.0f - haze);
    const float hazeShare = (1.0f - weather) * haze / total;
    const float glow = std::clamp(cfg.skyHazeGlow, 0.0f, kMaxSkyHazeGlow) * hazeShare * p.lightVisibility;
    for (int i = 0; i < 3; ++i)
    {
        p.overcastSky[i] = Mix(p.overcastSky[i], horizon[i], hazeShare);
        p.skyGlow[i] = sun[i] * glow;
    }
    p.overcastShare = total;
    p.skyHazeZenith = Mix(1.0f, std::clamp(cfg.skyHazeZenith, kMinSkyHazeZenith, 1.0f), hazeShare);
}

bool SkyColourKnown(const FrameInputs& in)
{
    return ((in.skyHorizonColor | in.skyAboveHorizonColor) & kColourBits) != 0u;
}

float SkyKeptAtNight(const FrameInputs& in, const FogParams& p, const float* sky)
{
    if (!(p.nightDarkness > 0.0f))
        return 1.0f;
    if (!p.linear)
        return std::pow(std::max(1.0f - p.nightDarkness, 0.0f), 1.0f / kDisplayGamma);
    float perLight[3];
    AlbedoPerSceneLight(in, perLight);
    float albedo[3];
    for (int i = 0; i < 3; ++i)
        albedo[i] = sky[i] * perLight[i];
    const float selfLit = std::clamp((Luminance(albedo) * SelfLitShare(in) - kSelfLitFrom) / kSelfLitSpan, 0.0f, 1.0f);
    return 1.0f - p.nightDarkness * (1.0f - selfLit);
}

void HazeFromTheSky(const FrameInputs& in, const Config& cfg, FogParams& p)
{
    std::fill(p.hazeFromSky, p.hazeFromSky + 3, 0.0f);
    p.hazeFromSkyShare = 0.0f;
    p.hazeSkyShown = kUnknownSky;
    p.hazeSkyNightKept = 1.0f;
    if (!SkyColourKnown(in))
        return;
    const float exposure = std::max(p.authored ? cfg.classicExposure : cfg.exposure, kMinHazeExposure);
    float sky[3];
    HorizonSkyColor(in, p.linear, sky);
    p.hazeSkyNightKept = SkyKeptAtNight(in, p, sky);
    float shown[3];
    for (int i = 0; i < 3; ++i)
    {
        const float drawn =
            std::max(Mix(sky[i] * p.hazeSkyNightKept, p.overcastSky[i] * exposure, p.overcastShare), 0.0f);
        shown[i] = p.linear ? std::pow(drawn, 1.0f / kDisplayGamma) * p.skyWhiteBalance[i] : drawn;
    }
    p.hazeSkyShown = Luminance(shown);
    const float skyChroma = p.linear ? p.skySaturation : 1.0f;
    const float level = std::clamp(cfg.hazeSkyLevel, kMinHazeSkyLevel, kMaxHazeSkyLevel);
    for (int i = 0; i < 3; ++i)
    {
        const float target = level * std::max(p.hazeSkyShown + (shown[i] - p.hazeSkyShown) * skyChroma, 0.0f);
        const float beforeGrade = p.linear ? std::pow(target / p.whiteBalance[i], kDisplayGamma) : target;
        p.hazeFromSky[i] = beforeGrade / exposure;
    }
    p.hazeFromSkyShare = std::clamp(cfg.hazeFromSky, 0.0f, 1.0f);
}

BankShape ShapeBank(const Config& cfg, const BankProfile& profile, float spread, float sharpness)
{
    BankShape shape;
    const float cover = std::clamp(cfg.bankCover * profile.cover, 0.0f, 1.0f);
    shape.threshold = BankThreshold(cover, spread);
    shape.norm = BankNorm(shape.threshold, spread, sharpness);
    shape.height = std::clamp(cfg.bankHeight, kMinBankHeight, kMaxBankHeight) * profile.height;
    shape.density = profile.density;
    return shape;
}

BankProfile MixProfiles(const BankProfile& a, const BankProfile& b, float t)
{
    return {Mix(a.cover, b.cover, t), Mix(a.height, b.height, t), Mix(a.density, b.density, t),
            Mix(a.pool, b.pool, t)};
}

void BankFogParams(const FrameInputs& in, const Config& cfg, float hourly, int openLandLayer, FogParams& p)
{
    p.bankStrength = BankStrength(cfg, in.dayFraction, hourly);
    const float dry = std::clamp(in.dryLand, 0.0f, 1.0f);
    const float spread = BankNoiseSpread();
    p.bankSharpness = 1.0f / (kBankEdgeSpreads * spread);
    p.bankThresholdRise = kBankRiseSpreads * spread;
    p.bankShadeNoiseScale = spread / kBankPatchSpread;
    p.bankShade = std::clamp(cfg.bankShadow, 0.0f, 3.0f) * kBankShadeDepth;
    p.bankShadeYards = kBankShadeShareOfSize * cfg.driftSize;
    p.bankValleysOnly = cfg.banks == kBanksValleysAtDawnAndDusk ? 1.0f : 0.0f;
    const BankProfile land = MixProfiles(kBankNeutral, BankHourProfile(kBankLand, in.dayFraction, dry), hourly);
    const float pool = std::clamp(cfg.bankPool, 0.0f, 3.0f) * land.pool;
    p.bankPoolGain = kBankPoolGainPerPool * pool;
    p.bankLift = std::min(pool, 1.0f);
    p.banks[kBankLand] = ShapeBank(cfg, land, spread, p.bankSharpness);
    p.banks[kBankLand].density *= Mix(1.0f, DustWindGain(WindAt(in, cfg)), dry * hourly);
    for (BankGround water : {kBankLake, kBankSea})
        p.banks[water] = ShapeBank(cfg, MixProfiles(kBankNeutral, BankHourProfile(water, in.dayFraction, dry), hourly),
                                   spread, p.bankSharpness);
    p.bankDust = dry * p.bankStrength * kBankDustTintShare;
    for (int i = 0; i < kSceneLayers; ++i)
    {
        FogLayer& l = p.layers[i];
        l.bankShare *= p.bankStrength;
        if (i == openLandLayer || !(p.bankDust > 0.0f))
            continue;
        const float tint = p.bankDust * std::fabs(l.bankShare);
        for (int c = 0; c < 3; ++c)
        {
            const float hue = Mix(1.0f, kDustTint[c], tint);
            l.emissive[c] *= hue;
            l.shadowEmissive[c] *= hue;
            l.diffuse[c] *= hue;
        }
    }
    p.layers[kDistanceFogLayer].bankShare = 0.0f;
}
}

FogParams BuildFogParams(const FrameInputs& in, const Config& cfg, const AuthoredFog* authored)
{
    FogParams p = {};
    p.linear = cfg.colorSpace == 1;
    p.authored = authored != nullptr;
    float fogColor[3];
    UnpackColor(in.fogColor, fogColor);
    UnpackColor(in.directColor, p.lightColor);
    UnpackColor(in.lightIsMoon ? in.directColor : in.sunColor, p.rayColor);
    Encode(fogColor, p.linear);
    Encode(p.lightColor, p.linear);

    float elevation = SmoothStep(-0.03f, 0.10f, in.toLight[2]);
    p.lightVisibility = elevation * (in.lightIsMoon ? kMoonLight : 1.0f);
    p.lightAboveHorizon = SmoothStep(-kLightSettingHalfWidth, kLightSettingHalfWidth, in.toLight[2]);
    p.shadowedLayerLightScale = p.authored ? p.lightAboveHorizon : 1.0f;
    const float hourly = HourlyLightWeight(in.lightParams);
    const bool moonShafts = in.lightIsMoon && hourly > 0.0f;
    const float lowSun = in.lightIsMoon ? 0.0f : hourly * LowSunWeight(in.toLight[2]);
    p.shaftStrength = moonShafts ? cfg.moonShafts : cfg.shafts * (1.0f + cfg.lowSunShafts * lowSun);
    p.shaftFocus = moonShafts ? cfg.moonShaftFocus : cfg.shaftFocus;
    p.farClip = in.farClip;
    p.maxDistance = std::max(cfg.maxDistance, in.farClip);
    p.horizonStart = in.farClip * 0.85f;
    p.referenceZ = ReferenceZ(in);
    p.farLimit = std::clamp(in.fogEnd * FarFogReach(in, cfg), 100.0f, in.farClip);

    float fogDistance = in.zoneFogDistance > 50.0f && in.zoneFogDistance < 20000.0f ? in.zoneFogDistance : in.fogEnd;
    fogDistance = std::clamp(fogDistance, 50.0f, 5000.0f);
    float elevationFadedScatter = p.lightVisibility * cfg.sunScatter;

    float horizonColor[3];
    HorizonSkyColor(in, p.linear, horizonColor);
    p.openLand = OpenLandShare(in);
    const float horizonFog = p.openLand * std::clamp(cfg.horizonFog, 0.0f, 1.0f);
    FogLayer& distanceFog = p.layers[kDistanceFogLayer];
    p.directLightMatch = 1.0f;
    if (p.authored)
    {
        const float stormLightMatch = ClientToClassicDirectLight(*authored, p.lightColor, p.linear);
        p.directLightMatch = 1.0f + (stormLightMatch - 1.0f) * WeatherStormWeight(in.lightParams);
        AuthoredLayers(*authored, cfg, p, cfg.sunScatter * p.directLightMatch, p.layers);
        HaloHue(*authored, p.rayColor);
        DistanceLayer(in, cfg, p, elevationFadedScatter, fogColor, horizonColor, horizonFog, distanceFog);
        distanceFog.density *= ClassicFogThinness(p, *authored, in.camPos[2]);
    }
    else
    {
        DerivedLayers(in, cfg, p, elevationFadedScatter, fogDistance, fogColor, p.layers);
        DistanceLayer(in, cfg, p, elevationFadedScatter, fogColor, horizonColor, horizonFog, distanceFog);
    }
    const int hazeLayer = FreeSceneLayer(p, authored);
    if (hazeLayer >= 0 && p.openLand > 0.0f)
        OpenLandHaze(in, cfg, p, elevationFadedScatter, horizonColor, p.layers[hazeLayer]);
    Scale(p.lightColor, elevationFadedScatter * p.directLightMatch * kAirShaftDensity * cfg.shaftHaze * cfg.density,
          p.airShaftScatter);
    BankFogParams(in, cfg, hourly, hazeLayer >= 0 && p.openLand > 0.0f ? hazeLayer : -1, p);

    p.weather = WeatherIntensity(in);
    p.shaftStrength *= 1.0f + (cfg.weatherShafts - 1.0f) * p.weather;
    p.windSpeed = cfg.windSpeed + (cfg.stormWind - cfg.windSpeed) * p.weather;
    p.drift = cfg.drift;
    p.mistBoost = hourly * cfg.morningMist * MorningMistWeight(in.dayFraction) *
                  MistGain(NearGroundFogDensity(p), kMistReferenceDensity * cfg.density);
    p.mistTop = p.referenceZ;
    p.mistFalloff = 1.0f / cfg.mistHeight;
    p.twilight = hourly * cfg.twilight * TwilightWeight(in.sunElevation, in.moonElevation);
    p.lightGlow = hourly * cfg.lightGlow * NightWeight(in.sunElevation);
    const float lampDayShare = LampDayShare(cfg, in.sunElevation);
    p.lampHaze = hourly * cfg.lampFog * lampDayShare *
                 (kLampClearHaze + kLampWeatherHaze * cfg.weatherFog * p.weather) *
                 LampFogDensityGain(NearGroundFogDensity(p) * (1.0f + p.mistBoost));
    p.lampLight = hourly * cfg.lampLight * lampDayShare;
    p.lanternGlow = LampNightWeight(in.sunElevation);
    p.nightDarkness = hourly * cfg.nightDarkness * LampNightWeight(in.sunElevation) * (1.0f - in.indoors);
    p.windowLight = hourly * cfg.windowLight * LampNightWeight(in.sunElevation);
    p.lanternLight = hourly * cfg.lanternLight;
    p.effectLight = hourly * cfg.effectLight * lampDayShare;
    p.corpseLight = hourly * cfg.corpseLight * lampDayShare;
    p.farLightCore = hourly * cfg.farLights * LampNightWeight(in.sunElevation);
    CloudShadows(in, cfg, hourly, p);
    ApplyWeatherAndTimeOfDay(in, cfg, p);
    std::copy(fogColor, fogColor + 3, p.overcastSky);
    PullTowardWeather(p.overcastSky, TintOf(in.weather), cfg.weatherTint * p.weather);
    p.overcastShare = kOvercastSkyShare * std::clamp(cfg.weatherSky * p.weather, 0.0f, 1.0f);
    p.sunAndMoon = std::clamp(cfg.sunAndMoon, 0.0f, 1.0f) * (1.0f - kSunAndMoonWeatherShare * p.weather);
    float direct[3];
    UnpackColor(in.directColor, direct);
    Encode(direct, true);
    const float peak = std::max({direct[0], direct[1], direct[2], kMinLightLuminance});
    for (int i = 0; i < 3; ++i)
        p.lowSunLift[i] = cfg.lowSunLight * lowSun * kLowSunLift * (1.0f + (direct[i] / peak - 1.0f) * kLowSunWarmth);
    SaturateLightColours(std::clamp(cfg.lightSaturation, 0.0f, kMaxLightSaturation), p);
    SaturateHazeColours(std::clamp(cfg.hazeSaturation, 0.0f, kMaxSaturation), p);
    p.saturation = std::clamp(cfg.saturation, 0.0f, kMaxSaturation);
    p.skySaturation = std::clamp(cfg.skySaturation, 0.0f, kMaxSaturation);
    const float temperature = std::clamp(cfg.temperature, -kMaxWhiteBalance, kMaxWhiteBalance);
    const float tint = std::clamp(cfg.tint, -kMaxWhiteBalance, kMaxWhiteBalance);
    WhiteBalanceGains(temperature, tint, p.whiteBalance);
    WhiteBalanceGains(temperature + std::clamp(cfg.skyTemperature, -kMaxWhiteBalance, kMaxWhiteBalance),
                      tint + std::clamp(cfg.skyTint, -kMaxWhiteBalance, kMaxWhiteBalance), p.skyWhiteBalance);
    const float skyBrightness =
        std::pow(std::clamp(cfg.skyBrightness, kMinSkyBrightness, kMaxSkyBrightness), 1.0f / kDisplayGamma);
    for (int i = 0; i < 3; ++i)
        p.skyWhiteBalance[i] *= skyBrightness;
    LandGains(in, hourly * std::clamp(cfg.lightBalance, 0.0f, kMaxLightBalance) *
                      (1.0f - std::clamp(in.indoors, 0.0f, 1.0f)),
              std::clamp(cfg.landBrightness, kMinLandBrightness, kMaxLandBrightness), p.lightBalance);
    HazeOverTheSky(in, cfg, hourly, p);
    HazeFromTheSky(in, cfg, p);
    p.lateDrawsInFront =
        std::clamp(cfg.rainInFront, 0.0f, 1.0f) * std::clamp(p.weather / kLateDrawFullWeather, 0.0f, 1.0f);
    return p;
}

bool WaterWanted(const Config& cfg)
{
    return cfg.waterReflect > 0.0f || cfg.waterGlitter > 0.0f || cfg.waterClarity > 0.0f || cfg.waterSmooth > 0.0f;
}

WaterParams BuildWaterParams(const FrameInputs& in, const Config& cfg)
{
    WaterParams w = {};
    const float weather = WeatherIntensity(in);
    w.reflect = cfg.waterReflect;
    w.clarity = cfg.waterClarity;
    w.smooth = cfg.waterSmooth;
    w.waves = cfg.waterWaves;
    w.windSpeed = cfg.windSpeed + (cfg.stormWind - cfg.windSpeed) * weather;
    w.windDirection = cfg.windDirection;
    w.glitter = cfg.waterGlitter * HourlyLightWeight(in.lightParams) *
                SmoothStep(kWaterGlitterFrom, kWaterGlitterFullAt, in.toLight[2]) *
                (1.0f - kWeatherGlitterLoss * weather) * (in.lightIsMoon ? kMoonGlitter : 1.0f);
    UnpackColor(in.lightIsMoon ? in.directColor : in.sunColor, w.glitterColor);
    const float peak = std::max({w.glitterColor[0], w.glitterColor[1], w.glitterColor[2]});
    if (peak > kMinGlitterPeak)
        Scale(w.glitterColor, 1.0f / peak, w.glitterColor);
    else
        w.glitter = 0.0f;
    UnpackColor(in.fogColor, w.skyHorizon);
    for (int i = 0; i < 3; ++i)
        w.skyZenith[i] = w.skyHorizon[i] * kSkyZenithTint[i];
    WaterBand(in.waterBandColors[kRiverShallowColour], in.waterBandAlphas[kRiverShallowAlpha], 1.0f, w.riverShallow);
    WaterBand(in.waterBandColors[kRiverDeepColour], in.waterBandAlphas[kRiverDeepAlpha], 1.0f, w.riverDeep);
    WaterBand(in.waterBandColors[kOceanShallowColour], in.waterBandAlphas[kOceanShallowAlpha], 1.0f,
              w.oceanShallow);
    WaterBand(in.waterBandColors[kOceanDeepColour], in.waterBandAlphas[kOceanDeepAlpha], kOceanDeepestValue,
              w.oceanDeep);
    return w;
}

float WaterFresnel(float cosine)
{
    const float r = (kWaterRefractiveIndex - 1.0f) / (kWaterRefractiveIndex + 1.0f);
    const float f0 = r * r;
    const float x = 1.0f - std::clamp(cosine, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * x * x * x * x * x;
}

bool MoonLightsTheFog(bool clientMoonHours, float sunElevation, float moonElevation)
{
    if (clientMoonHours)
        return !(moonElevation < -kLightHandoverMargin && sunElevation > kLightHandoverMargin);
    return sunElevation < -kLightHandoverMargin && moonElevation > kLightHandoverMargin;
}

float NearGroundFogDensity(const FogParams& p)
{
    float density = 0.0f;
    for (const FogLayer& l : p.layers)
    {
        if (l.start > kMistProbeDistance || l.endDistance < kMistProbeDistance)
            continue;
        const float curve =
            1.0f + l.strength * std::pow(std::clamp((kMistProbeDistance - l.start) / p.maxDistance, 0.0f, 1.0f),
                                         l.exponent);
        const float height = std::clamp(std::exp((l.upperHeight - p.referenceZ) * l.upperFalloff), 0.0f, 1.0f) *
                             std::clamp(std::exp((p.referenceZ - l.lowerHeight) * l.lowerFalloff), 0.0f, 1.0f);
        density += l.density * curve * height;
    }
    return density;
}

float MistGain(float nearGroundDensity, float referenceDensity)
{
    if (nearGroundDensity * kMaxMistGain <= referenceDensity)
        return kMaxMistGain;
    return std::max(referenceDensity / nearGroundDensity, 1.0f);
}

float BankStrength(const Config& cfg, float dayFraction, float hourly)
{
    if (cfg.banks == kBanksEverywhere)
        return 1.0f;
    if (cfg.banks != kBanksValleysAtDawnAndDusk)
        return 0.0f;
    return hourly * std::max(Plateau(dayFraction, kBankDawnRisesFrom, kBankDawnFullFrom, kBankDawnFadesFrom,
                                     kBankDawnGoneAt),
                             Plateau(dayFraction, kBankDuskRisesFrom, kBankDuskFullFrom, kBankDuskFadesFrom,
                                     kBankDuskGoneAt));
}

BankProfile BankHourProfile(BankGround ground, float dayFraction, float dry)
{
    const float day = dayFraction - std::floor(dayFraction);
    const float weights[kBankHours] = {
        SmoothStep(kBankNightFrom, kBankNightFullFrom, day) + 1.0f -
            SmoothStep(kBankNightFadesFrom, kBankNightGoneAt, day),
        Plateau(day, kBankDawnRisesFrom, kBankDawnFullFrom, kBankDawnFadesFrom, kBankDawnGoneAt),
        Plateau(day, kBankDayRisesFrom, kBankDayFullFrom, kBankDayFadesFrom, kBankDayGoneAt),
        Plateau(day, kBankDuskRisesFrom, kBankDuskFullFrom, kBankDuskFadesFrom, kBankDuskGoneAt),
    };
    float total = 0.0f;
    for (float w : weights)
        total += w;
    BankProfile mixed = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int h = 0; h < kBankHours; ++h)
    {
        const BankProfile humidOrWater = ground == kBankLake ? kBankLakeWater[h]
                                         : ground == kBankSea ? kBankSeaWater[h]
                                                              : kBankHumidLand[h];
        const BankProfile at = ground == kBankLand
                                   ? MixProfiles(humidOrWater, kBankDryLand[h], std::clamp(dry, 0.0f, 1.0f))
                                   : humidOrWater;
        const float w = total > 0.0f ? weights[h] / total : (h == kBankDay ? 1.0f : 0.0f);
        mixed.cover += w * at.cover;
        mixed.height += w * at.height;
        mixed.density += w * at.density;
        mixed.pool += w * at.pool;
    }
    return mixed;
}

float BankNoiseSpread()
{
    return std::sqrt(kBankPatchSpread * kBankPatchSpread +
                     kBankWispErosion * kBankWispErosion * kBankWispSpread * kBankWispSpread);
}

float BankThreshold(float cover, float spread)
{
    if (cover >= kBankEvenCover)
        return kBankNoThreshold;
    const double wanted = std::max(cover, kBankMinCover);
    double lo = -kBankCoverSearchReach;
    double hi = kBankCoverSearchReach;
    for (int k = 0; k < kBankCoverSearchSteps; ++k)
    {
        const double mid = 0.5 * (lo + hi);
        if (0.5 * std::erfc(mid / std::sqrt(2.0)) > wanted)
            lo = mid;
        else
            hi = mid;
    }
    return kBankNoiseMean + spread * static_cast<float>(0.5 * (lo + hi));
}

float BankNorm(float threshold, float spread, float sharpness)
{
    if (threshold <= kBankNoThreshold)
        return 1.0f;
    double expected = 0.0;
    const double reach = kBankNormReachSpreads * spread;
    const double step = 2.0 * reach / kBankNormSamples;
    for (int k = 0; k < kBankNormSamples; ++k)
    {
        const double n = kBankNoiseMean - reach + (k + 0.5) * step;
        const double z = (n - kBankNoiseMean) / spread;
        const double density = std::exp(-0.5 * z * z) / (spread * std::sqrt(2.0 * 3.14159265358979));
        expected += std::clamp((n - threshold) * sharpness, 0.0, 1.0) * density * step;
    }
    return expected > 1.0 / kBankMaxNorm ? static_cast<float>(1.0 / expected) : kBankMaxNorm;
}

float MorningMistWeight(float dayFraction)
{
    return Plateau(dayFraction, kMistRisesFrom, kMistFullFrom, kMistBurnsOffFrom, kMistGoneAt);
}

float EveningHazeWeight(float dayFraction)
{
    return Plateau(dayFraction, kHazeRisesFrom, kHazeFullFrom, kHazeFadesFrom, kHazeGoneAt);
}

float LowSunWeight(float sunElevation)
{
    return Plateau(sunElevation, kLowSunRisesFrom, kLowSunFullFrom, kLowSunFadesFrom, kLowSunGoneAt);
}

float TwilightWeight(float sunElevation, float moonElevation)
{
    return Plateau(-sunElevation, -kTwilightRisesBelow, -kTwilightFullBelow, -kTwilightFadesBelow,
                   -kTwilightGoneBelow) *
           (1.0f - SmoothStep(kTwilightFadesAsTheMoonPasses, kTwilightGoneWithTheMoonAt, moonElevation));
}

float NightWeight(float sunElevation)
{
    return SmoothStep(-kNightFromSunBelow, -kFullNightSunBelow, -sunElevation);
}

float LampNightWeight(float sunElevation)
{
    return 1.0f - std::clamp((sunElevation - kLampDaylightFrom) / kLampDaylightSpan, 0.0f, 1.0f);
}

float LampDayShare(const Config& cfg, float sunElevation)
{
    return cfg.lampDayLight + (1.0f - cfg.lampDayLight) * LampNightWeight(sunElevation);
}

float LampFogDensityGain(float nearGroundDensity)
{
    if (!(nearGroundDensity > 0.0f))
        return kMinLampDensityGain;
    return std::clamp(std::sqrt(nearGroundDensity / kLampReferenceDensity), kMinLampDensityGain,
                      kMaxLampDensityGain);
}

void SceneLight(const FrameInputs& in, float* light)
{
    float ambient[3];
    float direct[3];
    UnpackColor(in.ambientColor, ambient);
    UnpackColor(in.directColor, direct);
    Encode(ambient, true);
    Encode(direct, true);
    const float day = 1.0f - LampNightWeight(in.sunElevation);
    const float indoorDay = day * std::clamp(in.indoors, 0.0f, 1.0f);
    float outdoors[3];
    for (int i = 0; i < 3; ++i)
        outdoors[i] = ambient[i] + kSceneDirectShare * direct[i];
    const float grey = Luminance(outdoors);
    const float lit = std::max(grey - indoorDay * std::max(grey - kInteriorDaySceneLight, 0.0f), kMinSceneLight);
    for (int i = 0; i < 3; ++i)
        light[i] = lit;
}

SunFacing DaylightFacing(const FrameInputs& in)
{
    SunFacing facing = {{0.0f, 0.0f, 0.0f}, 1.0f, 0.0f};
    TransformDirection(in.toLight, in.cameraRelativeView, facing.toLightView);
    float ambient[3];
    float direct[3];
    UnpackColor(in.ambientColor, ambient);
    UnpackColor(in.directColor, direct);
    Encode(ambient, true);
    Encode(direct, true);
    const float a = Luminance(ambient);
    const float d = Luminance(direct);
    const float average = a + kSceneDirectShare * d;
    const float day = (1.0f - LampNightWeight(in.sunElevation)) * (1.0f - std::clamp(in.indoors, 0.0f, 1.0f));
    if (!(average > 0.0f) || !(day > 0.0f))
        return facing;
    facing.atZero = std::max(1.0f + day * (a / average - 1.0f), kMinSunFacingScale);
    facing.perFacing = day * d / average;
    return facing;
}

void AlbedoPerSceneLight(const FrameInputs& in, float* albedoPerLight)
{
    SceneLight(in, albedoPerLight);
    for (int i = 0; i < 3; ++i)
        albedoPerLight[i] = 1.0f / albedoPerLight[i];
}

float SelfLitShare(const FrameInputs& in)
{
    float ambient[3];
    float direct[3];
    UnpackColor(in.ambientColor, ambient);
    UnpackColor(in.directColor, direct);
    Encode(ambient, true);
    Encode(direct, true);
    float full[3];
    for (int i = 0; i < 3; ++i)
        full[i] = ambient[i] + direct[i];
    float light[3];
    SceneLight(in, light);
    const float fullLight = Luminance(full);
    return fullLight > light[0] ? light[0] / fullLight : 1.0f;
}

void WhiteBalanceGains(float temperature, float tint, float* gains)
{
    const float t = std::clamp(temperature, -2.0f * kMaxWhiteBalance, 2.0f * kMaxWhiteBalance);
    const float m = std::clamp(tint, -2.0f * kMaxWhiteBalance, 2.0f * kMaxWhiteBalance);
    if (t == 0.0f && m == 0.0f)
    {
        std::fill(gains, gains + 3, 1.0f);
        return;
    }
    const float linear[3] = {std::exp2(kWhiteBalanceStops * (t + 0.5f * m)), std::exp2(-kWhiteBalanceStops * m),
                             std::exp2(kWhiteBalanceStops * (0.5f * m - t))};
    const float luminance = Luminance(linear);
    for (int i = 0; i < 3; ++i)
        gains[i] = std::pow(linear[i] / luminance, 1.0f / kDisplayGamma);
}

float LampNearCameraShare(float cameraYards)
{
    return kNearLampHazeShare +
           (1.0f - kNearLampHazeShare) * SmoothStep(kNearLampFadeFrom, kNearLampFadeTo, cameraYards);
}

void ShadeAtStrength(const FrameInputs& in, float strength, float* shade)
{
    shade[0] = shade[1] = shade[2] = 1.0f;
    float ambient[3];
    float direct[3];
    UnpackColor(in.ambientColor, ambient);
    UnpackColor(in.directColor, direct);
    Encode(ambient, true);
    Encode(direct, true);
    const float ambientLuminance = Luminance(ambient);
    const float directLuminance = Luminance(direct);
    if (strength <= 0.0f || ambientLuminance + directLuminance < kMinLightLuminance)
        return;
    const float ambientShare = ambientLuminance / (ambientLuminance + directLuminance);
    float ambientPart[3];
    for (int i = 0; i < 3; ++i)
        ambientPart[i] = ambient[i] + direct[i] > kMinLightLuminance ? ambient[i] / (ambient[i] + direct[i]) : 1.0f;
    const float ambientPartLuminance = std::max(Luminance(ambientPart), kMinLightLuminance);
    for (int i = 0; i < 3; ++i)
    {
        const float hue = 1.0f + (ambientPart[i] / ambientPartLuminance - 1.0f) * kSurfaceShadowAmbientHue;
        shade[i] = std::clamp(1.0f + (ambientShare * hue - 1.0f) * strength, 0.0f, 1.0f);
    }
}

void SurfaceShadowShade(const FrameInputs& in, const Config& cfg, float* shade)
{
    ShadeAtStrength(in, cfg.surfaceShadows * SmoothStep(0.0f, kSurfaceShadowFullAtElevation, in.toLight[2]), shade);
}

void CloudShadowShade(const FrameInputs& in, float* shade)
{
    ShadeAtStrength(in, 1.0f, shade);
}

void Mul4x4(const float* a, const float* b, float* out)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] +
                           a[i * 4 + 3] * b[3 * 4 + j];
    std::memcpy(out, r, sizeof(r));
}

bool Invert4x4(const float* m, float* out)
{
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12)
        return false;
    for (int i = 0; i < 16; ++i)
        out[i] = static_cast<float>(inv[i] / det);
    return true;
}

void TransformDirection(const float* v, const float* m, float* out)
{
    float r[3];
    for (int j = 0; j < 3; ++j)
        r[j] = v[0] * m[0 * 4 + j] + v[1] * m[1 * 4 + j] + v[2] * m[2 * 4 + j];
    std::memcpy(out, r, sizeof(r));
}
