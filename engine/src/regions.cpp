#include "regions.h"

#include "client_world.h"
#include "config.h"
#include "hooks.h"
#include "log.h"

#include <windows.h>

namespace
{
constexpr double kReloadSeconds = 1.0;
constexpr double kWorldGoneSeconds = 2.0;
constexpr double kPanelGoneSeconds = 0.5;
constexpr size_t kMaxLoggedProblems = 8;

RegionsRuntime g_regions;
GameClock g_gameClock;
GameWeather g_gameWeather;

int KeysInEffect(const LookLayer& layer)
{
    int count = 0;
    for (size_t i = 0; i < layer.weight.size(); ++i)
        if (layer.weight[i] > 0.0f && LiveSettingAt(static_cast<int>(i)).regional)
            ++count;
    return count;
}

}

RegionsRuntime& Regions()
{
    return g_regions;
}

double RegionsNow()
{
    static LARGE_INTEGER frequency = {};
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

std::string RegionZoneName(int zone)
{
    return g_regions.atlas.ZoneName(zone);
}

void RegionsAttach(const std::string& directory)
{
    RegionsRuntime& r = g_regions;
    r.directory = directory;
    r.regionsPath = directory + kRegionsFile;
    r.zonesPath = directory + kZonesFile;
    r.store.Load(r.regionsPath);
    VF_LOG_INFO("regions: %zu region slots and %zu zones with progress from %s", r.store.Book().slots.size(),
                r.store.Book().progress.size(), r.regionsPath.c_str());
    std::vector<std::string> problems;
    if (!r.atlas.Load(r.zonesPath, problems))
        VF_LOG_INFO("regions: no %s; the checklist lists only the zones that have settings", r.zonesPath.c_str());
    else
        VF_LOG_INFO("regions: %zu zones and %zu areas from %s", r.atlas.ZoneCount(), r.atlas.AreaCount(),
                    r.zonesPath.c_str());
    for (size_t i = 0; i < problems.size() && i < kMaxLoggedProblems; ++i)
        VF_LOG_ERROR("regions: %s: %s", r.zonesPath.c_str(), problems[i].c_str());
}

void RegionsInstallClient()
{
    if (!ClientWorldMatchesClient())
    {
        VF_LOG_ERROR("regions: the client's clock or weather functions differ from the 12340 client; the region "
                     "panel cannot set the time or weather");
        return;
    }
    EnableClientWorld();
    g_regions.clientControls = true;
    VF_LOG_INFO("regions: time from the client's own clock setter 0x0076D810, weather from its weather setter "
                "0x007846A0");
}

bool RegionsInWorld()
{
    return RegionsNow() - g_regions.liveAt < kWorldGoneSeconds;
}

bool RegionsPreviewing()
{
    return g_regions.preview && RegionsNow() - g_regions.panelSeenAt < kPanelGoneSeconds;
}

RegionSlot EditedSlot(const RegionsRuntime& r)
{
    return ChooseSlot(r.editZone == EditZone::Here, r.live.zone, r.editTime, r.editSky, r.live.dayFraction,
                      r.live.weather);
}

void RegionsOnWorld(const FrameInputs& in)
{
    RegionsRuntime& r = g_regions;
    const double now = RegionsNow();
    if (now - r.lastReload >= kReloadSeconds)
    {
        r.lastReload = now;
        r.store.ReloadIfChanged();
    }
    r.live.known = true;
    r.live.zone = in.zoneId;
    r.live.area = CurrentAreaId();
    r.live.map = in.mapId;
    r.live.dayFraction = in.dayFraction;
    r.live.weather = in.weather;
    r.live.weatherIntensity = in.weatherIntensity;
    r.live.playerKnown = in.playerKnown;
    for (int i = 0; i < 3; ++i)
        r.live.position[i] = in.playerPos[i];
    r.live.facing = in.playerFacing;
    r.liveAt = now;

    const SlotWeights weights = SlotWeightsAt(in.dayFraction, in.weather, in.weatherIntensity);
    LookLayer layer = r.blender.Update(in.zoneId, RegionLayer(r.store.Book(), in.zoneId, weights), in.seconds);
    r.fade = r.blender.Progress();
    if (RegionsPreviewing())
    {
        const RegionSlot slot = EditedSlot(r);
        const auto it = r.store.Book().slots.find(slot);
        if (it != r.store.Book().slots.end() && (slot.zone == kAllZones || slot.zone == in.zoneId))
            ForceLookValues(layer, it->second);
    }
    r.keysInEffect = KeysInEffect(layer);
    GlobalConfig().SetLookLayer(layer);
}


void PressToggleKey()
{
    ConfigStore& store = GlobalConfig();
    const bool plain = !store.PlainGame();
    store.SetPlainGame(plain);
    RequestFarClipRefresh();
    VF_LOG_INFO("toggle: %s pressed, %s", HotkeyName(store.Global().toggleKey).c_str(),
                plain ? "the plain game: no fog, lights, water, relief or lifted view distance"
                      : "the fog and its lights again");
}

void RegionsAtPresent()
{
    RegionsRuntime& r = g_regions;
    if (!RegionsInWorld())
        return;
    RefreshFarClipNow();
    if (!r.clientControls || ClientWorldFailed())
        return;
    const double now = RegionsNow();
    r.clock.Update(g_gameClock, now);
    r.weather.Update(g_gameWeather, now);
}
