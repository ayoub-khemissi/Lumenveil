#pragma once

#include "config.h"
#include "engine.h"
#include "fog_data.h"

struct FogLayer
{
    float start;
    float density;
    float g;
    float isotropic;
    float emissive[3];
    float strength;
    float diffuse[3];
    float exponent;
    float upperHeight;
    float upperFalloff;
    float lowerHeight;
    float lowerFalloff;
    float shadowEmissive[3];
    float shadowDensity;
    float shadowed;
    float skyFalloff;
    float endDistance;
    float bankShare;
};

static_assert(sizeof(FogLayer) == 6 * sizeof(float[4]), "FogLayer uploads as a march layer's six float4 registers");

constexpr int kFogLayers = 4;
constexpr int kSceneLayers = 3;
constexpr int kDistanceFogLayer = kSceneLayers;
constexpr float kLateDrawYards = 25.0f;
constexpr float kLateDrawFullWeather = 0.1f;
static_assert(kDistanceFogLayer + 1 == kFogLayers, "the distance fog follows the scene layers");

enum BankGround
{
    kBankLand,
    kBankLake,
    kBankSea,
    kBankGrounds,
};

struct BankShape
{
    float threshold;
    float norm;
    float height;
    float density;
};

struct BankProfile
{
    float cover;
    float height;
    float density;
    float pool;
};

struct FogParams
{
    FogLayer layers[kFogLayers];
    BankShape banks[kBankGrounds];
    float bankStrength;
    float bankSharpness;
    float bankThresholdRise;
    float bankPoolGain;
    float bankLift;
    float bankShade;
    float bankShadeYards;
    float bankShadeNoiseScale;
    float bankValleysOnly;
    float bankDust;
    float lightColor[3];
    float rayColor[3];
    float airShaftScatter[3];
    float lightVisibility;
    float lightAboveHorizon;
    float shadowedLayerLightScale;
    float directLightMatch;
    float shaftStrength;
    float shaftFocus;
    float weather;
    float windSpeed;
    float drift;
    float mistBoost;
    float mistTop;
    float mistFalloff;
    float eveningHaze;
    float twilight;
    float lightGlow;
    float lampHaze;
    float lampLight;
    float lanternGlow;
    float nightDarkness;
    float windowLight;
    float lanternLight;
    float effectLight;
    float corpseLight;
    float farLightCore;
    float overcastSky[3];
    float overcastShare;
    float saturation;
    float skySaturation;
    float whiteBalance[3];
    float skyWhiteBalance[3];
    float lightBalance[3];
    float skyGlow[3];
    float skyHazeZenith;
    float skyHazeGlowGrade;
    float sunAndMoon;
    float hazeFromSky[3];
    float hazeFromSkyShare;
    float hazeSkyShown;
    float hazeSkyNightKept;
    float lateDrawsInFront;
    float lowSunLift[3];
    float cloudShadow;
    float cloudCover;
    float cloudTileYards;
    float cloudHeight;
    float cloudWind[2];
    float openLand;
    float maxDistance;
    float horizonStart;
    float farClip;
    float referenceZ;
    float farLimit;
    bool linear;
    bool authored;
};

struct WaterParams
{
    float reflect;
    float clarity;
    float waves;
    float windSpeed;
    float windDirection;
    float glitter;
    float glitterColor[3];
    float skyHorizon[3];
    float skyZenith[3];
};

struct ZoneClimateWeights
{
    float dry;
    float green;
};

constexpr double kZoneClimateFadeSeconds = 4.0;
constexpr float kZoneClimateSnap = 1.0e-4f;

ZoneClimateWeights ZoneClimateTarget(int zoneId, const Config& cfg);

class ZoneClimate
{
public:
    void Update(FrameInputs& in, const Config& cfg);
    void Reset() { m_seconds = -1.0; }
    int ZoneId() const { return m_zoneId; }
    ZoneClimateWeights Weights() const { return {m_dry, m_green}; }
    float DensityFactor() const { return m_density; }

private:
    int m_zoneId = -1;
    float m_dry = 0.0f;
    float m_green = 0.0f;
    float m_density = 1.0f;
    float m_densityFrom = 1.0f;
    float m_densityFade = 1.0f;
    double m_seconds = -1.0;
};

ZoneClimate& GlobalZoneClimate();
float ZoneDensityTarget(int zoneId, const Config& cfg);

constexpr float kFogCeilingFadeShare = 0.5f;
constexpr float kFogCeilingGateFrom = 1.5f;
constexpr float kFogCeilingGateFull = 3.0f;
constexpr double kFogCeilingGateSeconds = 0.5;

struct FogCeilingConstants
{
    float perYard;
    float strength;
    float groundAtCamera;
};

float FogCeilingGate(float ceiling, float cameraAboveGround);
float FogCeilingShare(float ceiling, float strength, float aboveGround);
float FogCeilingGateStep(float gate, float target, double seconds);
const Config& ZonedLook(const Config& look, float densityFactor, Config& storage);

void UnpackColor(uint32_t argb, float* rgb);
FogParams BuildFogParams(const FrameInputs& in, const Config& cfg, const AuthoredFog* authored);
float BankStrength(const Config& cfg, float dayFraction, float hourly);
BankProfile BankHourProfile(BankGround ground, float dayFraction, float dry);
constexpr float kBankPatchSpread = 0.27f;
constexpr float kBankWispSpread = 0.17f;
constexpr float kBankWispErosion = 0.35f;
float BankNoiseSpread();
float BankThreshold(float cover, float spread);
float BankNorm(float threshold, float spread, float sharpness);
bool WaterWanted(const Config& cfg);
WaterParams BuildWaterParams(const FrameInputs& in, const Config& cfg);
float WaterFresnel(float cosine);

bool MoonLightsTheFog(bool clientMoonHours, float sunElevation, float moonElevation);
float MorningMistWeight(float dayFraction);
float NearGroundFogDensity(const FogParams& p);
float MistGain(float nearGroundDensity, float referenceDensity);
float EveningHazeWeight(float dayFraction);
float TwilightWeight(float sunElevation, float moonElevation);
float LowSunWeight(float sunElevation);
float NightWeight(float sunElevation);
float LampNightWeight(float sunElevation);
float LampDayShare(const Config& cfg, float sunElevation);
float LampFogDensityGain(float nearGroundDensity);
float LampNearCameraShare(float cameraYards);
void SceneLight(const FrameInputs& in, float* light);
void AlbedoPerSceneLight(const FrameInputs& in, float* albedoPerLight);
float SelfLitShare(const FrameInputs& in);
void WhiteBalanceGains(float temperature, float tint, float* gains);

struct SunFacing
{
    float toLightView[3];
    float atZero;
    float perFacing;
};

SunFacing DaylightFacing(const FrameInputs& in);
void SurfaceShadowShade(const FrameInputs& in, const Config& cfg, float* shade);
void ShadeAtStrength(const FrameInputs& in, float strength, float* shade);
void CloudShadowShade(const FrameInputs& in, float* shade);

void Mul4x4(const float* a, const float* b, float* out);
bool Invert4x4(const float* m, float* out);
void TransformDirection(const float* v, const float* m, float* out);
