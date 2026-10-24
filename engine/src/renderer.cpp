#include "renderer.h"

#include "cloud_noise.h"
#include "drift_noise.h"
#include "fog_data.h"
#include "fog_model.h"
#include "lamp_catalog.h"
#include "log.h"
#include "terrain_material.h"
#include "terrain_shadow.h"
#include "water_lamps.h"
#include "water_rain.h"
#include "water_ripples.h"
#include "water_textures.h"

#include "ps_client_shadow.h"
#include "ps_composite.h"
#include "ps_edge_occluders.h"
#include "ps_effect_gather.h"
#include "ps_effect_reduce.h"
#include "ps_glow_blur.h"
#include "ps_glow_mask.h"
#include "ps_lamp_surfaces.h"
#include "ps_lamp_blur.h"
#include "ps_lamp_visibility.h"
#include "ps_lamps.h"
#include "ps_march_high.h"
#include "ps_march_low.h"
#include "ps_march_memory_high.h"
#include "ps_march_memory_low.h"
#include "ps_march_memory_mid.h"
#include "ps_march_mid.h"
#include "ps_memory_fill.h"
#include "ps_memory_keep.h"
#include "ps_memory_scatter.h"
#include "ps_probe.h"
#include "ps_ray_blur.h"
#include "ps_ray_mask.h"
#include "ps_sky_columns.h"
#include "ps_sky_gather.h"
#include "ps_sky_reference.h"
#include "ps_temporal.h"
#include "ps_water.h"
#include "ps_water_depth.h"
#include "ps_water_lamps.h"
#include "ps_water_memory.h"
#include "ps_water_reflect.h"
#include "ps_water_slope.h"
#include "ps_water_waves.h"
#include "ps_wet_surfaces.h"
#include "vs_fullscreen.h"
#include "vs_memory_realign.h"
#include "vs_memory_scatter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
constexpr UINT kPixelConstants = 67;
constexpr UINT kShaftConstantsRegister = 36;
constexpr UINT kDriftConstantsRegister = 38;
constexpr UINT kDriftConstantCount = 5;
constexpr UINT kTerrainShadowRegister = 43;
constexpr UINT kShaftFadeRegister = 44;
constexpr UINT kShaftMemoryRegister = 45;
constexpr UINT kShaftMemoryConstants = 4;
constexpr UINT kShaftMemoryBeforeRegister = 49;
constexpr UINT kCloudRegister = 53;
constexpr UINT kCloudConstants = 2;
constexpr UINT kShaftBodyRegister = 55;
constexpr UINT kShaftBodyConstants = 2;
constexpr UINT kHazeFromSkyRegister = 57;
constexpr UINT kSkyReferenceRegister = 58;
constexpr DWORD kSkyReferenceStage = 6;
constexpr UINT kBankRegister = 59;
constexpr UINT kBankConstants = 8;
constexpr DWORD kBankFloorStage = 7;
constexpr float kBankFlattening = 1.3f;
constexpr UINT kSkyColumns = 64;
constexpr UINT kSkyRows = 32;
constexpr UINT kSkyPassRegister = 9;
constexpr UINT kSkyPassConstants = 4;
constexpr DWORD kSkyFirstStage = 0;
constexpr DWORD kSkySecondStage = 1;
constexpr float kSkyElevationSpread = 0.08f;
constexpr UINT kSkyLightRegister = 13;
constexpr float kSkyAwayFromLightFullCos = 0.7071068f;
constexpr float kSkyAwayFromLightNoneCos = 0.9396926f;
constexpr float kSkyColumnSpread = 3.0f;
constexpr float kSkyMinimumWeight = 1e-3f;
constexpr float kSkyFullWeight = 1.0f;
constexpr double kSkySmoothingSeconds = 0.15;
constexpr double kSkyMaxStepSeconds = 0.5;
constexpr float kSkyRatioFloor = 1e-3f;
constexpr float kSkyRatioFloorShare = 0.05f;
constexpr float kSkyMinExposure = 1e-3f;
constexpr UINT kCompositeGlowRegister = 12;
constexpr DWORD kStages = 13;
constexpr DWORD kDriftNoiseStage = 1;
constexpr DWORD kTerrainShadowStage = 2;
constexpr DWORD kEdgeOccluderStage = 3;
constexpr DWORD kShaftMemoryBeforeStage = 4;
constexpr DWORD kCloudStage = 5;
constexpr UINT kEdgeBlockWidth = 8;
constexpr UINT kEdgeBlockHeight = 32;
constexpr DWORD kCompositeTerrainShadowStage = 5;
constexpr DWORD kCompositeLampHaloStage = 6;
constexpr DWORD kCompositeLampSurfaceStage = 7;
constexpr DWORD kCompositeCloudStage = 8;
constexpr DWORD kCompositeSurfaceBeforeStage = 9;
constexpr DWORD kCompositeDepthBeforeStage = 10;
constexpr DWORD kCompositeSunAndMoonStage = 11;
constexpr DWORD kCompositeWetStage = 12;
constexpr UINT kCompositeSurfaceShadowRegister = 13;
constexpr UINT kCompositeLampRegister = 17;
constexpr UINT kCompositeLampSurfaceRegister = 18;
constexpr UINT kCompositeOvercastRegister = 21;
constexpr UINT kCompositeLowSunRegister = 22;
constexpr UINT kCompositeCloudRegister = 24;
constexpr UINT kCompositeCloudConstants = 3;
constexpr UINT kCompositeGradeRegister = 27;
constexpr UINT kCompositeBalanceRegister = 28;
constexpr UINT kCompositeSkyGlowRegister = 29;
constexpr UINT kCompositeWhiteBalanceRegister = 30;
constexpr UINT kCompositeWhiteBalanceConstants = 2;
constexpr UINT kCompositeSunAndMoonRegister = 32;
constexpr DWORD kLampSurfaceDepthStage = 0;
constexpr DWORD kLampSurfaceTableStage = 1;
constexpr DWORD kLampSurfaceOccluderStage = 2;
constexpr DWORD kLampSurfaceTileStage = 3;
constexpr DWORD kLampSurfaceTerrainStage = 4;
constexpr UINT kLampSurfaceConstants = 7;
constexpr UINT kWetRegister = 9;
constexpr UINT kWetConstants = 20;
constexpr DWORD kWetDepthStage = 0;
constexpr DWORD kWetLampTableStage = 1;
constexpr DWORD kWetLampTileStage = 2;
constexpr DWORD kWetSurfaceStage = 3;
constexpr DWORD kWetLampLightStage = 4;
constexpr DWORD kWetTerrainStage = 5;
constexpr DWORD kWetNoiseStage = 6;
constexpr DWORD kWetWaterDepthStage = 7;
constexpr DWORD kWetFogStage = 8;
constexpr DWORD kWetMaterialStage = 9;
constexpr DWORD kWetMaterialTagStage = 10;
constexpr DWORD kLampBlurDepthStage = 0;
constexpr DWORD kLampBlurSourceStage = 1;
constexpr float kFixedFunctionAlbedo = 0.3f;
constexpr float kAlbedoCap = 0.9f;
constexpr UINT kLampPassRegister = 9;
constexpr DWORD kLampVisibilityDepthStage = 0;
constexpr DWORD kLampPreviousVisibilityStage = 1;
constexpr DWORD kLampVisibilityTableStage = 2;
constexpr DWORD kLampDepthStage = 0;
constexpr DWORD kLampFogStage = 1;
constexpr DWORD kLampVisibilityStage = 2;
constexpr DWORD kLampTableStage = 3;
constexpr DWORD kLampTileStage = 4;
constexpr DWORD kEffectTargets = 2;
constexpr UINT kEffectPassRegister = 9;
constexpr DWORD kEffectSceneNowStage = 0;
constexpr DWORD kEffectSceneBeforeStage = 1;
constexpr DWORD kEffectDepthBeforeStage = 2;
constexpr DWORD kEffectDepthNowStage = 3;
constexpr DWORD kEffectLightStage = 0;
constexpr DWORD kEffectPlaceStage = 1;
constexpr float kSurfaceShadowMapMarginYards = 16.0f;
constexpr float kSurfaceShadowMapFadeYards = 128.0f;
constexpr float kShaftFadeStart = 0.7f;
constexpr float kEdgeOccluderViewZRatio = 2.5f;
constexpr UINT kMemoryScatterStride = 4;
constexpr int kMemoryScatterOrder[kMemoryScatterStride * kMemoryScatterStride][2] = {
    {0, 0}, {2, 2}, {2, 0}, {0, 2}, {1, 1}, {3, 3}, {3, 1}, {1, 3}, {1, 0}, {3, 2}, {3, 0}, {1, 2}, {0, 1}, {2, 3}, {2, 1}, {0, 3}};
constexpr UINT kMemoryPassRegister = 9;
constexpr UINT kMemoryVertexConstants = 18;
constexpr DWORD kMemoryPreviousStage = 0;
constexpr DWORD kMemoryDepthStage = 1;
constexpr DWORD kMemoryDepthVertexSampler = D3DVERTEXTEXTURESAMPLER0;
constexpr double kMemoryBiasYards = 0.5;
constexpr double kMemoryRealignDegrees = 0.2;
constexpr double kMemoryReanchorYards = 2048.0;
constexpr unsigned kMemoryRealignSeeds = 4096;
constexpr double kMemoryFadeShareOfInterval = 0.9;
constexpr double kMemoryMinFadeSeconds = 0.25;
constexpr double kMemoryMaxFadeSeconds = 3.0;
constexpr double kMemoryFirstFadeSeconds = 1.0;
constexpr DWORD kLitTerrainTexel = 0xFFFFFFFF;
constexpr DWORD kLightGlowStage = 4;
constexpr double kPi = 3.14159265358979;
constexpr double kMaxDriftStepSeconds = 0.25;
constexpr float kDriftFlattening = 2.5f;
constexpr float kFineDriftScale = 5.0f;
constexpr float kDriftFootprintPerStep = 0.25f;
constexpr float kSwirlYardsPerPatch = 0.8f;
constexpr double kSwirlCentre = 0.5;
constexpr double kFineDriftHeadingOffset = 0.7;
constexpr double kFineDriftSpeedScale = 1.6;
constexpr double kCoarseDriftRisePerWind = 0.05;
constexpr double kFineDriftRisePerWind = 0.15;
constexpr float kMistRange = 500.0f;
constexpr UINT kGlowSourceScale = 2;
constexpr UINT kGlowScale = 8;
constexpr float kGlowBlurSpacings[] = {1.0f, 2.5f};
constexpr float kMinLightGlow = 0.005f;
constexpr float kLightGlowGain = 200.0f;
constexpr float kLightGlowCeiling = 0.4f;
constexpr float kLoggedWeatherSteps = 10.0f;
constexpr UINT kRayScale = 4;
constexpr float kMinViewportDepthExtent = 0.01f;
constexpr float kDeepestWorldDepthInFullRangeViewport = 0.9999995f;
constexpr float kWorldDepthMargin = 2.0e-6f;
constexpr float kSummarySeconds = 60.0f;
constexpr float kProbeSeconds = 60.0f;
constexpr float kProbeDebugSeconds = 30.0f;
constexpr unsigned kProbeFirstFrame = 60;
constexpr unsigned kInfoLevelGpuStallingProbeLimit = 5;
constexpr int kProbeGridSide = 5;
constexpr UINT kProbePoints = kProbeGridSide * kProbeGridSide;
constexpr UINT kWaterRegister = 9;
constexpr UINT kWaterLookupRegister = 16;
constexpr UINT kWaterTurnRegister = kWaterLookupRegister + kWaterWaveLookups;
constexpr UINT kWaterMemoryRegister = kWaterTurnRegister + 1;
constexpr UINT kWaterMemoryConstants = 2;
constexpr UINT kWaterRainRegister = kWaterMemoryRegister + kWaterMemoryConstants;
constexpr UINT kWaterRainConstants = 2;
constexpr UINT kWaterRippleRegister = kWaterRainRegister + kWaterRainConstants;
constexpr UINT kWaterRippleConstants = 2;
constexpr UINT kWaterConstants = kWaterRippleRegister + kWaterRippleConstants;
constexpr UINT kWaterLampRegister = kWaterConstants;
constexpr int kWaterFirstTurnedLookup = 2;
constexpr int kWaterSecondTurnedLookup = 3;
static_assert(kWaterWaveLookups == 7 && kWaterWaveLookupSpecs[0].layer == 0 && kWaterWaveLookupSpecs[1].layer == 1 &&
                  kWaterWaveLookupSpecs[2].layer == 1 && kWaterWaveLookupSpecs[3].layer == 1 &&
                  kWaterWaveLookupSpecs[4].layer == 2 && kWaterWaveLookupSpecs[5].layer == 2 &&
                  kWaterWaveLookupSpecs[6].layer == 2,
              "vf_water_slope.hlsl reads the long waves once and the other two layers three times each");
static_assert(kWaterWaveLookupSpecs[0].turnDegrees == 0.0 && kWaterWaveLookupSpecs[1].turnDegrees == 0.0 &&
                  kWaterWaveLookupSpecs[4].turnDegrees == 0.0 &&
                  kWaterWaveLookupSpecs[5].turnDegrees == kWaterWaveLookupSpecs[kWaterFirstTurnedLookup].turnDegrees &&
                  kWaterWaveLookupSpecs[6].turnDegrees == kWaterWaveLookupSpecs[kWaterSecondTurnedLookup].turnDegrees,
              "vf_water_slope.hlsl turns lookups 2 and 5 by the first turn and 3 and 6 by the second");
constexpr DWORD kWaterDepthBeforeStage = 0;
constexpr DWORD kWaterDepthAfterStage = 1;
constexpr DWORD kWaterSceneBeforeStage = 2;
constexpr DWORD kWaterSceneAfterStage = 3;
constexpr DWORD kWaterTerrainStage = 4;
constexpr DWORD kWaterWaveStage = 5;
constexpr DWORD kWaterSlopeStage = 5;
constexpr DWORD kWaterWaveTableStage = 0;
constexpr DWORD kWaterMemoryStage = 6;
constexpr DWORD kWaterRainStage = 4;
constexpr DWORD kWaterRippleStage = 3;
constexpr DWORD kWaterLampStage = 7;
constexpr DWORD kWaterBumpStage = 8;
constexpr DWORD kWaterFoamStage = 9;
constexpr DWORD kWaterFloorStage = 10;
constexpr UINT kWaterExtraRegister = 31;
constexpr UINT kWaterExtraConstants = 9;
constexpr int kWaterBandExtra = 4;
constexpr int kWaterBandSwitchExtra = 8;
constexpr float kWaterSeaProbeYards[] = {0.0f, 20.0f, 60.0f, 150.0f};
constexpr float kWaterMinWetShare = 0.01f;
constexpr double kWaterSeaShareSeconds = 1.0;
constexpr float kWaterBumpGain = 0.25f;
constexpr double kWaterBumpTiles[2] = {24.0, 41.0};
constexpr double kWaterBumpSpeeds[2] = {0.15, 0.1};
constexpr double kWaterBumpTurns[2] = {0.0, 0.7};
constexpr double kWaterFoamTile = 6.0;
constexpr double kWaterFoamSpeed = 0.1;
constexpr UINT kWaterMemorySize = 1024;
constexpr float kWaterMemoryForgetYards = 200.0f;
constexpr double kWaterMemoryForgetSeconds = 30.0;
constexpr UINT kWaterWaveLevelRegister = 9;
constexpr DWORD kWaterColourChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
constexpr float kWaterReflectionYards = 400.0f;
constexpr DWORD kWaterMaxAnisotropy = 16;
constexpr unsigned kMaxWaterSkipsLogged = 20;
static_assert(kWaterWaveStage + kWaterWaveLayers <= kStages, "the state block covers the wave textures' stages");
static_assert(kWaterConstants <= kPixelConstants, "the state block covers the water constants");
static_assert(kWaterLampRegister == 30 && kWaterLampRegister < kPixelConstants,
              "vf_water_lamps.hlsl reads its constants from c30, which the state block covers");
static_assert(kWaterLampStage < kStages && kWaterLampStage != kWaterDepthBeforeStage &&
                  kWaterLampStage != kWaterDepthAfterStage && kWaterLampStage != kWaterSlopeStage,
              "the lamp glints read their table on a stage of their own that the state block covers");
static_assert(kWaterLampTableRows == 3, "vf_water_lamps.hlsl reads three rows per lamp");
static_assert(kWaterMemoryStage > kWaterSlopeStage && kWaterMemoryStage < kStages,
              "the memory is bound after the slopes, on a stage the state block covers");
static_assert(kWaterBumpStage >= kWaterWaveStage + kWaterWaveLayers && kWaterBumpStage != kWaterLampStage &&
                  kWaterFoamStage != kWaterLampStage && kWaterBumpStage < kStages && kWaterFoamStage < kStages,
              "the water's bump and foam textures sit on stages of their own that the state block covers");
static_assert(kWaterExtraRegister > kWaterLampRegister &&
                  kWaterExtraRegister + kWaterExtraConstants <= kPixelConstants,
              "vf_water_slope.hlsl and vf_water.hlsl read the bump and foam constants from c31-c34 and vf_water.hlsl "
              "the water colours from c35-c39, covered by the state block");
static_assert(kWaterExtraRegister + kWaterBandExtra == 35 && kWaterBandSwitchExtra == kWaterExtraConstants - 1,
              "vf_water.hlsl reads the river and ocean colours from c35-c38 and their switches from c39");
static_assert(kWaterFloorStage < kStages && kWaterFloorStage != kWaterFoamStage && kWaterFloorStage > kWaterBumpStage,
              "the fog banks' floor map sits on a stage of its own in the water's body pass");
static_assert(kWaterMemoryRegister == 24, "vf_water_memory.hlsli reads the memory constants from c24 and c25");
static_assert(kWaterRainRegister == 26, "vf_water_rings.hlsli reads the rain constants from c26 and c27");
static_assert(kWaterRippleRegister == 28, "vf_water_rings.hlsli reads the ripple constants from c28 and c29");
static_assert(kWaterRippleStage == kWaterSceneAfterStage && kWaterRippleStage != kWaterRainStage &&
                  kWaterRippleStage != kWaterDepthAfterStage && kWaterRippleStage < kWaterWaveStage,
              "the slope pass reads the ripple table on the stage where the body pass then binds the scene after "
              "the water");
static_assert(kWaterRainStage != kWaterDepthAfterStage && kWaterRainStage < kWaterWaveStage && kWaterRainStage < kStages,
              "the slope pass reads the rain table on a stage of its own that the state block covers");
constexpr float kRayFalloff = 8.0f;
constexpr float kRayThreshold = 0.12f;
constexpr float kRayStep = 0.075f;
constexpr float kRayDecay = 0.9f;
constexpr int kRayTaps = 8;
constexpr float kHistoryMaxSeconds = 0.25f;
constexpr float kHistoryMaxMove = 30.0f;
constexpr float kShadowMinStep = 1.5f;
constexpr float kShadowStepPerYard = 0.035f;
constexpr float kShadowThicknessSteps = 4.0f;
constexpr float kLoggedStormBlendSteps = 10.0f;
constexpr UINT kClientShadowPairRegister = 9;
constexpr UINT kClientShadowViewRegister = kClientShadowPairRegister + 4 * kClientShadowPairs;
constexpr DWORD kClientShadowFirstMapStage = 1;
static_assert(kClientShadowViewRegister < kPixelConstants, "the state block covers the client shadow view");
static_assert(kClientShadowFirstMapStage + kClientShadowPairs <= kStages, "the state block covers its maps");

const D3DRENDERSTATETYPE kRenderStates[] = {
    D3DRS_ZENABLE,          D3DRS_ZWRITEENABLE,  D3DRS_ALPHATESTENABLE,   D3DRS_ALPHABLENDENABLE,
    D3DRS_SRCBLEND,         D3DRS_DESTBLEND,     D3DRS_BLENDOP,           D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_CULLMODE,         D3DRS_STENCILENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_SCISSORTESTENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_FOGENABLE,       D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE,         D3DRS_STENCILFUNC,   D3DRS_STENCILREF,        D3DRS_STENCILMASK,
    D3DRS_STENCILWRITEMASK, D3DRS_STENCILFAIL,   D3DRS_STENCILZFAIL,      D3DRS_STENCILPASS,
    D3DRS_POINTSIZE,        D3DRS_POINTSPRITEENABLE, D3DRS_POINTSCALEENABLE,
    D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2,
};

const D3DSAMPLERSTATETYPE kSamplerStates[] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV,  D3DSAMP_ADDRESSW,    D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL,
    D3DSAMP_MAXANISOTROPY,
};

struct Float4
{
    float x, y, z, w;
};

struct BodyConstants
{
    Float4 base;
    Float4 up;
};

BodyConstants BodyConstantsOf(const CarrierBody& body)
{
    return {{body.base[0], body.base[1], body.base[2], body.radius},
            {body.up[0], body.up[1], body.up[2], body.height}};
}

bool g_lampLightBlur = true;
float g_wetGlintShare = kGlintCellShare;
float g_wetPuddleThreshold = -1.0f;
constexpr int kLampTilesOff = 0;
constexpr int kLampTilesEmptied = 2;
int g_lampTiles = 1;

bool g_shaftMemoryFade = true;
bool g_bankWisps = true;

template <typename T>
void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

bool SameSize(IDirect3DTexture9* a, IDirect3DTexture9* b)
{
    D3DSURFACE_DESC da = {};
    D3DSURFACE_DESC db = {};
    return a && b && SUCCEEDED(a->GetLevelDesc(0, &da)) && SUCCEEDED(b->GetLevelDesc(0, &db)) && da.Width == db.Width &&
           da.Height == db.Height;
}

long long Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double TickSeconds(long long ticks)
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(ticks) / static_cast<double>(freq.QuadPart);
}

bool CreateTarget(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt, IDirect3DTexture9** out)
{
    return SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, out, nullptr));
}

void SetTarget(IDirect3DDevice9* dev, IDirect3DTexture9* tex)
{
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(tex->GetSurfaceLevel(0, &surface)))
    {
        dev->SetRenderTarget(0, surface);
        surface->Release();
    }
}

bool TextureFilterable(IDirect3DDevice9* dev, D3DFORMAT format)
{
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS cp;
    D3DDISPLAYMODE mode;
    bool filterable = false;
    if (SUCCEEDED(dev->GetDirect3D(&d3d)) && SUCCEEDED(dev->GetCreationParameters(&cp)) &&
        SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode)))
        filterable = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                                      D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, format));
    SafeRelease(d3d);
    return filterable;
}

bool TerrainMapWanted(const Config& cfg)
{
    return cfg.terrainShadows || cfg.surfaceShadows > 0.0f || cfg.lowSunLight > 0.0f || cfg.cloudShadows > 0.0f ||
           cfg.banks != kBanksOff || cfg.fogCeiling > 0.0f || WetSurfacesWanted(cfg);
}

bool FloorMapWanted(const Config& cfg)
{
    return cfg.banks != kBanksOff || cfg.fogCeiling > 0.0f;
}

bool SeaShareNear(const FrameInputs& in, float& share)
{
    const LiquidSource liquids = CurrentLiquidSource();
    if (!liquids.probe)
        return false;
    float ahead[2] = {in.camTarget[0] - in.camPos[0], in.camTarget[1] - in.camPos[1]};
    const float length = std::sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1]);
    for (float& a : ahead)
        a = length > 0.0f ? a / length : 0.0f;
    float lake = 0.0f;
    float sea = 0.0f;
    for (float yards : kWaterSeaProbeYards)
    {
        LiquidAt liquid = {};
        if (liquids.probe(liquids.context, in.mapId, in.camPos[0] + ahead[0] * yards, in.camPos[1] + ahead[1] * yards,
                          liquid))
        {
            lake += liquid.lake;
            sea += liquid.sea;
        }
    }
    if (!(lake + sea > kWaterMinWetShare))
        return false;
    share = sea / (lake + sea);
    return true;
}

struct BankConstants
{
    Float4 floor;
    Float4 shape;
    Float4 grounds[kBankGrounds];
    Float4 light;
    Float4 misc;
    Float4 noise;
};
static_assert(sizeof(BankConstants) == kBankConstants * sizeof(Float4), "the bank constants are c59-c66");

BankConstants BuildBankConstants(const FrameInputs& in, const FogParams& fog, const TerrainShadowConstants& floors,
                                 bool floorsKnown)
{
    BankConstants c = {};
    const bool known = floorsKnown && floors.uvPerYard > 0.0f;
    c.floor = {floors.uvAtCamera[0], floors.uvAtCamera[1], known ? floors.uvPerYard : 0.0f, known ? 1.0f : 0.0f};
    c.shape = {fog.bankSharpness, fog.bankThresholdRise, fog.bankPoolGain, fog.bankLift};
    for (int g = 0; g < kBankGrounds; ++g)
        c.grounds[g] = {fog.banks[g].threshold, fog.banks[g].norm, std::max(fog.banks[g].height, 1.0f),
                        fog.banks[g].density};
    c.light = {in.toLight[0] * fog.bankShadeYards, in.toLight[1] * fog.bankShadeYards,
               in.toLight[2] * fog.bankShadeYards, fog.bankShade};
    c.misc = {fog.referenceZ, fog.bankStrength, fog.bankValleysOnly, fog.bankShadeNoiseScale};
    c.noise = {g_bankWisps ? kBankWispErosion : 0.0f, 0.0f, 0.0f, 0.0f};
    return c;
}

struct CloudLookup
{
    Float4 map;
    Float4 shape;
};

constexpr float kCloudMinLightRise = 0.02f;

CloudLookup CloudConstants(const FrameInputs& in, const FogParams& fog, const double* offset, bool noise)
{
    if (!noise || !(fog.cloudShadow > 0.0f) || !(fog.cloudTileYards > 0.0f))
        return {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    const double tile = fog.cloudTileYards;
    const double u = (static_cast<double>(in.camPos[0]) - offset[0]) / tile;
    const double v = (static_cast<double>(in.camPos[1]) - offset[1]) / tile;
    const float rise = std::max(in.toLight[2], kCloudMinLightRise);
    return {{static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)),
             static_cast<float>(1.0 / tile), fog.cloudHeight},
            {in.toLight[0] / rise, in.toLight[1] / rise, 1.0f - fog.cloudCover, fog.cloudShadow}};
}

Float4 ShaftFadeConstants(float shaftRange, float shaftDepth, float shaftGlare)
{
    const float edgeThicknessLevels = std::log(1.0f + shaftDepth) / std::log(kEdgeOccluderViewZRatio);
    const float glarePerBrightness = 1.0f / std::max(shaftGlare, kMinShaftGlare);
    if (shaftRange <= 0.0f)
        return {0.0f, 1.0f, edgeThicknessLevels, glarePerBrightness};
    const float perYard = 1.0f / ((1.0f - kShaftFadeStart) * shaftRange);
    return {perYard, -kShaftFadeStart * shaftRange * perYard, edgeThicknessLevels, glarePerBrightness};
}

void Normalize3(float* v)
{
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

RECT ViewportRect(const D3DVIEWPORT9& vp)
{
    return {static_cast<LONG>(vp.X), static_cast<LONG>(vp.Y), static_cast<LONG>(vp.X + vp.Width),
            static_cast<LONG>(vp.Y + vp.Height)};
}

bool WorldViewFromCameraRelative(const FrameInputs& in, float* viewToWorld, float* worldToView)
{
    if (!Invert4x4(in.cameraRelativeView, viewToWorld))
        return false;
    viewToWorld[12] = in.camPos[0];
    viewToWorld[13] = in.camPos[1];
    viewToWorld[14] = in.camPos[2];
    return Invert4x4(viewToWorld, worldToView);
}

struct WorldDepthMapping
{
    float atInfinity;
    float perInverseViewDepth;
    float deepest;
};

WorldDepthMapping MapWorldDepth(const float* proj, const D3DVIEWPORT9& vp)
{
    const bool usableRange = vp.MaxZ - vp.MinZ > kMinViewportDepthExtent && vp.MinZ >= 0.0f && vp.MaxZ <= 1.0f;
    const float worldMinZ = usableRange ? vp.MinZ : 0.0f;
    const float worldExtent = usableRange ? vp.MaxZ - vp.MinZ : 1.0f;
    const float worldMaxZ = worldMinZ + worldExtent;
    return {worldMinZ + worldExtent * (1.0f + proj[10]) * 0.5f, worldExtent * proj[14] * 0.5f,
            worldMaxZ >= kDeepestWorldDepthInFullRangeViewport ? kDeepestWorldDepthInFullRangeViewport
                                                               : worldMaxZ + kWorldDepthMargin};
}

enum class FogBlend
{
    GammaFixedFunction,
    LinearOverSceneCopy,
    LinearFixedFunction,
};

const char* FogBlendName(FogBlend blend)
{
    static const char* const kNames[] = {"gamma, fixed function", "linear over a scene copy",
                                         "linear, fixed function (no scene copy)"};
    return kNames[static_cast<int>(blend)];
}

struct ProbeSample
{
    float rawDepth;
    float viewDepthYards;
    float fogOpacity;
    float depthClass;
};
static_assert(sizeof(ProbeSample) == sizeof(Float4), "ProbeSample is one A32B32G32R32F probe texel");

char DepthClassLetter(float depthClass)
{
    return depthClass > 1.5f ? 's' : (depthClass > 0.5f ? 'f' : 'w');
}

double Wrapped(double textureCoordinate)
{
    return textureCoordinate - std::floor(textureCoordinate);
}

IDirect3DTexture9* TextureFromBlp(IDirect3DDevice9* dev, const BlpImage& image)
{
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(dev->CreateTexture(image.width, image.height, static_cast<UINT>(image.levels.size()), 0, image.format,
                                  D3DPOOL_MANAGED, &texture, nullptr)))
        return nullptr;
    UINT height = image.height;
    for (UINT level = 0; level < image.levels.size(); ++level)
    {
        D3DLOCKED_RECT locked = {};
        if (FAILED(texture->LockRect(level, &locked, nullptr, 0)))
        {
            SafeRelease(texture);
            return nullptr;
        }
        const UINT rows = image.format == D3DFMT_A8R8G8B8 ? height : std::max(1u, (height + 3) / 4);
        const std::vector<uint8_t>& texels = image.levels[level];
        const size_t rowBytes = texels.size() / rows;
        for (UINT row = 0; row < rows; ++row)
            std::memcpy(static_cast<unsigned char*>(locked.pBits) + static_cast<size_t>(row) * locked.Pitch,
                        texels.data() + static_cast<size_t>(row) * rowBytes, rowBytes);
        texture->UnlockRect(level);
        height = std::max(1u, height / 2);
    }
    return texture;
}

void BindWrapped(IDirect3DDevice9* dev, DWORD stage, IDirect3DTexture9* texture, DWORD anisotropy)
{
    dev->SetTexture(stage, texture);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, anisotropy > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, anisotropy);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

float HalfToFloat(uint16_t half)
{
    const int exponent = (half >> 10) & 0x1F;
    const float mantissa = static_cast<float>(half & 0x3FFu);
    float magnitude = exponent == 0 ? std::ldexp(mantissa, -24) : std::ldexp(mantissa + 1024.0f, exponent - 25);
    if (exponent == 0x1F)
        magnitude = mantissa > 0.0f ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    return (half & 0x8000u) ? -magnitude : magnitude;
}

float CoarseDriftPerYard(const Config& cfg)
{
    return 1.0f / (kDriftCellsPerTile * cfg.driftSize);
}

const char* WeatherName(WeatherKind weather)
{
    static const char* const kNames[] = {"clear", "rain", "snow", "sand"};
    return kNames[static_cast<int>(weather)];
}

struct DriftConstants
{
    Float4 coarse;
    Float4 fine;
    Float4 scale;
    Float4 mist;
    Float4 detail;
};
static_assert(sizeof(DriftConstants) == kDriftConstantCount * sizeof(Float4), "drift constants are c38-c42");

DriftConstants BuildDriftConstants(const FrameInputs& in, const FogParams& fog, const Config& cfg,
                                   const double* coarseDrift, const double* fineDrift, bool noiseBound)
{
    const float coarseXY = CoarseDriftPerYard(cfg);
    const float flattening =
        kDriftFlattening + (kBankFlattening - kDriftFlattening) * std::clamp(fog.bankStrength, 0.0f, 1.0f);
    const float coarseZ = coarseXY * flattening;
    const float fineXY = coarseXY * kFineDriftScale;
    const float fineZ = fineXY * flattening;
    const double coarsePerYard[3] = {coarseXY, coarseXY, coarseZ};
    const double finePerYard[3] = {fineXY, fineXY, fineZ};
    const float swirl = cfg.driftSwirl * kSwirlYardsPerPatch * cfg.driftSize * fineXY;
    const double swirlCentring[3] = {-kSwirlCentre * swirl, -kSwirlCentre * swirl, 0.0};
    float coarseOrigin[3];
    float fineOrigin[3];
    for (int k = 0; k < 3; ++k)
    {
        coarseOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * coarsePerYard[k] + coarseDrift[k]));
        fineOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * finePerYard[k] + fineDrift[k] + swirlCentring[k]));
    }
    const float amplitude = noiseBound ? fog.drift : 0.0f;
    DriftConstants c;
    c.coarse = {coarseOrigin[0], coarseOrigin[1], coarseOrigin[2], 2.0f * amplitude};
    c.fine = {fineOrigin[0], fineOrigin[1], fineOrigin[2], std::log2(kFineDriftScale)};
    c.scale = {coarseXY, coarseZ, fineXY, fineZ};
    c.mist = {fog.mistTop, fog.mistFalloff, fog.mistBoost, 1.0f / kMistRange};
    c.detail = {kDriftNoiseSize * coarseXY * kDriftFootprintPerStep, swirl, cfg.driftDetail, 1.0f - amplitude};
    return c;
}
}

void SetLampLightBlur(bool enabled)
{
    g_lampLightBlur = enabled;
}

void SetWetGlintShare(float share)
{
    g_wetGlintShare = share;
}

void SetWetPuddleThreshold(float threshold)
{
    g_wetPuddleThreshold = threshold;
}

void SetLampTiles(int mode)
{
    g_lampTiles = mode;
}


void SetShaftMemoryFade(bool enabled)
{
    g_shaftMemoryFade = enabled;
}

void SetBankWisps(bool enabled)
{
    g_bankWisps = enabled;
}

Renderer::~Renderer()
{
    ReleaseAll();
}

void Renderer::ReleaseDefaultPool()
{
    m_profile.ReleaseQueries();
    SafeRelease(m_marchTarget);
    SafeRelease(m_history[0]);
    SafeRelease(m_history[1]);
    SafeRelease(m_rays[0]);
    SafeRelease(m_rays[1]);
    SafeRelease(m_sceneCopy);
    SafeRelease(m_skyGather);
    SafeRelease(m_skyColumns);
    SafeRelease(m_skyReference[0]);
    SafeRelease(m_skyReference[1]);
    m_skyReferenceValid = false;
    m_skyReferenceFailed = false;
    SafeRelease(m_probeTarget);
    SafeRelease(m_probeReadback);
    SafeRelease(m_glowSource);
    SafeRelease(m_glow[0]);
    SafeRelease(m_glow[1]);
    SafeRelease(m_lampTable);
    for (IDirect3DTexture9*& staging : m_lampTableStaging)
        SafeRelease(staging);
    SafeRelease(m_lampVisibility[0]);
    SafeRelease(m_lampVisibility[1]);
    SafeRelease(m_lampHalos);
    SafeRelease(m_lampSurfaces);
    SafeRelease(m_lampSurfacesBlur);
    SafeRelease(m_wetTarget);
    m_wetW = m_wetH = 0;
    m_wetFailed = false;
    m_wetDrawn = false;
    SafeRelease(m_lampTileTexture);
    for (IDirect3DTexture9*& staging : m_lampTileStaging)
        SafeRelease(staging);
    m_lampTileTextureW = m_lampTileTextureH = 0;
    SafeRelease(m_edgeOccluders);
    SafeRelease(m_occluderDepth);
    ReleaseEffectTargets();
    SafeRelease(m_memory[0]);
    SafeRelease(m_memory[1]);
    SafeRelease(m_memoryBefore);
    m_memoryFading = false;
    SafeRelease(m_memoryGrid);
    SafeRelease(m_memoryRealignGrid);
    SafeRelease(m_memoryFlush);
    m_memoryGridW = m_memoryGridH = 0;
    m_memoryValid = false;
    m_memoryFailed = false;
    m_occluderDepthW = m_occluderDepthH = 0;
    m_occluderDepthFresh = false;
    m_occluderDepthFailed = false;
    for (int i = 0; i < 2; ++i)
    {
        SafeRelease(m_waterDepth[i]);
        SafeRelease(m_waterScene[i]);
    }
    SafeRelease(m_waterSlopes);
    SafeRelease(m_waterQuery);
    SafeRelease(m_waterWaveTable);
    SafeRelease(m_waterRippleTable);
    m_waterRippleFailed = false;
    SafeRelease(m_waterLampTable);
    m_waterLampFailed = false;
    for (IDirect3DTexture9*& memory : m_waterMemory)
        SafeRelease(memory);
    SafeRelease(m_waterMemoryFlush);
    m_waterMemoryValid = false;
    m_waterMemoryFailed = false;
    m_waterMemoryOn = false;
    for (IDirect3DTexture9*& waves : m_waterWaves)
        SafeRelease(waves);
    m_waterQueryPending = false;
    m_waterQueryFailed = false;
    m_waterFailed = false;
    m_waterTarget = nullptr;
    m_waterDepthW = m_waterDepthH = m_waterSceneW = m_waterSceneH = 0;
    SafeRelease(m_state);
    m_glowSourceW = m_glowSourceH = m_glowW = m_glowH = 0;
    m_glowFailed = false;
    m_lampW = m_lampH = 0;
    m_lampsFailed = false;
    m_lampsDrawn = false;
    m_lampSurfaceW = m_lampSurfaceH = 0;
    m_lampSurfacesFailed = false;
    m_lampSurfacesDrawn = false;
    m_edgeW = m_edgeH = 0;
    m_edgeOccludersFailed = false;
    m_lowW = m_lowH = m_rayW = m_rayH = 0;
    m_sceneCopyW = m_sceneCopyH = 0;
    m_sceneCopyFailed = false;
    m_probeFailed = false;
    m_historyValid = false;
}

void Renderer::ReleaseAll()
{
    ReleaseDefaultPool();
    SafeRelease(m_vs);
    for (auto*& ps : m_march)
        SafeRelease(ps);
    SafeRelease(m_temporal);
    SafeRelease(m_composite);
    SafeRelease(m_rayMask);
    SafeRelease(m_rayBlur);
    SafeRelease(m_glowMask);
    SafeRelease(m_glowBlur);
    SafeRelease(m_lampVisibilityShader);
    SafeRelease(m_lampShader);
    SafeRelease(m_lampSurfaceShader);
    SafeRelease(m_lampBlurShader);
    SafeRelease(m_wetShader);
    m_wetChecked = false;
    SafeRelease(m_edgeOccluderShader);
    for (auto*& ps : m_marchMemory)
        SafeRelease(ps);
    SafeRelease(m_memoryScatterVs);
    SafeRelease(m_memoryScatterPs);
    SafeRelease(m_memoryKeepPs);
    SafeRelease(m_memoryRealignVs);
    SafeRelease(m_memoryFillPs);
    SafeRelease(m_memoryDecl);
    m_memoryChecked = false;
    m_memoryUnsupported = false;
    SafeRelease(m_waterDepthShader);
    SafeRelease(m_waterShader);
    SafeRelease(m_waterReflectShader);
    SafeRelease(m_waterWaveShader);
    SafeRelease(m_waterSlopeShader);
    SafeRelease(m_waterMemoryShader);
    SafeRelease(m_waterLampShader);
    SafeRelease(m_clientShadowShader);
    SafeRelease(m_effectGatherShader);
    SafeRelease(m_effectReduceShader);
    m_effectsChecked = false;
    SafeRelease(m_skyGatherShader);
    SafeRelease(m_skyColumnShader);
    SafeRelease(m_skyReferenceShader);
    SafeRelease(m_probe);
    SafeRelease(m_decl);
    SafeRelease(m_driftNoise);
    m_driftNoiseFailed = false;
    SafeRelease(m_cloudNoise);
    m_cloudNoiseFailed = false;
    SafeRelease(m_terrainTexture);
    m_terrainFallback = false;
    SafeRelease(m_bankFloorTexture);
    m_bankFloorFailed = false;
    SafeRelease(m_waterRainTable);
    m_waterRainFailed = false;
    SafeRelease(m_waterBump);
    SafeRelease(m_waterFoam);
    m_waterTexturesMade = false;
}

void Renderer::LogLightChange(const FrameInputs& in, const AuthoredFog& fog, bool authored)
{
    const LightParamsSelection& selection = in.lightParams;
    const auto loggedStormStep = static_cast<uint32_t>(std::lround(selection.stormBlend * kLoggedStormBlendSteps));
    uint32_t signature = authored ? 0x80000000u : 0u;
    for (int i = 0; i < fog.lightCount; ++i)
        signature = signature * 31u + fog.lightIds[i];
    signature = signature * 31u + loggedStormStep;
    signature = signature * 31u + static_cast<uint32_t>(selection.screenEffectSlot);
    signature ^= static_cast<uint32_t>(in.mapId) << 20;
    if (m_lightsLogged && signature == m_lightSignature)
        return;
    m_lightsLogged = true;
    m_lightSignature = signature;
    if (!authored)
    {
        VF_LOG_INFO("map %d at (%.0f %.0f %.0f): no Classic fog data, derived layers, screen effect slot %d",
                    in.mapId, in.camPos[0], in.camPos[1], in.camPos[2], selection.screenEffectSlot);
        return;
    }
    char lights[160] = {};
    int used = 0;
    for (int i = 0; i < fog.lightCount && used < static_cast<int>(sizeof(lights)) - 24; ++i)
        used += std::snprintf(lights + used, sizeof(lights) - used, "%s%u:%.2f", i ? " " : "", fog.lightIds[i],
                              fog.lightWeights[i]);
    VF_LOG_INFO("map %d at (%.0f %.0f %.0f): Classic lights %s, %d layers, storm %.1f, screen effect slot %d",
                in.mapId, in.camPos[0], in.camPos[1], in.camPos[2], lights, fog.layerCount,
                loggedStormStep / kLoggedStormBlendSteps, selection.screenEffectSlot);
}

bool Renderer::Skip(const char* reason)
{
    m_skip = reason;
    return false;
}

bool Renderer::EnsureShaders(IDirect3DDevice9* dev)
{
    if (m_vs)
        return true;
    auto ps = [dev](const BYTE* code, IDirect3DPixelShader9** out) {
        return SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code), out));
    };
    static const D3DVERTEXELEMENT9 kElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END(),
    };
    static const D3DVERTEXELEMENT9 kGridElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END(),
    };
    bool ok = SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_fullscreen), &m_vs)) &&
              ps(g_ps_march_low, &m_march[0]) && ps(g_ps_march_mid, &m_march[1]) &&
              ps(g_ps_march_high, &m_march[2]) && ps(g_ps_temporal, &m_temporal) &&
              ps(g_ps_composite, &m_composite) && ps(g_ps_ray_mask, &m_rayMask) && ps(g_ps_ray_blur, &m_rayBlur) &&
              ps(g_ps_glow_mask, &m_glowMask) && ps(g_ps_glow_blur, &m_glowBlur) &&
              ps(g_ps_lamp_visibility, &m_lampVisibilityShader) && ps(g_ps_lamps, &m_lampShader) &&
              ps(g_ps_lamp_surfaces, &m_lampSurfaceShader) && ps(g_ps_lamp_blur, &m_lampBlurShader) &&
              ps(g_ps_edge_occluders, &m_edgeOccluderShader) && ps(g_ps_march_memory_low, &m_marchMemory[0]) &&
              ps(g_ps_march_memory_mid, &m_marchMemory[1]) && ps(g_ps_march_memory_high, &m_marchMemory[2]) &&
              ps(g_ps_memory_scatter, &m_memoryScatterPs) && ps(g_ps_memory_keep, &m_memoryKeepPs) &&
              ps(g_ps_memory_fill, &m_memoryFillPs) && ps(g_ps_sky_gather, &m_skyGatherShader) &&
              ps(g_ps_sky_columns, &m_skyColumnShader) && ps(g_ps_sky_reference, &m_skyReferenceShader) &&
              SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_memory_realign), &m_memoryRealignVs)) &&
              SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_memory_scatter), &m_memoryScatterVs)) &&
              SUCCEEDED(dev->CreateVertexDeclaration(kElements, &m_decl)) &&
              SUCCEEDED(dev->CreateVertexDeclaration(kGridElements, &m_memoryDecl));
    if (!ok)
    {
        VF_LOG_ERROR("shader creation failed; fog disabled for this device");
        ReleaseAll();
        return Skip("shader creation failed");
    }
    return true;
}

bool Renderer::EnsureStateBlock(IDirect3DDevice9* dev)
{
    if (m_state)
        return true;
    if (FAILED(dev->BeginStateBlock()))
        return Skip("state block recording failed");
    for (D3DRENDERSTATETYPE rs : kRenderStates)
        dev->SetRenderState(rs, 0);
    for (DWORD stage = 0; stage < kStages; ++stage)
    {
        dev->SetTexture(stage, nullptr);
        for (D3DSAMPLERSTATETYPE ss : kSamplerStates)
            dev->SetSamplerState(stage, ss, 0);
    }
    dev->SetTexture(kMemoryDepthVertexSampler, nullptr);
    for (D3DSAMPLERSTATETYPE ss : kSamplerStates)
        dev->SetSamplerState(kMemoryDepthVertexSampler, ss, 0);
    float zeros[kPixelConstants * 4] = {};
    static_assert(kMemoryVertexConstants <= kPixelConstants, "the zeros cover the vertex constants");
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetPixelShaderConstantF(0, zeros, kPixelConstants);
    dev->SetVertexShaderConstantF(0, zeros, kMemoryVertexConstants);
    dev->SetVertexDeclaration(m_decl);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetStreamSourceFreq(0, 1);
    D3DVIEWPORT9 vp = {0, 0, 1, 1, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    RECT scissor = {0, 0, 1, 1};
    dev->SetScissorRect(&scissor);
    if (FAILED(dev->EndStateBlock(&m_state)) || !m_state)
    {
        m_state = nullptr;
        return Skip("state block recording failed");
    }
    return true;
}

bool Renderer::EnsureTargets(IDirect3DDevice9* dev, UINT lowW, UINT lowH, UINT rayW, UINT rayH)
{
    if (m_marchTarget && m_lowW == lowW && m_lowH == lowH && m_rayW == rayW && m_rayH == rayH)
        return true;
    SafeRelease(m_marchTarget);
    SafeRelease(m_history[0]);
    SafeRelease(m_history[1]);
    SafeRelease(m_rays[0]);
    SafeRelease(m_rays[1]);
    m_historyValid = false;

    D3DFORMAT fogFormat = D3DFMT_A16B16G16R16F;
    bool ok = CreateTarget(dev, lowW, lowH, fogFormat, &m_marchTarget);
    if (!ok)
    {
        fogFormat = D3DFMT_A8R8G8B8;
        ok = CreateTarget(dev, lowW, lowH, fogFormat, &m_marchTarget);
    }
    ok = ok && CreateTarget(dev, lowW, lowH, fogFormat, &m_history[0]) &&
         CreateTarget(dev, lowW, lowH, fogFormat, &m_history[1]) &&
         CreateTarget(dev, rayW, rayH, D3DFMT_A8R8G8B8, &m_rays[0]) &&
         CreateTarget(dev, rayW, rayH, D3DFMT_A8R8G8B8, &m_rays[1]);
    if (!ok)
    {
        VF_LOG_ERROR("render target creation failed (%ux%u)", lowW, lowH);
        SafeRelease(m_marchTarget);
        SafeRelease(m_history[0]);
        SafeRelease(m_history[1]);
        SafeRelease(m_rays[0]);
        SafeRelease(m_rays[1]);
        return Skip("render target creation failed");
    }

    m_fogFilterable = fogFormat == D3DFMT_A8R8G8B8;
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS cp;
    D3DDISPLAYMODE mode;
    if (!m_fogFilterable && SUCCEEDED(dev->GetDirect3D(&d3d)) && SUCCEEDED(dev->GetCreationParameters(&cp)) &&
        SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode)))
    {
        m_fogFilterable = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                                           D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER,
                                                           D3DRTYPE_TEXTURE, fogFormat));
    }
    SafeRelease(d3d);

    m_lowW = lowW;
    m_lowH = lowH;
    m_rayW = rayW;
    m_rayH = rayH;
    VF_LOG_INFO("targets: fog %ux%u (%s, filter %d), rays %ux%u", lowW, lowH,
                fogFormat == D3DFMT_A16B16G16R16F ? "fp16" : "rgba8", m_fogFilterable ? 1 : 0, rayW, rayH);
    return true;
}

bool Renderer::EnsureSceneCopy(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT w, UINT h)
{
    if (m_sceneCopy && m_sceneCopyW == w && m_sceneCopyH == h)
        return true;
    SafeRelease(m_sceneCopy);
    m_sceneCopyW = m_sceneCopyH = 0;
    if (m_sceneCopyFailed)
        return false;
    D3DSURFACE_DESC desc = {};
    target->GetDesc(&desc);
    if (!CreateTarget(dev, w, h, desc.Format, &m_sceneCopy) &&
        !CreateTarget(dev, w, h, D3DFMT_A8R8G8B8, &m_sceneCopy))
    {
        m_sceneCopyFailed = true;
        VF_LOG_ERROR("scene copy creation failed (%ux%u); fog blends in gamma space", w, h);
        return false;
    }
    m_sceneCopyW = w;
    m_sceneCopyH = h;
    return true;
}

bool Renderer::CopyWorldViewport(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DVIEWPORT9& vp)
{
    if (!EnsureSceneCopy(dev, target, vp.Width, vp.Height))
        return false;
    const RECT world = ViewportRect(vp);
    IDirect3DSurface9* sceneSurface = nullptr;
    const bool copied = SUCCEEDED(m_sceneCopy->GetSurfaceLevel(0, &sceneSurface)) &&
                        SUCCEEDED(dev->StretchRect(target, &world, sceneSurface, nullptr, D3DTEXF_POINT));
    SafeRelease(sceneSurface);
    return copied;
}

bool Renderer::EnsureSkyReference(IDirect3DDevice9* dev)
{
    if (m_skyGather)
        return true;
    if (m_skyReferenceFailed)
        return false;
    for (D3DFORMAT format : {D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F})
    {
        if (CreateTarget(dev, kSkyColumns, kSkyRows, format, &m_skyGather) &&
            CreateTarget(dev, kSkyColumns, 1, format, &m_skyColumns) &&
            CreateTarget(dev, kSkyColumns, 1, format, &m_skyReference[0]) &&
            CreateTarget(dev, kSkyColumns, 1, format, &m_skyReference[1]))
        {
            m_skyReferenceFilterable = TextureFilterable(dev, format);
            m_skyReferenceValid = false;
            VF_LOG_INFO("sky reference: the haze before the land takes the sky as drawn, in %u screen columns from "
                        "%ux%u blocks, leaving out the sky within %.0f deg of the sun or moon and counting it fully "
                        "from %.0f deg (%s, %s)",
                        kSkyColumns, kSkyColumns, kSkyRows, std::acos(kSkyAwayFromLightNoneCos) * 57.29578f,
                        std::acos(kSkyAwayFromLightFullCos) * 57.29578f,
                        format == D3DFMT_A16B16G16R16F ? "fp16" : "fp32",
                        m_skyReferenceFilterable ? "read with linear filtering" : "point sampled");
            return true;
        }
        SafeRelease(m_skyGather);
        SafeRelease(m_skyColumns);
        SafeRelease(m_skyReference[0]);
        SafeRelease(m_skyReference[1]);
    }
    m_skyReferenceFailed = true;
    VF_LOG_ERROR("sky reference unavailable: its targets could not be made; the haze takes the light data's horizon");
    return false;
}

bool Renderer::SkyReferenceFresh(const FrameInputs& in) const
{
    const double age = in.seconds - m_skyReferenceSeconds;
    return m_skyReferenceValid && age > 0.0 && age <= kSkyMaxStepSeconds;
}

void Renderer::MeasureSky(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                          const FrameInputs& in, const Config& cfg, const FogParams& fog)
{
    const double step = in.seconds - m_skyReferenceSeconds;
    const bool kept = SkyReferenceFresh(in);
    m_skyReferenceValid = false;
    m_skyReferenceSeconds = in.seconds;
    if (!EnsureSkyReference(dev) || !CopyWorldViewport(dev, target, in.viewport))
        return;
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

    SetTarget(dev, m_skyGather);
    dev->SetPixelShader(m_skyGatherShader);
    const Float4 gather = {kSkyElevationSpread, kSkyAwayFromLightFullCos, kSkyAwayFromLightNoneCos, 0.0f};
    dev->SetPixelShaderConstantF(kSkyPassRegister, &gather.x, 1);
    const Float4 light = {in.toLight[0], in.toLight[1], in.toLight[2], 0.0f};
    dev->SetPixelShaderConstantF(kSkyLightRegister, &light.x, 1);
    BindTexture(dev, kSkyFirstStage, depthTexture, false);
    BindTexture(dev, kSkySecondStage, m_sceneCopy, false);
    DrawFullscreen(dev);

    SetTarget(dev, m_skyColumns);
    dev->SetPixelShader(m_skyColumnShader);
    BindTexture(dev, kSkyFirstStage, m_skyGather, false);
    BindTexture(dev, kSkySecondStage, nullptr, false);
    DrawFullscreen(dev);

    const int write = m_skyReferenceIndex ^ 1;
    SetTarget(dev, m_skyReference[write]);
    dev->SetPixelShader(m_skyReferenceShader);
    const float exposure = std::max(fog.authored ? cfg.classicExposure : cfg.exposure, kSkyMinExposure);
    const float newShare = kept ? static_cast<float>(1.0 - std::exp(-step / kSkySmoothingSeconds)) : 1.0f;
    const Float4 reference[kSkyPassConstants] = {
        {kSkyColumnSpread, kSkyMinimumWeight, newShare, kept ? 1.0f : 0.0f},
        {std::clamp(cfg.hazeSkyLevel, kMinHazeSkyLevel, kMaxHazeSkyLevel), fog.saturation, exposure, kSkyFullWeight},
        {fog.whiteBalance[0], fog.whiteBalance[1], fog.whiteBalance[2], 0.0f},
        {fog.hazeFromSky[0], fog.hazeFromSky[1], fog.hazeFromSky[2], 0.0f},
    };
    dev->SetPixelShaderConstantF(kSkyPassRegister, &reference[0].x, kSkyPassConstants);
    BindTexture(dev, kSkyFirstStage, m_skyColumns, false);
    BindTexture(dev, kSkySecondStage, m_skyReference[m_skyReferenceIndex], false);
    DrawFullscreen(dev);
    m_skyReferenceIndex = write;
    m_skyReferenceValid = true;
    std::copy(fog.hazeFromSky, fog.hazeFromSky + 3, m_skyReferenceTarget);
}

bool Renderer::EnsureDriftNoise(IDirect3DDevice9* dev)
{
    if (m_driftNoise)
        return true;
    if (m_driftNoiseFailed)
        return false;
    const long long buildStart = Ticks();
    const std::vector<DriftNoiseLevel> levels = BuildDriftNoise();
    const double buildSeconds = TickSeconds(Ticks() - buildStart);
    bool ok = SUCCEEDED(dev->CreateVolumeTexture(kDriftNoiseSize, kDriftNoiseSize, kDriftNoiseSize,
                                                 static_cast<UINT>(levels.size()), 0, D3DFMT_A8R8G8B8,
                                                 D3DPOOL_MANAGED, &m_driftNoise, nullptr));
    for (UINT level = 0; ok && level < levels.size(); ++level)
    {
        const DriftNoiseLevel& source = levels[level];
        D3DLOCKED_BOX box = {};
        ok = SUCCEEDED(m_driftNoise->LockBox(level, &box, nullptr, 0));
        if (!ok)
            break;
        for (int z = 0; z < source.size; ++z)
            for (int y = 0; y < source.size; ++y)
                std::memcpy(static_cast<unsigned char*>(box.pBits) + z * box.SlicePitch + y * box.RowPitch,
                            &source.argb[(static_cast<size_t>(z) * source.size + y) * source.size],
                            source.size * sizeof(uint32_t));
        m_driftNoise->UnlockBox(level);
    }
    if (!ok)
    {
        SafeRelease(m_driftNoise);
        m_driftNoiseFailed = true;
        VF_LOG_ERROR("drift noise volume creation failed; the fog does not drift");
        return false;
    }
    VF_LOG_INFO("drift noise: %d^3 volume, %zu levels, %d channels, built in %.0f ms", kDriftNoiseSize,
                levels.size(), kDriftChannels, buildSeconds * 1000.0);
    return true;
}

bool Renderer::EnsureWaterRainTable(IDirect3DDevice9* dev)
{
    if (m_waterRainTable)
        return true;
    if (m_waterRainFailed)
        return false;
    std::vector<uint32_t> texels(static_cast<size_t>(kWaterRainTableTexels) * kWaterRainTableTexels);
    BuildWaterRainTable(texels.data());
    bool ok = SUCCEEDED(dev->CreateTexture(kWaterRainTableTexels, kWaterRainTableTexels, 1, 0, D3DFMT_A8R8G8B8,
                                           D3DPOOL_MANAGED, &m_waterRainTable, nullptr));
    D3DLOCKED_RECT locked = {};
    ok = ok && SUCCEEDED(m_waterRainTable->LockRect(0, &locked, nullptr, 0));
    if (ok)
    {
        for (int y = 0; y < kWaterRainTableTexels; ++y)
            std::memcpy(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch,
                        &texels[static_cast<size_t>(y) * kWaterRainTableTexels],
                        kWaterRainTableTexels * sizeof(uint32_t));
        m_waterRainTable->UnlockRect(0);
    }
    if (!ok)
    {
        SafeRelease(m_waterRainTable);
        m_waterRainFailed = true;
        VF_LOG_ERROR("water rain table creation failed; no rain rings on the water");
        return false;
    }
    VF_LOG_INFO("water rain: rings on the water from a %dx%d drop table, %.1f yd cells with %d drops each, rings "
                "%.2f yd wide after %.1f s",
                kWaterRainTableTexels, kWaterRainTableTexels, kWaterRainCellYards, kWaterRainSlots,
                2.0 * kWaterRainFrontYards, kWaterRainLifeSeconds);
    return true;
}

bool Renderer::FillWaterRippleTable(IDirect3DDevice9* dev, const WaterRippleConstants& ripples)
{
    if (!m_waterRippleTable)
    {
        if (m_waterRippleFailed)
            return false;
        if (FAILED(dev->CreateTexture(kWaterRippleSources, kWaterRippleTableRows, 1, D3DUSAGE_DYNAMIC,
                                      D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &m_waterRippleTable, nullptr)))
        {
            m_waterRippleTable = nullptr;
            m_waterRippleFailed = true;
            VF_LOG_ERROR("water ripple table creation failed; no ripples around the player");
            return false;
        }
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_waterRippleTable->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
        return false;
    for (int row = 0; row < kWaterRippleTableRows; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, ripples.table[row],
                    sizeof(ripples.table[row]));
    m_waterRippleTable->UnlockRect(0);
    return true;
}

bool Renderer::FillWaterLampTable(IDirect3DDevice9* dev, const WaterLampTable& table)
{
    if (!m_waterLampTable)
    {
        if (m_waterLampFailed)
            return false;
        if (FAILED(dev->CreateTexture(kWaterLampSlots, kWaterLampTableRows, 1, D3DUSAGE_DYNAMIC,
                                      D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &m_waterLampTable, nullptr)))
        {
            m_waterLampTable = nullptr;
            m_waterLampFailed = true;
            VF_LOG_ERROR("water lamp table creation failed; lamps are not mirrored on the water");
            return false;
        }
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_waterLampTable->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
        return false;
    for (int row = 0; row < kWaterLampTableRows; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, table.texels[row],
                    sizeof(table.texels[row]));
    m_waterLampTable->UnlockRect(0);
    return true;
}

void Renderer::DrawWaterLamps(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg)
{
    if (!(cfg.waterGlitter > 0.0f) || !WaterLampsCurrent(m_waterLampRecords, in))
        return;
    const WaterLampTable table = BuildWaterLampTable(m_waterLampRecords, in);
    if (table.count == 0 || !FillWaterLampTable(dev, table))
        return;
    if (!m_waterLampsLogged)
    {
        m_waterLampsLogged = true;
        VF_LOG_INFO("water lamps: the lamps and lit windows of the last frame's table mirrored on the water, a "
                    "Beckmann lobe of the waves' roughness widened by the lamp's size, the game's point-light "
                    "falloff, gain %.2f x WaterGlitter; hidden where %d steps toward the lamp meet the depth before "
                    "the water",
                    kWaterLampGain, kWaterLampOcclusionSteps);
    }
    dev->SetPixelShader(m_waterLampShader);
    const Float4 lamps = {static_cast<float>(table.count), cfg.waterGlitter * kWaterLampGain, 1.0f / kWaterLampSlots,
                          0.0f};
    dev->SetPixelShaderConstantF(kWaterLampRegister, &lamps.x, 1);
    BindTexture(dev, kWaterLampStage, m_waterLampTable, false);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    DrawFullscreen(dev);
    ++m_waterLampFrames;
    m_waterLampMost = std::max(m_waterLampMost, table.count - m_waterLampRecords.windows);
    m_waterLampWindowsMost = std::max(m_waterLampWindowsMost, m_waterLampRecords.windows);
    m_waterLampRecoveredMost = std::max(m_waterLampRecoveredMost, m_waterLampRecords.recovered);
}

void Renderer::NoteWaterRipple(const FrameInputs& in, bool swimming)
{
    ++m_waterRippleStarts;
    if (swimming)
        ++m_waterRippleSwims;
    if (m_waterRipplesLogged)
        return;
    m_waterRipplesLogged = true;
    VF_LOG_INFO("water ripples: rings around the player's feet at (%.1f %.1f %.1f)%s, one each %.1f yd moved (at most "
                "every %.3f s), every %.1f s swimming in place and every %.1f s standing, drawn where the feet are "
                "under the water and at most %.1f yd deep",
                in.playerPos[0], in.playerPos[1], in.playerPos[2], swimming ? ", swimming" : "",
                kWaterRippleSpacingYards, kWaterRippleStepSeconds, kWaterRippleSwimSeconds, kWaterRippleStillSeconds,
                kWaterRippleDeepestYards + kWaterRippleFadeYards);
}

bool Renderer::EnsureTerrainTexture(IDirect3DDevice9* dev)
{
    if (m_terrainTexture)
        return true;
    constexpr UINT side = TerrainShadowMap::kSide;
    if (SUCCEEDED(dev->CreateTexture(side, side, 1, 0, D3DFMT_G32R32F, D3DPOOL_MANAGED, &m_terrainTexture, nullptr)))
    {
        m_terrainFilterable = TextureFilterable(dev, D3DFMT_G32R32F);
        m_terrainFallback = false;
        m_terrainUploaded = m_terrain.Version() - 1;
        if (UploadTerrainShadows())
        {
            VF_LOG_INFO("terrain shadows: %ux%u map of %.0f yd cells, %s filtering", side, side,
                        TerrainShadowMap::kCellYards, m_terrainFilterable ? "linear" : "point");
            return true;
        }
        SafeRelease(m_terrainTexture);
    }
    D3DLOCKED_RECT locked = {};
    if (SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_terrainTexture, nullptr)) &&
        SUCCEEDED(m_terrainTexture->LockRect(0, &locked, nullptr, 0)))
    {
        *static_cast<DWORD*>(locked.pBits) = kLitTerrainTexel;
        m_terrainTexture->UnlockRect(0);
        m_terrainFilterable = false;
        m_terrainFallback = true;
        VF_LOG_ERROR("terrain shadow map creation failed; no terrain shadows");
        return true;
    }
    SafeRelease(m_terrainTexture);
    return Skip("terrain shadow texture creation failed");
}

bool Renderer::UploadTerrainShadows()
{
    if (m_terrainUploaded == m_terrain.Version())
        return true;
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_terrainTexture->LockRect(0, &locked, nullptr, 0)))
        return false;
    constexpr int side = TerrainShadowMap::kSide;
    for (int row = 0; row < side; ++row)
    {
        auto* lightAndGround = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch);
        const float* light = m_terrain.Texels() + row * side;
        const float* ground = m_terrain.GroundTexels() + row * side;
        for (int i = 0; i < side; ++i)
        {
            lightAndGround[2 * i] = light[i];
            lightAndGround[2 * i + 1] = ground[i];
        }
    }
    m_terrainTexture->UnlockRect(0);
    m_terrainUploaded = m_terrain.Version();
    return true;
}

TerrainShadowConstants Renderer::UpdateTerrainShadows(const FrameInputs& in, const Config& cfg)
{
    const HeightSource source = CurrentHeightSource();
    if (!source.probe)
        m_terrain.Forget();
    if (!TerrainMapWanted(cfg) || m_terrainFallback || !source.probe)
        return kTerrainShadowsOff;
    m_terrain.Update(in.camPos, in.mapId, in.toLight, source, CurrentLiquidSource(), FloorMapWanted(cfg));
    return UploadTerrainShadows() ? m_terrain.Constants(in.camPos) : kTerrainShadowsOff;
}

FogCeilingConstants Renderer::UpdateFogCeiling(const FrameInputs& in, const Config& cfg, bool floorsKnown)
{
    FogCeilingConstants c = {0.0f, 0.0f, in.camPos[2]};
    float target = 0.0f;
    if (cfg.fogCeiling > 0.0f && floorsKnown)
    {
        c.groundAtCamera = m_terrain.FloorAt(in.camPos[0], in.camPos[1]).nearFloor;
        c.perYard = 1.0f / (kFogCeilingFadeShare * cfg.fogCeiling);
        target = FogCeilingGate(cfg.fogCeiling, in.camPos[2] - c.groundAtCamera);
    }
    if (m_ceilingSeconds < 0.0 || !(in.seconds >= m_ceilingSeconds))
        m_ceilingGate = target;
    else
        m_ceilingGate = FogCeilingGateStep(m_ceilingGate, target, in.seconds - m_ceilingSeconds);
    m_ceilingSeconds = in.seconds;
    c.strength = c.perYard > 0.0f ? m_ceilingGate : 0.0f;
    return c;
}

bool Renderer::UploadBankFloors(IDirect3DDevice9* dev)
{
    if (m_bankFloorFailed)
        return false;
    constexpr UINT side = TerrainShadowMap::kFloorSide;
    if (!m_bankFloorTexture)
    {
        if (FAILED(dev->CreateTexture(side, side, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &m_bankFloorTexture,
                                      nullptr)))
        {
            m_bankFloorFailed = true;
            VF_LOG_ERROR("fog banks: the %ux%u floor map could not be created; the banks follow your own height", side,
                         side);
            return false;
        }
        m_bankFloorFilterable = TextureFilterable(dev, D3DFMT_A32B32G32R32F);
        m_bankFloorUploaded = m_terrain.FloorVersion() - 1;
    }
    if (m_bankFloorUploaded == m_terrain.FloorVersion())
        return true;
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_bankFloorTexture->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (UINT row = 0; row < side; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, m_terrain.Floors() + row * side,
                    side * sizeof(BankFloor));
    m_bankFloorTexture->UnlockRect(0);
    m_bankFloorUploaded = m_terrain.FloorVersion();
    if (!m_bankFloorLogged)
    {
        m_bankFloorLogged = true;
        VF_LOG_INFO("fog banks: floor map %ux%u of %.0f yd cells (ground or water around each cell within %.0f yd "
                    "and %.0f yd, lake and sea shares), %s filtering; water from %s",
                    side, side, TerrainShadowMap::kFloorBinYards,
                    (TerrainShadowMap::kFloorNearBins + 0.5f) * TerrainShadowMap::kFloorBinYards,
                    (TerrainShadowMap::kFloorBasinBins + 0.5f) * TerrainShadowMap::kFloorBinYards,
                    m_bankFloorFilterable ? "linear" : "point",
                    CurrentLiquidSource().probe ? "the liquid map" : "nowhere (no liquid map)");
    }
    return true;
}

void Renderer::LogTerrainShadows(const FrameInputs& in, const Config& cfg)
{
    const TerrainShadowStats s = m_terrain.TakeStats();
    if (!TerrainMapWanted(cfg) || m_terrainFallback || !CurrentHeightSource().probe)
        return;
    constexpr int cells = TerrainShadowMap::kSide * TerrainShadowMap::kSide;
    float shade[3];
    SurfaceShadowShade(in, cfg, shade);
    VF_LOG_INFO("  terrain shadows: light at the camera %.2f, surface shadow at its target %.2f, full shade %.2f %.2f "
                "%.2f; grid centre (%.0f %.0f), ground in %d cells, none in %d, unknown %d; %d probes (%.1f us each), "
                "%d sweeps (%.2f ms each), %d resets, %d shifts",
                m_terrain.Light(in.camPos[0], in.camPos[1], in.camPos[2]),
                m_terrain.SurfaceShadow(in.camTarget[0], in.camTarget[1], in.camTarget[2]), shade[0], shade[1],
                shade[2],
                (m_terrain.CentreCellX() + 0.5f) * TerrainShadowMap::kCellYards,
                (m_terrain.CentreCellY() + 0.5f) * TerrainShadowMap::kCellYards, s.groundCells, s.emptyCells,
                cells - s.groundCells - s.emptyCells, s.probes, s.probes ? s.probeSeconds * 1e6 / s.probes : 0.0,
                s.sweeps, s.sweeps ? s.sweepSeconds * 1e3 / s.sweeps : 0.0, s.resets, s.shifts);
}

bool Renderer::EnsureGlowTargets(IDirect3DDevice9* dev, UINT sourceW, UINT sourceH, UINT glowW, UINT glowH)
{
    if (m_glowSource && m_glowSourceW == sourceW && m_glowSourceH == sourceH && m_glowW == glowW &&
        m_glowH == glowH)
        return true;
    SafeRelease(m_glowSource);
    SafeRelease(m_glow[0]);
    SafeRelease(m_glow[1]);
    m_glowSourceW = m_glowSourceH = m_glowW = m_glowH = 0;
    if (m_glowFailed)
        return false;
    auto createGlowPair = [&](D3DFORMAT format) {
        SafeRelease(m_glow[0]);
        SafeRelease(m_glow[1]);
        return CreateTarget(dev, glowW, glowH, format, &m_glow[0]) &&
               CreateTarget(dev, glowW, glowH, format, &m_glow[1]);
    };
    if (!CreateTarget(dev, sourceW, sourceH, D3DFMT_A8R8G8B8, &m_glowSource) ||
        !(createGlowPair(D3DFMT_A16B16G16R16F) || createGlowPair(D3DFMT_A8R8G8B8)))
    {
        SafeRelease(m_glowSource);
        SafeRelease(m_glow[0]);
        SafeRelease(m_glow[1]);
        m_glowFailed = true;
        VF_LOG_ERROR("light glow target creation failed (%ux%u); lights do not glow in the fog", glowW, glowH);
        return false;
    }
    m_glowSourceW = sourceW;
    m_glowSourceH = sourceH;
    m_glowW = glowW;
    m_glowH = glowH;
    VF_LOG_INFO("light glow targets: source %ux%u, glow %ux%u", sourceW, sourceH, glowW, glowH);
    return true;
}

void Renderer::AdvanceDrift(const FrameInputs& in, const FogParams& fog, const Config& cfg)
{
    const double seconds =
        m_driftClockValid ? std::clamp(in.seconds - m_driftSeconds, 0.0, kMaxDriftStepSeconds) : 0.0;
    m_driftSeconds = in.seconds;
    m_driftClockValid = true;
    const double coarseXY = CoarseDriftPerYard(cfg);
    const double coarseZ = coarseXY * kDriftFlattening;
    const double fineXY = coarseXY * kFineDriftScale;
    const double fineZ = fineXY * kDriftFlattening;
    const double heading = cfg.windDirection * kPi / 180.0;
    const double fineHeading = heading + kFineDriftHeadingOffset;
    const double travelled = fog.windSpeed * seconds;
    const double fineTravelled = travelled * kFineDriftSpeedScale;
    m_coarseDrift[0] = Wrapped(m_coarseDrift[0] - std::cos(heading) * travelled * coarseXY);
    m_coarseDrift[1] = Wrapped(m_coarseDrift[1] + std::sin(heading) * travelled * coarseXY);
    m_coarseDrift[2] = Wrapped(m_coarseDrift[2] - kCoarseDriftRisePerWind * travelled * coarseZ);
    m_fineDrift[0] = Wrapped(m_fineDrift[0] - std::cos(fineHeading) * fineTravelled * fineXY);
    m_fineDrift[1] = Wrapped(m_fineDrift[1] + std::sin(fineHeading) * fineTravelled * fineXY);
    m_fineDrift[2] = Wrapped(m_fineDrift[2] - kFineDriftRisePerWind * travelled * fineZ);
}

void Renderer::MoveDriftPatches(const double* tileFraction)
{
    for (int k = 0; k < 3; ++k)
        m_coarseDrift[k] = Wrapped(m_coarseDrift[k] + tileFraction[k]);
}

void Renderer::SetCloudOffset(const double* yards)
{
    m_cloudOffset[0] = yards[0];
    m_cloudOffset[1] = yards[1];
}

void Renderer::BindDriftNoise(IDirect3DDevice9* dev, DWORD stage)
{
    dev->SetTexture(stage, m_driftNoise);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

bool Renderer::EnsureCloudNoise(IDirect3DDevice9* dev)
{
    if (m_cloudNoise)
        return true;
    if (m_cloudNoiseFailed)
        return false;
    const long long buildStart = Ticks();
    const std::vector<CloudNoiseLevel> levels = BuildCloudNoise();
    const double buildSeconds = TickSeconds(Ticks() - buildStart);
    bool ok = SUCCEEDED(dev->CreateTexture(kCloudNoiseSize, kCloudNoiseSize, static_cast<UINT>(levels.size()), 0,
                                           D3DFMT_L8, D3DPOOL_MANAGED, &m_cloudNoise, nullptr));
    for (UINT level = 0; ok && level < levels.size(); ++level)
    {
        const CloudNoiseLevel& source = levels[level];
        D3DLOCKED_RECT rect = {};
        ok = SUCCEEDED(m_cloudNoise->LockRect(level, &rect, nullptr, 0));
        if (!ok)
            break;
        for (int y = 0; y < source.size; ++y)
            std::memcpy(static_cast<unsigned char*>(rect.pBits) + y * rect.Pitch,
                        &source.texels[static_cast<size_t>(y) * source.size], source.size);
        m_cloudNoise->UnlockRect(level);
    }
    if (!ok)
    {
        SafeRelease(m_cloudNoise);
        m_cloudNoiseFailed = true;
        VF_LOG_ERROR("cloud noise texture creation failed; no cloud shadows");
        return false;
    }
    VF_LOG_INFO("cloud noise: %dx%d L8, %zu levels, built in %.0f ms", kCloudNoiseSize, kCloudNoiseSize,
                levels.size(), buildSeconds * 1000.0);
    return true;
}

void Renderer::AdvanceClouds(const FrameInputs& in, const FogParams& fog)
{
    const double seconds =
        m_cloudClockValid ? std::clamp(in.seconds - m_cloudSeconds, 0.0, kMaxDriftStepSeconds) : 0.0;
    m_cloudSeconds = in.seconds;
    m_cloudClockValid = true;
    if (!(fog.cloudTileYards > 0.0f))
        return;
    const double tile = fog.cloudTileYards;
    for (int k = 0; k < 2; ++k)
    {
        const double moved = m_cloudOffset[k] + static_cast<double>(fog.cloudWind[k]) * seconds;
        m_cloudOffset[k] = moved - std::floor(moved / tile) * tile;
    }
}

void Renderer::BindCloudNoise(IDirect3DDevice9* dev, DWORD stage)
{
    dev->SetTexture(stage, m_cloudNoise);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

void Renderer::BlurLightGlow(IDirect3DDevice9* dev, IDirect3DTexture9* source, IDirect3DTexture9* destination,
                             float stepU, float stepV)
{
    SetTarget(dev, destination);
    const Float4 blur[2] = {
        {stepU, stepV, 0.0f, 0.0f},
        {static_cast<float>(m_glowW), static_cast<float>(m_glowH), 1.0f / m_glowW, 1.0f / m_glowH},
    };
    dev->SetPixelShaderConstantF(9, &blur[0].x, 2);
    BindTexture(dev, 0, source, true);
    DrawFullscreen(dev);
}

bool Renderer::RenderLightGlow(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                               const D3DVIEWPORT9& vp, const Config& cfg)
{
    const UINT sourceW = std::max(1u, (static_cast<UINT>(vp.Width) + kGlowSourceScale - 1) / kGlowSourceScale);
    const UINT sourceH = std::max(1u, (static_cast<UINT>(vp.Height) + kGlowSourceScale - 1) / kGlowSourceScale);
    const UINT glowW = std::max(1u, (static_cast<UINT>(vp.Width) + kGlowScale - 1) / kGlowScale);
    const UINT glowH = std::max(1u, (static_cast<UINT>(vp.Height) + kGlowScale - 1) / kGlowScale);
    if (!EnsureGlowTargets(dev, sourceW, sourceH, glowW, glowH))
        return false;
    const RECT world = ViewportRect(vp);
    IDirect3DSurface9* sourceSurface = nullptr;
    const bool copied = SUCCEEDED(m_glowSource->GetSurfaceLevel(0, &sourceSurface)) &&
                        SUCCEEDED(dev->StretchRect(target, &world, sourceSurface, nullptr, D3DTEXF_LINEAR));
    SafeRelease(sourceSurface);
    if (!copied)
        return false;

    SetTarget(dev, m_glow[0]);
    dev->SetPixelShader(m_glowMask);
    const Float4 mask[2] = {
        {cfg.glowThreshold, static_cast<float>(kGlowScale), 1.0f / glowW, 1.0f / glowH},
        {1.0f / sourceW, 1.0f / sourceH, 0.0f, 0.0f},
    };
    dev->SetPixelShaderConstantF(9, &mask[0].x, 2);
    BindTexture(dev, 0, depthTexture, false);
    BindTexture(dev, 1, m_glowSource, true);
    DrawFullscreen(dev);

    dev->SetPixelShader(m_glowBlur);
    for (float spacing : kGlowBlurSpacings)
    {
        BlurLightGlow(dev, m_glow[0], m_glow[1], spacing / glowW, 0.0f);
        BlurLightGlow(dev, m_glow[1], m_glow[0], 0.0f, spacing / glowH);
    }
    return true;
}

bool Renderer::EnsureLampTargets(IDirect3DDevice9* dev, UINT lowW, UINT lowH)
{
    if (m_lampHalos && m_lampW == lowW && m_lampH == lowH)
        return true;
    if (m_lampsFailed)
        return false;
    SafeRelease(m_lampHalos);
    m_lampW = m_lampH = 0;
    bool staged = true;
    for (IDirect3DTexture9*& staging : m_lampTableStaging)
        staged = staged && (staging || SUCCEEDED(dev->CreateTexture(kLampSlots, kLampTableRows, 1, 0,
                                                                    D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM,
                                                                    &staging, nullptr)));
    const bool tables =
        staged &&
        (m_lampTable || SUCCEEDED(dev->CreateTexture(kLampSlots, kLampTableRows, 1, 0, D3DFMT_A32B32G32R32F,
                                                     D3DPOOL_DEFAULT, &m_lampTable, nullptr))) &&
        (m_lampVisibility[0] || CreateTarget(dev, kLampSlots, 1, D3DFMT_A8R8G8B8, &m_lampVisibility[0])) &&
        (m_lampVisibility[1] || CreateTarget(dev, kLampSlots, 1, D3DFMT_A8R8G8B8, &m_lampVisibility[1]));
    D3DFORMAT haloFormat = D3DFMT_A16B16G16R16F;
    const bool halos = tables && (CreateTarget(dev, lowW, lowH, haloFormat, &m_lampHalos) ||
                                  CreateTarget(dev, lowW, lowH, haloFormat = D3DFMT_A8R8G8B8, &m_lampHalos));
    if (!halos)
    {
        SafeRelease(m_lampTable);
        for (IDirect3DTexture9*& staging : m_lampTableStaging)
            SafeRelease(staging);
        SafeRelease(m_lampVisibility[0]);
        SafeRelease(m_lampVisibility[1]);
        SafeRelease(m_lampHalos);
        m_lampsFailed = true;
        VF_LOG_ERROR("lamp halo target creation failed (%ux%u); no lamp halos", lowW, lowH);
        return false;
    }
    m_lampW = lowW;
    m_lampH = lowH;
    m_lamps.ForgetVisibility();
    VF_LOG_INFO("lamp halo targets: %ux%u (%s), lamp table %dx%d", lowW, lowH,
                haloFormat == D3DFMT_A16B16G16R16F ? "fp16" : "rgba8", kLampSlots, kLampTableRows);
    return true;
}

bool Renderer::EnsureLampSurfaceTarget(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (m_lampSurfaces && m_lampSurfaceW == w && m_lampSurfaceH == h)
        return true;
    if (m_lampSurfacesFailed)
        return false;
    SafeRelease(m_lampSurfaces);
    SafeRelease(m_lampSurfacesBlur);
    m_lampSurfaceW = m_lampSurfaceH = 0;
    D3DFORMAT format = D3DFMT_A16B16G16R16F;
    if (!CreateTarget(dev, w, h, format, &m_lampSurfaces) &&
        !CreateTarget(dev, w, h, format = D3DFMT_A8R8G8B8, &m_lampSurfaces))
    {
        m_lampSurfacesFailed = true;
        VF_LOG_ERROR("lamp light target creation failed (%ux%u); no lamp light on surfaces", w, h);
        return false;
    }
    m_lampSurfaceW = w;
    m_lampSurfaceH = h;
    const bool blurred = CreateTarget(dev, w, h, format, &m_lampSurfacesBlur);
    VF_LOG_INFO("lamp light target: %ux%u (%s), %s", w, h, format == D3DFMT_A16B16G16R16F ? "fp16" : "rgba8",
                blurred ? "shadow edges smoothed" : "no second target, shadow edges not smoothed");
    return true;
}

bool Renderer::EnsureWetSurfaces(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (!m_wetShader)
    {
        if (m_wetChecked)
            return false;
        m_wetChecked = true;
        if (FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_wet_surfaces), &m_wetShader)))
        {
            m_wetShader = nullptr;
            VF_LOG_ERROR("wet surfaces unavailable: shader creation failed");
            return false;
        }
    }
    if (m_wetTarget && m_wetW == w && m_wetH == h)
        return true;
    if (m_wetFailed)
        return false;
    SafeRelease(m_wetTarget);
    m_wetW = m_wetH = 0;
    if (!CreateTarget(dev, w, h, D3DFMT_A16B16G16R16F, &m_wetTarget))
    {
        m_wetFailed = true;
        VF_LOG_ERROR("wet surface target creation failed (%ux%u); no wet or shiny surfaces", w, h);
        return false;
    }
    m_wetW = w;
    m_wetH = h;
    VF_LOG_INFO("wet surfaces: %ux%u fp16 target, sheen toward the sun or moon, wetness, puddles and screen-space "
                "reflections from the scene before the late draws",
                w, h);
    return true;
}

bool Renderer::RenderWetSurfaces(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fogTexture,
                                 const FrameInputs& in, const Config& cfg, const FogParams& fog,
                                 const TerrainShadowConstants& terrain, const float* cloudMap, const float* cloudShape,
                                 float exposure, bool sceneBlend)
{
    const bool waterKnown = m_waterBeforeFresh;
    m_waterBeforeFresh = false;
    if (!WetSurfacesWanted(cfg))
    {
        m_wetness.Reset();
        return false;
    }
    m_wetness.Update(in);
    m_wetParams = BuildWetSurfaceParams(in, cfg, m_wetness);
    if (!sceneBlend || !WetSurfacesShown(m_wetParams) || !EnsureWetSurfaces(dev, in.viewport.Width, in.viewport.Height))
        return false;
    const bool puddles = m_wetParams.puddleShare > 0.0f && EnsureCloudNoise(dev);
    const bool cloudy = cloudShape[3] > 0.0f && m_cloudNoise;
    const bool noise = puddles || cloudy;
    const bool lampsUsable = m_lampSurfacesDrawn && m_wetLampSlots > 0.0f;
    const bool surfaceBefore = m_surfaceBeforeLateDraws && m_effectBaseline;
    const TerrainMaterialTargets materials = m_terrainMaterials;
    m_terrainMaterials = {};
    m_wetMaterialsUsed = materials.written && materials.params && materials.tags &&
                         SameSize(materials.params, depthTexture) && SameSize(materials.tags, depthTexture);

    SetTarget(dev, m_wetTarget);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                                    D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
    dev->SetPixelShader(m_wetShader);

    const SunFacing facing = DaylightFacing(in);
    const double tile = kPuddleTileYards;
    const double u = static_cast<double>(in.camPos[0]) / tile;
    const double v = static_cast<double>(in.camPos[1]) / tile;
    const double turn = static_cast<double>(kPuddleSecondTurnDegrees) * 3.14159265358979 / 180.0;
    const double turnCos = std::cos(turn) / static_cast<double>(kPuddleSecondTileYards);
    const double turnSin = std::sin(turn) / static_cast<double>(kPuddleSecondTileYards);
    const double u2 = turnCos * in.camPos[0] - turnSin * in.camPos[1];
    const double v2 = turnSin * in.camPos[0] + turnCos * in.camPos[1];
    const float focalPixels = 0.5f * in.glProjection[5] * static_cast<float>(in.viewport.Height);
    float horizon[3];
    float above[3];
    UnpackColor(in.skyHorizonColor, horizon);
    UnpackColor(in.skyAboveHorizonColor, above);
    for (int i = 0; i < 3; ++i)
    {
        horizon[i] = std::pow(std::max(horizon[i], 0.0f), 2.2f);
        above[i] = std::pow(std::max(above[i], 0.0f), 2.2f);
    }
    float albedoPerLight[3];
    AlbedoPerSceneLight(in, albedoPerLight);
    const float threshold =
        puddles ? (g_wetPuddleThreshold >= 0.0f ? g_wetPuddleThreshold : PuddleThreshold(m_wetParams.puddleShare))
                : 1.0f;
    const Float4 constants[kWetConstants] = {
        {lampsUsable ? m_wetLampSlots : 0.0f, m_wetParams.sheen, m_wetParams.wetness, threshold},
        {facing.toLightView[0], facing.toLightView[1], facing.toLightView[2], m_wetParams.light},
        {m_wetParams.lightColor[0], m_wetParams.lightColor[1], m_wetParams.lightColor[2], exposure},
        {m_wetLampTiles[0], m_wetLampTiles[1], m_wetLampTiles[2], m_wetLampTiles[3]},
        {terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness},
        {static_cast<float>(TerrainShadowMap::kSide), TerrainShadowMap::kExtentYards / kSurfaceShadowMapFadeYards,
         -kSurfaceShadowMapMarginYards / kSurfaceShadowMapFadeYards, TerrainShadowMap::kUncoveredGain},
        {cloudMap[0], cloudMap[1], cloudMap[2], cloudMap[3]},
        {cloudShape[0], cloudShape[1], cloudShape[2], cloudy ? cloudShape[3] : 0.0f},
        {static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)), static_cast<float>(1.0 / tile),
         focalPixels},
        {horizon[0], horizon[1], horizon[2], waterKnown && m_waterDepth[0] ? 1.0f : 0.0f},
        {above[0], above[1], above[2], 0.0f},
        {fog.overcastSky[0], fog.overcastSky[1], fog.overcastSky[2], fog.overcastShare},
        {albedoPerLight[0], albedoPerLight[1], albedoPerLight[2], fog.nightDarkness},
        {SelfLitShare(in), 0.0f, 0.0f, 0.0f},
        {static_cast<float>(u2 - std::floor(u2)), static_cast<float>(v2 - std::floor(v2)), static_cast<float>(turnCos),
         static_cast<float>(turnSin)},
        {m_wetMaterialsUsed ? 1.0f : 0.0f, kMaterialTagShare, kMaterialTagYards, kGlintCellYards},
        {kGrassMaterial.darkening, kGrassMaterial.pores, kGrassMaterial.film, kGrassMaterial.puddle},
        {kSoilMaterial.darkening, kSoilMaterial.pores, kSoilMaterial.film, kSoilMaterial.puddle},
        {kStoneMaterial.darkening, kStoneMaterial.pores, kStoneMaterial.film, kStoneMaterial.puddle},
        {kGrassMaterial.glint, kSoilMaterial.glint, kStoneMaterial.glint, g_wetGlintShare},
    };
    static_assert(kWetRegister + kWetConstants <= kPixelConstants, "the state block covers the wet surface constants");
    dev->SetPixelShaderConstantF(kWetRegister, &constants[0].x, kWetConstants);
    static_assert(kWetFogStage < kWetMaterialStage && kWetMaterialTagStage < kStages,
                  "the state block covers the wet surface stages");
    BindTexture(dev, kWetDepthStage, depthTexture, false);
    BindTexture(dev, kWetLampTableStage, lampsUsable ? m_lampTable : nullptr, false);
    BindTexture(dev, kWetLampTileStage, lampsUsable && m_wetLampTiles[3] > 0.0f ? m_lampTileTexture : nullptr, false);
    BindTexture(dev, kWetSurfaceStage, surfaceBefore ? m_effectBaseline : m_sceneCopy, true);
    BindTexture(dev, kWetLampLightStage, lampsUsable ? m_lampSurfaces : nullptr, false);
    BindTexture(dev, kWetTerrainStage, terrain.uvPerYard > 0.0f ? m_terrainTexture : nullptr, false);
    if (noise)
        BindCloudNoise(dev, kWetNoiseStage);
    else
        BindTexture(dev, kWetNoiseStage, nullptr, false);
    BindTexture(dev, kWetWaterDepthStage, waterKnown ? m_waterDepth[0] : nullptr, false);
    BindTexture(dev, kWetFogStage, fogTexture, true);
    BindTexture(dev, kWetMaterialStage, m_wetMaterialsUsed ? materials.params : nullptr, false);
    BindTexture(dev, kWetMaterialTagStage, m_wetMaterialsUsed ? materials.tags : nullptr, false);
    DrawFullscreen(dev);
    BindTexture(dev, kWetMaterialStage, nullptr, false);
    BindTexture(dev, kWetMaterialTagStage, nullptr, false);
    ++m_wetFrames;
    if (waterKnown)
        ++m_wetWaterFrames;
    if (m_wetMaterialsUsed)
        ++m_wetMaterialFrames;
    return true;
}

void Renderer::LogWetSurfaces(const Config& cfg)
{
    if (!WetSurfacesWanted(cfg))
        return;
    VF_LOG_INFO("  wet surfaces: sheen %.2f, wetness %.2f (state %.2f), puddles %.2f of the flat ground (state %.2f), "
                "sun or moon %.2f; drawn in %u frames, %u of them with the water known, %u with the ground materials",
                m_wetParams.sheen, m_wetParams.wetness, m_wetness.Wetness(), m_wetParams.puddleShare,
                m_wetness.Puddles(), m_wetParams.light, m_wetFrames, m_wetWaterFrames, m_wetMaterialFrames);
    m_wetFrames = m_wetWaterFrames = m_wetMaterialFrames = 0;
}

void Renderer::ReadWetness(float* out) const
{
    out[0] = m_wetness.Wetness();
    out[1] = m_wetness.Puddles();
    out[2] = !m_wetDrawn ? 0.0f : m_wetMaterialsUsed ? 2.0f : 1.0f;
    out[3] = m_wetParams.puddleShare;
}

bool Renderer::EnsureEdgeOccluders(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (m_edgeOccluders && m_edgeW == w && m_edgeH == h)
        return true;
    if (m_edgeOccludersFailed)
        return false;
    SafeRelease(m_edgeOccluders);
    m_edgeW = m_edgeH = 0;
    if (!CreateTarget(dev, w, h, D3DFMT_A8R8G8B8, &m_edgeOccluders))
    {
        m_edgeOccludersFailed = true;
        VF_LOG_ERROR("edge occluder target creation failed (%ux%u); shafts from things past the screen edge are off",
                     w, h);
        return false;
    }
    m_edgeW = w;
    m_edgeH = h;
    VF_LOG_INFO("edge occluders: %ux%u blocks of up to %ux%u pixels for shafts from things past the screen edge", w,
                h, kEdgeBlockWidth, kEdgeBlockHeight);
    return true;
}

void Renderer::RenderEdgeOccluders(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const D3DVIEWPORT9& vp,
                                   const CarrierBody& body)
{
    const UINT w = std::max(1u, (static_cast<UINT>(vp.Width) + kEdgeBlockWidth - 1) / kEdgeBlockWidth);
    const UINT h = std::max(1u, (static_cast<UINT>(vp.Height) + kEdgeBlockHeight - 1) / kEdgeBlockHeight);
    if (!EnsureEdgeOccluders(dev, w, h))
        return;
    SetTarget(dev, m_edgeOccluders);
    dev->SetPixelShader(m_edgeOccluderShader);
    const Float4 block = {static_cast<float>(vp.Width) / w, static_cast<float>(vp.Height) / h, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(9, &block.x, 1);
    const BodyConstants bodyConstants = BodyConstantsOf(body);
    dev->SetPixelShaderConstantF(10, &bodyConstants.base.x, 2);
    BindTexture(dev, 0, depthTexture, false);
    DrawFullscreen(dev);
}

bool Renderer::UploadLampTable(IDirect3DDevice9* dev, const LampTable& table)
{
    const long long start = FrameProfile::Now();
    m_lampUpload = (m_lampUpload + 1) % kLampUploadRing;
    IDirect3DTexture9* staging = m_lampTableStaging[m_lampUpload];
    D3DLOCKED_RECT locked = {};
    if (!staging || !m_lampTable || FAILED(staging->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (int row = 0; row < kLampTableRows; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, table.texels[row],
                    sizeof(table.texels[row]));
    staging->UnlockRect(0);
    const bool sent = SUCCEEDED(dev->UpdateTexture(staging, m_lampTable));
    m_profile.AddCpuTicks(ProfileSection::LampTable, FrameProfile::Now() - start);
    return sent;
}

bool Renderer::UploadLampTiles(IDirect3DDevice9* dev, const LampTable& table, const FrameInputs& in)
{
    m_lampTilesUsed = false;
    if (g_lampTiles == kLampTilesOff || m_lampTilesFailed)
        return false;
    const long long buildStart = FrameProfile::Now();
    m_lampTiles.Build(table, in.glProjection, in.viewport, FarCoreAnglePerTexel(in, static_cast<int>(m_lowH)));
    if (g_lampTiles == kLampTilesEmptied)
        m_lampTiles.Empty();
    const long long buildEnd = FrameProfile::Now();
    const UINT w = m_lampTiles.TextureWidth();
    const UINT h = m_lampTiles.TextureHeight();
    if (!m_lampTileTexture || m_lampTileTextureW != w || m_lampTileTextureH != h)
    {
        const auto release = [&]() {
            SafeRelease(m_lampTileTexture);
            for (IDirect3DTexture9*& staging : m_lampTileStaging)
                SafeRelease(staging);
        };
        const auto make = [&](D3DFORMAT format) {
            bool made = SUCCEEDED(dev->CreateTexture(w, h, 1, 0, format, D3DPOOL_DEFAULT, &m_lampTileTexture, nullptr));
            for (IDirect3DTexture9*& staging : m_lampTileStaging)
                made = made &&
                       SUCCEEDED(dev->CreateTexture(w, h, 1, 0, format, D3DPOOL_SYSTEMMEM, &staging, nullptr));
            if (!made)
                release();
            return made;
        };
        release();
        m_lampTileBytes = 1;
        if (!make(D3DFMT_L8))
        {
            m_lampTileBytes = 4;
            if (!make(D3DFMT_A8R8G8B8))
            {
                m_lampTilesFailed = true;
                VF_LOG_ERROR("lamp tile list creation failed (%ux%u); every pixel tests every lamp", w, h);
                return false;
            }
        }
        m_lampTileTextureW = w;
        m_lampTileTextureH = h;
        VF_LOG_INFO("lamp tiles: %dx%d tiles of %d px, lists in a %ux%u %s texture, written through %d in system "
                    "memory",
                    m_lampTiles.TilesX(), m_lampTiles.TilesY(), kLampTilePixels, w, h,
                    m_lampTileBytes == 1 ? "L8" : "A8R8G8B8", kLampUploadRing);
    }
    IDirect3DTexture9* staging = m_lampTileStaging[m_lampUpload];
    D3DLOCKED_RECT locked = {};
    if (!staging || FAILED(staging->LockRect(0, &locked, nullptr, 0)))
        return false;
    const long long lockEnd = FrameProfile::Now();
    m_lampTiles.Write(static_cast<unsigned char*>(locked.pBits), locked.Pitch, m_lampTileBytes);
    staging->UnlockRect(0);
    const bool sent = SUCCEEDED(dev->UpdateTexture(staging, m_lampTileTexture));
    const long long writeEnd = FrameProfile::Now();
    m_profile.AddCpuTicks(ProfileSection::LampTileBuild, buildEnd - buildStart);
    m_profile.AddCpuTicks(ProfileSection::LampTileLock, lockEnd - buildEnd);
    m_profile.AddCpuTicks(ProfileSection::LampTileWrite, writeEnd - lockEnd);
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    const double msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);
    m_lampTiles.NoteBuildMs(static_cast<double>(buildEnd - buildStart) * msPerTick);
    m_lampTiles.NoteWriteMs(static_cast<double>(writeEnd - lockEnd) * msPerTick);
    VF_LOG_DEBUG("lamp tiles: build %.3f ms, lock %.3f ms, write %.3f ms",
                 static_cast<double>(buildEnd - buildStart) * msPerTick,
                 static_cast<double>(lockEnd - buildEnd) * msPerTick,
                 static_cast<double>(writeEnd - lockEnd) * msPerTick);
    m_lampTilesUsed = sent;
    return sent;
}

void Renderer::BlurLampLight(IDirect3DDevice9* dev, IDirect3DTexture9* source, IDirect3DTexture9* destination,
                             float stepX, float stepY)
{
    BindTexture(dev, kLampBlurSourceStage, source, false);
    SetTarget(dev, destination);
    const Float4 blur = {stepX, stepY, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kLampPassRegister, &blur.x, 1);
    DrawFullscreen(dev);
}


void Renderer::RenderLamps(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fogTexture,
                           const FrameInputs& in, const Config& cfg, const FogParams& fog,
                           const TerrainShadowConstants& terrain, bool effectScene)
{
    const bool halosBefore = m_lampsDrawn;
    m_lampsDrawn = false;
    m_lampSurfacesDrawn = false;
    m_wetLampSlots = 0.0f;
    const bool occluderFresh = m_occluderDepthFresh;
    m_occluderDepthFresh = false;
    const bool baselineFresh = m_effectBaselineFresh;
    m_effectBaselineFresh = false;
    m_surfaceBeforeLateDraws = baselineFresh && occluderFresh;
    if (!halosBefore)
        m_lamps.ForgetVisibility();
    const std::vector<ObjectLamp>& objects = m_objectLamps.Update(GlobalLampCatalog(), in, cfg.objectLamps);
    m_profile.Begin(dev, ProfileSection::Effects);
    const std::vector<EffectLamp>* effects =
        UpdateEffects(dev, depthTexture, in, fog, effectScene && baselineFresh && occluderFresh);
    m_profile.End(dev, ProfileSection::Effects);
    const long long unitsStart = FrameProfile::Now();
    const LanternLamp& lantern = m_lantern.Update(in, cfg, fog.lanternLight);
    const std::vector<EffectLamp>& corpses = m_corpses.Update(in, fog.corpseLight);
    m_carried.clear();
    if (effects && fog.effectLight > 0.0f)
        for (EffectLamp e : *effects)
        {
            e.intensity *= fog.effectLight;
            m_carried.push_back(e);
        }
    m_carried.insert(m_carried.end(), corpses.begin(), corpses.end());
    std::stable_sort(m_carried.begin(), m_carried.end(),
                     [](const EffectLamp& a, const EffectLamp& b) { return a.intensity > b.intensity; });
    const long long selectStart = FrameProfile::Now();
    m_profile.AddCpuTicks(ProfileSection::LampUnits, selectStart - unitsStart);
    const LampTable& table =
        m_lamps.Update(GlobalLampCatalog(), in, cfg, fog.lampHaze, fog.lampLight, fog.windowLight, fog.lanternGlow,
                       &lantern, fog.lanternLight, m_carried.empty() ? nullptr : &m_carried, 1.0f, fog.farLightCore,
                       &objects);
    std::bitset<kLampSlots> effectSlots;
    for (const EffectLamp& carried : m_carried)
    {
        const int slot = m_lamps.EffectSlot(carried.id);
        if (slot >= 0)
            effectSlots.set(static_cast<size_t>(slot));
    }
    m_waterLampRecords = RecordWaterLamps(table, in, fog.lampHaze, fog.lampLight, effectSlots);
    const long long uploadStart = FrameProfile::Now();
    m_profile.AddCpuTicks(ProfileSection::LampSelect, uploadStart - selectStart);
    const bool sunFacing = std::max({fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2]}) > 0.0f;
    const bool lamps = table.slots > 0 && EnsureLampTargets(dev, m_lowW, m_lowH) && UploadLampTable(dev, table);
    if (!lamps && !sunFacing)
    {
        m_profile.AddCpuTicks(ProfileSection::LampUpload, FrameProfile::Now() - uploadStart);
        return;
    }
    const bool tiled = lamps && UploadLampTiles(dev, table, in);
    m_profile.AddCpuTicks(ProfileSection::LampUpload, FrameProfile::Now() - uploadStart);
    const float tileTexels[2] = {tiled ? 1.0f / static_cast<float>(m_lampTileTextureW) : 0.0f,
                                 tiled ? 1.0f / static_cast<float>(m_lampTileTextureH) : 0.0f};
    const float tilePixels = tiled ? static_cast<float>(kLampTilePixels) : 0.0f;
    m_wetLampSlots = lamps ? static_cast<float>(table.surfaceSlots) : 0.0f;
    m_wetLampTiles[0] = tileTexels[0];
    m_wetLampTiles[1] = tileTexels[1];
    m_wetLampTiles[2] = 0.0f;
    m_wetLampTiles[3] = tilePixels;

    if ((sunFacing || fog.lampLight > 0.0f || fog.windowLight > 0.0f || m_lamps.Stats().lanternDrawn ||
         m_lamps.Stats().effectsDrawn > 0) &&
        EnsureLampSurfaceTarget(dev, in.viewport.Width, in.viewport.Height))
    {
        m_profile.Begin(dev, ProfileSection::LampSurfaces);
        SetTarget(dev, m_lampSurfaces);
        dev->SetPixelShader(m_lampSurfaceShader);
        const SunFacing facing = DaylightFacing(in);
        const CarrierBody carrier = CarrierBodyInView(in, lantern);
        const Float4 surfaces[kLampSurfaceConstants] = {
            {lamps ? static_cast<float>(table.surfaceSlots) : 0.0f, 0.0f, 0.0f, 0.0f},
            {facing.toLightView[0], facing.toLightView[1], facing.toLightView[2], 0.0f},
            {carrier.base[0], carrier.base[1], carrier.base[2], carrier.radius},
            {carrier.up[0], carrier.up[1], carrier.up[2], carrier.height},
            {tileTexels[0], tileTexels[1], 0.0f, tilePixels},
            {terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness},
            {static_cast<float>(TerrainShadowMap::kSide), 0.0f, 0.0f, 0.0f},
        };
        dev->SetPixelShaderConstantF(kLampPassRegister, &surfaces[0].x, kLampSurfaceConstants);
        BindTexture(dev, kLampSurfaceDepthStage, depthTexture, false);
        BindTexture(dev, kLampSurfaceTableStage, lamps ? m_lampTable : nullptr, false);
        BindTexture(dev, kLampSurfaceOccluderStage, occluderFresh ? m_occluderDepth : depthTexture, false);
        BindTexture(dev, kLampSurfaceTileStage, tiled ? m_lampTileTexture : nullptr, false);
        BindTexture(dev, kLampSurfaceTerrainStage, terrain.uvPerYard > 0.0f ? m_terrainTexture : nullptr, false);
        DrawFullscreen(dev);
        m_profile.End(dev, ProfileSection::LampSurfaces);
        if (lamps && m_lampSurfacesBlur && g_lampLightBlur)
        {
            m_profile.Begin(dev, ProfileSection::LampBlur);
            dev->SetPixelShader(m_lampBlurShader);
            BlurLampLight(dev, m_lampSurfaces, m_lampSurfacesBlur, 1.0f, 0.0f);
            BlurLampLight(dev, m_lampSurfacesBlur, m_lampSurfaces, 0.0f, 1.0f);
            m_profile.End(dev, ProfileSection::LampBlur);
        }
        m_lampSurfacesDrawn = true;
    }
    if (!lamps || !(fog.lampHaze > 0.0f || fog.farLightCore > 0.0f))
        return;

    m_profile.Begin(dev, ProfileSection::LampHalos);
    const int previous = m_lampVisibilityIndex;
    const int current = previous ^ 1;
    SetTarget(dev, m_lampVisibility[current]);
    dev->SetPixelShader(m_lampVisibilityShader);
    const Float4 visibility = {m_lamps.VisibilityBlend(), 0.0f, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kLampPassRegister, &visibility.x, 1);
    BindTexture(dev, kLampVisibilityDepthStage, depthTexture, false);
    BindTexture(dev, kLampPreviousVisibilityStage, m_lampVisibility[previous], false);
    BindTexture(dev, kLampVisibilityTableStage, m_lampTable, false);
    DrawFullscreen(dev);
    m_lampVisibilityIndex = current;

    SetTarget(dev, m_lampHalos);
    dev->SetPixelShader(m_lampShader);
    const Float4 pass[2] = {
        {static_cast<float>(table.slots), FarCoreAnglePerTexel(in, m_lowH), 0.0f, 0.0f},
        {tileTexels[0], tileTexels[1], static_cast<float>(m_lampTiles.TilesY() * kLampTileBlockHeight), tilePixels},
    };
    dev->SetPixelShaderConstantF(kLampPassRegister, &pass[0].x, 2);
    BindTexture(dev, kLampDepthStage, depthTexture, false);
    BindTexture(dev, kLampFogStage, fogTexture, false);
    BindTexture(dev, kLampVisibilityStage, m_lampVisibility[current], false);
    BindTexture(dev, kLampTableStage, m_lampTable, false);
    BindTexture(dev, kLampTileStage, tiled ? m_lampTileTexture : nullptr, false);
    DrawFullscreen(dev);
    m_profile.End(dev, ProfileSection::LampHalos);
    m_lampsDrawn = true;
}

bool Renderer::EnsureEffectShaders(IDirect3DDevice9* dev)
{
    if (m_effectGatherShader && m_effectReduceShader)
        return true;
    if (m_effectsChecked)
        return false;
    m_effectsChecked = true;
    D3DCAPS9 caps = {};
    const bool targets = SUCCEEDED(dev->GetDeviceCaps(&caps)) && caps.NumSimultaneousRTs >= kEffectTargets;
    const bool shaders =
        targets &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_effect_gather), &m_effectGatherShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_effect_reduce), &m_effectReduceShader));
    if (shaders)
        return true;
    SafeRelease(m_effectGatherShader);
    SafeRelease(m_effectReduceShader);
    m_effectsFailed = true;
    VF_LOG_ERROR("effect light unavailable: %s", targets ? "shader creation failed" : "fewer than two render targets");
    return false;
}

bool Renderer::EnsureEffectBaseline(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT w, UINT h)
{
    if (m_effectBaseline && m_effectBaselineW == w && m_effectBaselineH == h)
        return true;
    SafeRelease(m_effectBaseline);
    m_effectBaselineW = m_effectBaselineH = 0;
    D3DSURFACE_DESC desc = {};
    target->GetDesc(&desc);
    if (!CreateTarget(dev, w, h, desc.Format, &m_effectBaseline) &&
        !CreateTarget(dev, w, h, D3DFMT_A8R8G8B8, &m_effectBaseline))
    {
        m_effectsFailed = true;
        VF_LOG_ERROR("effect light unavailable: the scene copy after the liquid pass could not be made (%ux%u)", w, h);
        return false;
    }
    m_effectBaselineW = w;
    m_effectBaselineH = h;
    return true;
}

void Renderer::ReleaseEffectTargets()
{
    SafeRelease(m_effectBaseline);
    for (IDirect3DTexture9*& gather : m_effectGather)
        SafeRelease(gather);
    for (EffectReadback& r : m_effectRing)
    {
        SafeRelease(r.target);
        SafeRelease(r.copy);
        SafeRelease(r.done);
        r.pending = false;
    }
    m_effectBaselineW = m_effectBaselineH = 0;
    m_effectW = m_effectH = m_effectGatherW = m_effectGatherH = m_effectColumns = m_effectRows = 0;
    m_effectBaselineFresh = false;
    m_effectsFailed = false;
}

bool Renderer::EnsureEffectTargets(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (m_effectGather[0] && m_effectW == w && m_effectH == h)
        return true;
    if (m_effectsFailed || !EnsureEffectShaders(dev))
        return false;
    for (IDirect3DTexture9*& gather : m_effectGather)
        SafeRelease(gather);
    for (EffectReadback& r : m_effectRing)
    {
        SafeRelease(r.target);
        SafeRelease(r.copy);
        SafeRelease(r.done);
        r.pending = false;
    }
    const UINT gatherW = (w + kEffectGatherPixels - 1) / kEffectGatherPixels;
    const UINT gatherH = (h + kEffectGatherPixels - 1) / kEffectGatherPixels;
    const UINT columns = (gatherW + kEffectReduceTexels - 1) / kEffectReduceTexels;
    const UINT rows = (gatherH + kEffectReduceTexels - 1) / kEffectReduceTexels;
    bool ok = CreateTarget(dev, gatherW, gatherH, D3DFMT_A32B32G32R32F, &m_effectGather[0]) &&
              CreateTarget(dev, gatherW, gatherH, D3DFMT_A32B32G32R32F, &m_effectGather[1]);
    for (EffectReadback& r : m_effectRing)
        ok = ok && CreateTarget(dev, 2 * columns, rows, D3DFMT_A32B32G32R32F, &r.target) &&
             SUCCEEDED(dev->CreateOffscreenPlainSurface(2 * columns, rows, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM,
                                                       &r.copy, nullptr)) &&
             SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_EVENT, &r.done));
    if (!ok)
    {
        ReleaseEffectTargets();
        m_effectsFailed = true;
        VF_LOG_ERROR("effect light unavailable: its targets could not be made (%ux%u)", w, h);
        return false;
    }
    m_effectW = w;
    m_effectH = h;
    m_effectGatherW = gatherW;
    m_effectGatherH = gatherH;
    m_effectColumns = columns;
    m_effectRows = rows;
    VF_LOG_INFO("effect light: %ux%u sums of %dx%d pixels, %ux%u cells of %d pixels read back through a ring of %d",
                gatherW, gatherH, kEffectGatherPixels, kEffectGatherPixels, columns, rows, kEffectCellPixels,
                kEffectReadbackRing);
    return true;
}

bool Renderer::TakeEffectMeasurement()
{
    int newest = -1;
    for (int k = 0; k < kEffectReadbackRing; ++k)
    {
        EffectReadback& r = m_effectRing[k];
        if (!r.pending)
            continue;
        const DWORD flags = m_frame - r.issuedFrame >= kEffectFlushAfterFrames ? D3DGETDATA_FLUSH : 0;
        const HRESULT hr = r.done->GetData(nullptr, 0, flags);
        if (hr == S_FALSE)
            continue;
        r.pending = false;
        if (hr == S_OK && (newest < 0 || r.order > m_effectRing[newest].order))
            newest = k;
    }
    if (newest < 0)
        return false;
    EffectReadback& r = m_effectRing[newest];
    D3DLOCKED_RECT locked = {};
    if (FAILED(r.copy->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
        return false;
    const size_t rowFloats = static_cast<size_t>(2 * m_effectColumns) * 4;
    m_effectMeasurement.frame = r.frame;
    m_effectMeasurement.columns = static_cast<int>(m_effectColumns);
    m_effectMeasurement.rows = static_cast<int>(m_effectRows);
    m_effectMeasurement.texels.resize(rowFloats * m_effectRows);
    for (UINT y = 0; y < m_effectRows; ++y)
        std::memcpy(&m_effectMeasurement.texels[y * rowFloats], static_cast<const unsigned char*>(locked.pBits) +
                                                                       static_cast<size_t>(y) * locked.Pitch,
                    rowFloats * sizeof(float));
    r.copy->UnlockRect();
    const unsigned latency = m_frame - r.issuedFrame;
    for (EffectReport* report : {&m_effectReport, &m_effectLog})
    {
        ++report->measurements;
        report->lastLatencyFrames = latency;
        report->maxLatencyFrames = std::max(report->maxLatencyFrames, latency);
    }
    return true;
}

void Renderer::GatherEffects(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in)
{
    const D3DVIEWPORT9& vp = in.viewport;
    if (!EnsureEffectTargets(dev, vp.Width, vp.Height))
        return;
    EffectReadback* slot = nullptr;
    for (EffectReadback& r : m_effectRing)
        if (!r.pending && !slot)
            slot = &r;
    if (!slot)
    {
        ++m_effectReport.ringFull;
        ++m_effectLog.ringFull;
        return;
    }
    IDirect3DSurface9* second = nullptr;
    if (FAILED(m_effectGather[1]->GetSurfaceLevel(0, &second)))
        return;
    SetTarget(dev, m_effectGather[0]);
    dev->SetRenderTarget(1, second);
    SafeRelease(second);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
    dev->SetPixelShader(m_effectGatherShader);
    const Float4 gather[2] = {
        {kEffectThreshold, kEffectNearestYards, kEffectFarthestYards, kEffectDepthChange},
        {1.0f / vp.Width, 1.0f / vp.Height, 1.0f / (kEffectGatherPixels * kEffectGatherPixels), 0.0f},
    };
    dev->SetPixelShaderConstantF(kEffectPassRegister, &gather[0].x, 2);
    BindTexture(dev, kEffectSceneNowStage, m_sceneCopy, false);
    BindTexture(dev, kEffectSceneBeforeStage, m_effectBaseline, false);
    BindTexture(dev, kEffectDepthBeforeStage, m_occluderDepth, false);
    BindTexture(dev, kEffectDepthNowStage, depthTexture, false);
    DrawFullscreen(dev);
    dev->SetRenderTarget(1, nullptr);

    SetTarget(dev, slot->target);
    dev->SetPixelShader(m_effectReduceShader);
    const Float4 reduce[2] = {
        {static_cast<float>(m_effectColumns), 1.0f / (kEffectReduceTexels * kEffectReduceTexels), 0.0f, 0.0f},
        {static_cast<float>(m_effectGatherW), static_cast<float>(m_effectGatherH), 1.0f / m_effectGatherW,
         1.0f / m_effectGatherH},
    };
    dev->SetPixelShaderConstantF(kEffectPassRegister, &reduce[0].x, 2);
    BindTexture(dev, kEffectLightStage, m_effectGather[0], false);
    BindTexture(dev, kEffectPlaceStage, m_effectGather[1], false);
    DrawFullscreen(dev);

    IDirect3DSurface9* cells = nullptr;
    const bool copied = SUCCEEDED(slot->target->GetSurfaceLevel(0, &cells)) &&
                        SUCCEEDED(dev->GetRenderTargetData(cells, slot->copy));
    SafeRelease(cells);
    if (!copied)
        return;
    slot->done->Issue(D3DISSUE_END);
    slot->pending = true;
    slot->issuedFrame = m_frame;
    slot->order = ++m_effectOrder;
    EffectFrame& f = slot->frame;
    std::memcpy(f.cameraRelativeView, in.cameraRelativeView, sizeof(f.cameraRelativeView));
    std::memcpy(f.glProjection, in.glProjection, sizeof(f.glProjection));
    std::memcpy(f.camPos, in.camPos, sizeof(f.camPos));
    f.viewportWidth = static_cast<float>(vp.Width);
    f.viewportHeight = static_cast<float>(vp.Height);
    f.seconds = in.seconds;
    f.mapId = in.mapId;
    f.weatherIntensity = in.weather == WeatherKind::Clear ? 0.0f : in.weatherIntensity;
    f.falling = in.weather == WeatherKind::Rain || in.weather == WeatherKind::Snow;
    const LanternLamp& lantern = m_lantern.Lamp();
    EffectFramePlayer(in, lantern.pivotMeasured ? lantern.pivotHeight : kDefaultPivotHeight, f);
    f.targetKnown = in.targetKnown;
    if (in.targetKnown)
    {
        std::memcpy(f.targetFeet, in.targetPos, sizeof(f.targetFeet));
        f.targetRingYards = EffectTargetRingYards(in.targetBoundingRadius);
    }
    f.anchorCount = EffectUnitAnchors(in.units, in.unitCount, f.anchors);
    ++m_effectReport.gathers;
    ++m_effectLog.gathers;
}

const std::vector<EffectLamp>* Renderer::UpdateEffects(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                                       const FrameInputs& in, const FogParams& fog, bool gather)
{
    if (!(fog.effectLight > 0.0f) || m_effectsFailed)
    {
        m_effectTracker.Reset();
        for (EffectReadback& r : m_effectRing)
            r.pending = false;
        return nullptr;
    }
    const long long start = Ticks();
    const bool measured = TakeEffectMeasurement() && m_effectMeasurement.frame.mapId == in.mapId;
    std::vector<EffectCluster> clusters;
    if (measured)
    {
        EffectClusterStats stats = {};
        clusters = ClusterEffects(m_effectMeasurement, GlobalLampCatalog().Map(in.mapId), &stats,
                                  &m_objectLamps.Lamps());
        m_effectReport.lastClusters = m_effectLog.lastClusters = stats;
    }
    m_effectTracker.Update(measured ? &clusters : nullptr, in.camPos, in.seconds, in.mapId,
                           measured ? m_effectMeasurement.frame.seconds : -1.0, in.units, in.unitCount);
    if (gather)
        GatherEffects(dev, depthTexture, in);
    const double microseconds = TickSeconds(Ticks() - start) * 1e6;
    for (EffectReport* report : {&m_effectReport, &m_effectLog})
    {
        report->lastCpuMicroseconds = microseconds;
        report->maxCpuMicroseconds = std::max(report->maxCpuMicroseconds, microseconds);
        report->totalCpuMicroseconds += microseconds;
        ++report->cpuFrames;
    }
    return &m_effectTracker.Lights();
}

void Renderer::ReportEffects(EffectReport& out, bool reset)
{
    out = m_effectReport;
    out.available = !m_effectsFailed && m_effectGatherShader != nullptr;
    const std::vector<EffectLamp>& lights = m_effectTracker.Lights();
    out.lights = static_cast<int>(std::min(lights.size(), static_cast<size_t>(kMaxEffectLights)));
    for (int k = 0; k < out.lights; ++k)
        out.lamps[k] = lights[k];
    out.drawn = m_lamps.Stats().effectsDrawn;
    out.columns = static_cast<int>(m_effectColumns);
    out.rows = static_cast<int>(m_effectRows);
    if (reset)
        m_effectReport = {};
}

void Renderer::LogEffects(const FogParams& fog)
{
    const EffectReport& s = m_effectLog;
    if (s.cpuFrames == 0 && !(fog.effectLight > 0.0f))
        return;
    const EffectTrackerStats tracker = m_effectTracker.Stats();
    VF_LOG_INFO("  effects: light %.2f, %d lit, %d tracked (%d carried by a unit, %d moving on their own, last read %d "
                "on units); %u gathers, %u read back %u-%u frames later, %u skipped "
                "with the ring full; %.0f us a frame, at most %.0f; last read: %d cells over %.1f, %d colourless in the "
                "weather, %d cells touched in the weather (field %.1f), %d at lamps, %d behind a lamp's glow, %d at "
                "the target's ring, %d clusters (%d glow sprites, %d at your weapon, %d left out in rain or snow); %u "
                "flashes seen for %.2f-%.2f s, "
                "%u of them at your weapon, %u still seen after %.1f s, %u went on as a bullet",
                fog.effectLight, m_lamps.Stats().effectsDrawn, static_cast<int>(m_effectTracker.Lights().size()),
                tracker.carried, tracker.moving, s.lastClusters.carried, s.gathers, s.measurements, s.lastLatencyFrames, s.maxLatencyFrames, s.ringFull,
                s.cpuFrames ? s.totalCpuMicroseconds / s.cpuFrames : 0.0, s.maxCpuMicroseconds, s.lastClusters.cells,
                s.lastClusters.least, s.lastClusters.colourless, s.lastClusters.fieldCells, s.lastClusters.field,
                s.lastClusters.suppressedCells, s.lastClusters.lampGlowCells, s.lastClusters.ringCells,
                s.lastClusters.clusters, s.lastClusters.sprites, s.lastClusters.atWeapon,
                s.lastClusters.fallingDropped, m_effectTracker.Flashes().flashes, m_effectTracker.Flashes().shortestSeconds,
                m_effectTracker.Flashes().longestSeconds, m_effectTracker.Flashes().weaponFlashes,
                m_effectTracker.Flashes().outlasted, kEffectFlashSeconds, m_effectTracker.Flashes().bullets);
    m_effectLog = {};
    m_effectTracker.ResetFlashes();
    VF_LOG_INFO("  corpses: light %.2f, %d lootable within %.0f yd, %d lit", fog.corpseLight, m_corpses.Lootable(),
                engine::kCorpseScanYards, static_cast<int>(m_corpses.Lamps().size()));
}

void Renderer::LogLamps(const FogParams& fog)
{
    const LampCatalog& catalog = GlobalLampCatalog();
    if (!catalog.Loaded())
        return;
    const LampStats& s = m_lamps.Stats();
    VF_LOG_INFO("  lamps: haze %.2f, light %.2f, night darkness %.2f, %d within %.0f yd, %d in view, %d drawn, "
                "%d far halos; nearest %s at %.0f yd; windows: light %.2f, %d within %.0f yd, %d in view, %d drawn; "
                "lanterns %.2f; far lights %.2f, %d cores",
                fog.lampHaze, fog.lampLight, fog.nightDarkness, s.nearby, kLampSearchYards, s.inView, s.drawn,
                s.farDrawn, s.inView ? catalog.Name(s.nearestName) : "-", s.nearestYards, fog.windowLight,
                s.windowsNearby, kWindowSearchYards, s.windowsInView, s.windowsDrawn, fog.lanternGlow,
                fog.farLightCore, s.cores);
    VF_LOG_INFO("  game object lamps: %d object%s with a lamp's model within %.0f yd, %d lamps, %d left out at a "
                "placed lamp or another object's",
                m_objectLamps.Objects(), m_objectLamps.Objects() == 1 ? "" : "s", engine::kObjectScanYards,
                static_cast<int>(m_objectLamps.Lamps().size()), m_objectLamps.Duplicates());
}

void Renderer::LogLantern(const FrameInputs& in, const Config& cfg, const FogParams& fog)
{
    const LanternLamp& lantern = m_lantern.Lamp();
    if (!lantern.placed)
    {
        VF_LOG_INFO("  lantern: %s, no player position", LanternModeName(cfg.lanternMode));
        return;
    }
    VF_LOG_INFO("  lantern: %s, lit %.2f of %.2f (auto %s), light %.2f, %s; pivot %.2f yd above the feet%s, hook "
                "%.2f yd; swing up to %.2f yd; movement flags 0x%08X",
                LanternModeName(cfg.lanternMode), lantern.weight, lantern.target, lantern.autoLit ? "lit" : "dark",
                fog.lanternLight, m_lamps.Stats().lanternDrawn ? "drawn" : "not drawn", lantern.pivotHeight,
                lantern.pivotMeasured ? "" : " (not measured)", lantern.hookHeight, m_lantern.TakeLargestSwing(),
                in.playerMoveFlags);
}

void Renderer::LogConditionChanges(const FrameInputs& in, const FogParams& fog)
{
    const int weatherKind = static_cast<int>(in.weather);
    const int weatherStep = static_cast<int>(std::lround(fog.weather * kLoggedWeatherSteps));
    if (weatherKind != m_loggedWeatherKind || weatherStep != m_loggedWeatherStep)
    {
        m_loggedWeatherKind = weatherKind;
        m_loggedWeatherStep = weatherStep;
        VF_LOG_INFO("weather: %s %.1f, wind %.1f yd/s", WeatherName(in.weather), weatherStep / kLoggedWeatherSteps,
                    fog.windSpeed);
    }
    const int lightIsMoon = in.lightIsMoon ? 1 : 0;
    if (lightIsMoon != m_loggedLightIsMoon)
    {
        m_loggedLightIsMoon = lightIsMoon;
        VF_LOG_INFO("light: %s at day %.4f (sun elevation %.3f, moon %.3f)", in.lightIsMoon ? "moon" : "sun",
                    in.dayFraction, in.sunElevation, in.moonElevation);
    }
}

bool Renderer::DepthProbeDue(long long now) const
{
    const bool debugLog = LogEnabled(LogLevel::Debug);
    const float probeIntervalSeconds = debugLog ? kProbeDebugSeconds : kProbeSeconds;
    return LogEnabled(LogLevel::Info) && !m_probeFailed && m_frame >= kProbeFirstFrame &&
           (debugLog || m_probeAttempts < kInfoLevelGpuStallingProbeLimit) &&
           (m_probeAttempts == 0 || TickSeconds(now - m_probeTicks) > probeIntervalSeconds);
}

void Renderer::LogDepthProbe(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fog,
                             const D3DVIEWPORT9& vp, float deepestWorldDepth, float dayFraction)
{
    if (m_probeFailed)
        return;
    if ((!m_probe && FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_probe), &m_probe))) ||
        (!m_probeTarget &&
         (!CreateTarget(dev, kProbePoints, 1, D3DFMT_A32B32G32R32F, &m_probeTarget) ||
          FAILED(dev->CreateOffscreenPlainSurface(kProbePoints, 1, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM,
                                                  &m_probeReadback, nullptr)))))
    {
        SafeRelease(m_probeTarget);
        SafeRelease(m_probeReadback);
        m_probeFailed = true;
        VF_LOG_INFO("depth probe unavailable (no probe shader or float render target)");
        return;
    }
    SetTarget(dev, m_probeTarget);
    D3DVIEWPORT9 probeVp = {0, 0, kProbePoints, 1, 0.0f, 1.0f};
    dev->SetViewport(&probeVp);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetPixelShader(m_probe);
    BindTexture(dev, 0, depthTexture, false);
    BindTexture(dev, 1, fog, false);
    DrawFullscreen(dev);

    IDirect3DSurface9* surface = nullptr;
    D3DLOCKED_RECT locked = {};
    bool read = SUCCEEDED(m_probeTarget->GetSurfaceLevel(0, &surface)) &&
                SUCCEEDED(dev->GetRenderTargetData(surface, m_probeReadback)) &&
                SUCCEEDED(m_probeReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    SafeRelease(surface);
    if (!read)
    {
        VF_LOG_INFO("depth probe %u: readback failed", m_probeAttempts);
        return;
    }
    const auto* samples = static_cast<const ProbeSample*>(locked.pBits);
    VF_LOG_INFO("depth probe %u: day %.4f, viewport depth %.4f..%.4f, world depth up to %.7f; per point raw depth / yd "
                "/ fog opacity (w world, f beyond the far clip, s sky)",
                m_probeAttempts, dayFraction, vp.MinZ, vp.MaxZ, deepestWorldDepth);
    for (int row = 0; row < kProbeGridSide; ++row)
    {
        char line[256] = {};
        int used = 0;
        for (int col = 0; col < kProbeGridSide; ++col)
        {
            const ProbeSample& sample = samples[row * kProbeGridSide + col];
            used += std::snprintf(line + used, sizeof(line) - used, "  %.7f/%.0f/%.2f %c", sample.rawDepth,
                                  sample.viewDepthYards, sample.fogOpacity, DepthClassLetter(sample.depthClass));
        }
        VF_LOG_INFO("  row %d:%s", row, line);
    }
    m_probeReadback->UnlockRect();
}

void Renderer::DrawFullscreen(IDirect3DDevice9* dev)
{
    static const float kTriangle[3][4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 3.0f, 0.0f, 1.0f}, {3.0f, -1.0f, 0.0f, 1.0f}};
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, kTriangle, sizeof(kTriangle[0]));
}

void Renderer::BindTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex, bool linear)
{
    DWORD filter = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    dev->SetTexture(stage, tex);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, filter);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, filter);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

bool Renderer::Render(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                      const FrameInputs& in, const Config& cfg, const ClientShadowFrame* clientShadows)
{
    m_skip = "";
    m_sunAndMoonShown = false;
    if (dev->TestCooperativeLevel() != D3D_OK)
        return Skip("device not ready");
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev))
        return false;

    IDirect3DSurface9* saved[4] = {};
    for (DWORD i = 0; i < 4; ++i)
        dev->GetRenderTarget(i, &saved[i]);
    IDirect3DSurface9* savedDepth = nullptr;
    dev->GetDepthStencilSurface(&savedDepth);

    bool ok = false;
    D3DSURFACE_DESC rtDesc = {};
    D3DSURFACE_DESC depthDesc = {};
    if (!saved[0])
        Skip("no render target");
    else if (savedDepth != boundDepthStencil)
        Skip("fog depth surface not bound");
    else if (FAILED(saved[0]->GetDesc(&rtDesc)) || FAILED(boundDepthStencil->GetDesc(&depthDesc)))
        Skip("surface description failed");
    else if (rtDesc.Width != depthDesc.Width || rtDesc.Height != depthDesc.Height)
        Skip("render target and depth sizes differ");
    else if (rtDesc.MultiSampleType != D3DMULTISAMPLE_NONE)
        Skip("multisampled render target");
    else
    {
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT streamOffset = 0;
        UINT streamStride = 0;
        dev->GetStreamSource(0, &stream, &streamOffset, &streamStride);
        m_state->Capture();
        for (DWORD i = 1; i < 4; ++i)
            if (saved[i])
                dev->SetRenderTarget(i, nullptr);
        ok = RenderPasses(dev, depthTexture, saved[0], depthDesc, in, cfg, clientShadows);
        dev->SetRenderTarget(0, saved[0]);
        for (DWORD i = 1; i < 4; ++i)
            if (saved[i])
                dev->SetRenderTarget(i, saved[i]);
        dev->SetDepthStencilSurface(savedDepth);
        m_state->Apply();
        dev->SetStreamSource(0, stream, streamOffset, streamStride);
        SafeRelease(stream);
    }

    for (auto*& s : saved)
        SafeRelease(s);
    SafeRelease(savedDepth);
    return ok;
}

void Renderer::LogClientShadows(const ClientShadowFrame* clientShadows, const float* toLightInView, bool summaryDue)
{
    if (!clientShadows)
    {
        m_clientShadowsLogged = -1;
        return;
    }
    const int found = clientShadows->mapCount > 0 ? 1 : 0;
    if (found != m_clientShadowsLogged || (found && summaryDue))
        LogClientShadowFrame(*clientShadows, toLightInView);
    m_clientShadowsLogged = found;
}

bool Renderer::DrawClientShadowView(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                    IDirect3DSurface9* target, const D3DVIEWPORT9& vp, const Config& cfg,
                                    const ClientShadowFrame* clientShadows)
{
    if (!m_clientShadowShader &&
        FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_client_shadow), &m_clientShadowShader)))
    {
        m_clientShadowShader = nullptr;
        return Skip("client shadow view shader creation failed");
    }
    const ClientShadowPairs pairs = ClientShadowDebugPairs(clientShadows, cfg.debugView);
    if (cfg.debugView != m_loggedClientShadowView || pairs.count != m_loggedClientShadowPairs)
    {
        m_loggedClientShadowView = cfg.debugView;
        m_loggedClientShadowPairs = pairs.count;
        VF_LOG_INFO("client shadow view %d: %d map%s from %s", cfg.debugView, pairs.count, pairs.count == 1 ? "" : "s",
                    pairs.source);
    }
    const RECT world = ViewportRect(vp);
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetScissorRect(&world);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetPixelShader(m_clientShadowShader);
    dev->SetPixelShaderConstantF(kClientShadowPairRegister, &pairs.constants[0][0][0], 4 * kClientShadowPairs);
    const Float4 view = {static_cast<float>(pairs.count), 0.0f, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kClientShadowViewRegister, &view.x, 1);
    BindTexture(dev, 0, depthTexture, false);
    for (int k = 0; k < kClientShadowPairs; ++k)
        BindTexture(dev, kClientShadowFirstMapStage + k, k < pairs.count ? pairs.textures[k] : nullptr, false);
    DrawFullscreen(dev);
    NoteClientShadowDebugPass();
    m_historyValid = false;
    ++m_frame;
    return true;
}

bool Renderer::RenderPasses(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                            const D3DSURFACE_DESC& depthDesc, const FrameInputs& in, const Config& cfg,
                            const ClientShadowFrame* clientShadows)
{
    const D3DVIEWPORT9 vp = in.viewport;
    if (vp.Width < 16 || vp.Height < 16 || vp.X + vp.Width > depthDesc.Width || vp.Y + vp.Height > depthDesc.Height)
        return Skip("world viewport outside the render target");

    const UINT scale = cfg.quality == 1 ? 4u : 2u;
    const UINT lowW = (vp.Width + scale - 1) / scale;
    const UINT lowH = (vp.Height + scale - 1) / scale;
    const UINT rayW = std::max(1u, static_cast<UINT>(vp.Width) / kRayScale);
    const UINT rayH = std::max(1u, static_cast<UINT>(vp.Height) / kRayScale);
    if (!EnsureTargets(dev, lowW, lowH, rayW, rayH))
        return false;

    float viewToWorld[16];
    float worldToView[16];
    if (!WorldViewFromCameraRelative(in, viewToWorld, worldToView))
        return Skip("view matrix not invertible");

    AuthoredFog authored = {};
    const bool hasAuthored =
        cfg.dataMode == 1 && GlobalFogData().Resolve(in.mapId, in.camPos, in.dayFraction, in.lightParams, authored);
    const FogParams fog = BuildFogParams(in, cfg, hasAuthored ? &authored : nullptr);
    LogLightChange(in, authored, hasAuthored);
    LogConditionChanges(in, fog);
    AdvanceDrift(in, fog, cfg);
    const bool driftNoise = EnsureDriftNoise(dev);
    AdvanceClouds(in, fog);
    const bool cloudNoise = cfg.cloudShadows > 0.0f && EnsureCloudNoise(dev);
    const CloudLookup clouds = CloudConstants(in, fog, m_cloudOffset, cloudNoise);
    const bool cloudy = clouds.shape.w > 0.0f;
    if (!EnsureTerrainTexture(dev))
        return false;
    const TerrainShadowConstants terrain = UpdateTerrainShadows(in, cfg);
    const TerrainShadowConstants marchTerrain = cfg.terrainShadows ? terrain : kTerrainShadowsOff;
    float surfaceShade[3];
    SurfaceShadowShade(in, cfg, surfaceShade);
    const bool lowSunLift = std::max({fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2]}) > 0.0f;
    const bool surfaceShadows =
        terrain.uvPerYard > 0.0f &&
        ((cfg.surfaceShadows > 0.0f && std::min({surfaceShade[0], surfaceShade[1], surfaceShade[2]}) < 1.0f) ||
         lowSunLift || cloudy);
    const float* proj = in.glProjection;
    const WorldDepthMapping worldDepth = MapWorldDepth(proj, vp);

    float toLightInView[3];
    TransformDirection(in.toLight, in.cameraRelativeView, toLightInView);
    Normalize3(toLightInView);

    float common[9][4] = {
        {static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width),
         static_cast<float>(vp.Height)},
        {static_cast<float>(scale), static_cast<float>(m_frame % 1024u), 1.0f / depthDesc.Width,
         1.0f / depthDesc.Height},
        {proj[0], proj[5], proj[8], proj[9]},
        {worldDepth.atInfinity, worldDepth.perInverseViewDepth, fog.maxDistance, worldDepth.deepest},
        {},
        {},
        {},
        {},
        {static_cast<float>(lowW), static_cast<float>(lowH), 1.0f / lowW, 1.0f / lowH},
    };
    std::memcpy(common[4], viewToWorld, sizeof(viewToWorld));

    const long long now = Ticks();
    const float dx = in.camPos[0] - m_prevCam[0];
    const float dy = in.camPos[1] - m_prevCam[1];
    const float dz = in.camPos[2] - m_prevCam[2];
    const bool historyValid = m_historyValid && cfg.temporal > 0.0f && m_prevScale == scale &&
                              std::memcmp(&m_prevViewport, &vp, sizeof(vp)) == 0 &&
                              TickSeconds(now - m_prevTicks) < kHistoryMaxSeconds &&
                              dx * dx + dy * dy + dz * dz < kHistoryMaxMove * kHistoryMaxMove;
    float reproj[16];
    float prevViewProj[16];
    Mul4x4(m_prevWorldToView, m_prevProj, prevViewProj);
    Mul4x4(viewToWorld, prevViewProj, reproj);

    float sunPx[2] = {};
    bool sunInFront = toLightInView[2] > 0.05f;
    float sunScreenFade = 0.0f;
    if (sunInFront)
    {
        float ndcX = toLightInView[0] / toLightInView[2] * proj[0] + proj[8];
        float ndcY = toLightInView[1] / toLightInView[2] * proj[5] + proj[9];
        sunPx[0] = vp.X + (ndcX * 0.5f + 0.5f) * vp.Width;
        sunPx[1] = vp.Y + (0.5f - ndcY * 0.5f) * vp.Height;
        sunScreenFade = std::clamp(1.6f - std::max(std::fabs(ndcX), std::fabs(ndcY)), 0.0f, 1.0f);
    }
    const float rayStrength = cfg.godRays * fog.lightVisibility * sunScreenFade;
    const bool rays = rayStrength > 0.005f;

    const bool viewChanged = std::fabs(in.farClip - m_loggedFarClip) > 1.0f || vp.Width != m_loggedViewport.Width ||
                             vp.Height != m_loggedViewport.Height || vp.MinZ != m_loggedViewport.MinZ ||
                             vp.MaxZ != m_loggedViewport.MaxZ;
    const bool summaryDue = m_logged < 1 || viewChanged || TickSeconds(now - m_summaryTicks) > kSummarySeconds ||
                            (LogEnabled(LogLevel::Debug) && m_frame % 600u == 0);
    LogClientShadows(clientShadows, toLightInView, summaryDue);
    if (summaryDue)
    {
        ++m_logged;
        m_summaryTicks = now;
        m_loggedFarClip = in.farClip;
        m_loggedViewport = vp;
        float projectionNear = -proj[14] / (1.0f + proj[10]);
        float projectionFar = proj[14] / (1.0f - proj[10]);
        VF_LOG_INFO("frame %u: viewport %lu,%lu %lux%lu of %ux%u depth %.4f..%.4f; near %.3f far %.1f (farclip %.1f) "
                    "maxdist %.0f",
                    m_frame, vp.X, vp.Y, vp.Width, vp.Height, depthDesc.Width, depthDesc.Height, vp.MinZ, vp.MaxZ,
                    projectionNear, projectionFar, in.farClip, fog.maxDistance);
        VF_LOG_INFO("  camera (%.2f %.2f %.2f) inverse-view origin (%.2f %.2f %.2f) target (%.2f %.2f %.2f)",
                    in.camPos[0], in.camPos[1], in.camPos[2], viewToWorld[12], viewToWorld[13], viewToWorld[14],
                    in.camTarget[0], in.camTarget[1], in.camTarget[2]);
        VF_LOG_INFO("  day %.4f %s toLight (%.3f %.3f %.3f) view (%.3f %.3f %.3f) vis %.2f above %.2f "
                    "sunPx (%.0f %.0f) rays %.2f",
                    in.dayFraction, in.lightIsMoon ? "moon" : "sun", in.toLight[0], in.toLight[1], in.toLight[2],
                    toLightInView[0], toLightInView[1], toLightInView[2], fog.lightVisibility, fog.lightAboveHorizon,
                    sunPx[0], sunPx[1], rayStrength);
        VF_LOG_INFO("  map %d fog %08X start %.1f end %.1f zone %.1f sun %08X direct %08X ambient %08X refZ %.1f "
                    "glow %.2f, direct light vs Classic %.2f",
                    in.mapId, in.fogColor, in.fogStart, in.fogEnd, in.zoneFogDistance, in.sunColor, in.directColor,
                    in.ambientColor, fog.referenceZ, in.clientGlowAmount, fog.directLightMatch);
        VF_LOG_INFO("  sky horizon %08X above %08X, overcast %.2f, kept at night %.2f, shown %.3f; the haze before "
                    "the land takes %.2f of it at %.2f: %.4f %.4f %.4f%s",
                    in.skyHorizonColor, in.skyAboveHorizonColor, fog.overcastShare, fog.hazeSkyNightKept,
                    fog.hazeSkyShown, fog.hazeFromSkyShare, cfg.hazeSkyLevel, fog.hazeFromSky[0], fog.hazeFromSky[1],
                    fog.hazeFromSky[2],
                    SkyReferenceFresh(in) ? "; before land the sky as drawn is used instead" : "");
        for (int i = 0; i < kFogLayers; ++i)
        {
            const FogLayer& l = fog.layers[i];
            VF_LOG_INFO("  layer %d (%s): start %.0f density %.6f curve %.2f^%.2f g %.2f diffuse %.2f %.2f %.2f "
                        "emissive %.2f %.2f %.2f upper %.1f/%.4f lower %.1f/%.4f shadowed %.0f limit %.0f",
                        i, fog.authored && i < kSceneLayers ? "classic" : "derived", l.start, l.density, l.strength,
                        l.exponent, l.g, l.diffuse[0], l.diffuse[1], l.diffuse[2], l.emissive[0], l.emissive[1],
                        l.emissive[2], l.upperHeight, l.upperFalloff, l.lowerHeight, l.lowerFalloff, l.shadowed,
                        std::min(l.endDistance, 99999.0f));
        }
        VF_LOG_INFO("  %s: weather %s %.2f, wind %.1f yd/s, drift %.2f, mist %.2f, evening haze %.2f, twilight %.2f, "
                    "light glow %.2f; sun elevation %.3f, moon %.3f",
                    in.lightIsMoon ? "moon" : "sun", WeatherName(in.weather), fog.weather, fog.windSpeed, fog.drift,
                    fog.mistBoost, fog.eveningHaze, fog.twilight, fog.lightGlow, in.sunElevation, in.moonElevation);
        LogTerrainShadows(in, cfg);
        LogLamps(fog);
        LogLantern(in, cfg, fog);
        LogEffects(fog);
        LogWater(in, cfg);
        LogWetSurfaces(cfg);
        LogShaftMemory(cfg);
    }

    dev->SetDepthStencilSurface(nullptr);
    dev->SetVertexShader(m_vs);
    dev->SetVertexDeclaration(m_decl);
    dev->SetStreamSourceFreq(0, 1);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetPixelShaderConstantF(0, &common[0][0], 9);
    if (cfg.debugView >= kDebugViewClientShadowReceivers)
        return DrawClientShadowView(dev, depthTexture, target, vp, cfg, clientShadows);

    const bool shadowed =
        cfg.lightShafts && (fog.authored ? fog.lightAboveHorizon : fog.lightVisibility) > 0.001f;
    m_profile.Begin(dev, ProfileSection::ShaftMemory);
    const CarrierBody shaftBody = ShaftBodyInView(in, m_lantern.Lamp().pivotHeight);
    const bool memory = shadowed && cfg.shaftMemory &&
                        UpdateShaftMemory(dev, depthTexture, depthDesc, in, &common[0][0], shaftBody);
    m_profile.End(dev, ProfileSection::ShaftMemory);
    m_profile.Begin(dev, ProfileSection::March);
    if (shadowed && cfg.shaftRange > 0.0f && !memory)
        RenderEdgeOccluders(dev, depthTexture, vp, shaftBody);
    SetTarget(dev, m_marchTarget);
    const int quality = std::clamp(cfg.quality, 1, 3) - 1;
    dev->SetPixelShader(memory ? m_marchMemory[quality] : m_march[quality]);
    const Float4 march[3] = {
        {toLightInView[0], toLightInView[1], toLightInView[2], fog.shadowedLayerLightScale},
        {kShadowMinStep, kShadowStepPerYard, shadowed ? 1.0f : 0.0f, kShadowThicknessSteps},
        {1.0f, 1.0f / std::max(fog.maxDistance, 1.0f), fog.horizonStart, fog.farClip},
    };
    dev->SetPixelShaderConstantF(9, &march[0].x, 3);
    static_assert(sizeof(FogLayer) == 6 * sizeof(Float4), "FogLayer is six shader registers");
    FogLayer marchLayers[kFogLayers];
    std::memcpy(marchLayers, fog.layers, sizeof(marchLayers));
    if (!driftNoise)
        for (FogLayer& l : marchLayers)
            l.bankShare = 0.0f;
    dev->SetPixelShaderConstantF(12, &marchLayers[0].start, 6 * kFogLayers);
    const float shaftRange = shadowed ? cfg.shaftRange : 0.0f;
    const Float4 shafts[2] = {
        {fog.shaftStrength, fog.shaftFocus, shaftRange, cfg.shaftDepth},
        {fog.airShaftScatter[0], fog.airShaftScatter[1], fog.airShaftScatter[2], cfg.shaftLength},
    };
    static_assert(kShaftConstantsRegister == 12 + 6 * kFogLayers, "the shaft constants follow the fog layers");
    static_assert(kShaftConstantsRegister + 2 == kDriftConstantsRegister, "the drift constants follow the shafts");
    static_assert(kDriftConstantsRegister + kDriftConstantCount == kTerrainShadowRegister,
                  "the terrain shadow constants follow the drift constants");
    static_assert(kTerrainShadowRegister + 1 == kShaftFadeRegister && kShaftFadeRegister + 1 == kShaftMemoryRegister &&
                      kShaftMemoryRegister + kShaftMemoryConstants == kShaftMemoryBeforeRegister &&
                      kShaftMemoryBeforeRegister + kShaftMemoryConstants == kCloudRegister,
                  "the shaft memory constants, then the map before the last realignment's");
    static_assert(kCloudRegister + kCloudConstants == kShaftBodyRegister && kCloudStage < kStages &&
                      kCloudStage > kShaftMemoryBeforeStage,
                  "the cloud constants follow the map before, and the state block covers their stage");
    static_assert(kShaftBodyRegister + kShaftBodyConstants == kHazeFromSkyRegister &&
                      sizeof(BodyConstants) == kShaftBodyConstants * sizeof(Float4),
                  "the haze's colour from the sky follows the body left out of the shafts");
    static_assert(kShaftMemoryBeforeStage < kStages, "the state block covers the map before the last realignment");
    dev->SetPixelShaderConstantF(kShaftConstantsRegister, &shafts[0].x, 2);
    const DriftConstants drift = BuildDriftConstants(in, fog, cfg, m_coarseDrift, m_fineDrift, driftNoise);
    dev->SetPixelShaderConstantF(kDriftConstantsRegister, &drift.coarse.x, kDriftConstantCount);
    const Float4 terrainAndFade[2] = {
        {marchTerrain.uvAtCamera[0], marchTerrain.uvAtCamera[1], marchTerrain.uvPerYard, marchTerrain.sharpness},
        ShaftFadeConstants(shaftRange, cfg.shaftDepth, cfg.shaftGlare),
    };
    dev->SetPixelShaderConstantF(kTerrainShadowRegister, &terrainAndFade[0].x, 2);
    dev->SetPixelShaderConstantF(kCloudRegister, &clouds.map.x, kCloudConstants);
    const BodyConstants marchBody = BodyConstantsOf(shaftBody);
    dev->SetPixelShaderConstantF(kShaftBodyRegister, &marchBody.base.x, kShaftBodyConstants);
    const Float4 hazeFromSky = {fog.hazeFromSky[0], fog.hazeFromSky[1], fog.hazeFromSky[2], fog.hazeFromSkyShare};
    dev->SetPixelShaderConstantF(kHazeFromSkyRegister, &hazeFromSky.x, 1);
    const bool measureSky = fog.linear && fog.hazeFromSkyShare > 0.0f && cfg.debugView == 0;
    const bool skyReference = measureSky && SkyReferenceFresh(in);
    const Float4 skyReferenceUsed = {skyReference ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    static_assert(kSkyReferenceRegister == kHazeFromSkyRegister + 1 && kSkyReferenceRegister + 1 == kBankRegister &&
                      kSkyReferenceStage > kCloudStage && kSkyReferenceStage < kStages,
                  "the sky reference follows the haze's colour from the sky, inside the state block");
    dev->SetPixelShaderConstantF(kSkyReferenceRegister, &skyReferenceUsed.x, 1);
    BindTexture(dev, kSkyReferenceStage, skyReference ? m_skyReference[m_skyReferenceIndex] : nullptr,
                m_skyReferenceFilterable);
    const bool banks = driftNoise && fog.bankStrength > 0.0f;
    const bool bankFloors =
        (banks || cfg.fogCeiling > 0.0f) && UploadBankFloors(dev) && m_terrain.FloorsKnown();
    BankConstants bankConstants = BuildBankConstants(in, fog, terrain, bankFloors);
    if (!banks)
        bankConstants.misc.y = 0.0f;
    const FogCeilingConstants ceiling = UpdateFogCeiling(in, cfg, bankFloors);
    bankConstants.noise.y = ceiling.perYard;
    bankConstants.noise.z = ceiling.strength;
    bankConstants.noise.w = ceiling.groundAtCamera;
    static_assert(kBankRegister + kBankConstants == kPixelConstants && kBankFloorStage > kSkyReferenceStage &&
                      kBankFloorStage < kStages,
                  "the bank constants and floor map follow the sky reference, inside the state block");
    dev->SetPixelShaderConstantF(kBankRegister, &bankConstants.floor.x, kBankConstants);
    BindTexture(dev, kBankFloorStage, bankFloors ? m_bankFloorTexture : nullptr, m_bankFloorFilterable);
    if (cloudy)
        BindCloudNoise(dev, kCloudStage);
    else
        dev->SetTexture(kCloudStage, nullptr);
    BindTexture(dev, 0, depthTexture, false);
    if (driftNoise)
        BindDriftNoise(dev, kDriftNoiseStage);
    else
        dev->SetTexture(kDriftNoiseStage, nullptr);
    BindTexture(dev, kTerrainShadowStage, m_terrainTexture, m_terrainFilterable);
    if (memory)
    {
        dev->SetPixelShaderConstantF(kShaftMemoryRegister, &m_memoryLookup[0][0], kShaftMemoryConstants);
        dev->SetPixelShaderConstantF(kShaftMemoryBeforeRegister, &m_memoryBeforeLookup[0][0], kShaftMemoryConstants);
        const DWORD stages[2] = {kEdgeOccluderStage, kShaftMemoryBeforeStage};
        IDirect3DTexture9* const maps[2] = {m_memory[m_memoryIndex], m_memoryBefore};
        for (int k = 0; k < 2; ++k)
        {
            const DWORD stage = stages[k];
            dev->SetTexture(stage, maps[k]);
            dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
            dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
        }
    }
    else
        BindTexture(dev, kEdgeOccluderStage, m_edgeOccluders, true);
    DrawFullscreen(dev);

    const int write = m_historyIndex ^ 1;
    SetTarget(dev, m_history[write]);
    dev->SetPixelShader(m_temporal);
    const Float4 temporal = {cfg.temporal, historyValid ? 1.0f : 0.0f, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(9, reproj, 4);
    dev->SetPixelShaderConstantF(13, &temporal.x, 1);
    BindTexture(dev, 0, m_marchTarget, false);
    BindTexture(dev, 1, m_history[m_historyIndex], m_fogFilterable);
    BindTexture(dev, 2, depthTexture, false);
    DrawFullscreen(dev);

    m_profile.End(dev, ProfileSection::March);
    m_profile.Begin(dev, ProfileSection::Lamps);
    const long long copyStart = FrameProfile::Now();
    const bool effectScene =
        fog.effectLight > 0.0f && m_effectBaselineFresh && !m_effectsFailed && CopyWorldViewport(dev, target, vp);
    m_profile.AddCpuTicks(ProfileSection::WorldCopy, FrameProfile::Now() - copyStart);
    RenderLamps(dev, depthTexture, m_history[write], in, cfg, fog, terrain, effectScene);
    m_profile.End(dev, ProfileSection::Lamps);
    m_profile.Begin(dev, ProfileSection::Composite);

    if (rays)
    {
        IDirect3DSurface9* raySurface = nullptr;
        const RECT world = ViewportRect(vp);
        if (SUCCEEDED(m_rays[0]->GetSurfaceLevel(0, &raySurface)))
        {
            dev->StretchRect(target, &world, raySurface, nullptr, D3DTEXF_LINEAR);
            raySurface->Release();
        }
        SetTarget(dev, m_rays[1]);
        dev->SetPixelShader(m_rayMask);
        const Float4 mask[2] = {
            {toLightInView[0], toLightInView[1], toLightInView[2], kRayFalloff},
            {static_cast<float>(vp.Width) / rayW, kRayThreshold, 1.0f / rayW, 1.0f / rayH},
        };
        dev->SetPixelShaderConstantF(9, &mask[0].x, 2);
        BindTexture(dev, 0, depthTexture, false);
        BindTexture(dev, 1, m_rays[0], true);
        DrawFullscreen(dev);

        float norm = 0.0f;
        for (int k = 0; k < kRayTaps; ++k)
            norm += std::pow(kRayDecay, static_cast<float>(k));
        const float sunUv[2] = {(sunPx[0] - vp.X) / vp.Width, (sunPx[1] - vp.Y) / vp.Height};
        dev->SetPixelShader(m_rayBlur);
        float step = kRayStep;
        for (int pass = 0; pass < 2; ++pass)
        {
            SetTarget(dev, m_rays[pass == 0 ? 0 : 1]);
            const Float4 blur[2] = {
                {sunUv[0], sunUv[1], step, 1.0f / norm},
                {static_cast<float>(rayW), static_cast<float>(rayH), 1.0f / rayW, 1.0f / rayH},
            };
            dev->SetPixelShaderConstantF(9, &blur[0].x, 2);
            BindTexture(dev, 0, m_rays[pass == 0 ? 1 : 0], true);
            DrawFullscreen(dev);
            step /= kRayTaps;
        }
    }

    const bool lightGlow = fog.lightGlow > kMinLightGlow && RenderLightGlow(dev, depthTexture, target, vp, cfg);

    FogBlend blend = FogBlend::GammaFixedFunction;
    if (fog.linear)
        blend = effectScene || CopyWorldViewport(dev, target, vp) ? FogBlend::LinearOverSceneCopy
                                                                   : FogBlend::LinearFixedFunction;
    const bool sceneBlend = blend == FogBlend::LinearOverSceneCopy;
    const float blendMode = static_cast<float>(blend);
    if (blendMode != m_loggedBlendMode)
    {
        m_loggedBlendMode = blendMode;
        VF_LOG_INFO("fog blend: %s", FogBlendName(blend));
    }

    m_profile.Begin(dev, ProfileSection::WetSurfaces);
    m_wetDrawn = RenderWetSurfaces(dev, depthTexture, m_history[write], in, cfg, fog, terrain, &clouds.map.x,
                                   &clouds.shape.x, fog.authored ? cfg.classicExposure : cfg.exposure, sceneBlend);
    m_profile.End(dev, ProfileSection::WetSurfaces);

    const RECT world = ViewportRect(vp);
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetScissorRect(&world);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, sceneBlend ? FALSE : TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetPixelShader(m_composite);
    const Float4 composite[4] = {
        {fog.authored ? cfg.classicExposure : cfg.exposure, rays ? rayStrength : 0.0f,
         static_cast<float>(cfg.debugView), blendMode},
        {fog.rayColor[0], fog.rayColor[1], fog.rayColor[2], 0.0f},
        {sunPx[0], sunPx[1], cfg.sunMarker && sunInFront ? 1.0f : 0.0f,
         cfg.glowCompensation && sceneBlend ? in.clientGlowAmount : 0.0f},
        {lightGlow ? fog.lightGlow * kLightGlowGain : 0.0f, kLightGlowCeiling, fog.hazeFromSkyShare, 0.0f},
    };
    static_assert(kCompositeGlowRegister == 9 + 3, "the light glow constant follows the composite constants");
    dev->SetPixelShaderConstantF(9, &composite[0].x, 4);
    const Float4 surface[4] = {
        {terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness},
        {surfaceShade[0], surfaceShade[1], surfaceShade[2], surfaceShadows ? 1.0f : 0.0f},
        {static_cast<float>(TerrainShadowMap::kSide), TerrainShadowMap::kExtentYards / kSurfaceShadowMapFadeYards,
         -kSurfaceShadowMapMarginYards / kSurfaceShadowMapFadeYards, TerrainShadowMap::kUncoveredGain},
        {TerrainShadowMap::kOutwardLookupCells,
         terrain.sharpness > 0.0f ? -1.0f / (terrain.sharpness * TerrainShadowMap::kCellYards) : 0.0f, 0.0f, 0.0f},
    };
    static_assert(kCompositeSurfaceShadowRegister == kCompositeGlowRegister + 1,
                  "the surface shadow constants follow the light glow constant");
    dev->SetPixelShaderConstantF(kCompositeSurfaceShadowRegister, &surface[0].x, 4);
    const bool surfaceBefore = sceneBlend && m_surfaceBeforeLateDraws;
    const Float4 lamps = {m_lampsDrawn ? 1.0f : 0.0f, SelfLitShare(in), surfaceBefore ? 1.0f : 0.0f,
                          m_wetDrawn ? 1.0f : 0.0f};
    static_assert(kCompositeLampRegister == kCompositeSurfaceShadowRegister + 4,
                  "the lamp constant follows the surface shadow constants");
    static_assert(kCompositeLampRegister < kPixelConstants, "the state block covers the lamp constant");
    dev->SetPixelShaderConstantF(kCompositeLampRegister, &lamps.x, 1);
    float albedoPerLight[3];
    AlbedoPerSceneLight(in, albedoPerLight);
    float sceneLight[3];
    SceneLight(in, sceneLight);
    for (float& light : sceneLight)
        light = std::sqrt(light);
    const SunFacing facing = DaylightFacing(in);
    const Float4 lampLight[3] = {
        {m_lampSurfacesDrawn ? 1.0f : 0.0f, kFixedFunctionAlbedo, kAlbedoCap, fog.nightDarkness},
        {albedoPerLight[0], albedoPerLight[1], albedoPerLight[2], m_lampSurfacesDrawn ? facing.atZero : 1.0f},
        {sceneLight[0], sceneLight[1], sceneLight[2], m_lampSurfacesDrawn ? facing.perFacing : 0.0f},
    };
    static_assert(kCompositeLampSurfaceRegister == kCompositeLampRegister + 1,
                  "the lamp light constants follow the lamp constant");
    static_assert(kCompositeLampSurfaceRegister + 2 < kPixelConstants, "the state block covers the lamp light");
    dev->SetPixelShaderConstantF(kCompositeLampSurfaceRegister, &lampLight[0].x, 3);
    const Float4 overcast = {fog.overcastSky[0], fog.overcastSky[1], fog.overcastSky[2], fog.overcastShare};
    static_assert(kCompositeOvercastRegister == kCompositeLampSurfaceRegister + 3 &&
                      kCompositeOvercastRegister < kPixelConstants,
                  "the overcast sky follows the lamp light constants inside the state block");
    dev->SetPixelShaderConstantF(kCompositeOvercastRegister, &overcast.x, 1);
    const Float4 lowSun[2] = {
        {fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2], 0.0f},
        {in.toLight[0], in.toLight[1], in.toLight[2], 0.0f},
    };
    static_assert(kCompositeLowSunRegister == kCompositeOvercastRegister + 1 &&
                      kCompositeLowSunRegister + 1 < kPixelConstants,
                  "the low sun's lift and direction follow the overcast sky inside the state block");
    dev->SetPixelShaderConstantF(kCompositeLowSunRegister, &lowSun[0].x, 2);
    float cloudShade[3];
    CloudShadowShade(in, cloudShade);
    const Float4 cloudComposite[kCompositeCloudConstants] = {
        clouds.map,
        clouds.shape,
        {cloudShade[0], cloudShade[1], cloudShade[2], 0.0f},
    };
    static_assert(kCompositeCloudRegister == kCompositeLowSunRegister + 2 &&
                      kCompositeCloudRegister + kCompositeCloudConstants <= kPixelConstants &&
                      kCompositeCloudStage < kStages,
                  "the clouds follow the low sun inside the state block");
    dev->SetPixelShaderConstantF(kCompositeCloudRegister, &cloudComposite[0].x, kCompositeCloudConstants);
    bool balanced = false;
    for (int i = 0; i < 3; ++i)
        balanced = balanced || fog.whiteBalance[i] != 1.0f || fog.skyWhiteBalance[i] != 1.0f;
    const bool graded = fog.saturation != 1.0f || fog.skySaturation != 1.0f || balanced;
    const Float4 grade ={fog.saturation, fog.skySaturation, graded ? 1.0f : 0.0f, fog.skyHazeGlowGrade};
    static_assert(kCompositeGradeRegister == kCompositeCloudRegister + kCompositeCloudConstants &&
                      kCompositeGradeRegister < kPixelConstants,
                  "the colour grade follows the clouds inside the state block");
    dev->SetPixelShaderConstantF(kCompositeGradeRegister, &grade.x, 1);
    m_lateDrawsInFront = sceneBlend && fog.lateDrawsInFront > 0.0f;
    const Float4 balance = {fog.lightBalance[0], fog.lightBalance[1], fog.lightBalance[2], fog.lateDrawsInFront};
    static_assert(kCompositeBalanceRegister == kCompositeGradeRegister + 1 &&
                      kCompositeBalanceRegister < kPixelConstants,
                  "the light balance follows the colour grade inside the state block");
    dev->SetPixelShaderConstantF(kCompositeBalanceRegister, &balance.x, 1);
    const Float4 skyGlow = {fog.skyGlow[0], fog.skyGlow[1], fog.skyGlow[2], kLateDrawYards};
    static_assert(kCompositeSkyGlowRegister == kCompositeBalanceRegister + 1 &&
                      kCompositeSkyGlowRegister < kPixelConstants,
                  "the sky haze's glow follows the light balance inside the state block");
    dev->SetPixelShaderConstantF(kCompositeSkyGlowRegister, &skyGlow.x, 1);
    const Float4 whiteBalance[kCompositeWhiteBalanceConstants] = {
        {fog.whiteBalance[0], fog.whiteBalance[1], fog.whiteBalance[2], 0.0f},
        {fog.skyWhiteBalance[0], fog.skyWhiteBalance[1], fog.skyWhiteBalance[2], fog.skyHazeZenith},
    };
    static_assert(kCompositeWhiteBalanceRegister == kCompositeSkyGlowRegister + 1 &&
                      kCompositeWhiteBalanceRegister + kCompositeWhiteBalanceConstants <= kPixelConstants,
                  "the white balance follows the sky haze's glow inside the state block");
    dev->SetPixelShaderConstantF(kCompositeWhiteBalanceRegister, &whiteBalance[0].x, kCompositeWhiteBalanceConstants);
    m_sunAndMoonShown = sceneBlend && fog.sunAndMoon > 0.0f;
    IDirect3DTexture9* sunAndMoon = m_sunAndMoonShown ? m_sunAndMoonDrawn : nullptr;
    D3DSURFACE_DESC sunAndMoonSize = {};
    if (sunAndMoon && FAILED(sunAndMoon->GetLevelDesc(0, &sunAndMoonSize)))
        sunAndMoon = nullptr;
    const Float4 sunAndMoonShown = {
        sunAndMoon ? fog.sunAndMoon : 0.0f,
        sunAndMoon ? 1.0f / static_cast<float>(sunAndMoonSize.Width) : 0.0f,
        sunAndMoon ? 1.0f / static_cast<float>(sunAndMoonSize.Height) : 0.0f,
        0.0f,
    };
    static_assert(kCompositeSunAndMoonRegister == kCompositeWhiteBalanceRegister + kCompositeWhiteBalanceConstants &&
                      kCompositeSunAndMoonRegister < kPixelConstants && kCompositeSunAndMoonStage < kStages &&
                      kCompositeSunAndMoonStage > kCompositeDepthBeforeStage,
                  "the sun and moon follow the white balance inside the state block");
    dev->SetPixelShaderConstantF(kCompositeSunAndMoonRegister, &sunAndMoonShown.x, 1);
    BindTexture(dev, 0, depthTexture, false);
    BindTexture(dev, 1, m_history[write], false);
    BindTexture(dev, 2, m_rays[1], true);
    BindTexture(dev, 3, sceneBlend ? m_sceneCopy : nullptr, false);
    BindTexture(dev, kLightGlowStage, lightGlow ? m_glow[0] : nullptr, true);
    BindTexture(dev, kCompositeTerrainShadowStage, surfaceShadows ? m_terrainTexture : nullptr, false);
    BindTexture(dev, kCompositeLampHaloStage, m_lampsDrawn ? m_lampHalos : nullptr, false);
    BindTexture(dev, kCompositeLampSurfaceStage, m_lampSurfacesDrawn ? m_lampSurfaces : nullptr, false);
    static_assert(kCompositeDepthBeforeStage < kStages && kCompositeSurfaceBeforeStage > kCompositeCloudStage,
                  "the state block covers the surface before the late draws");
    BindTexture(dev, kCompositeSurfaceBeforeStage, surfaceBefore ? m_effectBaseline : nullptr, false);
    BindTexture(dev, kCompositeDepthBeforeStage, surfaceBefore ? m_occluderDepth : nullptr, false);
    BindTexture(dev, kCompositeSunAndMoonStage, sunAndMoon, false);
    static_assert(kCompositeWetStage < kStages && kCompositeWetStage > kCompositeSunAndMoonStage,
                  "the state block covers the wet surfaces");
    BindTexture(dev, kCompositeWetStage, m_wetDrawn ? m_wetTarget : nullptr, false);
    if (cloudy && surfaceShadows)
        BindCloudNoise(dev, kCompositeCloudStage);
    else
        dev->SetTexture(kCompositeCloudStage, nullptr);
    DrawFullscreen(dev);
    if (measureSky)
        MeasureSky(dev, depthTexture, target, in, cfg, fog);
    else
        m_skyReferenceValid = false;
    m_profile.End(dev, ProfileSection::Composite);

    if (DepthProbeDue(now))
    {
        m_probeTicks = now;
        ++m_probeAttempts;
        LogDepthProbe(dev, depthTexture, m_history[write], vp, worldDepth.deepest, in.dayFraction);
    }

    std::memcpy(m_prevWorldToView, worldToView, sizeof(m_prevWorldToView));
    std::memcpy(m_prevProj, in.glProjection, sizeof(m_prevProj));
    std::memcpy(m_prevCam, in.camPos, sizeof(m_prevCam));
    m_prevViewport = vp;
    m_prevScale = scale;
    m_prevTicks = now;
    m_historyIndex = write;
    m_historyValid = true;
    ++m_frame;
    return true;
}

template <typename Passes>
bool Renderer::WithEngineStateSaved(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepthStencil, Passes passes)
{
    IDirect3DSurface9* saved[4] = {};
    for (DWORD i = 0; i < 4; ++i)
        dev->GetRenderTarget(i, &saved[i]);
    IDirect3DSurface9* savedDepth = nullptr;
    dev->GetDepthStencilSurface(&savedDepth);

    bool ok = false;
    D3DSURFACE_DESC rtDesc = {};
    D3DSURFACE_DESC depthDesc = {};
    if (!saved[0])
        Skip("no render target");
    else if (savedDepth != boundDepthStencil)
        Skip("fog depth surface not bound");
    else if (FAILED(saved[0]->GetDesc(&rtDesc)) || FAILED(boundDepthStencil->GetDesc(&depthDesc)))
        Skip("surface description failed");
    else if (rtDesc.Width != depthDesc.Width || rtDesc.Height != depthDesc.Height)
        Skip("render target and depth sizes differ");
    else if (rtDesc.MultiSampleType != D3DMULTISAMPLE_NONE)
        Skip("multisampled render target");
    else
    {
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT streamOffset = 0;
        UINT streamStride = 0;
        dev->GetStreamSource(0, &stream, &streamOffset, &streamStride);
        m_state->Capture();
        for (DWORD i = 1; i < 4; ++i)
            if (saved[i])
                dev->SetRenderTarget(i, nullptr);
        ok = passes(saved[0], depthDesc);
        dev->SetRenderTarget(0, saved[0]);
        for (DWORD i = 1; i < 4; ++i)
            if (saved[i])
                dev->SetRenderTarget(i, saved[i]);
        dev->SetDepthStencilSurface(savedDepth);
        m_state->Apply();
        dev->SetStreamSource(0, stream, streamOffset, streamStride);
        SafeRelease(stream);
    }

    for (auto*& s : saved)
        SafeRelease(s);
    SafeRelease(savedDepth);
    return ok;
}

void Renderer::NoteLiquidInstance(bool magma)
{
    if (magma)
        ++m_magmaInstances;
    else
        ++m_waterInstances;
}

bool Renderer::WaterResult(bool ok)
{
    if (!ok && m_skip != m_waterLastSkip && m_waterSkipsLogged < kMaxWaterSkipsLogged)
    {
        ++m_waterSkipsLogged;
        VF_LOG_INFO("water skipped: %s", m_skip);
    }
    m_waterLastSkip = ok ? "" : m_skip;
    return ok;
}

bool Renderer::EnsureWaterShaders(IDirect3DDevice9* dev)
{
    if (m_waterShader)
        return true;
    if (SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_depth), &m_waterDepthShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_reflect), &m_waterReflectShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_waves), &m_waterWaveShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_slope), &m_waterSlopeShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_memory), &m_waterMemoryShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water_lamps), &m_waterLampShader)) &&
        SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_water), &m_waterShader)))
        return true;
    SafeRelease(m_waterDepthShader);
    SafeRelease(m_waterReflectShader);
    SafeRelease(m_waterWaveShader);
    SafeRelease(m_waterSlopeShader);
    SafeRelease(m_waterMemoryShader);
    SafeRelease(m_waterLampShader);
    SafeRelease(m_waterShader);
    m_waterFailed = true;
    VF_LOG_ERROR("water shader creation failed; the water stays as the client draws it");
    return Skip("water shader creation failed");
}

bool Renderer::EnsureWaterWaves(IDirect3DDevice9* dev)
{
    if (m_waterWaves[0])
        return true;
    D3DCAPS9 caps = {};
    const bool targets = SUCCEEDED(dev->GetDeviceCaps(&caps)) && caps.NumSimultaneousRTs >= kWaterWaveLayers;
    const bool filterable = TextureFilterable(dev, D3DFMT_A16B16G16R16F);
    bool ok = targets && filterable &&
              SUCCEEDED(dev->CreateTexture(kWaterWavesPerLayer, kWaterWaveLayers, 1, D3DUSAGE_DYNAMIC,
                                           D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &m_waterWaveTable, nullptr));
    for (IDirect3DTexture9*& waves : m_waterWaves)
        ok = ok && SUCCEEDED(dev->CreateTexture(kWaterWaveTexels, kWaterWaveTexels, kWaterWaveLevels,
                                                D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT,
                                                &waves, nullptr));
    if (!ok)
    {
        SafeRelease(m_waterWaveTable);
        for (IDirect3DTexture9*& waves : m_waterWaves)
            SafeRelease(waves);
        m_waterFailed = true;
        VF_LOG_ERROR("water wave textures unavailable (%lu render targets at once, fp16 %sfilterable); the water stays "
                     "as the client draws it",
                     caps.NumSimultaneousRTs, filterable ? "" : "not ");
        return Skip("water wave textures unavailable");
    }
    const bool anisotropic = (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0;
    m_waterAnisotropy = anisotropic ? std::min<DWORD>(kWaterMaxAnisotropy, std::max<DWORD>(caps.MaxAnisotropy, 1)) : 1;
    VF_LOG_INFO("water waves: %d layers of %d waves, %dx%d fp16 with %d levels each, anisotropy %lu", kWaterWaveLayers,
                kWaterWavesPerLayer, kWaterWaveTexels, kWaterWaveTexels, kWaterWaveLevels, m_waterAnisotropy);
    return true;
}

bool Renderer::DrawWaterWaves(IDirect3DDevice9* dev, const WaterWaveField& field)
{
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_waterWaveTable->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
        return Skip("water wave table lock failed");
    for (int layer = 0; layer < kWaterWaveLayers; ++layer)
    {
        float* row = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + layer * locked.Pitch);
        for (int i = 0; i < kWaterWavesPerLayer; ++i)
        {
            const WaterWave& w = field.waves[layer][i];
            row[4 * i + 0] = w.cyclesX;
            row[4 * i + 1] = w.cyclesY;
            row[4 * i + 2] = w.slope;
            row[4 * i + 3] = w.phase;
        }
    }
    m_waterWaveTable->UnlockRect(0);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetPixelShader(m_waterWaveShader);
    BindTexture(dev, kWaterWaveTableStage, m_waterWaveTable, false);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE2, 0xF);
    bool ok = true;
    for (int level = 0; level < kWaterWaveLevels && ok; ++level)
    {
        for (int layer = 0; layer < kWaterWaveLayers; ++layer)
        {
            IDirect3DSurface9* surface = nullptr;
            ok = ok && SUCCEEDED(m_waterWaves[layer]->GetSurfaceLevel(level, &surface)) &&
                 SUCCEEDED(dev->SetRenderTarget(layer, surface));
            SafeRelease(surface);
        }
        const UINT texels = static_cast<UINT>(WaterWaveLevelTexels(level));
        const D3DVIEWPORT9 vp = {0, 0, texels, texels, 0.0f, 1.0f};
        dev->SetViewport(&vp);
        const Float4 c = {static_cast<float>(texels), 1.0f / texels, kWaterWaveFadeFromTexels,
                          1.0f / kWaterWaveFadeWidthTexels};
        dev->SetPixelShaderConstantF(kWaterWaveLevelRegister, &c.x, 1);
        if (ok)
            DrawFullscreen(dev);
    }
    for (DWORD layer = 1; layer < kWaterWaveLayers; ++layer)
        dev->SetRenderTarget(layer, nullptr);
    return ok || Skip("water wave targets could not be bound");
}

void Renderer::EnsureWaterTextures(IDirect3DDevice9* dev)
{
    if (m_waterTexturesMade)
        return;
    m_waterTexturesMade = true;
    const WaterTextureFiles& files = GlobalWaterTextureFiles();
    m_waterBump = files.Bump() ? TextureFromBlp(dev, *files.Bump()) : nullptr;
    m_waterFoam = files.Foam() ? TextureFromBlp(dev, *files.Foam()) : nullptr;
    VF_LOG_INFO("water: bump %s, foam %s", m_waterBump ? "ready" : (files.Bump() ? "not made" : "absent"),
                m_waterFoam ? "ready" : (files.Foam() ? "not made" : "absent"));
}

void Renderer::BindWaterWaves(IDirect3DDevice9* dev)
{
    for (int layer = 0; layer < kWaterWaveLayers; ++layer)
    {
        const DWORD stage = kWaterWaveStage + layer;
        dev->SetTexture(stage, m_waterWaves[layer]);
        dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetSamplerState(stage, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
        dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(stage, D3DSAMP_MINFILTER,
                             m_waterAnisotropy > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
        dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, m_waterAnisotropy);
        dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
        dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
    }
}

bool ReadFloatTarget(IDirect3DDevice9* dev, IDirect3DTexture9* texture, UINT level, float* out, unsigned floats,
                     UINT* width, UINT* height)
{
    D3DSURFACE_DESC desc = {};
    if (!texture || FAILED(texture->GetLevelDesc(level, &desc)) ||
        (desc.Format != D3DFMT_A16B16G16R16F && desc.Format != D3DFMT_A32B32G32R32F) ||
        floats < 4 * desc.Width * desc.Height)
        return false;
    IDirect3DSurface9* surface = nullptr;
    IDirect3DSurface9* readback = nullptr;
    bool ok = SUCCEEDED(texture->GetSurfaceLevel(level, &surface)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                                                        &readback, nullptr)) &&
              SUCCEEDED(dev->GetRenderTargetData(surface, readback));
    D3DLOCKED_RECT locked = {};
    if (ok && SUCCEEDED(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
    {
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const unsigned char* row = static_cast<const unsigned char*>(locked.pBits) + y * locked.Pitch;
            float* line = out + y * 4 * desc.Width;
            if (desc.Format == D3DFMT_A32B32G32R32F)
                std::memcpy(line, row, 4 * desc.Width * sizeof(float));
            else
                for (UINT x = 0; x < 4 * desc.Width; ++x)
                    line[x] = HalfToFloat(reinterpret_cast<const uint16_t*>(row)[x]);
        }
        readback->UnlockRect();
    }
    else
        ok = false;
    SafeRelease(surface);
    SafeRelease(readback);
    if (ok && width)
        *width = desc.Width;
    if (ok && height)
        *height = desc.Height;
    return ok;
}

bool Renderer::ReadWaterWaves(IDirect3DDevice9* dev, int layer, int level, float* out, unsigned floats)
{
    if (layer < 0 || layer >= kWaterWaveLayers || level < 0 || level >= kWaterWaveLevels)
        return false;
    return ReadFloatTarget(dev, m_waterWaves[layer], static_cast<UINT>(level), out, floats, nullptr, nullptr);
}

bool Renderer::ReadWaterSlopes(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height)
{
    UINT w = 0;
    UINT h = 0;
    if (!ReadFloatTarget(dev, m_waterSlopes, 0, out, floats, &w, &h))
        return false;
    *width = w;
    *height = h;
    return true;
}

bool Renderer::ReadWaterMemory(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height)
{
    UINT w = 0;
    UINT h = 0;
    if (!m_waterMemoryOn || !ReadFloatTarget(dev, m_waterMemory[m_waterMemoryIndex], 0, out, floats, &w, &h))
        return false;
    *width = w;
    *height = h;
    return true;
}

bool Renderer::ReadSkyReference(IDirect3DDevice9* dev, float* out, unsigned floats)
{
    if (!m_skyReferenceValid ||
        !ReadFloatTarget(dev, m_skyReference[m_skyReferenceIndex], 0, out, floats, nullptr, nullptr))
        return false;
    const float peak = std::max({m_skyReferenceTarget[0], m_skyReferenceTarget[1], m_skyReferenceTarget[2]});
    for (unsigned i = 0; i < floats; ++i)
    {
        if (i % 4 != 3)
            out[i] *= std::max({m_skyReferenceTarget[i % 4], kSkyRatioFloor, kSkyRatioFloorShare * peak});
    }
    return true;
}

bool Renderer::ReadWetSurfaces(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height)
{
    UINT w = 0;
    UINT h = 0;
    if (!m_wetDrawn || !ReadFloatTarget(dev, m_wetTarget, 0, out, floats, &w, &h))
        return false;
    *width = w;
    *height = h;
    return true;
}

double WaterMemoryTexelRadians()
{
    return std::sqrt(4.0 * kPi) / kWaterMemorySize;
}

bool Renderer::EnsureWaterMemory(IDirect3DDevice9* dev)
{
    if (m_waterMemoryFailed)
        return false;
    if (m_waterMemory[0])
        return true;
    bool ok = true;
    for (IDirect3DTexture9*& memory : m_waterMemory)
        ok = ok && CreateTarget(dev, kWaterMemorySize, kWaterMemorySize, D3DFMT_A32B32G32R32F, &memory);
    if (!ok)
    {
        for (IDirect3DTexture9*& memory : m_waterMemory)
            SafeRelease(memory);
        m_waterMemoryFailed = true;
        VF_LOG_ERROR("water memory: target creation failed; reflections use the screen alone");
        return false;
    }
    m_waterMemoryValid = false;
    VF_LOG_INFO("water memory: two %ux%u fp32 octahedral maps of the point and colour the camera saw in each "
                "direction, %.2f deg texels",
                kWaterMemorySize, kWaterMemorySize, WaterMemoryTexelRadians() * 180.0 / kPi);
    return true;
}

bool Renderer::UpdateWaterMemory(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg,
                                 float (*constants)[4])
{
    float* motion = constants[kWaterMemoryRegister];
    float* grid = constants[kWaterMemoryRegister + 1];
    std::fill(motion, motion + 4, 0.0f);
    std::fill(grid, grid + 4, 0.0f);
    m_waterMemoryOn = false;
    if (!cfg.waterMemory)
    {
        m_waterMemoryValid = false;
        return false;
    }
    if (!EnsureWaterMemory(dev))
        return false;
    float moved[3];
    for (int k = 0; k < 3; ++k)
        moved[k] = in.camPos[k] - m_waterMemoryCamPos[k];
    const float movedYards = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);
    const double elapsed = in.seconds - m_waterMemorySeconds;
    const char* forget = "";
    if (!m_waterMemoryValid)
        forget = "a start";
    else if (in.mapId != m_waterMemoryMapId)
        forget = "a map change";
    else if (!(movedYards <= kWaterMemoryForgetYards))
        forget = "a camera jump";
    else if (!(elapsed >= 0.0))
        forget = "the clock going back";
    else if (!(elapsed <= kWaterMemoryForgetSeconds))
        forget = "a pause";
    const bool history = !*forget;
    if (!history)
    {
        ++m_waterMemoryForgets;
        m_waterMemoryForgetReason = forget;
    }
    for (int k = 0; k < 3; ++k)
        motion[k] = history ? moved[k] : 0.0f;
    motion[3] = history ? 1.0f : 0.0f;
    grid[0] = static_cast<float>(kWaterMemorySize);
    grid[1] = 1.0f / kWaterMemorySize;
    grid[2] = static_cast<float>(WaterMemoryTexelRadians());
    grid[3] = 1.0f;

    if (!m_waterMemoryFlush && FAILED(dev->CreateQuery(D3DQUERYTYPE_EVENT, &m_waterMemoryFlush)))
        m_waterMemoryFlush = nullptr;
    if (m_waterMemoryFlush)
    {
        m_waterMemoryFlush->Issue(D3DISSUE_END);
        m_waterMemoryFlush->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    }

    const int write = m_waterMemoryIndex ^ 1;
    SetTarget(dev, m_waterMemory[write]);
    dev->SetDepthStencilSurface(nullptr);
    const D3DVIEWPORT9 whole = {0, 0, kWaterMemorySize, kWaterMemorySize, 0.0f, 1.0f};
    dev->SetViewport(&whole);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetPixelShader(m_waterMemoryShader);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kWaterConstants);
    BindTexture(dev, kWaterDepthBeforeStage, m_waterDepth[0], false);
    BindTexture(dev, kWaterSceneBeforeStage, m_waterScene[0], true);
    BindTexture(dev, kWaterMemoryStage, m_waterMemory[m_waterMemoryIndex], false);
    DrawFullscreen(dev);

    m_waterMemoryIndex = write;
    m_waterMemoryValid = true;
    m_waterMemoryOn = true;
    std::memcpy(m_waterMemoryCamPos, in.camPos, sizeof(m_waterMemoryCamPos));
    m_waterMemorySeconds = in.seconds;
    m_waterMemoryMapId = in.mapId;
    ++m_waterMemoryUpdates;
    return true;
}

bool Renderer::EnsureWaterTargets(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT fullW, UINT fullH,
                                  const D3DVIEWPORT9& vp)
{
    if (m_waterDepth[0] && m_waterDepthW == fullW && m_waterDepthH == fullH && m_waterScene[0] &&
        m_waterSceneW == vp.Width && m_waterSceneH == vp.Height)
        return true;
    for (int i = 0; i < 2; ++i)
    {
        SafeRelease(m_waterDepth[i]);
        SafeRelease(m_waterScene[i]);
    }
    SafeRelease(m_waterSlopes);
    m_waterDepthW = m_waterDepthH = m_waterSceneW = m_waterSceneH = 0;
    D3DSURFACE_DESC desc = {};
    target->GetDesc(&desc);
    bool ok = CreateTarget(dev, fullW, fullH, D3DFMT_A16B16G16R16F, &m_waterSlopes);
    for (int i = 0; i < 2; ++i)
        ok = ok && CreateTarget(dev, fullW, fullH, D3DFMT_R32F, &m_waterDepth[i]) &&
             (CreateTarget(dev, vp.Width, vp.Height, desc.Format, &m_waterScene[i]) ||
              CreateTarget(dev, vp.Width, vp.Height, D3DFMT_A8R8G8B8, &m_waterScene[i]));
    if (!ok)
    {
        for (int i = 0; i < 2; ++i)
        {
            SafeRelease(m_waterDepth[i]);
            SafeRelease(m_waterScene[i]);
        }
        SafeRelease(m_waterSlopes);
        m_waterFailed = true;
        VF_LOG_ERROR("water target creation failed (%ux%u); the water stays as the client draws it", fullW, fullH);
        return Skip("water target creation failed");
    }
    m_waterDepthW = fullW;
    m_waterDepthH = fullH;
    m_waterSceneW = vp.Width;
    m_waterSceneH = vp.Height;
    VF_LOG_INFO("water targets: two %ux%u depth copies (R32F), a slope target (fp16), two %lux%lu scene copies", fullW,
                fullH, vp.Width, vp.Height);
    return true;
}

bool Renderer::CopyWaterScene(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DTexture9* copy,
                              const D3DVIEWPORT9& vp)
{
    const RECT world = ViewportRect(vp);
    IDirect3DSurface9* surface = nullptr;
    const bool copied = SUCCEEDED(copy->GetSurfaceLevel(0, &surface)) &&
                        SUCCEEDED(dev->StretchRect(target, &world, surface, nullptr, D3DTEXF_POINT));
    SafeRelease(surface);
    return copied;
}

void Renderer::PrepareFullscreenPasses(IDirect3DDevice9* dev)
{
    dev->SetVertexShader(m_vs);
    dev->SetVertexDeclaration(m_decl);
    dev->SetStreamSourceFreq(0, 1);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
}

bool Renderer::CaptureOccluderDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                    IDirect3DSurface9* boundDepthStencil, const D3DVIEWPORT9& world,
                                    bool sceneBaseline)
{
    m_occluderDepthFresh = false;
    m_effectBaselineFresh = false;
    if (m_occluderDepthFailed || !depthTexture || dev->TestCooperativeLevel() != D3D_OK)
        return false;
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev) || !EnsureWaterShaders(dev))
        return false;
    return WithEngineStateSaved(dev, boundDepthStencil, [&](IDirect3DSurface9* target,
                                                             const D3DSURFACE_DESC& depthDesc) {
        if (world.Width < 16 || world.Height < 16 || world.X + world.Width > depthDesc.Width ||
            world.Y + world.Height > depthDesc.Height)
            return false;
        if (!EnsureOccluderDepthTarget(dev, depthDesc.Width, depthDesc.Height))
            return false;
        PrepareFullscreenPasses(dev);
        CopyWaterDepth(dev, depthTexture, m_occluderDepth, world, depthDesc);
        m_occluderDepthFresh = true;
        m_effectBaselineFresh = sceneBaseline && !m_effectsFailed &&
                                EnsureEffectBaseline(dev, target, world.Width, world.Height) &&
                                CopyWaterScene(dev, target, m_effectBaseline, world);
        return true;
    });
}

bool Renderer::EnsureOccluderDepthTarget(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (m_occluderDepth && m_occluderDepthW == w && m_occluderDepthH == h)
        return true;
    if (m_occluderDepthFailed)
        return false;
    SafeRelease(m_occluderDepth);
    m_occluderDepthW = m_occluderDepthH = 0;
    if (!CreateTarget(dev, w, h, D3DFMT_R32F, &m_occluderDepth))
    {
        m_occluderDepthFailed = true;
        VF_LOG_ERROR("occluder depth target creation failed (%ux%u); lamp shadows use the final depth", w, h);
        return false;
    }
    m_occluderDepthW = w;
    m_occluderDepthH = h;
    VF_LOG_INFO("occluder depth: %ux%u, copied after the liquid pass so names and later draws cast no lamp or shaft "
                "shadows",
                w, h);
    return true;
}

bool Renderer::EnsureShaftMemory(IDirect3DDevice9* dev, const D3DSURFACE_DESC& depthDesc, const D3DVIEWPORT9& vp)
{
    if (!m_memoryChecked)
    {
        m_memoryChecked = true;
        IDirect3D9* d3d = nullptr;
        D3DDEVICE_CREATION_PARAMETERS cp;
        D3DDISPLAYMODE mode;
        bool vertexTextures = false;
        bool blending = false;
        if (SUCCEEDED(dev->GetDirect3D(&d3d)) && SUCCEEDED(dev->GetCreationParameters(&cp)) &&
            SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode)))
        {
            vertexTextures = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                                              D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE,
                                                              D3DFMT_R32F));
            blending = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                                        D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
                                                        D3DRTYPE_TEXTURE, D3DFMT_R32F));
        }
        SafeRelease(d3d);
        m_memoryUnsupported = !vertexTextures || !blending;
        if (m_memoryUnsupported)
            VF_LOG_ERROR("shaft memory: the device %s; shafts use the screen alone",
                         !vertexTextures ? "reads no R32F textures in vertex shaders" : "cannot blend R32F targets");
    }
    if (m_memoryUnsupported || m_memoryFailed)
        return false;
    if (!EnsureOccluderDepthTarget(dev, depthDesc.Width, depthDesc.Height))
        return false;
    if (!m_memory[0])
    {
        bool ok = CreateTarget(dev, kShaftMemorySize, kShaftMemorySize, D3DFMT_R32F, &m_memory[0]) &&
                  CreateTarget(dev, kShaftMemorySize, kShaftMemorySize, D3DFMT_R32F, &m_memory[1]) &&
                  CreateTarget(dev, kShaftMemorySize, kShaftMemorySize, D3DFMT_R32F, &m_memoryBefore);
        const UINT realignBytes = kShaftMemoryRealignGrid * kShaftMemoryRealignGrid * 2 * sizeof(float);
        void* locked = nullptr;
        ok = ok &&
             SUCCEEDED(dev->CreateVertexBuffer(realignBytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                               &m_memoryRealignGrid, nullptr)) &&
             SUCCEEDED(m_memoryRealignGrid->Lock(0, 0, &locked, 0));
        if (ok)
        {
            float* grid = static_cast<float*>(locked);
            for (UINT y = 0; y < kShaftMemoryRealignGrid; ++y)
                for (UINT x = 0; x < kShaftMemoryRealignGrid; ++x)
                {
                    *grid++ = static_cast<float>(x);
                    *grid++ = static_cast<float>(y);
                }
            m_memoryRealignGrid->Unlock();
        }
        else
        {
            SafeRelease(m_memory[0]);
            SafeRelease(m_memory[1]);
            SafeRelease(m_memoryBefore);
            SafeRelease(m_memoryRealignGrid);
            m_memoryFailed = true;
            VF_LOG_ERROR("shaft memory: target creation failed; shafts use the screen alone");
            return false;
        }
        m_memoryValid = false;
        VF_LOG_INFO("shaft memory: two %ux%u R32F maps seen from the light, %.3f yd texels (%.0f yd around the "
                    "camera), each texel the surface nearest the light on its ray; realigned when the light moves "
                    "%.1f degrees, the map before each realignment faded out over %.0f%% of the time between "
                    "realignments (%.2f-%.0f s) in a third map",
                    kShaftMemorySize, kShaftMemorySize, kShaftMemoryTexelYards,
                    (kShaftMemorySize / 2.0 - kShaftMemoryWindowMarginTexels) * kShaftMemoryTexelYards,
                    kMemoryRealignDegrees, kMemoryFadeShareOfInterval * 100.0, kMemoryMinFadeSeconds,
                    kMemoryMaxFadeSeconds);
    }
    const UINT gridW = (vp.Width + kMemoryScatterStride - 1) / kMemoryScatterStride;
    const UINT gridH = (vp.Height + kMemoryScatterStride - 1) / kMemoryScatterStride;
    if (m_memoryGrid && m_memoryGridW == gridW && m_memoryGridH == gridH)
        return true;
    SafeRelease(m_memoryGrid);
    m_memoryGridW = m_memoryGridH = 0;
    const UINT bytes = gridW * gridH * 2 * sizeof(float);
    void* locked = nullptr;
    if (FAILED(dev->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &m_memoryGrid, nullptr)) ||
        FAILED(m_memoryGrid->Lock(0, 0, &locked, 0)))
    {
        SafeRelease(m_memoryGrid);
        m_memoryFailed = true;
        VF_LOG_ERROR("shaft memory: point grid creation failed (%ux%u); shafts use the screen alone", gridW, gridH);
        return false;
    }
    float* grid = static_cast<float*>(locked);
    for (UINT y = 0; y < gridH; ++y)
        for (UINT x = 0; x < gridW; ++x)
        {
            *grid++ = static_cast<float>(x);
            *grid++ = static_cast<float>(y);
        }
    m_memoryGrid->Unlock();
    m_memoryGridW = gridW;
    m_memoryGridH = gridH;
    VF_LOG_INFO("shaft memory: %ux%u points, every %u px, cycling through the %u offsets in %u frames", gridW, gridH,
                kMemoryScatterStride, kMemoryScatterStride * kMemoryScatterStride,
                kMemoryScatterStride * kMemoryScatterStride);
    return true;
}

void Renderer::BeginMemoryPoints(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexBuffer9* grid,
                                 IDirect3DTexture9* vertexTexture)
{
    dev->SetVertexShader(vs);
    dev->SetVertexDeclaration(m_memoryDecl);
    dev->SetStreamSource(0, grid, 0, 2 * sizeof(float));
    dev->SetTexture(kMemoryDepthVertexSampler, vertexTexture);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(kMemoryDepthVertexSampler, D3DSAMP_MAXMIPLEVEL, 0);
    dev->SetPixelShader(m_memoryScatterPs);
    dev->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MAX);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
}

void Renderer::EndMemoryPoints(IDirect3DDevice9* dev)
{
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetTexture(kMemoryDepthVertexSampler, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetVertexShader(m_vs);
    dev->SetVertexDeclaration(m_decl);
}

bool Renderer::UpdateShaftMemory(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                 const D3DSURFACE_DESC& depthDesc, const FrameInputs& in, const float* common,
                                 const CarrierBody& body)
{
    const D3DVIEWPORT9& vp = in.viewport;
    if (!EnsureShaftMemory(dev, depthDesc, vp))
        return false;
    if (!m_occluderDepthFresh)
    {
        if (!EnsureWaterShaders(dev))
            return false;
        ++m_memoryFallbackDepth;
        CopyWaterDepth(dev, depthTexture, m_occluderDepth, vp, depthDesc);
        dev->SetPixelShaderConstantF(0, common, 9);
    }
    if (!m_memoryFlush && FAILED(dev->CreateQuery(D3DQUERYTYPE_EVENT, &m_memoryFlush)))
        m_memoryFlush = nullptr;
    if (m_memoryFlush)
    {
        m_memoryFlush->Issue(D3DISSUE_END);
        m_memoryFlush->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    }

    const double size = kShaftMemorySize;
    const float halfWindowTexels = static_cast<float>(size / 2.0 - kShaftMemoryWindowMarginTexels);
    ShaftMemoryCamera camera = ShaftMemoryCameraIn(m_memoryFrame, in.camPos);
    const long long shiftX = camera.cell[0] - m_memoryCell[0];
    const long long shiftY = camera.cell[1] - m_memoryCell[1];
    const long long limit = static_cast<long long>(kShaftMemorySize);
    const bool forget = !m_memoryValid || in.mapId != m_memoryMapId || shiftX <= -limit || shiftX >= limit ||
                        shiftY <= -limit || shiftY >= limit;
    if (forget)
    {
        ++m_memoryForgets;
        ++m_memoryForgetsTotal;
        m_memoryFrame = MakeShaftMemoryFrame(in.toLight, in.camPos, m_memoryFrame.u);
        camera = ShaftMemoryCameraIn(m_memoryFrame, in.camPos);
        m_memoryFading = false;
        m_memoryRealignedBefore = false;
    }
    auto frameConstants = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, Float4* out) {
        out[0] = {at.index[0], at.index[1], at.fraction[0], at.fraction[1]};
        out[1] = {static_cast<float>(frame.u[0]), static_cast<float>(frame.u[1]), static_cast<float>(frame.u[2]),
                  static_cast<float>(kShaftMemoryTexelYards)};
        out[2] = {static_cast<float>(frame.v[0]), static_cast<float>(frame.v[1]), static_cast<float>(frame.v[2]),
                  static_cast<float>(size)};
        out[3] = {static_cast<float>(frame.l[0]), static_cast<float>(frame.l[1]), static_cast<float>(frame.l[2]),
                  static_cast<float>(at.depth)};
    };

    const int write = m_memoryIndex ^ 1;
    const float focalPixels = in.glProjection[5] * vp.Height * 0.5f;
    Float4 keep[5];
    keep[0] = {forget ? 0.0f : static_cast<float>(shiftX), forget ? 0.0f : static_cast<float>(shiftY),
               forget ? 1.0f : 0.0f, focalPixels};
    frameConstants(m_memoryFrame, camera, &keep[1]);
    SetTarget(dev, m_memory[write]);
    dev->SetPixelShader(m_memoryKeepPs);
    dev->SetPixelShaderConstantF(kMemoryPassRegister, &keep[0].x, 5);
    BindTexture(dev, kMemoryPreviousStage, m_memory[m_memoryIndex], false);
    BindTexture(dev, kMemoryDepthStage, m_occluderDepth, false);
    DrawFullscreen(dev);

    const double* l = m_memoryFrame.l;
    const double lightLength =
        std::sqrt(in.toLight[0] * in.toLight[0] + in.toLight[1] * in.toLight[1] + in.toLight[2] * in.toLight[2]);
    const double cosine =
        lightLength > 0.0 ? (in.toLight[0] * l[0] + in.toLight[1] * l[1] + in.toLight[2] * l[2]) / lightLength : 1.0;
    const double fromAnchor[3] = {in.camPos[0] - m_memoryFrame.anchor[0], in.camPos[1] - m_memoryFrame.anchor[1],
                                  in.camPos[2] - m_memoryFrame.anchor[2]};
    const bool reanchor = fromAnchor[0] * fromAnchor[0] + fromAnchor[1] * fromAnchor[1] +
                              fromAnchor[2] * fromAnchor[2] >
                          kMemoryReanchorYards * kMemoryReanchorYards;
    if (!forget && (cosine < std::cos(kMemoryRealignDegrees * kPi / 180.0) || reanchor))
    {
        const ShaftMemoryFrame next = MakeShaftMemoryFrame(in.toLight, in.camPos, m_memoryFrame.u);
        const ShaftMemoryCamera nextCamera = ShaftMemoryCameraIn(next, in.camPos);
        Float4 realign[9];
        realign[0] = {0.0f, 0.0f, halfWindowTexels,
                      static_cast<float>(m_memoryRealignmentsTotal % kMemoryRealignSeeds)};
        frameConstants(next, nextCamera, &realign[1]);
        frameConstants(m_memoryFrame, camera, &realign[5]);
        SetTarget(dev, m_memory[m_memoryIndex]);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        BeginMemoryPoints(dev, m_memoryRealignVs, m_memoryRealignGrid, m_memory[write]);
        dev->SetVertexShaderConstantF(kMemoryPassRegister, &realign[0].x, 9);
        static_assert(kMemoryPassRegister + 9 == kMemoryVertexConstants, "the state block covers the vertex constants");
        const UINT tiles = kShaftMemorySize / kShaftMemoryRealignGrid;
        for (UINT ty = 0; ty < tiles; ++ty)
            for (UINT tx = 0; tx < tiles; ++tx)
            {
                realign[0].x = static_cast<float>(tx * kShaftMemoryRealignGrid);
                realign[0].y = static_cast<float>(ty * kShaftMemoryRealignGrid);
                dev->SetVertexShaderConstantF(kMemoryPassRegister, &realign[0].x, 1);
                dev->DrawPrimitive(D3DPT_POINTLIST, 0, kShaftMemoryRealignGrid * kShaftMemoryRealignGrid);
            }
        EndMemoryPoints(dev);
        const double sinceLast = in.seconds - m_memoryLastRealign;
        m_memoryFadeSeconds = m_memoryRealignedBefore && sinceLast > 0.0
                                  ? std::clamp(kMemoryFadeShareOfInterval * sinceLast, kMemoryMinFadeSeconds,
                                               kMemoryMaxFadeSeconds)
                                  : kMemoryFirstFadeSeconds;
        m_memoryLastRealign = in.seconds;
        m_memoryRealignedBefore = true;
        m_memoryFadeStart = in.seconds;
        m_memoryFading = true;
        m_memoryBeforeFrame = m_memoryFrame;
        m_memoryBeforeCell[0] = camera.cell[0];
        m_memoryBeforeCell[1] = camera.cell[1];
        std::swap(m_memory[write], m_memoryBefore);
        SetTarget(dev, m_memory[write]);
        dev->SetPixelShader(m_memoryFillPs);
        const Float4 fill = {static_cast<float>(1.0 / size), 0.0f, 0.0f, 0.0f};
        dev->SetPixelShaderConstantF(kMemoryPassRegister, &fill.x, 1);
        BindTexture(dev, kMemoryPreviousStage, m_memory[m_memoryIndex], false);
        DrawFullscreen(dev);
        m_memoryFrame = next;
        camera = nextCamera;
        ++m_memoryRealignments;
        ++m_memoryRealignmentsTotal;
    }

    const UINT offsets = kMemoryScatterStride * kMemoryScatterStride;
    const UINT firstOffset = forget ? 0u : m_frame % offsets;
    const UINT scatters = forget ? offsets : 1u;
    Float4 scatter[7];
    scatter[0] = {static_cast<float>(kMemoryScatterStride), 0.0f, 0.0f, halfWindowTexels};
    frameConstants(m_memoryFrame, camera, &scatter[1]);
    const BodyConstants scatterBody = BodyConstantsOf(body);
    scatter[5] = scatterBody.base;
    scatter[6] = scatterBody.up;
    static_assert(kMemoryPassRegister + 7 <= kMemoryVertexConstants, "the state block covers the body left out");
    SetTarget(dev, m_memory[write]);
    BeginMemoryPoints(dev, m_memoryScatterVs, m_memoryGrid, m_occluderDepth);
    dev->SetVertexShaderConstantF(0, common, 9);
    dev->SetVertexShaderConstantF(kMemoryPassRegister, &scatter[0].x, 7);
    for (UINT s = 0; s < scatters; ++s)
    {
        const UINT order = (firstOffset + s) % offsets;
        scatter[0].y = static_cast<float>(kMemoryScatterOrder[order][0]);
        scatter[0].z = static_cast<float>(kMemoryScatterOrder[order][1]);
        dev->SetVertexShaderConstantF(kMemoryPassRegister, &scatter[0].x, 1);
        dev->DrawPrimitive(D3DPT_POINTLIST, 0, m_memoryGridW * m_memoryGridH);
    }
    EndMemoryPoints(dev);

    auto lookupIn = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, float window, float weight,
                        float (&out)[4][4]) {
        const double* u = frame.u;
        const double* v = frame.v;
        const double* ll = frame.l;
        const float cameraTexel[2] = {at.index[0] + at.fraction[0], at.index[1] + at.fraction[1]};
        const float lookup[4][4] = {
            {static_cast<float>(u[0]), static_cast<float>(u[1]), static_cast<float>(u[2]), cameraTexel[0]},
            {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), cameraTexel[1]},
            {static_cast<float>(ll[0]), static_cast<float>(ll[1]), static_cast<float>(ll[2]),
             static_cast<float>(at.depth + kShaftMemoryDepthOffset + kMemoryBiasYards)},
            {static_cast<float>(1.0 / kShaftMemoryTexelYards), static_cast<float>(1.0 / size), window, weight},
        };
        std::memcpy(out, lookup, sizeof(lookup));
    };
    lookupIn(m_memoryFrame, camera, halfWindowTexels, 0.0f, m_memoryLookup);

    float beforeWeight = 0.0f;
    if (m_memoryFading)
    {
        const ShaftMemoryCamera before = ShaftMemoryCameraIn(m_memoryBeforeFrame, in.camPos);
        const long long moved = std::max(std::llabs(before.cell[0] - m_memoryBeforeCell[0]),
                                         std::llabs(before.cell[1] - m_memoryBeforeCell[1]));
        const float beforeWindow = halfWindowTexels - static_cast<float>(moved);
        const double elapsed = in.seconds - m_memoryFadeStart;
        if (g_shaftMemoryFade && elapsed >= 0.0 && elapsed < m_memoryFadeSeconds && beforeWindow > 0.0f)
            beforeWeight = static_cast<float>(1.0 - elapsed / m_memoryFadeSeconds);
        m_memoryFading = beforeWeight > 0.0f;
        lookupIn(m_memoryBeforeFrame, before, std::max(beforeWindow, 0.0f), beforeWeight, m_memoryBeforeLookup);
    }
    if (!m_memoryFading)
        std::memset(m_memoryBeforeLookup, 0, sizeof(m_memoryBeforeLookup));
    else
        ++m_memoryFadedFrames;

    m_memoryIndex = write;
    m_memoryCell[0] = camera.cell[0];
    m_memoryCell[1] = camera.cell[1];
    m_memoryMapId = in.mapId;
    m_memoryValid = true;
    ++m_memoryUpdates;
    return true;
}

void Renderer::ForgetShaftMemory()
{
    m_memoryValid = false;
}

bool Renderer::ReadShaftMemory(IDirect3DDevice9* dev, float* codes, ShaftMemoryReadback& info)
{
    info = {};
    info.forgets = m_memoryForgetsTotal;
    info.realignments = m_memoryRealignmentsTotal;
    if (!m_memoryValid || !m_memory[m_memoryIndex])
        return false;
    IDirect3DSurface9* surface = nullptr;
    IDirect3DSurface9* readback = nullptr;
    D3DLOCKED_RECT locked = {};
    bool ok = SUCCEEDED(m_memory[m_memoryIndex]->GetSurfaceLevel(0, &surface)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(kShaftMemorySize, kShaftMemorySize, D3DFMT_R32F,
                                                        D3DPOOL_SYSTEMMEM, &readback, nullptr)) &&
              SUCCEEDED(dev->GetRenderTargetData(surface, readback)) &&
              SUCCEEDED(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    if (ok)
    {
        for (UINT y = 0; y < kShaftMemorySize; ++y)
            std::memcpy(codes + y * kShaftMemorySize, static_cast<const unsigned char*>(locked.pBits) + y * locked.Pitch,
                        kShaftMemorySize * sizeof(float));
        readback->UnlockRect();
        info.frame = m_memoryFrame;
        info.cell[0] = m_memoryCell[0];
        info.cell[1] = m_memoryCell[1];
        info.valid = 1;
    }
    SafeRelease(readback);
    SafeRelease(surface);
    return ok;
}

void Renderer::LogShaftMemory(const Config& cfg)
{
    if (!cfg.shaftMemory || !cfg.lightShafts)
        return;
    if (m_memoryUnsupported || m_memoryFailed)
        VF_LOG_INFO("  shaft memory: unavailable; shafts use the screen alone");
    else
        VF_LOG_INFO("  shaft memory: updated in %u frames (%u with the final depth, no liquid pass), realigned %u "
                    "times to the light (fading from the map before in %u frames, last fade %.2f s), forgotten %u "
                    "times (start, map change or a jump of %.0f yd)",
                    m_memoryUpdates, m_memoryFallbackDepth, m_memoryRealignments, m_memoryFadedFrames,
                    m_memoryFadeSeconds, m_memoryForgets, kShaftMemorySize * kShaftMemoryTexelYards);
    m_memoryUpdates = m_memoryFallbackDepth = m_memoryForgets = m_memoryRealignments = m_memoryFadedFrames = 0;
}

void Renderer::CopyWaterDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* copy,
                              const D3DVIEWPORT9& vp, const D3DSURFACE_DESC& depthDesc)
{
    dev->SetDepthStencilSurface(nullptr);
    SetTarget(dev, copy);
    dev->SetViewport(&vp);
    dev->SetPixelShader(m_waterDepthShader);
    const Float4 grid = {1.0f, 0.0f, 1.0f / depthDesc.Width, 1.0f / depthDesc.Height};
    dev->SetPixelShaderConstantF(1, &grid.x, 1);
    BindTexture(dev, 0, depthTexture, false);
    DrawFullscreen(dev);
}

bool Renderer::BeginWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                          IDirect3DSurface9* boundDepthStencil, const D3DVIEWPORT9& world)
{
    m_skip = "";
    ++m_waterBegun;
    m_waterTarget = nullptr;
    if (m_waterFailed)
        return WaterResult(Skip("water unavailable after an earlier failure"));
    if (dev->TestCooperativeLevel() != D3D_OK)
        return WaterResult(Skip("device not ready"));
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev) || !EnsureWaterShaders(dev) || !EnsureWaterWaves(dev))
        return WaterResult(false);
    const bool ok = WithEngineStateSaved(dev, boundDepthStencil, [&](IDirect3DSurface9* target,
                                                                      const D3DSURFACE_DESC& depthDesc) {
        if (world.Width < 16 || world.Height < 16 || world.X + world.Width > depthDesc.Width ||
            world.Y + world.Height > depthDesc.Height)
            return Skip("world viewport outside the render target");
        if (!EnsureWaterTargets(dev, target, depthDesc.Width, depthDesc.Height, world))
            return false;
        if (!CopyWaterScene(dev, target, m_waterScene[0], world))
            return Skip("water scene copy failed");
        PrepareFullscreenPasses(dev);
        CopyWaterDepth(dev, depthTexture, m_waterDepth[0], world, depthDesc);
        dev->SetRenderTarget(0, target);
        dev->SetDepthStencilSurface(boundDepthStencil);
        dev->SetViewport(&world);
        dev->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);
        m_waterTarget = target;
        m_waterViewport = world;
        return true;
    });
    return WaterResult(ok);
}

bool Renderer::EndWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                        const FrameInputs& in, const Config& cfg)
{
    m_skip = "";
    ReadWaterQuery();
    if (!m_waterTarget || !m_waterShader || !m_state)
        return WaterResult(Skip("the liquid pass began without water targets"));
    if (dev->TestCooperativeLevel() != D3D_OK)
        return WaterResult(Skip("device not ready"));
    if (!EnsureTerrainTexture(dev))
        return WaterResult(false);
    const bool ok = WithEngineStateSaved(dev, boundDepthStencil, [&](IDirect3DSurface9* target,
                                                                      const D3DSURFACE_DESC& depthDesc) {
        if (target != m_waterTarget)
            return Skip("the render target changed during the liquid pass");
        return DrawWater(dev, depthTexture, boundDepthStencil, target, depthDesc, in, cfg);
    });
    m_waterTarget = nullptr;
    if (ok)
    {
        ++m_waterDrawn;
        m_waterBeforeFresh = true;
    }
    return WaterResult(ok);
}

bool Renderer::DrawWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                         IDirect3DSurface9* target, const D3DSURFACE_DESC& depthDesc, const FrameInputs& in,
                         const Config& cfg)
{
    const D3DVIEWPORT9 vp = m_waterViewport;
    float viewToWorld[16];
    float worldToView[16];
    if (!WorldViewFromCameraRelative(in, viewToWorld, worldToView))
        return Skip("view matrix not invertible");
    if (!CopyWaterScene(dev, target, m_waterScene[1], vp))
        return Skip("water scene copy failed");
    PrepareFullscreenPasses(dev);
    EnsureWaterTextures(dev);
    CopyWaterDepth(dev, depthTexture, m_waterDepth[1], vp, depthDesc);

    const WaterParams water = BuildWaterParams(in, cfg);
    m_waterField = BuildWaterWaveField(water.windSpeed, water.windDirection, water.waves, in.seconds);
    if (!DrawWaterWaves(dev, m_waterField))
        return false;
    const float* proj = in.glProjection;
    const WorldDepthMapping worldDepth = MapWorldDepth(proj, vp);
    const bool terrainMap = TerrainMapWanted(cfg) && !m_terrainFallback && CurrentHeightSource().probe;
    const TerrainShadowConstants terrain = terrainMap ? m_terrain.Constants(in.camPos) : kTerrainShadowsOff;
    float toLight[3] = {in.toLight[0], in.toLight[1], in.toLight[2]};
    Normalize3(toLight);
    float c[kWaterConstants][4] = {
        {static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width),
         static_cast<float>(vp.Height)},
        {1.0f, static_cast<float>(m_frame % 1024u), 1.0f / depthDesc.Width, 1.0f / depthDesc.Height},
        {proj[0], proj[5], proj[8], proj[9]},
        {worldDepth.atInfinity, worldDepth.perInverseViewDepth, std::max(cfg.maxDistance, in.farClip),
         worldDepth.deepest},
    };
    std::memcpy(c[4], viewToWorld, sizeof(viewToWorld));
    const float rainStrength = WaterRainStrength(cfg.waterRain);
    float rain =
        rainStrength > 0.0f ? WaterRainActiveShare(in.weather == WeatherKind::Rain, in.weatherIntensity) : 0.0f;
    if (rain > 0.0f && !EnsureWaterRainTable(dev))
        rain = 0.0f;
    m_waterRainShare = std::max(m_waterRainShare, rain);
    m_waterRainStrength = rainStrength;
    const WaterRainConstants rainConstants = BuildWaterRainConstants(in.camPos, in.seconds, rain, rainStrength);
    std::memcpy(c[kWaterRainRegister], rainConstants.rain, sizeof(rainConstants.rain));
    std::memcpy(c[kWaterRainRegister + 1], rainConstants.cell, sizeof(rainConstants.cell));
    const bool swimming = (in.playerMoveFlags & engine::kMovementSwimming) != 0;
    if (m_waterRipples.Update(in.playerKnown, in.playerPos, swimming, in.seconds))
        NoteWaterRipple(in, swimming);
    WaterRippleConstants ripples = m_waterRipples.Constants(in.camPos, in.seconds, water.waves);
    if (ripples.ripples[0] > 0.0f && !FillWaterRippleTable(dev, ripples))
        ripples.ripples[0] = 0.0f;
    std::memcpy(c[kWaterRippleRegister], ripples.ripples, sizeof(ripples.ripples));
    std::memcpy(c[kWaterRippleRegister + 1], ripples.reach, sizeof(ripples.reach));
    const Float4 waterConstants[7] = {
        {water.reflect, water.clarity, kWaterReflectionYards, WaterFresnel(1.0f)},
        {toLight[0], toLight[1], toLight[2], water.glitter},
        {water.glitterColor[0], water.glitterColor[1], water.glitterColor[2], proj[5] * vp.Height * 0.5f},
        {water.skyHorizon[0], water.skyHorizon[1], water.skyHorizon[2], in.fogStart},
        {water.skyZenith[0], water.skyZenith[1], water.skyZenith[2], 1.0f / std::max(in.fogEnd - in.fogStart, 1.0f)},
        {terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness},
        {water.smooth, 0.0f, in.camPos[2],
         WaterBaseRoughness(m_waterField)},
    };
    static_assert(kWaterRegister + 7 == kWaterLookupRegister, "the wave lookups follow the water constants");
    std::memcpy(c[kWaterRegister], waterConstants, sizeof(waterConstants));
    for (int lookup = 0; lookup < kWaterWaveLookups; ++lookup)
    {
        const double turn = WaterLookupTurnRadians(lookup);
        const double tile = kWaterWaveLayerSpecs[kWaterWaveLookupSpecs[lookup].layer].tileYards;
        double u = 0.0;
        double v = 0.0;
        WaterLookupUv(lookup, in.camPos[0], in.camPos[1], &u, &v);
        float* w = c[kWaterLookupRegister + lookup];
        w[0] = static_cast<float>(Wrapped(u));
        w[1] = static_cast<float>(Wrapped(v));
        w[2] = static_cast<float>(std::cos(turn) / tile);
        w[3] = static_cast<float>(std::sin(turn) / tile);
    }
    const double firstTurn = WaterLookupTurnRadians(kWaterFirstTurnedLookup);
    const double secondTurn = WaterLookupTurnRadians(kWaterSecondTurnedLookup);
    c[kWaterTurnRegister][0] = static_cast<float>(std::cos(firstTurn));
    c[kWaterTurnRegister][1] = static_cast<float>(std::sin(firstTurn));
    c[kWaterTurnRegister][2] = static_cast<float>(std::cos(secondTurn));
    c[kWaterTurnRegister][3] = static_cast<float>(std::sin(secondTurn));
    const bool memory = (water.reflect > 0.0f || water.glitter > 0.0f) && UpdateWaterMemory(dev, in, cfg, c);
    const float bump = m_waterBump ? cfg.waterBump * kWaterBumpGain : 0.0f;
    const float foam = m_waterFoam ? cfg.waterFoam : 0.0f;
    float extras[kWaterExtraConstants][4] = {};
    const bool floors = water.smooth > 0.0f && terrainMap && FloorMapWanted(cfg) && m_terrain.FloorsKnown() &&
                        UploadBankFloors(dev);
    if (water.smooth > 0.0f)
    {
        float nearby = 0.0f;
        if (SeaShareNear(in, nearby))
        {
            const double elapsed = m_waterSeaSeconds > 0.0 ? std::max(in.seconds - m_waterSeaSeconds, 0.0) : 1.0e9;
            m_waterSeaShare += (nearby - m_waterSeaShare) *
                               static_cast<float>(1.0 - std::exp(-elapsed / kWaterSeaShareSeconds));
        }
        m_waterSeaSeconds = in.seconds;
        std::memcpy(extras[kWaterBandExtra], water.riverShallow, sizeof(water.riverShallow));
        std::memcpy(extras[kWaterBandExtra + 1], water.riverDeep, sizeof(water.riverDeep));
        std::memcpy(extras[kWaterBandExtra + 2], water.oceanShallow, sizeof(water.oceanShallow));
        std::memcpy(extras[kWaterBandExtra + 3], water.oceanDeep, sizeof(water.oceanDeep));
        extras[kWaterBandSwitchExtra][0] = floors ? 1.0f : 0.0f;
        extras[kWaterBandSwitchExtra][1] = m_waterSeaShare;
        extras[kWaterBandSwitchExtra][2] = cfg.stockFog == 0 ? 1.0f : 0.0f;
        m_waterSmoothFrames += 1;
        m_waterFloorFrames += floors ? 1 : 0;
    }
    if (bump > 0.0f || foam > 0.0f)
    {
        const double toNorth = std::cos(water.windDirection * kPi / 180.0);
        const double toWest = -std::sin(water.windDirection * kPi / 180.0);
        for (int layer = 0; layer < 2; ++layer)
        {
            const double turnCos = std::cos(kWaterBumpTurns[layer]);
            const double turnSin = std::sin(kWaterBumpTurns[layer]);
            const double travel = water.windSpeed * kWaterBumpSpeeds[layer] * in.seconds;
            const double north = (toNorth * turnCos - toWest * turnSin) * travel;
            const double west = (toNorth * turnSin + toWest * turnCos) * travel;
            extras[0][2 * layer] = static_cast<float>(Wrapped((in.camPos[0] - north) / kWaterBumpTiles[layer]));
            extras[0][2 * layer + 1] = static_cast<float>(Wrapped((in.camPos[1] - west) / kWaterBumpTiles[layer]));
        }
        const double foamTravel = water.windSpeed * kWaterFoamSpeed * in.seconds;
        extras[1][0] = static_cast<float>(1.0 / kWaterBumpTiles[0]);
        extras[1][1] = static_cast<float>(1.0 / kWaterBumpTiles[1]);
        extras[1][2] = bump;
        extras[1][3] = foam;
        extras[2][0] = static_cast<float>(Wrapped((in.camPos[0] - toNorth * foamTravel) / kWaterFoamTile));
        extras[2][1] = static_cast<float>(Wrapped((in.camPos[1] - toWest * foamTravel) / kWaterFoamTile));
        extras[2][2] = static_cast<float>(1.0 / kWaterFoamTile);
        extras[3][0] = GlobalWaterTextureFiles().BumpBias()[0];
        extras[3][1] = GlobalWaterTextureFiles().BumpBias()[1];
    }

    SetTarget(dev, m_waterSlopes);
    dev->SetDepthStencilSurface(boundDepthStencil);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    dev->SetRenderState(D3DRS_STENCILREF, kWaterStencilBit);
    dev->SetRenderState(D3DRS_STENCILMASK, kWaterStencilBit);
    dev->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
    dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    dev->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    dev->SetPixelShader(m_waterSlopeShader);
    dev->SetPixelShaderConstantF(0, &c[0][0], kWaterConstants);
    dev->SetPixelShaderConstantF(kWaterExtraRegister, &extras[0][0], kWaterExtraConstants);
    BindTexture(dev, kWaterDepthAfterStage, m_waterDepth[1], false);
    BindWaterWaves(dev);
    if (bump > 0.0f)
        BindWrapped(dev, kWaterBumpStage, m_waterBump, m_waterAnisotropy);
    if (rain > 0.0f)
        BindTexture(dev, kWaterRainStage, m_waterRainTable, false);
    if (ripples.ripples[0] > 0.0f)
        BindTexture(dev, kWaterRippleStage, m_waterRippleTable, false);
    DrawFullscreen(dev);

    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kWaterColourChannels);
    dev->SetPixelShader(m_waterShader);
    BindTexture(dev, kWaterDepthBeforeStage, m_waterDepth[0], false);
    BindTexture(dev, kWaterSceneBeforeStage, m_waterScene[0], true);
    BindTexture(dev, kWaterSceneAfterStage, m_waterScene[1], false);
    BindTexture(dev, kWaterSlopeStage, m_waterSlopes, false);
    if (foam > 0.0f)
        BindWrapped(dev, kWaterFoamStage, m_waterFoam, m_waterAnisotropy);
    if (floors)
        BindTexture(dev, kWaterFloorStage, m_bankFloorTexture, m_bankFloorFilterable);
    if (!m_waterQuery && !m_waterQueryFailed && FAILED(dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &m_waterQuery)))
    {
        m_waterQuery = nullptr;
        m_waterQueryFailed = true;
        VF_LOG_INFO("water: no occlusion query; the log counts no water pixels");
    }
    const bool counted = m_waterQuery && !m_waterQueryPending;
    if (counted)
        m_waterQuery->Issue(D3DISSUE_BEGIN);
    DrawFullscreen(dev);
    if (counted)
    {
        m_waterQuery->Issue(D3DISSUE_END);
        m_waterQueryPending = true;
    }
    if (water.reflect > 0.0f || water.glitter > 0.0f)
    {
        dev->SetPixelShader(m_waterReflectShader);
        BindTexture(dev, kWaterTerrainStage, m_terrainTexture, m_terrainFilterable);
        if (memory)
            BindTexture(dev, kWaterMemoryStage, m_waterMemory[m_waterMemoryIndex], false);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        DrawFullscreen(dev);
    }
    DrawWaterLamps(dev, in, cfg);
    return true;
}

void Renderer::ReadWaterQuery()
{
    if (!m_waterQuery || !m_waterQueryPending)
        return;
    DWORD pixels = 0;
    const HRESULT hr = m_waterQuery->GetData(&pixels, sizeof(pixels), 0);
    if (hr == S_FALSE)
        return;
    m_waterQueryPending = false;
    if (hr == S_OK)
        m_waterMostPixels = std::max(m_waterMostPixels, pixels);
}

void Renderer::LogWater(const FrameInputs& in, const Config& cfg)
{
    ReadWaterQuery();
    const WaterParams water = BuildWaterParams(in, cfg);
    VF_LOG_INFO("  water: reflect %.2f, glitter %.2f from the %s, clarity %.2f, waves %.2f; drawn in %u of %u frames "
                "with water, up to %lu water pixels; %u water and %u lava or slime instances%s",
                water.reflect, water.glitter, in.lightIsMoon ? "moon" : "sun", water.clarity, water.waves,
                m_waterDrawn, m_waterBegun, m_waterMostPixels, m_waterInstances, m_magmaInstances,
                m_magmaInstances ? " (lava or slime seen, left as the client draws it)" : "");
    const WaterWaveField field = BuildWaterWaveField(water.windSpeed, water.windDirection, water.waves, in.seconds);
    VF_LOG_INFO("  water waves: wind %.1f yd/s toward %.0f deg, longest waves near %.1f yd; slope rms %.3f in the "
                "wave textures, %.3f finer than their shortest %.1f yd",
                field.windSpeed, field.headingDegrees, field.peakYards, std::sqrt(field.resolvedMeanSquareSlope),
                std::sqrt(field.unresolvedMeanSquareSlope), kWaterWaveLayerSpecs[kWaterWaveLayers - 1].shortestYards);
    if (m_waterSmoothFrames)
        VF_LOG_INFO("  water colours (WaterSmooth %.2f): river %06X at %.2f to %06X at %.2f, ocean %06X at %.2f to "
                    "%06X at %.2f; sea share near the camera %.2f; river or sea per pixel from the floor map in %u of "
                    "%u frames",
                    water.smooth, in.waterBandColors[kRiverShallowColour] & 0xFFFFFFu,
                    in.waterBandAlphas[kRiverShallowAlpha], in.waterBandColors[kRiverDeepColour] & 0xFFFFFFu,
                    in.waterBandAlphas[kRiverDeepAlpha], in.waterBandColors[kOceanShallowColour] & 0xFFFFFFu,
                    in.waterBandAlphas[kOceanShallowAlpha], in.waterBandColors[kOceanDeepColour] & 0xFFFFFFu,
                    in.waterBandAlphas[kOceanDeepAlpha], m_waterSeaShare, m_waterFloorFrames, m_waterSmoothFrames);
    m_waterSmoothFrames = m_waterFloorFrames = 0;
    if (m_waterRainShare > 0.0f)
        VF_LOG_INFO("  water rain: rings from up to %.0f%% of the drop slots at strength %.2f, the water %.3f rougher "
                    "(slope rms) where the rings are too small to draw",
                    m_waterRainShare * 100.0f, m_waterRainStrength,
                    m_waterRainStrength * std::sqrt(WaterRainVariance(m_waterRainShare)));
    if (m_waterMemoryUpdates || m_waterMemoryForgets)
        VF_LOG_INFO("  water memory: %u updates, %u of them started over from the screen alone (the last after %s)",
                    m_waterMemoryUpdates, m_waterMemoryForgets, m_waterMemoryForgetReason);
    m_waterDrawn = m_waterBegun = m_waterInstances = m_magmaInstances = 0;
    m_waterMostPixels = 0;
    m_waterMemoryUpdates = m_waterMemoryForgets = 0;
    if (m_waterRippleStarts)
        VF_LOG_INFO("  water ripples: %u rings started at the player's feet, %u of them swimming; drawn where the feet "
                    "are under the water and at most %.1f yd deep",
                    m_waterRippleStarts, m_waterRippleSwims, kWaterRippleDeepestYards + kWaterRippleFadeYards);
    m_waterRainShare = 0.0f;
    m_waterRippleStarts = m_waterRippleSwims = 0;
    if (m_waterLampFrames)
        VF_LOG_INFO("  water lamps: mirrored in %u frames, up to %d lamps (%d of them far halos) and %d windows",
                    m_waterLampFrames, m_waterLampMost, m_waterLampRecoveredMost, m_waterLampWindowsMost);
    m_waterLampFrames = 0;
    m_waterLampMost = m_waterLampWindowsMost = m_waterLampRecoveredMost = 0;
}
