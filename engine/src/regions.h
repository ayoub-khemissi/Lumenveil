#pragma once

#include "engine.h"
#include "region_settings.h"
#include "region_zones.h"
#include "world_controls.h"

#include <string>
#include <vector>

constexpr const char* kRegionsFile = "CoAVolFog-regions.ini";
constexpr const char* kZonesFile = "CoAVolFog-zones.txt";

enum class EditZone
{
    Here,
    All,
};


struct RegionsLive
{
    bool known;
    int zone;
    int area;
    int map;
    float dayFraction;
    WeatherKind weather;
    float weatherIntensity;
    bool playerKnown;
    float position[3];
    float facing;
};

struct RegionsRuntime
{
    std::string directory;
    std::string regionsPath;
    std::string zonesPath;
    RegionStore store;
    ZoneAtlas atlas;
    RegionBlender blender;
    ClockOverride clock;
    WeatherOverride weather;
    bool clientControls = false;
    RegionsLive live = {};
    double liveAt = -1.0e9;
    double lastReload = -1.0e9;
    double panelSeenAt = -1.0e9;
    EditZone editZone = EditZone::Here;
    int editTime = kFollowChoice;
    int editSky = kFollowChoice;
    bool preview = true;
    int keysInEffect = 0;
    float fade = 1.0f;
    bool saveFailed = false;
};

constexpr float kRegionPanelTestWidth = 1280.0f;
constexpr float kRegionPanelTestHeight = 720.0f;

struct RegionPanelReport
{
    int keysInEffect;
    int slots;
    int zones;
    int clientControls;
    Config look;
};

RegionsRuntime& Regions();
double RegionsNow();
void RegionsAttach(const std::string& directory);
void RegionsInstallClient();
void RegionsOnWorld(const FrameInputs& in);
void RegionsAtPresent();
bool RegionsInWorld();
bool RegionsPreviewing();
RegionSlot EditedSlot(const RegionsRuntime& r);
std::string RegionZoneName(int zone);
void PressToggleKey();
