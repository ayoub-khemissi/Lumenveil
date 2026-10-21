#pragma once

#include "config.h"
#include "effect_lights.h"
#include "engine.h"
#include "frame_profile.h"
#include "lamp_tiles.h"
#include "shaft_memory.h"
#include "sky_filter.h"
#include "terrain_relief.h"

#include <d3d9.h>

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);

void SetRealDirect3DCreate9(Direct3DCreate9Fn fn);
IDirect3D9* WINAPI WrappedDirect3DCreate9(UINT sdkVersion);

void AllowFogOnNewDevices(bool allowedOnNewDevices);

class FogDevice;
FogDevice* LatestFogDevice();
FogDevice* WrapperOrLatestFogDevice(void* gameDevice);
bool IsWrapperOf(FogDevice* device, void* gameDevice);
IDirect3DDevice9* RealDevice(FogDevice* device);
void ForceDepthWrite(FogDevice* device, bool force);
void BeginLiquidSurfaces(FogDevice* device, const D3DVIEWPORT9* world, bool cameraInLiquid, const Config& cfg);
void MarkLiquidInstance(FogDevice* device, bool magma, bool begin);
void FinishLiquidSurfaces(FogDevice* device, const FrameInputs* in, const Config& cfg);
void EndLiquidSurfaces(FogDevice* device);
void CaptureOccluderDepth(FogDevice* device, const D3DVIEWPORT9& world, const Config& cfg);
bool RenderFog(FogDevice* device, const FrameInputs& in, const Config& cfg, const char** skipReason);
void MoveDriftPatches(FogDevice* device, const double* tileFraction);
void SetCloudOffset(FogDevice* device, const double* yards);
void ProfileReportOf(FogDevice* device, ProfileReport& out, bool reset);
bool ReadLampTiles(FogDevice* device, LampTileStats& stats);
bool ReadShaftMemory(FogDevice* device, float* codes, ShaftMemoryReadback& info);
void ForgetShaftMemory(FogDevice* device);
bool ReadWaterWaves(FogDevice* device, int layer, int level, float* out, unsigned floats);
bool ReadWaterSlopes(FogDevice* device, float* out, unsigned floats, unsigned* width, unsigned* height);
bool ReadWaterMemory(FogDevice* device, float* out, unsigned floats, unsigned* width, unsigned* height);
void ForgetWaterMemory(FogDevice* device);
bool ReadSkyReference(FogDevice* device, float* out, unsigned floats);
bool ReadWetness(FogDevice* device, float* out);
bool ReadWetSurfaces(FogDevice* device, float* out, unsigned floats, unsigned* width, unsigned* height);
bool ReadGroundMaterials(FogDevice* device, uint8_t* params, uint8_t* tags, size_t bytes, unsigned* width,
                         unsigned* height);
void ReportEffects(FogDevice* device, EffectReport& out, bool reset);
bool ReadTerrainRelief(FogDevice* device, TerrainReliefStats& stats, ReliefDrawInputs& last);
bool WaitRuntimeHeights(FogDevice* device, unsigned milliseconds, RuntimeHeightStats& stats, unsigned& cached);
void RedrawSkyMoon(FogDevice* device);
bool ReadSkyFilter(FogDevice* device, SkyFilterStats& stats);
