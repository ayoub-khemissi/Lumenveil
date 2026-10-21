#include "engine.h"

#include "client_code.h"
#include "effect_lights.h"
#include "fog_model.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine
{
namespace
{
constexpr uintptr_t kClientImageBase = 0x00400000;

constexpr uintptr_t kGxDevice = 0x00C5DF88;
constexpr uintptr_t kGxD3DDevice = 0x397C;
constexpr uintptr_t kGxProjection = 0xF88;
constexpr uintptr_t kGxViewportDepthRange = 0xF80;
constexpr uintptr_t kGxViewIndex = 0x1AF8;
constexpr uintptr_t kGxViewBase = 0x1B00;
constexpr uintptr_t kGxViewStride = 64;
constexpr uint32_t kGxViewStackDepth = 64;
constexpr float kMinWorldDepthRangeSpan = 0.01f;

constexpr uintptr_t kFfxCVar = 0x00D45774;
constexpr uintptr_t kCurrentScreenEffect = 0x00D45780;
constexpr uintptr_t kGlowScreenEffect = 0x00B74364;
constexpr uintptr_t kScreenEffectEnableCVar = 0x4;
constexpr uintptr_t kCVarIntValue = 0x30;
constexpr uintptr_t kDayNightGlow = 0x00D38C2C;

constexpr uintptr_t kViewGlobal = 0x00ADF5E8;
constexpr uintptr_t kProjectionGlobal = 0x00ADF628;
constexpr uintptr_t kCameraPosition = 0x00CD8F5C;
constexpr uintptr_t kCameraTarget = 0x00CD8F68;
constexpr uintptr_t kCameraInLiquid = 0x00CD8794;
constexpr uintptr_t kClientMapId = 0x00ADFBC4;
constexpr uintptr_t kUpstreamMapId = 0x00AB63BC;
constexpr uintptr_t kFarClipMapRead = 0x00780803;
constexpr ClientCode kFarClipMapReadCode = {kFarClipMapRead, 5, 0xf496b995bc571e34ull};
constexpr ClientCode kZoneSetterCode = {kZoneSetter, 53, 0xcf84196ccbee8062ull};
constexpr size_t kZoneSetterStoreOffset = 38;
constexpr uintptr_t kWorldFrame = 0x00B7436C;
constexpr uintptr_t kWorldFrameFarClip = 0xB14;

constexpr uintptr_t kFogGroupStart[kDayNightFogGroupCount] = {0x00D38B90, 0x00D38BA4};
constexpr uintptr_t kFogGroupEnd[kDayNightFogGroupCount] = {0x00D38B94, 0x00D38BA8};

constexpr uintptr_t kDayFraction = 0x00D38B04;
constexpr uintptr_t kSkyCenter = 0x00D38B18;
constexpr uintptr_t kFogColor = 0x00D38BA0;
constexpr uintptr_t kAmbientColor = 0x00D38BD4;
constexpr uintptr_t kDirectColor = 0x00D38BD8;
constexpr uintptr_t kSunColor = 0x00D38BF8;
constexpr uintptr_t kSkyAboveHorizonColor = 0x00D38BEC;
constexpr uintptr_t kSkyHorizonColor = 0x00D38BF0;
constexpr uintptr_t kZoneFogDistance = 0x00D38C1C;
constexpr uintptr_t kSunPosition = 0x00D38E28;
constexpr uintptr_t kMoonPosition = 0x00D38E48;
constexpr uintptr_t kSunDayEnd = 0x00A41CA0;
constexpr uintptr_t kSunDayStart = 0x00A41CA4;
constexpr uintptr_t kDayNightScreenEffectLightSlot = 0x00D38B58;
constexpr uintptr_t kDayNightStormBlend = 0x00D38B88;

constexpr uintptr_t kWeather = 0x00CD7544;
constexpr uintptr_t kWeatherCurrentIntensity = 0x08;
constexpr uintptr_t kWeatherRainEffect = 0x13C;
constexpr uintptr_t kWeatherSnowEffect = 0x140;
constexpr uintptr_t kWeatherSandEffect = 0x144;

struct OpaqueState
{
    bool valid;
    D3DVIEWPORT9 viewport;
    float cameraRelativeView[16];
    float glProjection[16];
};

OpaqueState g_opaque = {};
bool g_groundProbeFailed = false;
bool g_indoorTestEnabled = false;
bool g_clientMapIdEnabled = false;
bool g_zoneReaderEnabled = false;
bool g_indoorTestFailed = false;
bool g_playerIndoors = false;
float g_indoorWeight = 0.0f;
double g_indoorSeconds = -1.0;

constexpr ClientCode kIntersectThunkCode = {kWorldIntersectThunk, 5, 0x49ec54d5f59cb615ull};
constexpr ClientCode kIntersectCode = {kWorldIntersect, 6, 0x140d44943c9e98b8ull};
constexpr uintptr_t kIntersectThunkSize = 5 + sizeof(int32_t);

constexpr ClientCode kLuaIsIndoorsCode = {kLuaIsIndoors, 45, 0x7a5eb044277759bcull};
constexpr ClientCode kUnitOutdoorsCode = {kUnitOutdoors, 22, 0x43fa526a98a55ce8ull};
constexpr double kMaxIndoorStepSeconds = 0.25;

constexpr ClientCode kLuaFacingCallCode = {kLuaFacingCall, 9, 0x6513d9246c08afd3ull};
constexpr ClientCode kMapPositionCallCode = {kMapPositionCall, 13, 0xbf0d6028e0ca56a2ull};
constexpr ClientCode kLuaIsSwimmingTestCode = {kLuaIsSwimmingTest, 10, 0x0ceedd0ecd01c594ull};
constexpr uintptr_t kLuaFacingVirtualByte = 6;
constexpr uintptr_t kMapPositionVirtualByte = 4;
constexpr uintptr_t kSwimmingTestFlagsField = 2;
constexpr uintptr_t kSwimmingTestFlag = 6;

constexpr ClientCode kEnumVisibleObjectsCode = {kEnumVisibleObjects, 100, 0xd769ffe4ed4ac62eull};

struct CorpseScan
{
    float from[3];
    int count;
    float position[kMaxCorpseInputs][3];
    uint32_t ids[kMaxCorpseInputs];
    float distanceSquared[kMaxCorpseInputs];
    uint32_t visited;
    int unitCount;
    UnitInput units[kMaxUnitInputs];
    uint64_t unitGuids[kMaxUnitInputs];
    uint64_t unitTargets[kMaxUnitInputs];
    float unitDistanceSquared[kMaxUnitInputs];
};

bool g_corpseReaderEnabled = false;
bool g_corpseReaderFailed = false;
bool g_corpseLogged = false;
bool g_unitsLogged = false;
double g_corpseScanSeconds = -1.0;
CorpseScan g_corpses = {};

struct PlayerPlacement
{
    bool known;
    float position[3];
    float facing;
    uint32_t moveFlags;
    uint32_t mountDisplay;
    uint32_t playerClass;
    bool targetKnown;
    uint64_t targetGuid;
    float targetPosition[3];
    float targetBoundingRadius;
};

bool g_playerReaderEnabled = false;
bool g_playerReaderFailed = false;
bool g_playerLogged = false;
bool g_targetLogged = false;
uint32_t g_loggedMountDisplay = 0;
bool g_mountLogged = false;
uint32_t g_loggedClass = 0;

template <typename T>
T Read(uintptr_t address)
{
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void ReadFloats(uintptr_t address, float* out, int count)
{
    std::memcpy(out, reinterpret_cast<const void*>(address), sizeof(float) * count);
}

bool Finite(const float* v, int count)
{
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}

bool IsPerspective(const float* p)
{
    return Finite(p, 16) && std::fabs(p[11] - 1.0f) < 1e-3f && std::fabs(p[15]) < 1e-3f &&
           std::fabs(p[0]) > 1e-4f && std::fabs(p[5]) > 1e-4f;
}

bool ReadGxMatrices(float* view, float* proj)
{
    uintptr_t gx = Read<uintptr_t>(kGxDevice);
    if (!gx)
        return false;
    uint32_t index = Read<uint32_t>(gx + kGxViewIndex);
    if (index >= kGxViewStackDepth)
        return false;
    ReadFloats(gx + kGxViewBase + index * kGxViewStride, view, 16);
    ReadFloats(gx + kGxProjection, proj, 16);
    return Finite(view, 16) && IsPerspective(proj);
}

bool IsWorldDepthRange(const float* minMaxZ)
{
    return Finite(minMaxZ, 2) && minMaxZ[0] >= 0.0f && minMaxZ[1] <= 1.0f &&
           minMaxZ[1] - minMaxZ[0] > kMinWorldDepthRangeSpan;
}

void ApplyPendingGxViewportDepthRange(D3DVIEWPORT9& viewport)
{
    uintptr_t gx = Read<uintptr_t>(kGxDevice);
    if (!gx)
        return;
    float minMaxZ[2];
    ReadFloats(gx + kGxViewportDepthRange, minMaxZ, 2);
    if (IsWorldDepthRange(minMaxZ))
    {
        viewport.MinZ = minMaxZ[0];
        viewport.MaxZ = minMaxZ[1];
    }
}

bool CaptureOpaqueStateUnsafe(IDirect3DDevice9* device, OpaqueState& state)
{
    if (FAILED(device->GetViewport(&state.viewport)))
        return false;
    ApplyPendingGxViewportDepthRange(state.viewport);
    if (!ReadGxMatrices(state.cameraRelativeView, state.glProjection))
    {
        ReadFloats(kViewGlobal, state.cameraRelativeView, 16);
        ReadFloats(kProjectionGlobal, state.glProjection, 16);
    }
    return state.viewport.Width > 0 && state.viewport.Height > 0 && IsPerspective(state.glProjection) &&
           Finite(state.cameraRelativeView, 16);
}

void Normalize(float* v)
{
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f)
    {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 1.0f;
        return;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

bool CVarEnabled(uintptr_t cvar)
{
    return cvar && Read<int32_t>(cvar + kCVarIntValue) != 0;
}

float GlowScreenEffectAmount()
{
    uintptr_t effect = Read<uintptr_t>(kCurrentScreenEffect);
    if (!CVarEnabled(Read<uintptr_t>(kFfxCVar)) || !effect || effect != Read<uintptr_t>(kGlowScreenEffect))
        return 0.0f;
    float glow = Read<float>(kDayNightGlow);
    if (!CVarEnabled(Read<uintptr_t>(effect + kScreenEffectEnableCVar)) || !std::isfinite(glow))
        return 0.0f;
    return std::clamp(glow, 0.0f, 1.0f);
}

LightParamsSelection ReadLightParamsSelection()
{
    LightParamsSelection selection;
    const float storm = Read<float>(kDayNightStormBlend);
    selection.stormBlend = std::isfinite(storm) ? std::clamp(storm, 0.0f, 1.0f) : 0.0f;
    const int32_t slot = Read<int32_t>(kDayNightScreenEffectLightSlot);
    if (slot >= 0 && slot < FogData::kLightParamsSlots)
        selection.screenEffectSlot = slot;
    return selection;
}

void DirectionFromSkyCentre(uintptr_t body, const float* centre, float* out)
{
    float position[3];
    ReadFloats(body, position, 3);
    out[0] = position[0] - centre[0];
    out[1] = position[1] - centre[1];
    out[2] = position[2] - centre[2];
    Normalize(out);
}

double MonotonicSeconds()
{
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / frequency;
}

void ReadWeather(FrameInputs& out)
{
    out.weather = WeatherKind::Clear;
    out.weatherIntensity = 0.0f;
    const uintptr_t weather = Read<uintptr_t>(kWeather);
    if (!weather)
        return;
    if (Read<uintptr_t>(weather + kWeatherRainEffect))
        out.weather = WeatherKind::Rain;
    else if (Read<uintptr_t>(weather + kWeatherSnowEffect))
        out.weather = WeatherKind::Snow;
    else if (Read<uintptr_t>(weather + kWeatherSandEffect))
        out.weather = WeatherKind::Sand;
    const float intensity = Read<float>(weather + kWeatherCurrentIntensity);
    if (out.weather != WeatherKind::Clear && std::isfinite(intensity))
        out.weatherIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

bool IntersectGuarded(const float* start, const float* end, float* hit, float* fraction, bool& found)
{
    __try
    {
        found = reinterpret_cast<WorldIntersectFn>(kWorldIntersectThunk)(start, end, hit, fraction,
                                                                         kGroundProbeFlags, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PlayerIndoorsGuarded(bool& indoors)
{
    __try
    {
        indoors = false;
        const uint64_t guid = reinterpret_cast<ActivePlayerGuidFn>(kActivePlayerGuid)();
        if (!guid)
            return true;
        void* unit = reinterpret_cast<ObjectPointerFn>(kObjectPointer)(
            guid, kUnitTypeMask, reinterpret_cast<const char*>(kObjectPointerFile), kObjectPointerLine);
        if (unit)
            indoors = !reinterpret_cast<UnitOutdoorsFn>(kUnitOutdoors)(unit, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

float ReadIndoorWeight(double seconds)
{
    if (!g_indoorTestEnabled || g_indoorTestFailed)
        return 0.0f;
    bool indoors = false;
    if (!PlayerIndoorsGuarded(indoors))
    {
        g_indoorTestFailed = true;
        VF_LOG_ERROR("exception in the client's indoor test; night darkness applies indoors for this session");
        return 0.0f;
    }
    if (indoors != g_playerIndoors)
    {
        g_playerIndoors = indoors;
        VF_LOG_INFO("indoors: the player is %s", indoors ? "indoors" : "outdoors");
    }
    const double step = g_indoorSeconds < 0.0 ? kIndoorFadeSeconds : seconds - g_indoorSeconds;
    g_indoorSeconds = seconds;
    g_indoorWeight = IndoorWeight(g_indoorWeight, indoors, std::clamp(step, 0.0, kMaxIndoorStepSeconds));
    return g_indoorWeight;
}

bool PlayerPlacementGuarded(PlayerPlacement& placement)
{
    __try
    {
        placement.known = false;
        const uint64_t guid = reinterpret_cast<ActivePlayerGuidFn>(kActivePlayerGuid)();
        if (!guid)
            return true;
        void* unit = reinterpret_cast<ObjectPointerFn>(kObjectPointer)(
            guid, kUnitTypeMask, reinterpret_cast<const char*>(kObjectPointerFile), kObjectPointerLine);
        if (!unit)
            return true;
        const uintptr_t vtable = Read<uintptr_t>(reinterpret_cast<uintptr_t>(unit));
        float buffer[3] = {};
        const float* position = reinterpret_cast<UnitPositionFn>(Read<uintptr_t>(vtable + kUnitPositionVirtual))(
            unit, nullptr, buffer);
        placement.facing = reinterpret_cast<UnitFacingFn>(Read<uintptr_t>(vtable + kUnitFacingVirtual))(unit, nullptr);
        ReadFloats(reinterpret_cast<uintptr_t>(position), placement.position, 3);
        placement.moveFlags = Read<uint32_t>(reinterpret_cast<uintptr_t>(unit) + kUnitMovementFlags);
        placement.known = true;
        placement.targetKnown = false;
        const uintptr_t fields = Read<uintptr_t>(reinterpret_cast<uintptr_t>(unit) + kObjectFields);
        placement.mountDisplay = fields ? Read<uint32_t>(fields + kUnitMountDisplayField) : 0;
        placement.playerClass = fields ? Read<uint8_t>(fields + kUnitClassByte) : 0;
        placement.targetGuid = fields ? Read<uint64_t>(fields + kUnitTargetField) : 0;
        if (!placement.targetGuid)
            return true;
        void* target = reinterpret_cast<ObjectPointerFn>(kObjectPointer)(
            placement.targetGuid, kUnitObjectTypeMask, reinterpret_cast<const char*>(kObjectPointerFile),
            kObjectPointerLine);
        if (!target)
            return true;
        const uintptr_t targetFields = Read<uintptr_t>(reinterpret_cast<uintptr_t>(target) + kObjectFields);
        if (!targetFields)
            return true;
        const uintptr_t targetVtable = Read<uintptr_t>(reinterpret_cast<uintptr_t>(target));
        float targetBuffer[3] = {};
        const float* targetPosition = reinterpret_cast<UnitPositionFn>(
            Read<uintptr_t>(targetVtable + kUnitPositionVirtual))(target, nullptr, targetBuffer);
        ReadFloats(reinterpret_cast<uintptr_t>(targetPosition), placement.targetPosition, 3);
        placement.targetBoundingRadius = Read<float>(targetFields + kUnitBoundingRadiusField);
        placement.targetKnown = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void KeepUnit(CorpseScan& scan, uint64_t guid, uintptr_t fields, const float* at, float squared)
{
    if (squared > kUnitScanYards * kUnitScanYards)
        return;
    int slot = scan.unitCount;
    if (scan.unitCount == kMaxUnitInputs)
    {
        slot = 0;
        for (int k = 1; k < kMaxUnitInputs; ++k)
            if (scan.unitDistanceSquared[k] > scan.unitDistanceSquared[slot])
                slot = k;
        if (scan.unitDistanceSquared[slot] <= squared)
            return;
    }
    else
        ++scan.unitCount;
    UnitInput& u = scan.units[slot];
    std::memcpy(u.feet, at, sizeof(u.feet));
    u.boundingRadius = Read<float>(fields + kUnitBoundingRadiusField);
    u.target = -1;
    u.guid = guid;
    scan.unitGuids[slot] = guid;
    scan.unitTargets[slot] = Read<uint64_t>(fields + kUnitTargetField);
    scan.unitDistanceSquared[slot] = squared;
}

int __cdecl VisitCorpse(uint64_t guid, void* context)
{
    CorpseScan& scan = *static_cast<CorpseScan*>(context);
    if (++scan.visited > kMaxVisitedObjects)
        return 0;
    const uint32_t high = static_cast<uint32_t>(guid >> 48);
    const bool creature = high == kCreatureGuidHigh || high == kVehicleGuidHigh;
    if (!creature && high != kPlayerGuidHigh && high != kPetGuidHigh)
        return 1;
    void* unit = reinterpret_cast<ObjectPointerFn>(kObjectPointer)(
        guid, kUnitObjectTypeMask, reinterpret_cast<const char*>(kObjectPointerFile), kObjectPointerLine);
    if (!unit)
        return 1;
    const uintptr_t base = reinterpret_cast<uintptr_t>(unit);
    const uint32_t type = Read<uint32_t>(base + kObjectTypeId);
    if (type != kUnitTypeId && type != kPlayerTypeId)
        return 1;
    const uintptr_t fields = Read<uintptr_t>(base + kObjectFields);
    if (!fields)
        return 1;
    const bool alive = Read<uint32_t>(fields + kUnitHealthField) != 0;
    const bool lootable = creature && type == kUnitTypeId && !alive &&
                          (Read<uint32_t>(fields + kUnitDynamicFlagsField) & kUnitLootable);
    if (!alive && !lootable)
        return 1;
    const uintptr_t vtable = Read<uintptr_t>(base);
    float buffer[3] = {};
    const float* position =
        reinterpret_cast<UnitPositionFn>(Read<uintptr_t>(vtable + kUnitPositionVirtual))(unit, nullptr, buffer);
    float at[3];
    ReadFloats(reinterpret_cast<uintptr_t>(position), at, 3);
    if (!Finite(at, 3))
        return 1;
    const float d[3] = {at[0] - scan.from[0], at[1] - scan.from[1], at[2] - scan.from[2]};
    const float squared = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (alive)
    {
        KeepUnit(scan, guid, fields, at, squared);
        return 1;
    }
    if (squared > kCorpseScanYards * kCorpseScanYards)
        return 1;
    int slot = scan.count;
    if (scan.count == kMaxCorpseInputs)
    {
        slot = 0;
        for (int k = 1; k < kMaxCorpseInputs; ++k)
            if (scan.distanceSquared[k] > scan.distanceSquared[slot])
                slot = k;
        if (scan.distanceSquared[slot] <= squared)
            return 1;
    }
    else
        ++scan.count;
    std::memcpy(scan.position[slot], at, sizeof(at));
    scan.ids[slot] = static_cast<uint32_t>(guid);
    scan.distanceSquared[slot] = squared;
    return 1;
}

bool ScanCorpsesGuarded(CorpseScan& scan)
{
    __try
    {
        reinterpret_cast<EnumVisibleObjectsFn>(kEnumVisibleObjects)(VisitCorpse, &scan);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RefreshUnitFeetGuarded(CorpseScan& scan)
{
    __try
    {
        for (int i = 0; i < scan.unitCount; ++i)
        {
            void* unit = reinterpret_cast<ObjectPointerFn>(kObjectPointer)(
                scan.unitGuids[i], kUnitObjectTypeMask, reinterpret_cast<const char*>(kObjectPointerFile),
                kObjectPointerLine);
            if (!unit)
                continue;
            const uintptr_t vtable = Read<uintptr_t>(reinterpret_cast<uintptr_t>(unit));
            float buffer[3] = {};
            const float* position = reinterpret_cast<UnitPositionFn>(Read<uintptr_t>(vtable + kUnitPositionVirtual))(
                unit, nullptr, buffer);
            float at[3];
            ReadFloats(reinterpret_cast<uintptr_t>(position), at, 3);
            if (Finite(at, 3))
                std::memcpy(scan.units[i].feet, at, sizeof(at));
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void ReadCorpses(FrameInputs& out)
{
    out.corpseCount = 0;
    out.unitCount = 0;
    if (!g_corpseReaderEnabled || g_corpseReaderFailed)
        return;
    if (g_corpseScanSeconds < 0.0 || out.seconds - g_corpseScanSeconds >= kCorpseScanSeconds ||
        out.seconds < g_corpseScanSeconds)
    {
        g_corpseScanSeconds = out.seconds;
        CorpseScan scan = {};
        std::memcpy(scan.from, out.camPos, sizeof(scan.from));
        if (!ScanCorpsesGuarded(scan))
        {
            g_corpseReaderFailed = true;
            g_corpses = {};
            VF_LOG_ERROR("exception walking the client's objects; no light from lootable corpses and no spell "
                         "glows placed at units for this session");
            return;
        }
        int targeted = 0;
        for (int i = 0; i < scan.unitCount; ++i)
            for (int j = 0; j < scan.unitCount && scan.unitTargets[i]; ++j)
                if (j != i && scan.unitGuids[j] == scan.unitTargets[i])
                {
                    scan.units[i].target = j;
                    ++targeted;
                    break;
                }
        g_corpses = scan;
        if (!g_unitsLogged && scan.unitCount > 0)
        {
            g_unitsLogged = true;
            VF_LOG_INFO("units: %d alive within %.0f yd of %u objects, %d with their target among them (spell glows "
                        "light from them and the paths to their targets)",
                        scan.unitCount, kUnitScanYards, scan.visited, targeted);
        }
        if (!g_corpseLogged && scan.count > 0)
        {
            g_corpseLogged = true;
            VF_LOG_INFO("corpses: %d lootable within %.0f yd of %u objects, the nearest at %.1f yd", scan.count,
                        kCorpseScanYards, scan.visited,
                        std::sqrt(*std::min_element(scan.distanceSquared, scan.distanceSquared + scan.count)));
        }
    }
    else if (!RefreshUnitFeetGuarded(g_corpses))
    {
        g_corpseReaderFailed = true;
        g_corpses = {};
        VF_LOG_ERROR("exception reading where the units stand; no light from lootable corpses and no spell glows "
                     "placed at units for this session");
        return;
    }
    out.corpseCount = g_corpses.count;
    std::memcpy(out.corpsePos, g_corpses.position, sizeof(out.corpsePos));
    std::memcpy(out.corpseIds, g_corpses.ids, sizeof(out.corpseIds));
    out.unitCount = g_corpses.unitCount;
    std::memcpy(out.units, g_corpses.units, sizeof(out.units));
}

void ReadPlayer(FrameInputs& out)
{
    out.playerKnown = false;
    out.targetKnown = false;
    if (!g_playerReaderEnabled || g_playerReaderFailed)
        return;
    PlayerPlacement placement = {};
    if (!PlayerPlacementGuarded(placement))
    {
        g_playerReaderFailed = true;
        VF_LOG_ERROR("exception reading the player's position; no lantern for this session");
        return;
    }
    if (!placement.known || !Finite(placement.position, 3) || !std::isfinite(placement.facing))
        return;
    out.playerKnown = true;
    std::memcpy(out.playerPos, placement.position, sizeof(out.playerPos));
    out.playerFacing = placement.facing;
    out.playerMoveFlags = placement.moveFlags;
    out.playerMountDisplay = placement.mountDisplay;
    out.playerClass = placement.playerClass;
    if (placement.playerClass != g_loggedClass)
    {
        g_loggedClass = placement.playerClass;
        VF_LOG_INFO("player: class %u; %s", placement.playerClass,
                    EffectHandGlows(placement.playerClass)
                        ? "a glow at your hands lights like any glow, and what leaves them goes on lighting"
                        : "a glow at your weapon is its muzzle flash: on for a moment, then dark");
    }
    if (placement.mountDisplay != g_loggedMountDisplay)
    {
        g_loggedMountDisplay = placement.mountDisplay;
        if (!g_mountLogged && placement.mountDisplay)
        {
            g_mountLogged = true;
            VF_LOG_INFO("player: mounted (mount display %u); the shafts leave out the mount around you",
                        placement.mountDisplay);
        }
        else
            VF_LOG_DEBUG("player: mount display %u", placement.mountDisplay);
    }
    out.targetKnown = placement.targetKnown && Finite(placement.targetPosition, 3);
    if (out.targetKnown)
    {
        std::memcpy(out.targetPos, placement.targetPosition, sizeof(out.targetPos));
        out.targetBoundingRadius = placement.targetBoundingRadius;
        if (!g_targetLogged)
        {
            g_targetLogged = true;
            VF_LOG_INFO("target: 0x%016llX at %.1f %.1f %.1f, bounding radius %.2f yd (the selection ring gives no "
                        "effect light)",
                        static_cast<unsigned long long>(placement.targetGuid), placement.targetPosition[0],
                        placement.targetPosition[1], placement.targetPosition[2], placement.targetBoundingRadius);
        }
    }
    if (!g_playerLogged)
    {
        g_playerLogged = true;
        VF_LOG_INFO("player: at %.1f %.1f %.1f facing %.2f, movement flags 0x%08X", placement.position[0],
                    placement.position[1], placement.position[2], placement.facing, placement.moveFlags);
    }
}

bool BuildFrameInputsUnsafe(FrameInputs& out)
{
    std::memcpy(out.cameraRelativeView, g_opaque.cameraRelativeView, sizeof(out.cameraRelativeView));
    std::memcpy(out.glProjection, g_opaque.glProjection, sizeof(out.glProjection));
    out.viewport = g_opaque.viewport;
    ReadFloats(kCameraPosition, out.camPos, 3);
    ReadFloats(kCameraTarget, out.camTarget, 3);
    out.seconds = MonotonicSeconds();

    out.dayFraction = Read<float>(kDayFraction);
    float dayStart = Read<float>(kSunDayStart);
    float dayEnd = Read<float>(kSunDayEnd);
    const bool clientMoonHours = !(out.dayFraction >= dayStart && out.dayFraction <= dayEnd);
    float center[3];
    float toSun[3];
    float toMoon[3];
    ReadFloats(kSkyCenter, center, 3);
    DirectionFromSkyCentre(kSunPosition, center, toSun);
    DirectionFromSkyCentre(kMoonPosition, center, toMoon);
    out.sunElevation = toSun[2];
    out.moonElevation = toMoon[2];
    out.lightIsMoon = MoonLightsTheFog(clientMoonHours, out.sunElevation, out.moonElevation);
    std::memcpy(out.toLight, out.lightIsMoon ? toMoon : toSun, sizeof(out.toLight));
    ReadWeather(out);

    out.fogColor = Read<uint32_t>(kFogColor);
    out.fogStart = Read<float>(kFogGroupStart[kFrameInputsFogGroup]);
    out.fogEnd = Read<float>(kFogGroupEnd[kFrameInputsFogGroup]);
    out.sunColor = Read<uint32_t>(kSunColor);
    out.directColor = Read<uint32_t>(kDirectColor);
    out.ambientColor = Read<uint32_t>(kAmbientColor);
    out.inLiquid = CameraInLiquid();
    out.mapId = Read<int32_t>(g_clientMapIdEnabled ? kClientMapId : kUpstreamMapId);
    out.zoneId = g_zoneReaderEnabled ? Read<int32_t>(kZoneId) : -1;
    out.skyHorizonColor = Read<uint32_t>(kSkyHorizonColor);
    out.skyAboveHorizonColor = Read<uint32_t>(kSkyAboveHorizonColor);
    out.lightParams = ReadLightParamsSelection();
    out.indoors = ReadIndoorWeight(out.seconds);
    ReadPlayer(out);
    ReadCorpses(out);

    out.zoneFogDistance = Read<float>(kZoneFogDistance);
    out.clientGlowAmount = out.inLiquid ? 0.0f : GlowScreenEffectAmount();
    out.farClip = out.glProjection[14] / (1.0f - out.glProjection[10]);
    if (!(out.farClip > 10.0f && out.farClip < 100000.0f))
    {
        uintptr_t worldFrame = Read<uintptr_t>(kWorldFrame);
        out.farClip = worldFrame ? Read<float>(worldFrame + kWorldFrameFarClip) : 0.0f;
    }
    if (!(out.farClip > 10.0f && out.farClip < 100000.0f))
        out.farClip = 1000.0f;

    return Finite(out.camPos, 3) && Finite(out.toLight, 3) && std::isfinite(out.fogEnd) &&
           std::isfinite(out.fogStart);
}
}

bool IsSupportedClient()
{
    auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleA(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp == kClientTimestamp && nt->OptionalHeader.ImageBase == kClientImageBase;
}

void* GameD3DDevice()
{
    __try
    {
        uintptr_t gx = Read<uintptr_t>(kGxDevice);
        return gx ? Read<void*>(gx + kGxD3DDevice) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool CameraInLiquid()
{
    return Read<uint32_t>(kCameraInLiquid) != 0;
}

bool GroundProbeMatchesClient()
{
    int32_t jump;
    std::memcpy(&jump, reinterpret_cast<const void*>(kWorldIntersectThunk + kIntersectThunkCode.size), sizeof(jump));
    return ClientCodeMatches(kIntersectThunkCode) &&
           kWorldIntersectThunk + kIntersectThunkSize + jump == kWorldIntersect && ClientCodeMatches(kIntersectCode);
}

bool IndoorTestMatchesClient()
{
    return ClientCodeMatches(kLuaIsIndoorsCode) && ClientCodeMatches(kUnitOutdoorsCode);
}

void EnableIndoorTest()
{
    g_indoorTestEnabled = true;
}

bool PlayerReaderMatchesClient()
{
    return ClientCodeMatches(kLuaFacingCallCode) && ClientCodeMatches(kMapPositionCallCode) &&
           ClientCodeMatches(kLuaIsSwimmingTestCode) &&
           Read<uint8_t>(kLuaFacingCall + kLuaFacingVirtualByte) == kUnitFacingVirtual &&
           Read<uint8_t>(kMapPositionCall + kMapPositionVirtualByte) == kUnitPositionVirtual &&
           std::memcmp(reinterpret_cast<const void*>(kLuaIsSwimmingTest + kSwimmingTestFlagsField),
                       &kUnitMovementFlags, 2) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(kLuaIsSwimmingTest + kSwimmingTestFlag), &kMovementSwimming,
                       sizeof(kMovementSwimming)) == 0;
}

void EnablePlayerReader()
{
    g_playerReaderEnabled = true;
}

bool ObjectListMatchesClient()
{
    return ClientCodeMatches(kEnumVisibleObjectsCode);
}

void EnableCorpseReader()
{
    g_corpseReaderEnabled = true;
}

bool ClientMapIdMatchesClient()
{
    return ClientCodeMatches(kFarClipMapReadCode);
}

void EnableClientMapId()
{
    g_clientMapIdEnabled = true;
}

bool ZoneReaderMatchesClient()
{
    return ClientCodeMatches(kZoneSetterCode) && Read<uintptr_t>(kZoneSetter + kZoneSetterStoreOffset) == kZoneId;
}

void EnableZoneReader()
{
    g_zoneReaderEnabled = true;
}

float IndoorWeight(float current, bool indoors, double stepSeconds)
{
    const float change = static_cast<float>(stepSeconds / kIndoorFadeSeconds);
    return indoors ? std::min(current + change, 1.0f) : std::max(current - change, 0.0f);
}

bool ProbeGroundHeight(void*, float x, float y, float top, float bottom, float& height)
{
    if (g_groundProbeFailed)
        return false;
    const float start[3] = {x, y, top};
    const float end[3] = {x, y, bottom};
    float hit[3] = {x, y, bottom};
    float fraction = 1.0f;
    bool found = false;
    if (!IntersectGuarded(start, end, hit, &fraction, found))
    {
        g_groundProbeFailed = true;
        VF_LOG_ERROR("exception in the world collision test at (%.0f %.0f); terrain shadows off for this session", x,
                     y);
        return false;
    }
    height = hit[2];
    return found && fraction >= 0.0f && fraction <= 1.0f;
}

StockFog ReadStockFog()
{
    StockFog fog;
    for (int group = 0; group < kDayNightFogGroupCount; ++group)
    {
        fog.start[group] = Read<float>(kFogGroupStart[group]);
        fog.end[group] = Read<float>(kFogGroupEnd[group]);
    }
    return fog;
}

void WriteStockFog(const StockFog& fog)
{
    for (int group = 0; group < kDayNightFogGroupCount; ++group)
    {
        *reinterpret_cast<float*>(kFogGroupStart[group]) = fog.start[group];
        *reinterpret_cast<float*>(kFogGroupEnd[group]) = fog.end[group];
    }
}

void CaptureOpaqueState(IDirect3DDevice9* device)
{
    OpaqueState state = {};
    bool ok = false;
    __try
    {
        ok = CaptureOpaqueStateUnsafe(device, state);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    state.valid = ok;
    g_opaque = state;
}

bool HasOpaqueState()
{
    return g_opaque.valid;
}

bool OpaqueViewport(D3DVIEWPORT9& out)
{
    if (!g_opaque.valid)
        return false;
    out = g_opaque.viewport;
    return true;
}

void ClearOpaqueState()
{
    g_opaque.valid = false;
}

bool BuildFrameInputs(FrameInputs& out)
{
    if (!g_opaque.valid)
        return false;
    __try
    {
        return BuildFrameInputsUnsafe(out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}
