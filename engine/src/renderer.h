#pragma once

#include "client_shadows.h"
#include "config.h"
#include "corpse_lights.h"
#include "effect_lights.h"
#include "engine.h"
#include "fog_data.h"
#include "fog_model.h"
#include "frame_profile.h"
#include "lamp_selection.h"
#include "lamp_tiles.h"
#include "shaft_memory.h"
#include "terrain_relief.h"
#include "terrain_shadow.h"
#include "water_lamps.h"
#include "water_ripples.h"
#include "water_waves.h"
#include "wet_surfaces.h"

#include <d3d9.h>

struct FogParams;

void SetLampLightBlur(bool enabled);
void SetWetGlintShare(float share);
void SetWetPuddleThreshold(float threshold);
void SetLampTiles(int mode);

void SetShaftMemoryFade(bool enabled);
void SetBankWisps(bool enabled);

constexpr DWORD kWaterStencilBit = 0x80;
constexpr int kLampUploadRing = 4;

class Renderer
{
public:
    ~Renderer();

    void ReleaseDefaultPool();
    void ReleaseAll();

    bool Render(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                const FrameInputs& in, const Config& cfg, const ClientShadowFrame* clientShadows);
    bool BeginWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                    const D3DVIEWPORT9& world);
    bool EndWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                  const FrameInputs& in, const Config& cfg);
    void NoteLiquidInstance(bool magma);
    bool CaptureOccluderDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                              IDirect3DSurface9* boundDepthStencil, const D3DVIEWPORT9& world, bool sceneBaseline);
    void ReportEffects(EffectReport& out, bool reset);

    const char* LastSkipReason() const { return m_skip; }
    void MoveDriftPatches(const double* tileFraction);
    void SetCloudOffset(const double* yards);
    FrameProfile& Profile() { return m_profile; }
    bool LateDrawsInFront() const { return m_lateDrawsInFront; }
    void SetSunAndMoon(IDirect3DTexture9* drawn) { m_sunAndMoonDrawn = drawn; }
    void SetTerrainMaterials(const TerrainMaterialTargets& targets) { m_terrainMaterials = targets; }
    bool SunAndMoonShown() const { return m_sunAndMoonShown; }
    bool ReadLampTiles(LampTileStats& stats) const
    {
        stats = m_lampTiles.Stats();
        return m_lampTilesUsed;
    }
    bool ReadShaftMemory(IDirect3DDevice9* dev, float* codes, ShaftMemoryReadback& info);
    void ForgetShaftMemory();
    bool ReadWaterWaves(IDirect3DDevice9* dev, int layer, int level, float* out, unsigned floats);
    bool ReadWaterSlopes(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height);
    bool ReadWaterMemory(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height);
    void ForgetWaterMemory() { m_waterMemoryValid = false; }
    bool ReadSkyReference(IDirect3DDevice9* dev, float* out, unsigned floats);
    void ReadWetness(float* out) const;
    bool ReadWetSurfaces(IDirect3DDevice9* dev, float* out, unsigned floats, unsigned* width, unsigned* height);

private:
    void BlurLampLight(IDirect3DDevice9* dev, IDirect3DTexture9* source, IDirect3DTexture9* destination, float stepX,
                       float stepY);
    template <typename Passes>
    bool WithEngineStateSaved(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepthStencil, Passes passes);
    bool EnsureWaterShaders(IDirect3DDevice9* dev);
    bool EnsureWaterTargets(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT fullW, UINT fullH,
                            const D3DVIEWPORT9& vp);
    bool CopyWaterScene(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DTexture9* copy,
                        const D3DVIEWPORT9& vp);
    void CopyWaterDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* copy,
                        const D3DVIEWPORT9& vp, const D3DSURFACE_DESC& depthDesc);
    void PrepareFullscreenPasses(IDirect3DDevice9* dev);
    bool EnsureWaterWaves(IDirect3DDevice9* dev);
    void EnsureWaterTextures(IDirect3DDevice9* dev);
    bool DrawWaterWaves(IDirect3DDevice9* dev, const WaterWaveField& field);
    void BindWaterWaves(IDirect3DDevice9* dev);
    bool EnsureWaterMemory(IDirect3DDevice9* dev);
    bool UpdateWaterMemory(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg, float (*constants)[4]);
    bool DrawWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepthStencil,
                   IDirect3DSurface9* target, const D3DSURFACE_DESC& depthDesc, const FrameInputs& in,
                   const Config& cfg);
    void ReadWaterQuery();
    bool WaterResult(bool ok);
    void LogWater(const FrameInputs& in, const Config& cfg);

    bool EnsureShaders(IDirect3DDevice9* dev);
    bool EnsureStateBlock(IDirect3DDevice9* dev);
    bool EnsureTargets(IDirect3DDevice9* dev, UINT lowW, UINT lowH, UINT rayW, UINT rayH);
    bool EnsureSceneCopy(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT w, UINT h);
    bool EnsureDriftNoise(IDirect3DDevice9* dev);
    bool EnsureTerrainTexture(IDirect3DDevice9* dev);
    bool EnsureWaterRainTable(IDirect3DDevice9* dev);
    bool FillWaterRippleTable(IDirect3DDevice9* dev, const WaterRippleConstants& ripples);
    void NoteWaterRipple(const FrameInputs& in, bool swimming);
    bool FillWaterLampTable(IDirect3DDevice9* dev, const WaterLampTable& table);
    void DrawWaterLamps(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg);
    bool UploadTerrainShadows();
    bool UploadBankFloors(IDirect3DDevice9* dev);
    FogCeilingConstants UpdateFogCeiling(const FrameInputs& in, const Config& cfg, bool floorsKnown);
    TerrainShadowConstants UpdateTerrainShadows(const FrameInputs& in, const Config& cfg);
    void LogTerrainShadows(const FrameInputs& in, const Config& cfg);
    bool EnsureGlowTargets(IDirect3DDevice9* dev, UINT sourceW, UINT sourceH, UINT glowW, UINT glowH);
    void AdvanceDrift(const FrameInputs& in, const FogParams& fog, const Config& cfg);
    void BindDriftNoise(IDirect3DDevice9* dev, DWORD stage);
    bool EnsureCloudNoise(IDirect3DDevice9* dev);
    void AdvanceClouds(const FrameInputs& in, const FogParams& fog);
    void BindCloudNoise(IDirect3DDevice9* dev, DWORD stage);
    bool RenderLightGlow(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                         const D3DVIEWPORT9& vp, const Config& cfg);
    void BlurLightGlow(IDirect3DDevice9* dev, IDirect3DTexture9* source, IDirect3DTexture9* destination, float stepU,
                       float stepV);
    bool EnsureLampTargets(IDirect3DDevice9* dev, UINT lowW, UINT lowH);
    bool EnsureLampSurfaceTarget(IDirect3DDevice9* dev, UINT w, UINT h);
    bool EnsureWetSurfaces(IDirect3DDevice9* dev, UINT w, UINT h);
    bool RenderWetSurfaces(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fogTexture,
                           const FrameInputs& in, const Config& cfg, const FogParams& fog,
                           const TerrainShadowConstants& terrain, const float* cloudMap, const float* cloudShape,
                           float exposure, bool sceneBlend);
    void LogWetSurfaces(const Config& cfg);
    bool EnsureEdgeOccluders(IDirect3DDevice9* dev, UINT w, UINT h);
    void RenderEdgeOccluders(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const D3DVIEWPORT9& vp,
                             const CarrierBody& body);
    bool EnsureOccluderDepthTarget(IDirect3DDevice9* dev, UINT w, UINT h);
    bool EnsureShaftMemory(IDirect3DDevice9* dev, const D3DSURFACE_DESC& depthDesc, const D3DVIEWPORT9& vp);
    bool UpdateShaftMemory(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const D3DSURFACE_DESC& depthDesc,
                           const FrameInputs& in, const float* common, const CarrierBody& body);
    void LogShaftMemory(const Config& cfg);
    void BeginMemoryPoints(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexBuffer9* grid,
                           IDirect3DTexture9* vertexTexture);
    void EndMemoryPoints(IDirect3DDevice9* dev);
    bool UploadLampTable(IDirect3DDevice9* dev, const LampTable& table);
    bool UploadLampTiles(IDirect3DDevice9* dev, const LampTable& table, const FrameInputs& in);
    void RenderLamps(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fogTexture,
                     const FrameInputs& in, const Config& cfg, const FogParams& fog,
                     const TerrainShadowConstants& terrain, bool effectScene);
    bool EnsureEffectShaders(IDirect3DDevice9* dev);
    bool EnsureEffectBaseline(IDirect3DDevice9* dev, IDirect3DSurface9* target, UINT w, UINT h);
    bool EnsureEffectTargets(IDirect3DDevice9* dev, UINT w, UINT h);
    void ReleaseEffectTargets();
    const std::vector<EffectLamp>* UpdateEffects(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                                 const FrameInputs& in, const FogParams& fog, bool gather);
    bool TakeEffectMeasurement();
    void GatherEffects(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in);
    void LogEffects(const FogParams& fog);
    void LogLamps(const FogParams& fog);
    void LogLantern(const FrameInputs& in, const Config& cfg, const FogParams& fog);
    void LogConditionChanges(const FrameInputs& in, const FogParams& fog);
    bool CopyWorldViewport(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DVIEWPORT9& vp);
    bool EnsureSkyReference(IDirect3DDevice9* dev);
    bool SkyReferenceFresh(const FrameInputs& in) const;
    void MeasureSky(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                    const FrameInputs& in, const Config& cfg, const FogParams& fog);
    bool Skip(const char* reason);
    void LogLightChange(const FrameInputs& in, const AuthoredFog& fog, bool authored);
    bool DepthProbeDue(long long now) const;
    void LogDepthProbe(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DTexture9* fog,
                       const D3DVIEWPORT9& vp, float deepestWorldDepth, float dayFraction);
    void DrawFullscreen(IDirect3DDevice9* dev);
    void BindTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex, bool linear);
    bool RenderPasses(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                      const D3DSURFACE_DESC& depthDesc, const FrameInputs& in, const Config& cfg,
                      const ClientShadowFrame* clientShadows);
    void LogClientShadows(const ClientShadowFrame* clientShadows, const float* toLightInView, bool summaryDue);
    bool DrawClientShadowView(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                              const D3DVIEWPORT9& vp, const Config& cfg, const ClientShadowFrame* clientShadows);

    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_march[3] = {};
    IDirect3DPixelShader9* m_temporal = nullptr;
    IDirect3DPixelShader9* m_composite = nullptr;
    IDirect3DPixelShader9* m_rayMask = nullptr;
    IDirect3DPixelShader9* m_rayBlur = nullptr;
    IDirect3DPixelShader9* m_probe = nullptr;
    IDirect3DPixelShader9* m_glowMask = nullptr;
    IDirect3DPixelShader9* m_glowBlur = nullptr;
    IDirect3DPixelShader9* m_lampVisibilityShader = nullptr;
    IDirect3DPixelShader9* m_lampShader = nullptr;
    IDirect3DPixelShader9* m_lampSurfaceShader = nullptr;
    IDirect3DPixelShader9* m_lampBlurShader = nullptr;
    IDirect3DPixelShader9* m_clientShadowShader = nullptr;
    IDirect3DVertexDeclaration9* m_decl = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;

    IDirect3DTexture9* m_marchTarget = nullptr;
    IDirect3DTexture9* m_history[2] = {};
    IDirect3DTexture9* m_rays[2] = {};
    IDirect3DTexture9* m_sceneCopy = nullptr;
    IDirect3DPixelShader9* m_skyGatherShader = nullptr;
    IDirect3DPixelShader9* m_skyColumnShader = nullptr;
    IDirect3DPixelShader9* m_skyReferenceShader = nullptr;
    IDirect3DTexture9* m_skyGather = nullptr;
    IDirect3DTexture9* m_skyColumns = nullptr;
    IDirect3DTexture9* m_skyReference[2] = {};
    int m_skyReferenceIndex = 0;
    bool m_skyReferenceValid = false;
    bool m_skyReferenceFailed = false;
    bool m_skyReferenceFilterable = false;
    double m_skyReferenceSeconds = 0.0;
    float m_skyReferenceTarget[3] = {};
    IDirect3DTexture9* m_probeTarget = nullptr;
    IDirect3DSurface9* m_probeReadback = nullptr;
    IDirect3DVolumeTexture9* m_driftNoise = nullptr;
    bool m_driftNoiseFailed = false;
    IDirect3DTexture9* m_cloudNoise = nullptr;
    bool m_cloudNoiseFailed = false;
    TerrainShadowMap m_terrain;
    IDirect3DTexture9* m_terrainTexture = nullptr;
    bool m_terrainFilterable = false;
    bool m_terrainFallback = false;
    unsigned m_terrainUploaded = 0;
    IDirect3DTexture9* m_bankFloorTexture = nullptr;
    bool m_bankFloorFilterable = false;
    bool m_bankFloorFailed = false;
    bool m_bankFloorLogged = false;
    unsigned m_bankFloorUploaded = 0;
    float m_ceilingGate = 0.0f;
    double m_ceilingSeconds = -1.0;
    IDirect3DTexture9* m_glowSource = nullptr;
    IDirect3DTexture9* m_glow[2] = {};
    UINT m_glowSourceW = 0;
    UINT m_glowSourceH = 0;
    UINT m_glowW = 0;
    UINT m_glowH = 0;
    bool m_glowFailed = false;
    LampSelection m_lamps;
    BeltLantern m_lantern;
    IDirect3DTexture9* m_lampTable = nullptr;
    IDirect3DTexture9* m_lampTableStaging[kLampUploadRing] = {};
    int m_lampUpload = 0;
    IDirect3DTexture9* m_lampVisibility[2] = {};
    IDirect3DTexture9* m_lampHalos = nullptr;
    UINT m_lampW = 0;
    UINT m_lampH = 0;
    int m_lampVisibilityIndex = 0;
    bool m_lampsFailed = false;
    bool m_lampsDrawn = false;
    IDirect3DTexture9* m_lampSurfaces = nullptr;
    IDirect3DTexture9* m_lampSurfacesBlur = nullptr;
    LampTiles m_lampTiles;
    IDirect3DTexture9* m_lampTileTexture = nullptr;
    IDirect3DTexture9* m_lampTileStaging[kLampUploadRing] = {};
    UINT m_lampTileTextureW = 0;
    UINT m_lampTileTextureH = 0;
    int m_lampTileBytes = 0;
    bool m_lampTilesFailed = false;
    bool m_lampTilesUsed = false;
    UINT m_lampSurfaceW = 0;
    UINT m_lampSurfaceH = 0;
    bool m_lampSurfacesFailed = false;
    bool m_lampSurfacesDrawn = false;
    float m_wetLampSlots = 0.0f;
    float m_wetLampTiles[4] = {};
    SurfaceWetness m_wetness;
    WetSurfaceParams m_wetParams = {};
    IDirect3DPixelShader9* m_wetShader = nullptr;
    IDirect3DTexture9* m_wetTarget = nullptr;
    UINT m_wetW = 0;
    UINT m_wetH = 0;
    bool m_wetFailed = false;
    bool m_wetChecked = false;
    bool m_wetDrawn = false;
    bool m_waterBeforeFresh = false;
    unsigned m_wetFrames = 0;
    unsigned m_wetWaterFrames = 0;
    unsigned m_wetMaterialFrames = 0;
    bool m_wetMaterialsUsed = false;
    TerrainMaterialTargets m_terrainMaterials = {};
    IDirect3DPixelShader9* m_edgeOccluderShader = nullptr;
    IDirect3DTexture9* m_edgeOccluders = nullptr;
    UINT m_edgeW = 0;
    UINT m_edgeH = 0;
    bool m_edgeOccludersFailed = false;
    IDirect3DTexture9* m_occluderDepth = nullptr;
    UINT m_occluderDepthW = 0;
    UINT m_occluderDepthH = 0;
    bool m_occluderDepthFresh = false;
    bool m_occluderDepthFailed = false;
    struct EffectReadback
    {
        IDirect3DTexture9* target = nullptr;
        IDirect3DSurface9* copy = nullptr;
        IDirect3DQuery9* done = nullptr;
        bool pending = false;
        unsigned issuedFrame = 0;
        unsigned order = 0;
        EffectFrame frame = {};
    };
    IDirect3DPixelShader9* m_effectGatherShader = nullptr;
    IDirect3DPixelShader9* m_effectReduceShader = nullptr;
    IDirect3DTexture9* m_effectBaseline = nullptr;
    UINT m_effectBaselineW = 0;
    UINT m_effectBaselineH = 0;
    bool m_effectBaselineFresh = false;
    bool m_surfaceBeforeLateDraws = false;
    bool m_lateDrawsInFront = false;
    IDirect3DTexture9* m_sunAndMoonDrawn = nullptr;
    bool m_sunAndMoonShown = false;
    FrameProfile m_profile;
    IDirect3DTexture9* m_effectGather[2] = {};
    UINT m_effectW = 0;
    UINT m_effectH = 0;
    UINT m_effectGatherW = 0;
    UINT m_effectGatherH = 0;
    UINT m_effectColumns = 0;
    UINT m_effectRows = 0;
    EffectReadback m_effectRing[kEffectReadbackRing];
    unsigned m_effectOrder = 0;
    bool m_effectsFailed = false;
    bool m_effectsChecked = false;
    EffectMeasurement m_effectMeasurement = {};
    EffectTracker m_effectTracker;
    CorpseLights m_corpses;
    std::vector<EffectLamp> m_carried;
    EffectReport m_effectReport = {};
    EffectReport m_effectLog = {};
    IDirect3DPixelShader9* m_marchMemory[3] = {};
    IDirect3DVertexShader9* m_memoryScatterVs = nullptr;
    IDirect3DPixelShader9* m_memoryScatterPs = nullptr;
    IDirect3DPixelShader9* m_memoryKeepPs = nullptr;
    IDirect3DVertexShader9* m_memoryRealignVs = nullptr;
    IDirect3DPixelShader9* m_memoryFillPs = nullptr;
    IDirect3DVertexDeclaration9* m_memoryDecl = nullptr;
    IDirect3DVertexBuffer9* m_memoryGrid = nullptr;
    IDirect3DVertexBuffer9* m_memoryRealignGrid = nullptr;
    IDirect3DQuery9* m_memoryFlush = nullptr;
    IDirect3DTexture9* m_memory[2] = {};
    UINT m_memoryGridW = 0;
    UINT m_memoryGridH = 0;
    int m_memoryIndex = 0;
    long long m_memoryCell[2] = {};
    ShaftMemoryFrame m_memoryFrame = {{0.0, 1.0, 0.0}, {}, {0.0, 0.0, 1.0}, {}};
    float m_memoryLookup[4][4] = {};
    IDirect3DTexture9* m_memoryBefore = nullptr;
    ShaftMemoryFrame m_memoryBeforeFrame = {{0.0, 1.0, 0.0}, {}, {0.0, 0.0, 1.0}, {}};
    long long m_memoryBeforeCell[2] = {};
    float m_memoryBeforeLookup[4][4] = {};
    double m_memoryFadeStart = 0.0;
    double m_memoryFadeSeconds = 0.0;
    double m_memoryLastRealign = 0.0;
    bool m_memoryFading = false;
    bool m_memoryRealignedBefore = false;
    unsigned m_memoryFadedFrames = 0;
    int m_memoryMapId = 0;
    bool m_memoryValid = false;
    bool m_memoryFailed = false;
    bool m_memoryChecked = false;
    bool m_memoryUnsupported = false;
    unsigned m_memoryUpdates = 0;
    unsigned m_memoryForgets = 0;
    unsigned m_memoryForgetsTotal = 0;
    unsigned m_memoryRealignments = 0;
    unsigned m_memoryRealignmentsTotal = 0;
    unsigned m_memoryFallbackDepth = 0;
    IDirect3DPixelShader9* m_waterDepthShader = nullptr;
    IDirect3DPixelShader9* m_waterShader = nullptr;
    IDirect3DPixelShader9* m_waterReflectShader = nullptr;
    IDirect3DPixelShader9* m_waterWaveShader = nullptr;
    IDirect3DPixelShader9* m_waterSlopeShader = nullptr;
    IDirect3DTexture9* m_waterSlopes = nullptr;
    IDirect3DTexture9* m_waterWaveTable = nullptr;
    IDirect3DTexture9* m_waterWaves[kWaterWaveLayers] = {};
    IDirect3DTexture9* m_waterBump = nullptr;
    IDirect3DTexture9* m_waterFoam = nullptr;
    bool m_waterTexturesMade = false;
    IDirect3DTexture9* m_waterRainTable = nullptr;
    bool m_waterRainFailed = false;
    float m_waterRainShare = 0.0f;
    float m_waterSeaShare = 0.0f;
    double m_waterSeaSeconds = 0.0;
    unsigned m_waterSmoothFrames = 0;
    unsigned m_waterFloorFrames = 0;
    float m_waterRainStrength = 0.0f;
    WaterRipples m_waterRipples;
    IDirect3DTexture9* m_waterRippleTable = nullptr;
    bool m_waterRippleFailed = false;
    bool m_waterRipplesLogged = false;
    unsigned m_waterRippleStarts = 0;
    unsigned m_waterRippleSwims = 0;
    IDirect3DPixelShader9* m_waterLampShader = nullptr;
    IDirect3DTexture9* m_waterLampTable = nullptr;
    bool m_waterLampFailed = false;
    bool m_waterLampsLogged = false;
    WaterLampRecords m_waterLampRecords = {};
    unsigned m_waterLampFrames = 0;
    int m_waterLampMost = 0;
    int m_waterLampWindowsMost = 0;
    int m_waterLampRecoveredMost = 0;
    DWORD m_waterAnisotropy = 1;
    WaterWaveField m_waterField = {};
    IDirect3DTexture9* m_waterDepth[2] = {};
    IDirect3DTexture9* m_waterScene[2] = {};
    IDirect3DPixelShader9* m_waterMemoryShader = nullptr;
    IDirect3DTexture9* m_waterMemory[2] = {};
    IDirect3DQuery9* m_waterMemoryFlush = nullptr;
    int m_waterMemoryIndex = 0;
    bool m_waterMemoryValid = false;
    bool m_waterMemoryFailed = false;
    bool m_waterMemoryOn = false;
    float m_waterMemoryCamPos[3] = {};
    double m_waterMemorySeconds = 0.0;
    int m_waterMemoryMapId = 0;
    unsigned m_waterMemoryUpdates = 0;
    unsigned m_waterMemoryForgets = 0;
    const char* m_waterMemoryForgetReason = "";
    IDirect3DQuery9* m_waterQuery = nullptr;
    IDirect3DSurface9* m_waterTarget = nullptr;
    D3DVIEWPORT9 m_waterViewport = {};
    UINT m_waterDepthW = 0;
    UINT m_waterDepthH = 0;
    UINT m_waterSceneW = 0;
    UINT m_waterSceneH = 0;
    bool m_waterFailed = false;
    bool m_waterQueryFailed = false;
    bool m_waterQueryPending = false;
    DWORD m_waterMostPixels = 0;
    unsigned m_waterBegun = 0;
    unsigned m_waterDrawn = 0;
    unsigned m_waterInstances = 0;
    unsigned m_magmaInstances = 0;
    unsigned m_waterSkipsLogged = 0;
    const char* m_waterLastSkip = "";
    double m_driftSeconds = 0.0;
    bool m_driftClockValid = false;
    double m_coarseDrift[3] = {};
    double m_fineDrift[3] = {};
    double m_cloudSeconds = 0.0;
    bool m_cloudClockValid = false;
    double m_cloudOffset[2] = {};
    int m_loggedWeatherKind = -1;
    int m_loggedWeatherStep = -1;
    int m_loggedLightIsMoon = -1;
    UINT m_lowW = 0;
    UINT m_lowH = 0;
    UINT m_rayW = 0;
    UINT m_rayH = 0;
    UINT m_sceneCopyW = 0;
    UINT m_sceneCopyH = 0;
    bool m_sceneCopyFailed = false;
    bool m_probeFailed = false;
    bool m_fogFilterable = false;

    int m_historyIndex = 0;
    bool m_historyValid = false;
    float m_prevWorldToView[16] = {};
    float m_prevProj[16] = {};
    float m_prevCam[3] = {};
    D3DVIEWPORT9 m_prevViewport = {};
    UINT m_prevScale = 0;
    long long m_prevTicks = 0;
    unsigned m_frame = 0;
    unsigned m_logged = 0;
    long long m_summaryTicks = 0;
    long long m_probeTicks = 0;
    unsigned m_probeAttempts = 0;
    bool m_lightsLogged = false;
    float m_loggedFarClip = 0.0f;
    float m_loggedBlendMode = -1.0f;
    D3DVIEWPORT9 m_loggedViewport = {};
    uint32_t m_lightSignature = 0;
    int m_clientShadowsLogged = -1;
    int m_loggedClientShadowView = -1;
    int m_loggedClientShadowPairs = -1;
    const char* m_skip = "";
};
