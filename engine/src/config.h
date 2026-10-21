#pragma once

#include "relief_classes.h"

#include <cstdint>
#include <string>
#include <vector>

constexpr float kEngineFarClipMin = 183.333f;
constexpr float kEngineFarClipMax = 1583.33f;
constexpr float kFarClipMaxKeepsClientCap = 0.0f;
constexpr int kMaxLampCount = 128;
constexpr float kMinShaftGlare = 0.02f;
constexpr float kMaxShaftGlare = 4.0f;
constexpr float kMaxLightSaturation = 1.5f;
constexpr float kMaxSaturation = 2.0f;
constexpr float kMaxLightBalance = 2.0f;
constexpr float kMinLandBrightness = 0.25f;
constexpr float kMaxLandBrightness = 2.0f;
constexpr float kMaxSkyHazeGlow = 4.0f;
constexpr float kMinSkyHazeZenith = 0.02f;
constexpr float kMinHazeSkyLevel = 0.25f;
constexpr float kMaxHazeSkyLevel = 1.5f;
constexpr float kMaxWhiteBalance = 1.0f;
constexpr float kMinSkyBrightness = 0.25f;
constexpr float kMaxSkyBrightness = 8.0f;

constexpr unsigned kVirtualKeyF3 = 0x72;
constexpr unsigned kVirtualKeyF4 = 0x73;
constexpr unsigned kVirtualKeyF5 = 0x74;
constexpr unsigned kVirtualKeyF6 = 0x75;
constexpr unsigned kVirtualKeyF7 = 0x76;
constexpr unsigned kVirtualKeyF10 = 0x79;
constexpr unsigned kVirtualKeyF12 = 0x7B;

constexpr int kLanternOff = 0;
constexpr int kLanternAuto = 1;
constexpr int kLanternOn = 2;

constexpr int kBanksOff = 0;
constexpr int kBanksEverywhere = 1;
constexpr int kBanksValleysAtDawnAndDusk = 2;
constexpr float kMinBankHeight = 5.0f;
constexpr float kMaxBankHeight = 300.0f;

struct Hotkey
{
    unsigned virtualKey;
    bool ctrl;
    bool shift;
    bool alt;
};

constexpr Hotkey kDefaultOverlayHotkey = {kVirtualKeyF7, true, false, false};
constexpr Hotkey kDefaultDrawLogHotkey = {kVirtualKeyF10, true, true, false};
constexpr Hotkey kDefaultLanternHotkey = {kVirtualKeyF6, true, false, false};
constexpr Hotkey kDefaultSpotHotkey = {kVirtualKeyF5, true, false, false};
constexpr Hotkey kDefaultReliefHotkey = {kVirtualKeyF4, true, false, false};
constexpr Hotkey kDefaultRegionHotkey = {kVirtualKeyF3, true, false, false};
constexpr Hotkey kDefaultToggleHotkey = {kVirtualKeyF12, true, false, false};

bool ParseHotkey(const char* text, Hotkey& out);
std::string HotkeyName(const Hotkey& key);

constexpr int kMaxConstantRanges = 16;
constexpr unsigned kVertexShaderConstantRegisters = 256;
constexpr unsigned kPixelShaderConstantRegisters = 224;

struct ConstantRanges
{
    int count;
    unsigned short first[kMaxConstantRanges];
    unsigned short last[kMaxConstantRanges];
};

constexpr ConstantRanges kDefaultDrawLogVertexConstants = {5, {0, 14, 31, 37, 224}, {7, 16, 33, 48, 235}};
constexpr ConstantRanges kDefaultDrawLogPixelConstants = {1, {0}, {12}};

bool ParseConstantRanges(const char* text, unsigned registers, ConstantRanges& out);
std::string ConstantRangesText(const ConstantRanges& ranges);
bool ConstantRangesInclude(const ConstantRanges& ranges, unsigned reg);

constexpr int kMaxZoneListZones = 48;
constexpr int kMaxZoneId = 65535;

struct ZoneList
{
    int count;
    unsigned short ids[kMaxZoneListZones];
};

constexpr ZoneList kDefaultDryZones = {12, {14, 17, 1637, 405, 400, 440, 1377, 3, 51, 46, 4, 40}};
constexpr ZoneList kDefaultGreenZones = {9, {215, 1638, 12, 44, 38, 45, 267, 47, 11}};

bool ParseZoneList(const char* text, ZoneList& out);
std::string ZoneListText(const ZoneList& zones);
bool ZoneListIncludes(const ZoneList& zones, int zoneId);
bool ZoneListAdd(ZoneList& zones, int zoneId);
void ZoneListRemove(ZoneList& zones, int zoneId);
bool SameZoneList(const ZoneList& a, const ZoneList& b);

constexpr int kMaxZoneFactors = 128;
constexpr float kMinZoneFactor = 0.0f;
constexpr float kMaxZoneFactor = 4.0f;

struct ZoneFactors
{
    int count;
    unsigned short ids[kMaxZoneFactors];
    float factors[kMaxZoneFactors];
    float otherZones = 1.0f;
};

bool ParseZoneFactors(const char* text, ZoneFactors& out);
std::string ZoneFactorsText(const ZoneFactors& zones);
float ZoneFactorOf(const ZoneFactors& zones, int zoneId);
bool SetZoneFactor(ZoneFactors& zones, int zoneId, float factor);
bool SameZoneFactors(const ZoneFactors& a, const ZoneFactors& b);

constexpr float kMaxFogCeiling = 300.0f;

struct Config
{
    bool enable = true;
    bool hooks = true;
    bool overlay = true;
    Hotkey overlayKey = kDefaultOverlayHotkey;
    Hotkey toggleKey = kDefaultToggleHotkey;
    int quality = 2;
    float density = 1.0f;
    ZoneFactors zoneDensity = {};
    float fogCeiling = 40.0f;
    float haze = 1.0f;
    float groundFog = 0.6f;
    float farFog = 1.0f;
    float farFogStretch = 1.0f;
    float aerialPerspective = 1.0f;
    int stockFog = 1;
    int dataMode = 1;
    int colorSpace = 1;
    float sunScatter = 1.0f;
    float ambient = 1.0f;
    float exposure = 1.0f;
    float lightSaturation = 1.0f;
    float hazeSaturation = 1.0f;
    float saturation = 1.0f;
    float skySaturation = 1.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float skyTemperature = 0.0f;
    float skyTint = 0.0f;
    float skyBrightness = 1.0f;
    float lightBalance = 0.0f;
    float landBrightness = 1.0f;
    float skyHaze = 0.0f;
    float skyHazeGlow = 1.0f;
    float skyHazeZenith = 1.0f;
    float skyHazeGlowGrade = 1.0f;
    float hazeFromSky = 1.0f;
    float hazeSkyLevel = 0.85f;
    float classicExposure = 1.0f;
    bool lightShafts = true;
    bool terrainShadows = true;
    float surfaceShadows = 0.7f;
    float lowSunLight = 1.0f;
    float shafts = 1.0f;
    float lowSunShafts = 1.0f;
    float shaftGlare = 0.15f;
    float shaftFocus = 0.7f;
    float moonShafts = 2.0f;
    float moonShaftFocus = 0.5f;
    float shaftHaze = 1.0f;
    float shaftRange = 200.0f;
    float shaftLength = 250.0f;
    float shaftDepth = 1.5f;
    bool shaftMemory = true;
    float drift = 0.6f;
    float driftSize = 60.0f;
    float driftDetail = 0.65f;
    float driftSwirl = 1.0f;
    int banks = kBanksEverywhere;
    float bankCover = 0.55f;
    float bankHeight = 60.0f;
    float bankPool = 1.0f;
    float bankShadow = 1.0f;
    float windSpeed = 2.0f;
    float stormWind = 7.0f;
    float windDirection = 60.0f;
    float morningMist = 2.0f;
    float mistHeight = 12.0f;
    float eveningHaze = 0.4f;
    float weatherFog = 1.0f;
    float weatherShafts = 0.35f;
    float weatherTint = 1.0f;
    float weatherSky = 1.0f;
    float rainInFront = 1.0f;
    bool skyboxMoons = false;
    float sunAndMoon = 1.0f;
    float twilight = 1.0f;
    float dustHaze = 1.0f;
    float humidHaze = 1.0f;
    float horizonFog = 1.0f;
    float cloudShadows = 0.6f;
    float cloudCover = 1.0f;
    float cloudSize = 400.0f;
    ZoneList dryZones = kDefaultDryZones;
    ZoneList greenZones = kDefaultGreenZones;
    float farLights = 1.0f;
    float lightGlow = 1.0f;
    float glowThreshold = 0.4f;
    float lampFog = 1.0f;
    int lampCount = 16;
    float lampFlicker = 1.0f;
    float fireStrength = 1.0f;
    float lampIndoors = 1.0f;
    float lampLight = 2.0f;
    float lampDayLight = 0.35f;
    float lampShadows = 1.0f;
    float carriedShadows = 0.0f;
    float nightDarkness = 0.7f;
    float windowLight = 2.0f;
    float effectLight = 1.0f;
    float corpseLight = 1.0f;
    int lanternMode = kLanternAuto;
    float lanternLight = 1.0f;
    float lanternHalo = 0.3f;
    float lanternSwing = 1.0f;
    Hotkey lanternKey = kDefaultLanternHotkey;
    Hotkey spotKey = kDefaultSpotHotkey;
    bool relief = true;
    bool reliefModels = true;
    float reliefDepth = 0.25f;
    float reliefLight = 1.0f;
    float reliefShadows = 1.0f;
    float reliefDistance = 40.0f;
    float reliefRoads = reliefclass::kInfo[reliefclass::kRoads].share;
    float reliefRock = reliefclass::kInfo[reliefclass::kRock].share;
    float reliefDirt = reliefclass::kInfo[reliefclass::kDirt].share;
    float reliefSand = reliefclass::kInfo[reliefclass::kSand].share;
    float reliefSnow = reliefclass::kInfo[reliefclass::kSnow].share;
    float reliefGrass = reliefclass::kInfo[reliefclass::kGrass].share;
    float reliefWalls = reliefclass::kInfo[reliefclass::kWalls].share;
    float reliefRoofs = reliefclass::kInfo[reliefclass::kRoofs].share;
    float reliefWood = reliefclass::kInfo[reliefclass::kWood].share;
    float reliefMetal = reliefclass::kInfo[reliefclass::kMetal].share;
    float reliefBark = reliefclass::kInfo[reliefclass::kBark].share;
    float reliefLeaves = reliefclass::kInfo[reliefclass::kLeaves].share;
    float reliefCreatures = reliefclass::kInfo[reliefclass::kCreatures].share;
    float reliefCharacters = reliefclass::kInfo[reliefclass::kCharacters].share;
    float reliefArmour = reliefclass::kInfo[reliefclass::kArmour].share;
    float reliefProps = reliefclass::kInfo[reliefclass::kProps].share;
    Hotkey reliefKey = kDefaultReliefHotkey;
    Hotkey regionKey = kDefaultRegionHotkey;
    float godRays = 0.0f;
    bool glowCompensation = true;
    float farClipMax = kFarClipMaxKeepsClientCap;
    float maxDistance = 5000.0f;
    float temporal = 0.85f;
    bool underwater = false;
    bool liquidDepth = true;
    float waterReflect = 1.0f;
    float waterGlitter = 1.0f;
    float waterClarity = 0.6f;
    float waterWaves = 1.0f;
    float waterRain = 1.0f;
    bool waterMemory = true;
    float surfaceSheen = 1.0f;
    float wetSurfaces = 1.0f;
    float puddles = 1.0f;
    bool terrainMaterials = true;
    int debugView = 0;
    bool sunMarker = false;
    int logLevel = 1;
    int drawLog = 0;
    int perfLog = 0;
    Hotkey drawLogKey = kDefaultDrawLogHotkey;
    ConstantRanges drawLogVertexConstants = kDefaultDrawLogVertexConstants;
    ConstantRanges drawLogPixelConstants = kDefaultDrawLogPixelConstants;
    bool clientShadowMaps = false;
    bool plainGame = false;
};

constexpr const char* kPlainGameSkip = "the plain game";

float ReliefClassShare(const Config& c, uint8_t reliefClass);
float* ReliefClassShareField(Config& c, uint8_t reliefClass);
float ReliefLayerDepth(const Config& c, float flatness, uint8_t reliefClass);
std::string ReliefClassSharesText(const Config& c);

Config PlainGameLook(const Config& look);

constexpr int kDebugViewClientShadowReceivers = 7;
constexpr int kDebugViewClientShadowCasters = 8;

bool ClientShadowMapsWanted(const Config& c);

bool SameLiveSettings(const Config& a, const Config& b);
std::string LookSettingsText(const Config& c);

enum class SettingKind
{
    Int,
    Float,
    Bool,
};

struct LiveSettingInfo
{
    const char* key;
    SettingKind kind;
    float lo;
    float hi;
    bool regional;
};

int LiveSettingCount();
const LiveSettingInfo& LiveSettingAt(int index);
int FindLiveSetting(const std::string& key);
float LiveSettingValue(const Config& c, int index);
void SetLiveSettingValue(Config& c, int index, float value);
float LiveSettingStored(int index, float value);
std::string LiveSettingText(int index, float value);

struct LookLayer
{
    std::vector<float> weight;
    std::vector<float> sum;
};

LookLayer EmptyLookLayer();
bool LookLayerEmpty(const LookLayer& layer);
Config ComposeLook(const Config& global, const LookLayer& layer);

class ConfigStore
{
public:
    void Load(const std::string& path);
    bool ReloadIfChanged();
    void Override(const Config& config)
    {
        m_config = config;
        m_config.plainGame = false;
        ++m_generation;
        Recompose();
    }
    void Apply(const Config& edited);
    bool Save();
    bool SaveLanternMode(int mode);
    bool SaveRelief(bool on);
    void Revert();
    void SetLookLayer(const LookLayer& layer);
    void SetPlainGame(bool on);
    bool PlainGame() const { return m_plainGame; }
    bool HasUnsavedChanges() const { return !SameLiveSettings(m_config, m_saved); }
    const Config& Get() const { return m_look; }
    const Config& Global() const { return m_config; }
    const LookLayer& Layer() const { return m_layer; }
    const std::string& Path() const { return m_path; }
    unsigned Generation() const { return m_generation; }

private:
    void Read();
    Config ReadFile() const;
    void ReadKeepingStartupSwitches();
    void Recompose();

    std::string m_path;
    unsigned long long m_stamp = 0;
    Config m_config;
    Config m_saved;
    Config m_look;
    LookLayer m_layer;
    bool m_plainGame = false;
    unsigned m_generation = 0;
};

ConfigStore& GlobalConfig();
