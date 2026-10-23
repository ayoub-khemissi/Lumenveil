#include "client_shadows.h"
#include "config.h"
#include "d3d9_wrap.h"
#include "draw_log.h"
#include "engine.h"
#include "fog_data.h"
#include "hooks.h"
#include "lamp_catalog.h"
#include "liquid_map.h"
#include "log.h"
#include "material_catalog.h"
#include "overlay.h"
#include "region_panel.h"
#include "regions.h"
#include "relief_catalog.h"
#include "renderer.h"
#include "spots.h"
#include "terrain_shadow.h"
#include "water_textures.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
std::string ModuleDirectory(HMODULE module)
{
    char path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
    std::string dir(path, n);
    size_t slash = dir.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
}

void InstallGroundProbe()
{
    if (!engine::GroundProbeMatchesClient())
    {
        VF_LOG_ERROR("world collision test at 0x%08X differs from the 12340 client; no terrain shadows",
                     static_cast<unsigned>(engine::kWorldIntersectThunk));
        return;
    }
    SetHeightSource(&engine::ProbeGroundHeight, nullptr);
    VF_LOG_INFO("terrain shadows: ground heights from the world collision test 0x%08X (flags 0x%X)",
                static_cast<unsigned>(engine::kWorldIntersectThunk), engine::kGroundProbeFlags);
}

void InstallIndoorTest()
{
    if (!engine::IndoorTestMatchesClient())
    {
        VF_LOG_ERROR("the client's IsIndoors at 0x%08X differs from the 12340 client; night darkness applies indoors",
                     static_cast<unsigned>(engine::kLuaIsIndoors));
        return;
    }
    engine::EnableIndoorTest();
    VF_LOG_INFO("indoors: from the client's own test 0x%08X (the one behind IsIndoors)",
                static_cast<unsigned>(engine::kUnitOutdoors));
}

void InstallPlayerReader()
{
    if (!engine::PlayerReaderMatchesClient())
    {
        VF_LOG_ERROR("the client's GetPlayerFacing, GetPlayerMapPosition or IsSwimming differ from the 12340 client; "
                     "the player's position is not read");
        return;
    }
    engine::EnablePlayerReader();
    VF_LOG_INFO("player: position and facing from the unit's own virtuals (vtable +0x%X, +0x%X), movement flags at "
                "+0x%X",
                static_cast<unsigned>(engine::kUnitPositionVirtual), static_cast<unsigned>(engine::kUnitFacingVirtual),
                static_cast<unsigned>(engine::kUnitMovementFlags));
}

void InstallCorpseReader()
{
    if (!engine::ObjectListMatchesClient())
    {
        VF_LOG_ERROR("the client's object enumeration at 0x%08X differs from the 12340 client; no light from "
                     "lootable corpses",
                     static_cast<unsigned>(engine::kEnumVisibleObjects));
        return;
    }
    engine::EnableCorpseReader();
    VF_LOG_INFO("corpses: lootable ones from the client's object list 0x%08X (dead units with the lootable dynamic "
                "flag), every %.1f s",
                static_cast<unsigned>(engine::kEnumVisibleObjects), engine::kCorpseScanSeconds);
}

void InstallClientMapId()
{
    if (!engine::ClientMapIdMatchesClient())
    {
        VF_LOG_ERROR("the far clip's map read differs from the 12340 client; the map id is read from the old "
                     "address, which reads -1 after a ship or zeppelin trip");
        return;
    }
    engine::EnableClientMapId();
    VF_LOG_INFO("map id: the one the client passes to its far clip clamp");
}

void InstallZoneReader()
{
    if (!engine::ZoneReaderMatchesClient())
    {
        VF_LOG_ERROR("the client's zone setter at 0x%08X differs from the 12340 client; no zone climates (dust, "
                     "humid haze, cloud shadows)",
                     static_cast<unsigned>(engine::kZoneSetter));
        return;
    }
    engine::EnableZoneReader();
    VF_LOG_INFO("zone: the id the client's zone setter 0x%08X stores at 0x%08X", static_cast<unsigned>(engine::kZoneSetter),
                static_cast<unsigned>(engine::kZoneId));
}

void Attach(HMODULE module)
{
    std::string dir = ModuleDirectory(module);
    LogOpen((dir + "CoAVolFog.log").c_str());
    ConfigStore::Upgrade(dir + "CoAVolFog.ini");
    GlobalConfig().Load(dir + "CoAVolFog.ini");
    DrawLogSetDirectory(dir);
    SpotsSetDirectory(dir);
    RegionsAttach(dir);
    const Config& cfg = GlobalConfig().Get();
    VF_LOG_INFO("CoAVolFog loaded from %s", dir.c_str());
    GlobalFogData().Load(dir + "fogdata.bin");
    GlobalLampCatalog().Load(dir + "lampdata.bin");
    GlobalReliefCatalog().Load(dir + "reliefdata.bin");
    GlobalMaterialCatalog().Load(dir + "materialdata.bin");
    GlobalWaterTextureFiles().Load(dir + "Lumenveil\\");
    if (GlobalLiquidMap().Load(dir + "liquiddata.bin"))
        SetLiquidSource(&LiquidMap::Probe, &GlobalLiquidMap());

    if (!engine::IsSupportedClient())
    {
        VF_LOG_INFO("host is not the 3.3.5a (12340) client; engine hooks skipped");
        return;
    }
    if (!cfg.enable)
    {
        VF_LOG_INFO("Enable=0; the client runs unmodified");
        return;
    }
    if (!cfg.hooks)
    {
        VF_LOG_INFO("EngineHooks=0; the client runs unmodified");
        return;
    }
    const bool hooked = InstallEngineHooks();
    AllowFogOnNewDevices(hooked);
    if (hooked)
    {
        InstallGroundProbe();
        InstallIndoorTest();
        InstallPlayerReader();
        InstallCorpseReader();
        InstallClientMapId();
        InstallZoneReader();
        RegionsInstallClient();
        InstallWaterHooks();
    }
    InstallFarClipHooks();
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        Attach(instance);
    }
    return TRUE;
}

extern "C" int __cdecl vf_loader_anchor()
{
    return 1;
}

extern "C" IDirect3D9* __cdecl vf_test_wrap_direct3d9(Direct3DCreate9Fn realCreate, UINT sdkVersion)
{
    SetRealDirect3DCreate9(realCreate);
    AllowFogOnNewDevices(true);
    return WrappedDirect3DCreate9(sdkVersion);
}

extern "C" void __cdecl vf_test_set_config(const Config* cfg)
{
    GlobalConfig().Override(*cfg);
    ForgetShaftMemory(LatestFogDevice());
}

extern "C" void __cdecl vf_test_get_config(Config* out)
{
    *out = GlobalConfig().Get();
}

extern "C" void __cdecl vf_test_force_depth_write(int force)
{
    ForceDepthWrite(LatestFogDevice(), force != 0);
}

extern "C" void __cdecl vf_test_liquid_begin(const D3DVIEWPORT9* world, int cameraInLiquid)
{
    BeginLiquidSurfaces(LatestFogDevice(), world, cameraInLiquid != 0, GlobalConfig().Get());
}

extern "C" void __cdecl vf_test_liquid_material(int magma, int begin)
{
    MarkLiquidInstance(LatestFogDevice(), magma != 0, begin != 0);
}

extern "C" void __cdecl vf_test_liquid_end(const FrameInputs* in)
{
    FogDevice* device = LatestFogDevice();
    FinishLiquidSurfaces(device, in, GlobalConfig().Get());
    EndLiquidSurfaces(device);
    if (in)
        CaptureOccluderDepth(device, in->viewport, GlobalConfig().Get());
}

extern "C" int __cdecl vf_test_render(const FrameInputs* in, const char** skipReason)
{
    return RenderFog(LatestFogDevice(), *in, GlobalConfig().Get(), skipReason) ? 1 : 0;
}

extern "C" void __cdecl vf_test_set_height_source(HeightProbe probe, void* context)
{
    SetHeightSource(probe, context);
}

extern "C" void __cdecl vf_test_set_liquid_source(LiquidProbe probe, void* context)
{
    SetLiquidSource(probe, context);
}

extern "C" int __cdecl vf_test_set_lamp_data(const void* bytes, unsigned size)
{
    if (!bytes)
    {
        GlobalLampCatalog().Clear();
        return 1;
    }
    return GlobalLampCatalog().Parse(bytes, size, "the harness") ? 1 : 0;
}

extern "C" int __cdecl vf_test_set_relief_data(const void* bytes, unsigned size)
{
    if (!bytes)
    {
        GlobalReliefCatalog().Clear();
        return 1;
    }
    return GlobalReliefCatalog().Parse(bytes, size, "the harness") ? 1 : 0;
}

extern "C" int __cdecl vf_test_set_material_data(const void* bytes, unsigned size)
{
    if (!bytes)
    {
        GlobalMaterialCatalog().Clear();
        return 1;
    }
    return GlobalMaterialCatalog().Parse(bytes, size, "the harness") ? 1 : 0;
}

extern "C" int __cdecl vf_test_runtime_heights(unsigned milliseconds, RuntimeHeightStats* stats, unsigned* cached)
{
    return WaitRuntimeHeights(LatestFogDevice(), milliseconds, *stats, *cached) ? 1 : 0;
}

extern "C" int __cdecl vf_test_relief_stats(TerrainReliefStats* stats, ReliefDrawInputs* last)
{
    return ReadTerrainRelief(LatestFogDevice(), *stats, *last) ? 1 : 0;
}

extern "C" void __cdecl vf_test_set_sky_texture(int kind, unsigned long long hash, unsigned format, unsigned width,
                                                 unsigned height)
{
    if (!hash)
    {
        SetTestSkyTexture(static_cast<SkyKind>(kind), nullptr);
        return;
    }
    const SkyTexture texture = {hash, format, width, height, static_cast<SkyKind>(kind), nullptr};
    SetTestSkyTexture(static_cast<SkyKind>(kind), &texture);
}

extern "C" int __cdecl vf_test_sky_kind(unsigned long long hash, unsigned format, unsigned width, unsigned height)
{
    const SkyTexture* sky = FindSkyTexture(hash, format, width, height);
    return sky ? static_cast<int>(sky->kind) : 0;
}

extern "C" void __cdecl vf_test_redraw_sky_moon()
{
    RedrawSkyMoon(LatestFogDevice());
}

extern "C" int __cdecl vf_test_sky_stats(SkyFilterStats* stats)
{
    return ReadSkyFilter(LatestFogDevice(), *stats) ? 1 : 0;
}

extern "C" void __cdecl vf_test_move_drift_patches(const double* tileFraction)
{
    MoveDriftPatches(LatestFogDevice(), tileFraction);
}

extern "C" void __cdecl vf_test_profile_report(ProfileReport* out, int reset)
{
    ProfileReportOf(LatestFogDevice(), *out, reset != 0);
}

extern "C" void __cdecl vf_test_set_cloud_offset(const double* yards){
    SetCloudOffset(LatestFogDevice(), yards);
}

extern "C" int __cdecl vf_test_read_shaft_memory(float* codes, ShaftMemoryReadback* info)
{
    return ReadShaftMemory(LatestFogDevice(), codes, *info) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_water_waves(int layer, int level, float* out, unsigned floats)
{
    return ReadWaterWaves(LatestFogDevice(), layer, level, out, floats) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_water_slopes(float* out, unsigned floats, unsigned* width, unsigned* height)
{
    return ReadWaterSlopes(LatestFogDevice(), out, floats, width, height) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_water_memory(float* out, unsigned floats, unsigned* width, unsigned* height)
{
    return ReadWaterMemory(LatestFogDevice(), out, floats, width, height) ? 1 : 0;
}

extern "C" void __cdecl vf_test_forget_water_memory()
{
    ForgetWaterMemory(LatestFogDevice());
}

extern "C" int __cdecl vf_test_read_sky_reference(float* out, unsigned floats)
{
    return ReadSkyReference(LatestFogDevice(), out, floats) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_wetness(float* out)
{
    return ReadWetness(LatestFogDevice(), out) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_wet_surfaces(float* out, unsigned floats, unsigned* width, unsigned* height)
{
    return ReadWetSurfaces(LatestFogDevice(), out, floats, width, height) ? 1 : 0;
}

extern "C" int __cdecl vf_test_read_ground_materials(unsigned char* params, unsigned char* tags, unsigned bytes,
                                                     unsigned* width, unsigned* height)
{
    return ReadGroundMaterials(LatestFogDevice(), params, tags, bytes, width, height) ? 1 : 0;
}

extern "C" void __cdecl vf_test_effect_report(EffectReport* out, int reset)
{
    ReportEffects(LatestFogDevice(), *out, reset != 0);
}

extern "C" void __cdecl vf_test_set_lamp_blur(int enabled)
{
    SetLampLightBlur(enabled != 0);
}

extern "C" void __cdecl vf_test_set_glint_share(float share)
{
    SetWetGlintShare(share);
}

extern "C" void __cdecl vf_test_set_puddle_threshold(float threshold)
{
    SetWetPuddleThreshold(threshold);
}

extern "C" void __cdecl vf_test_set_plain_game(int on)
{
    GlobalConfig().SetPlainGame(on != 0);
}

extern "C" int __cdecl vf_test_far_clip_refresh_requests()
{
    return FarClipRefreshRequests();
}

extern "C" void __cdecl vf_test_set_lamp_tiles(int mode)
{
    SetLampTiles(mode);
}


extern "C" int __cdecl vf_test_lamp_tile_stats(LampTileStats* out)
{
    return ReadLampTiles(LatestFogDevice(), *out) ? 1 : 0;
}

extern "C" void __cdecl vf_test_set_shaft_memory_fade(int enabled)
{
    SetShaftMemoryFade(enabled != 0);
}

extern "C" void __cdecl vf_test_set_bank_wisps(int enabled)
{
    SetBankWisps(enabled != 0);
}

extern "C" int __cdecl vf_test_overlay_visible()
{
    return OverlayVisible() ? 1 : 0;
}

extern "C" int __cdecl vf_test_region_panel_visible()
{
    return RegionPanelVisible() ? 1 : 0;
}

extern "C" int __cdecl vf_test_region_panel(const char* directory, const FrameInputs* in, int frames,
                                            RegionPanelReport* report)
{
    RegionsAttach(directory);
    Regions().blender.Reset();
    if (in)
        RegionsOnWorld(*in);
    const int drawn = DrawRegionPanelWithoutDevice(frames, kRegionPanelTestWidth, kRegionPanelTestHeight);
    *report = {};
    report->keysInEffect = Regions().keysInEffect;
    report->look = GlobalConfig().Get();
    report->slots = static_cast<int>(Regions().store.Book().slots.size());
    report->zones = static_cast<int>(Regions().atlas.ZoneCount());
    report->clientControls = Regions().clientControls ? 1 : 0;
    Regions().store.Load(std::string());
    Regions().atlas = ZoneAtlas();
    Regions().blender.Reset();
    GlobalConfig().SetLookLayer(EmptyLookLayer());
    return drawn;
}

extern "C" void __cdecl vf_test_region_edit(int timeChoice, int skyChoice, int panelSeen)
{
    RegionsRuntime& r = Regions();
    r.editZone = EditZone::Here;
    r.editTime = timeChoice;
    r.editSky = skyChoice;
    r.preview = true;
    r.panelSeenAt = panelSeen ? RegionsNow() : -1.0e9;
}

extern "C" void __cdecl vf_test_draw_overlay()
{
    DrawOverlay(RealDevice(LatestFogDevice()));
}

extern "C" void __cdecl vf_test_spots_directory(const char* directory)
{
    SpotsSetDirectory(directory);
}

extern "C" int __cdecl vf_test_save_spot(const FrameInputs* in)
{
    return SaveSpotIfArmed(*in, GlobalConfig().Get()) ? 1 : 0;
}

extern "C" IDirect3DDevice9* __cdecl vf_test_real_device()
{
    return RealDevice(LatestFogDevice());
}

extern "C" void __cdecl vf_test_draw_log_directory(const char* directory)
{
    DrawLogSetDirectory(directory);
}

extern "C" void __cdecl vf_test_draw_log_mark(int phase)
{
    DrawLogMark(static_cast<DrawPhase>(phase));
}

extern "C" void __cdecl vf_test_draw_log_stats(DrawLogStats* out, int reset)
{
    *out = DrawLogStatistics();
    if (reset)
        DrawLogResetStatistics();
}

extern "C" unsigned __cdecl vf_test_draw_log_summary(char* out, unsigned size)
{
    const std::string& summary = DrawLogLastSummary();
    if (out && size)
    {
        const size_t n = std::min<size_t>(summary.size(), size - 1);
        std::memcpy(out, summary.data(), n);
        out[n] = 0;
    }
    return static_cast<unsigned>(summary.size());
}

extern "C" void __cdecl vf_test_set_client_shader_role(unsigned long long hash, int role)
{
    SetExtraClientShaderRole(hash, static_cast<ClientShaderRole>(role));
}

extern "C" int __cdecl vf_test_client_shader_role(unsigned long long hash)
{
    return static_cast<int>(KnownClientShaderRole(hash));
}

extern "C" unsigned __cdecl vf_test_client_shader_reads(const DWORD* code, unsigned bytes)
{
    return ClientShaderConstantReads(code, bytes / sizeof(DWORD));
}

extern "C" void __cdecl vf_test_client_shadow_stats(ClientShadowStats* out, int reset)
{
    *out = ClientShadowStatistics();
    if (reset)
        ResetClientShadowStatistics();
}

extern "C" void __cdecl vf_test_client_shadow_last_frame(ClientShadowFrame* out)
{
    *out = LastClientShadowFrame();
}

extern "C" unsigned __cdecl vf_test_client_shadow_log(char* out, unsigned size)
{
    const std::string& line = LastClientShadowLogLine();
    if (out && size)
    {
        const size_t n = std::min<size_t>(line.size(), size - 1);
        std::memcpy(out, line.data(), n);
        out[n] = 0;
    }
    return static_cast<unsigned>(line.size());
}
