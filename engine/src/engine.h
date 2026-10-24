#pragma once

#include <windows.h>
#include <d3d9.h>

#include "fog_data.h"

#include <cstdint>

enum class WeatherKind
{
    Clear,
    Rain,
    Snow,
    Sand,
};

constexpr int kMaxCorpseInputs = 16;
constexpr int kMaxUnitInputs = 32;
constexpr int kMaxObjectInputs = 48;

struct UnitInput
{
    float feet[3];
    float boundingRadius;
    int target;
    uint64_t guid;
};

struct ObjectInput
{
    float position[3];
    float facing;
    float scale;
    uint32_t displayId;
    uint64_t guid;
};

struct FrameInputs
{
    float cameraRelativeView[16];
    float glProjection[16];
    float camPos[3];
    float camTarget[3];
    D3DVIEWPORT9 viewport;
    double seconds;
    float dayFraction;
    float toLight[3];
    bool lightIsMoon;
    float sunElevation;
    float moonElevation;
    WeatherKind weather;
    float weatherIntensity;
    uint32_t fogColor;
    uint32_t sunColor;
    uint32_t directColor;
    uint32_t ambientColor;
    float fogStart;
    float fogEnd;
    float zoneFogDistance;
    float farClip;
    float clientFarClip;
    float clientGlowAmount;
    bool inLiquid;
    int mapId;
    int zoneId;
    uint32_t skyHorizonColor;
    uint32_t skyAboveHorizonColor;
    uint32_t waterBandColors[4];
    float waterBandAlphas[4];
    float dryLand;
    float greenLand;
    LightParamsSelection lightParams;
    float indoors;
    bool playerKnown;
    float playerPos[3];
    float playerFacing;
    uint32_t playerMoveFlags;
    uint32_t playerMountDisplay;
    uint32_t playerClass;
    bool targetKnown;
    float targetPos[3];
    float targetBoundingRadius;
    int corpseCount;
    float corpsePos[kMaxCorpseInputs][3];
    uint32_t corpseIds[kMaxCorpseInputs];
    int unitCount;
    UnitInput units[kMaxUnitInputs];
    int objectCount;
    ObjectInput objects[kMaxObjectInputs];
};

namespace engine
{
constexpr uint32_t kClientTimestamp = 0x4C2452FE;

constexpr uintptr_t kGetProcAddressSlot = 0x00B2ED98;
constexpr uintptr_t kGetProcAddressThunk = 0x0041C654;

constexpr uintptr_t kWorldRenderSite = 0x004FB03D;
constexpr uintptr_t kWorldRenderTarget = 0x004F8EA0;
constexpr uintptr_t kOpaqueM2PassSite = 0x004F911D;
constexpr uintptr_t kOpaqueM2PassTarget = 0x00823CB0;
constexpr uintptr_t kLiquidSurfaceSite = 0x004F9170;
constexpr uintptr_t kLiquidSurfaceTarget = 0x0077F020;
constexpr uintptr_t kScreenEffectsSite = 0x004F9281;
constexpr uintptr_t kScreenEffectsTarget = 0x008C1010;

using FarClipClampFn = float(__cdecl*)(float farClipSetting, int mapId);
using FarClipSetFn = void(__cdecl*)(float farClipSetting);
constexpr uintptr_t kFarClipClamp = 0x00780770;
constexpr uintptr_t kFarClipSet = 0x00780800;
constexpr uintptr_t kFarClipCVarSetSite = 0x00780810;
constexpr uintptr_t kFarClipMapLoadSite = 0x00781444;

constexpr uintptr_t kZoneSetter = 0x005204C0;
constexpr uintptr_t kZoneId = 0x00BD080C;

using WorldIntersectFn = bool(__cdecl*)(const float* start, const float* end, float* hit, float* fraction,
                                        uint32_t flags, void* details);
constexpr uintptr_t kWorldIntersectThunk = 0x0077F310;
constexpr uintptr_t kWorldIntersect = 0x007A3B70;
constexpr uint32_t kIntersectWmo = 0x10;
constexpr uint32_t kIntersectTerrain = 0x100;
constexpr uint32_t kGroundProbeFlags = kIntersectTerrain | kIntersectWmo;

using ActivePlayerGuidFn = uint64_t(__cdecl*)();
using ObjectPointerFn = void*(__cdecl*)(uint64_t guid, uint32_t typeMask, const char* file, uint32_t line);
using UnitOutdoorsFn = bool(__fastcall*)(void* unit, void* unused);
constexpr uintptr_t kLuaIsIndoors = 0x00612300;
constexpr uintptr_t kActivePlayerGuid = 0x004D3790;
constexpr uintptr_t kObjectPointer = 0x004D4DB0;
constexpr uintptr_t kUnitOutdoors = 0x0071B7F0;
constexpr uint32_t kUnitTypeMask = 0x10;
constexpr uintptr_t kObjectPointerFile = 0x009F2340;
constexpr uint32_t kObjectPointerLine = 0xA0;
constexpr double kIndoorFadeSeconds = 0.5;

using UnitPositionFn = const float*(__fastcall*)(void* unit, void* unused, float* out);
using UnitFacingFn = float(__fastcall*)(void* unit, void* unused);
constexpr uintptr_t kUnitPositionVirtual = 0x2C;
constexpr uintptr_t kUnitFacingVirtual = 0x34;
constexpr uintptr_t kUnitMovementFlags = 0xA30;
constexpr uint32_t kMovementSwimming = 0x00200000;
constexpr uintptr_t kLuaFacingCall = 0x0060A4DF;
constexpr uintptr_t kMapPositionCall = 0x00544531;
constexpr uintptr_t kLuaIsSwimmingTest = 0x006124C2;

using VisitObjectFn = int(__cdecl*)(uint64_t guid, void* context);
using EnumVisibleObjectsFn = int(__cdecl*)(VisitObjectFn visit, void* context);
constexpr uintptr_t kEnumVisibleObjects = 0x004D4B30;
constexpr uint32_t kUnitObjectTypeMask = 0x8;
constexpr uintptr_t kObjectTypeId = 0x14;
constexpr uint32_t kUnitTypeId = 3;
constexpr uint32_t kPlayerTypeId = 4;
constexpr uintptr_t kObjectFields = 0x8;
constexpr uintptr_t kUnitHealthField = 0x60;
constexpr uintptr_t kUnitDynamicFlagsField = 0x13C;
constexpr uintptr_t kUnitTargetField = 0x48;
constexpr uintptr_t kUnitBoundingRadiusField = 0x104;
constexpr uintptr_t kUnitMountDisplayField = 0x114;
constexpr uintptr_t kUnitClassByte = 0x5D;
constexpr uint32_t kUnitLootable = 0x1;
constexpr uint32_t kCreatureGuidHigh = 0xF130;
constexpr uint32_t kVehicleGuidHigh = 0xF150;
constexpr uint32_t kPlayerGuidHigh = 0x0000;
constexpr uint32_t kPetGuidHigh = 0xF140;
constexpr uint32_t kGameObjectGuidHigh = 0xF110;
constexpr uint32_t kGameObjectTypeMask = 0x20;
constexpr uint32_t kGameObjectTypeId = 5;
constexpr uintptr_t kObjectScaleField = 0x10;
constexpr uintptr_t kGameObjectDisplayField = 0x20;
constexpr float kMinObjectScale = 0.01f;
constexpr float kMaxObjectScale = 50.0f;
constexpr float kObjectScanYards = 250.0f;
constexpr uint32_t kMaxVisitedObjects = 4096;
constexpr double kCorpseScanSeconds = 0.1;
constexpr float kUnitScanYards = 80.0f;
constexpr float kCorpseScanYards = 60.0f;

struct LiquidMaterialClass
{
    const char* name;
    uintptr_t vtable;
    uintptr_t slots[3];
    bool magma;
};
constexpr int kLiquidMaterialDrawSlot = 2;
constexpr uintptr_t kLiquidInstancesSite = 0x00790AA2;
constexpr uintptr_t kLiquidInstancesTarget = 0x008A2240;
constexpr LiquidMaterialClass kLiquidMaterials[] = {
    {"CMaterialProcWater", 0x00A59518, {0x008A4740, 0x008A3DE0, 0x008A48F0}, false},
    {"CMaterialWater", 0x00A59544, {0x008A47C0, 0x008A3F50, 0x008A5590}, false},
    {"CMaterialWaterNoSpec", 0x00A59578, {0x008A4820, 0x008A4050, 0x008A5900}, false},
    {"CMaterialMagma", 0x00A595A4, {0x008A48A0, 0x008A4170, 0x008A6090}, true},
    {"CMaterialProcWaterFFP", 0x00A594CC, {0x008C6780, 0x008A3F30, 0x008A5170}, false},
    {"CMaterialWaterFFP", 0x00A594D8, {0x008C6780, 0x008A4150, 0x008A5C70}, false},
    {"CMaterialMagmaFFP", 0x00A594E4, {0x008C6780, 0x008A4260, 0x008A6350}, true},
};
constexpr int kLiquidMaterialCount = sizeof(kLiquidMaterials) / sizeof(kLiquidMaterials[0]);

bool IsSupportedClient();
void* GameD3DDevice();
bool CameraInLiquid();
bool GroundProbeMatchesClient();
bool IndoorTestMatchesClient();
void EnableIndoorTest();
bool ClientMapIdMatchesClient();
bool ZoneReaderMatchesClient();
void EnableZoneReader();
void EnableClientMapId();
float IndoorWeight(float current, bool indoors, double stepSeconds);
bool PlayerReaderMatchesClient();
void EnablePlayerReader();
bool ObjectListMatchesClient();
void EnableCorpseReader();
bool ProbeGroundHeight(void* context, float x, float y, float top, float bottom, float& height);

constexpr int kDayNightFogGroupCount = 2;
constexpr int kFrameInputsFogGroup = 1;

struct StockFog
{
    float start[kDayNightFogGroupCount];
    float end[kDayNightFogGroupCount];
};
StockFog ReadStockFog();
void WriteStockFog(const StockFog& fog);

void CaptureOpaqueState(IDirect3DDevice9* device);
bool HasOpaqueState();
bool OpaqueViewport(D3DVIEWPORT9& out);
void ClearOpaqueState();

bool BuildFrameInputs(FrameInputs& out);
}
