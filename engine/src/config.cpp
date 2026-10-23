#include "config.h"

#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr const char* kSection = "CoAVolFog";
constexpr unsigned kMaxFunctionKey = 24;

struct IntSetting
{
    const char* key;
    int Config::*value;
    int lo;
    int hi;
};

struct FloatSetting
{
    const char* key;
    float Config::*value;
    float lo;
    float hi;
};

struct BoolSetting
{
    const char* key;
    bool Config::*value;
};

const IntSetting kIntSettings[] = {
    {"Quality", &Config::quality, 1, 3},     {"StockFog", &Config::stockFog, 0, 1},
    {"DataMode", &Config::dataMode, 0, 1},   {"ColorSpace", &Config::colorSpace, 0, 1},
    {"DebugView", &Config::debugView, 0, kDebugViewClientShadowCasters}, {"LogLevel", &Config::logLevel, 0, 2},
    {"LampCount", &Config::lampCount, 0, kMaxLampCount},
    {"Lantern", &Config::lanternMode, kLanternOff, kLanternOn},
    {"Banks", &Config::banks, kBanksOff, kBanksValleysAtDawnAndDusk},
    {"DrawLog", &Config::drawLog, 0, 2},
    {"PerfLog", &Config::perfLog, 0, 1},
};

const FloatSetting kFloatSettings[] = {
    {"Density", &Config::density, 0.0f, 10.0f},
    {"FogCeiling", &Config::fogCeiling, 0.0f, kMaxFogCeiling},
    {"Haze", &Config::haze, 0.0f, 10.0f},
    {"GroundFog", &Config::groundFog, 0.0f, 10.0f},
    {"FarFog", &Config::farFog, 0.0f, 10.0f},
    {"FarFogStretch", &Config::farFogStretch, 0.0f, 1.0f},
    {"AerialPerspective", &Config::aerialPerspective, 0.0f, 1.0f},
    {"SunScatter", &Config::sunScatter, 0.0f, 10.0f},
    {"Ambient", &Config::ambient, 0.0f, 10.0f},
    {"Exposure", &Config::exposure, 0.0f, 10.0f},
    {"ClassicExposure", &Config::classicExposure, 0.0f, 10.0f},
    {"LightSaturation", &Config::lightSaturation, 0.0f, kMaxLightSaturation},
    {"HazeSaturation", &Config::hazeSaturation, 0.0f, kMaxSaturation},
    {"Saturation", &Config::saturation, 0.0f, kMaxSaturation},
    {"SkySaturation", &Config::skySaturation, 0.0f, kMaxSaturation},
    {"Temperature", &Config::temperature, -kMaxWhiteBalance, kMaxWhiteBalance},
    {"Tint", &Config::tint, -kMaxWhiteBalance, kMaxWhiteBalance},
    {"SkyTemperature", &Config::skyTemperature, -kMaxWhiteBalance, kMaxWhiteBalance},
    {"SkyTint", &Config::skyTint, -kMaxWhiteBalance, kMaxWhiteBalance},
    {"SkyBrightness", &Config::skyBrightness, kMinSkyBrightness, kMaxSkyBrightness},
    {"LightBalance", &Config::lightBalance, 0.0f, kMaxLightBalance},
    {"LandBrightness", &Config::landBrightness, kMinLandBrightness, kMaxLandBrightness},
    {"SkyHaze", &Config::skyHaze, 0.0f, 1.0f},
    {"SkyHazeGlow", &Config::skyHazeGlow, 0.0f, kMaxSkyHazeGlow},
    {"SkyHazeZenith", &Config::skyHazeZenith, kMinSkyHazeZenith, 1.0f},
    {"SkyHazeGlowGrade", &Config::skyHazeGlowGrade, 0.0f, 1.0f},
    {"HazeFromSky", &Config::hazeFromSky, 0.0f, 1.0f},
    {"HazeSkyLevel", &Config::hazeSkyLevel, kMinHazeSkyLevel, kMaxHazeSkyLevel},
    {"Shafts", &Config::shafts, 0.0f, 4.0f},
    {"LowSunShafts", &Config::lowSunShafts, 0.0f, 3.0f},
    {"ShaftGlare", &Config::shaftGlare, kMinShaftGlare, kMaxShaftGlare},
    {"ShaftFocus", &Config::shaftFocus, 0.0f, 0.95f},
    {"MoonShafts", &Config::moonShafts, 0.0f, 8.0f},
    {"MoonShaftFocus", &Config::moonShaftFocus, 0.0f, 0.95f},
    {"ShaftHaze", &Config::shaftHaze, 0.0f, 10.0f},
    {"ShaftRange", &Config::shaftRange, 30.0f, 600.0f},
    {"ShaftLength", &Config::shaftLength, 10.0f, 300.0f},
    {"ShaftDepth", &Config::shaftDepth, 0.1f, 4.0f},
    {"SurfaceShadows", &Config::surfaceShadows, 0.0f, 1.0f},
    {"LowSunLight", &Config::lowSunLight, 0.0f, 2.0f},
    {"Drift", &Config::drift, 0.0f, 2.0f},
    {"DriftSize", &Config::driftSize, 10.0f, 300.0f},
    {"DriftDetail", &Config::driftDetail, 0.0f, 1.0f},
    {"DriftSwirl", &Config::driftSwirl, 0.0f, 3.0f},
    {"BankCover", &Config::bankCover, 0.0f, 1.0f},
    {"BankHeight", &Config::bankHeight, kMinBankHeight, kMaxBankHeight},
    {"BankPool", &Config::bankPool, 0.0f, 3.0f},
    {"BankShadow", &Config::bankShadow, 0.0f, 3.0f},
    {"WindSpeed", &Config::windSpeed, 0.0f, 10.0f},
    {"StormWind", &Config::stormWind, 0.0f, 20.0f},
    {"WindDirection", &Config::windDirection, 0.0f, 360.0f},
    {"MorningMist", &Config::morningMist, 0.0f, 8.0f},
    {"MistHeight", &Config::mistHeight, 2.0f, 60.0f},
    {"EveningHaze", &Config::eveningHaze, 0.0f, 2.0f},
    {"WeatherFog", &Config::weatherFog, 0.0f, 4.0f},
    {"WeatherShafts", &Config::weatherShafts, 0.0f, 1.0f},
    {"WeatherTint", &Config::weatherTint, 0.0f, 1.0f},
    {"WeatherSky", &Config::weatherSky, 0.0f, 1.0f},
    {"RainInFront", &Config::rainInFront, 0.0f, 1.0f},
    {"SunAndMoon", &Config::sunAndMoon, 0.0f, 1.0f},
    {"Twilight", &Config::twilight, 0.0f, 2.0f},
    {"DustHaze", &Config::dustHaze, 0.0f, 3.0f},
    {"HumidHaze", &Config::humidHaze, 0.0f, 3.0f},
    {"HorizonFog", &Config::horizonFog, 0.0f, 1.0f},
    {"CloudShadows", &Config::cloudShadows, 0.0f, 1.0f},
    {"CloudCover", &Config::cloudCover, 0.0f, 2.0f},
    {"CloudSize", &Config::cloudSize, 100.0f, 2000.0f},
    {"FarLights", &Config::farLights, 0.0f, 4.0f},
    {"LightGlow", &Config::lightGlow, 0.0f, 8.0f},
    {"GlowThreshold", &Config::glowThreshold, 0.2f, 0.95f},
    {"LampFog", &Config::lampFog, 0.0f, 4.0f},
    {"LampFlicker", &Config::lampFlicker, 0.0f, 2.0f},
    {"FireStrength", &Config::fireStrength, 0.0f, 2.0f},
    {"LampIndoors", &Config::lampIndoors, 0.0f, 2.0f},
    {"LampLight", &Config::lampLight, 0.0f, 4.0f},
    {"LampDayLight", &Config::lampDayLight, 0.0f, 1.0f},
    {"LampShadows", &Config::lampShadows, 0.0f, 1.0f},
    {"CarriedShadows", &Config::carriedShadows, 0.0f, 1.0f},
    {"NightDarkness", &Config::nightDarkness, 0.0f, 0.9f},
    {"WindowLight", &Config::windowLight, 0.0f, 4.0f},
    {"EffectLight", &Config::effectLight, 0.0f, 4.0f},
    {"CorpseLight", &Config::corpseLight, 0.0f, 2.0f},
    {"LanternLight", &Config::lanternLight, 0.0f, 4.0f},
    {"LanternHalo", &Config::lanternHalo, 0.0f, 2.0f},
    {"LanternSwing", &Config::lanternSwing, 0.0f, 2.0f},
    {"GodRays", &Config::godRays, 0.0f, 4.0f},
    {"FarClipMax", &Config::farClipMax, 0.0f, kEngineFarClipMax},
    {"MaxDistance", &Config::maxDistance, 200.0f, 5000.0f},
    {"Temporal", &Config::temporal, 0.0f, 0.97f},
    {"WaterReflect", &Config::waterReflect, 0.0f, 2.0f},
    {"WaterGlitter", &Config::waterGlitter, 0.0f, 4.0f},
    {"WaterClarity", &Config::waterClarity, 0.0f, 1.0f},
    {"WaterSmooth", &Config::waterSmooth, 0.0f, 1.0f},
    {"WaterBump", &Config::waterBump, 0.0f, 2.0f},
    {"WaterFoam", &Config::waterFoam, 0.0f, 2.0f},
    {"WaterWaves", &Config::waterWaves, 0.0f, 2.0f},
    {"WaterRain", &Config::waterRain, 0.0f, 2.0f},
    {"SurfaceSheen", &Config::surfaceSheen, 0.0f, 2.0f},
    {"WetSurfaces", &Config::wetSurfaces, 0.0f, 2.0f},
    {"Puddles", &Config::puddles, 0.0f, 2.0f},
    {"ReliefDepth", &Config::reliefDepth, 0.0f, 0.75f},
    {"ReliefLight", &Config::reliefLight, 0.0f, 2.0f},
    {"ReliefShadows", &Config::reliefShadows, 0.0f, 1.0f},
    {"ReliefDistance", &Config::reliefDistance, 5.0f, 1500.0f},
    {reliefclass::kInfo[reliefclass::kRoads].key, &Config::reliefRoads, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kRock].key, &Config::reliefRock, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kDirt].key, &Config::reliefDirt, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kSand].key, &Config::reliefSand, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kSnow].key, &Config::reliefSnow, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kGrass].key, &Config::reliefGrass, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kWalls].key, &Config::reliefWalls, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kRoofs].key, &Config::reliefRoofs, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kWood].key, &Config::reliefWood, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kMetal].key, &Config::reliefMetal, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kBark].key, &Config::reliefBark, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kLeaves].key, &Config::reliefLeaves, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kCreatures].key, &Config::reliefCreatures, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kCharacters].key, &Config::reliefCharacters, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kArmour].key, &Config::reliefArmour, reliefclass::kMinShare, reliefclass::kMaxShare},
    {reliefclass::kInfo[reliefclass::kProps].key, &Config::reliefProps, reliefclass::kMinShare, reliefclass::kMaxShare},
};

float Config::* const kReliefClassShares[reliefclass::kCount] = {
    nullptr,
    &Config::reliefRoads,
    &Config::reliefRock,
    &Config::reliefDirt,
    &Config::reliefSand,
    &Config::reliefSnow,
    &Config::reliefGrass,
    &Config::reliefWalls,
    &Config::reliefRoofs,
    &Config::reliefWood,
    &Config::reliefMetal,
    &Config::reliefBark,
    &Config::reliefLeaves,
    &Config::reliefCreatures,
    &Config::reliefCharacters,
    &Config::reliefArmour,
    &Config::reliefProps,
};

const BoolSetting kBoolSettings[] = {
    {"LightShafts", &Config::lightShafts}, {"TerrainShadows", &Config::terrainShadows},
    {"ShaftMemory", &Config::shaftMemory},
    {"GlowCompensation", &Config::glowCompensation},
    {"Underwater", &Config::underwater},   {"LiquidDepth", &Config::liquidDepth},
    {"WaterMemory", &Config::waterMemory}, {"TerrainMaterials", &Config::terrainMaterials},
    {"SunMarker", &Config::sunMarker},     {"ClientShadowMaps", &Config::clientShadowMaps},
    {"Relief", &Config::relief},         {"SkyboxMoons", &Config::skyboxMoons},
    {"ReliefModels", &Config::reliefModels},
};

const char* const kGlobalOnlySettings[] = {
    "Quality",     "DebugView",  "LogLevel",    "DrawLog",     "Lantern",          "Relief",    "FarClipMax",
    "ShaftMemory", "WaterMemory", "LiquidDepth", "Underwater", "ClientShadowMaps", "SunMarker",
    "SkyboxMoons", "PerfLog",     "TerrainMaterials", "ReliefModels",
};

struct NamedKey
{
    const char* name;
    unsigned virtualKey;
};

const NamedKey kNamedKeys[] = {
    {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Home", VK_HOME},   {"End", VK_END},
    {"PageUp", VK_PRIOR},  {"PageDown", VK_NEXT}, {"Pause", VK_PAUSE}, {"ScrollLock", VK_SCROLL},
};

const NamedKey kReportedOnlyKeys[] = {
    {"Media Previous", VK_MEDIA_PREV_TRACK}, {"Media Next", VK_MEDIA_NEXT_TRACK},
    {"Media Play/Pause", VK_MEDIA_PLAY_PAUSE}, {"Media Stop", VK_MEDIA_STOP},
    {"Volume Mute", VK_VOLUME_MUTE},         {"Volume Down", VK_VOLUME_DOWN},
    {"Volume Up", VK_VOLUME_UP},             {"Cancel", VK_CANCEL},
};

unsigned long long FileStamp(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           data.ftLastWriteTime.dwLowDateTime;
}

float ClampedSetting(float v, float lo, float hi)
{
    return v >= lo ? std::min(v, hi) : lo;
}

float ReadFloat(const std::string& path, const char* key, float fallback, float lo, float hi)
{
    char buf[64];
    GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), path.c_str());
    if (!buf[0])
        return fallback;
    char* end = nullptr;
    float v = std::strtof(buf, &end);
    if (end == buf || v != v)
        return fallback;
    return std::clamp(v, lo, hi);
}

int ReadInt(const std::string& path, const char* key, int fallback, int lo, int hi)
{
    int v = static_cast<int>(GetPrivateProfileIntA(kSection, key, fallback, path.c_str()));
    return std::clamp(v, lo, hi);
}

ConstantRanges ReadConstantRanges(const std::string& path, const char* key, const ConstantRanges& fallback,
                                  unsigned registers)
{
    char buf[512];
    GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), path.c_str());
    ConstantRanges parsed = fallback;
    if (buf[0] && !ParseConstantRanges(buf, registers, parsed))
        VF_LOG_ERROR("%s=%s is not a list of registers like c2-5,c37-48 below c%u; using %s", key, buf, registers,
                     ConstantRangesText(fallback).c_str());
    return parsed;
}

ZoneList ReadZoneList(const std::string& path, const char* key, const ZoneList& fallback)
{
    char buf[512];
    GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), path.c_str());
    ZoneList parsed = fallback;
    if (buf[0] && !ParseZoneList(buf, parsed))
        VF_LOG_ERROR("%s=%s is not a list of zone ids like 17,405 (at most %d); using %s", key, buf,
                     kMaxZoneListZones, ZoneListText(fallback).c_str());
    return parsed;
}

ZoneFactors ReadZoneFactors(const std::string& path, const char* key, const ZoneFactors& fallback)
{
    char buf[2048];
    GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), path.c_str());
    ZoneFactors parsed = fallback;
    if (buf[0] && !ParseZoneFactors(buf, parsed))
        VF_LOG_ERROR("%s=%s is not a list of zones and factors like *:0.5,148:0.2,490:0.7 (factors %.0f to %.0f, "
                     "* for every other zone, at most %d zones); using %s",
                     key, buf, kMinZoneFactor, kMaxZoneFactor, kMaxZoneFactors, ZoneFactorsText(fallback).c_str());
    return parsed;
}

Hotkey ReadHotkey(const std::string& path, const char* key, const Hotkey& fallback)
{
    char buf[64];
    GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), path.c_str());
    Hotkey parsed = fallback;
    if (buf[0] && !ParseHotkey(buf, parsed))
        VF_LOG_ERROR("%s=%s is not a key; using %s", key, buf, HotkeyName(fallback).c_str());
    return parsed;
}

std::string SettingText(float value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    std::string text = buf;
    text.erase(text.find_last_not_of('0') + 1);
    if (text.back() == '.')
        text.pop_back();
    return text;
}

std::string ExactSettingText(float value)
{
    const std::string text = SettingText(value);
    if (std::strtof(text.c_str(), nullptr) == value)
        return text;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", value);
    return buf;
}

bool IsDiagnosticSetting(const char* key)
{
    return std::strcmp(key, "DebugView") == 0 || std::strcmp(key, "LogLevel") == 0 || std::strcmp(key, "DrawLog") == 0 ||
           std::strcmp(key, "PerfLog") == 0;
}

bool WriteSetting(const std::string& path, const char* key, const std::string& text)
{
    return WritePrivateProfileStringA(kSection, key, text.c_str(), path.c_str()) != FALSE;
}

void ClampLiveSettings(Config& c)
{
    for (const IntSetting& s : kIntSettings)
        c.*s.value = std::clamp(c.*s.value, s.lo, s.hi);
    for (const FloatSetting& s : kFloatSettings)
        c.*s.value = ClampedSetting(c.*s.value, s.lo, s.hi);
    if (c.farClipMax < kEngineFarClipMin)
        c.farClipMax = kFarClipMaxKeepsClientCap;
}

std::string Trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return std::string();
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

bool SameText(const std::string& text, const char* name)
{
    return _stricmp(text.c_str(), name) == 0;
}

bool IsLetterOrDigitKey(unsigned virtualKey)
{
    return (virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z');
}

bool ParseFunctionKey(const std::string& name, unsigned& virtualKey)
{
    if (name.size() < 2 || std::toupper(static_cast<unsigned char>(name[0])) != 'F' ||
        !std::all_of(name.begin() + 1, name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    const unsigned number = static_cast<unsigned>(std::atoi(name.c_str() + 1));
    if (number < 1 || number > kMaxFunctionKey)
        return false;
    virtualKey = VK_F1 + number - 1;
    return true;
}

bool ParseKeyName(const std::string& name, unsigned& virtualKey)
{
    if (name.size() == 1)
    {
        const unsigned key = static_cast<unsigned>(std::toupper(static_cast<unsigned char>(name[0])));
        if (!IsLetterOrDigitKey(key))
            return false;
        virtualKey = key;
        return true;
    }
    if (ParseFunctionKey(name, virtualKey))
        return true;
    for (const NamedKey& k : kNamedKeys)
        if (SameText(name, k.name))
        {
            virtualKey = k.virtualKey;
            return true;
        }
    return false;
}

std::string KeyName(unsigned virtualKey)
{
    if (virtualKey >= VK_F1 && virtualKey < VK_F1 + kMaxFunctionKey)
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    for (const NamedKey& k : kNamedKeys)
        if (k.virtualKey == virtualKey)
            return k.name;
    if (IsLetterOrDigitKey(virtualKey))
        return std::string(1, static_cast<char>(virtualKey));
    for (const NamedKey& k : kReportedOnlyKeys)
        if (k.virtualKey == virtualKey)
            return k.name;
    return "key " + std::to_string(virtualKey);
}
}

bool ParseHotkey(const char* text, Hotkey& out)
{
    Hotkey key = {0, false, false, false};
    std::string rest = text ? text : "";
    for (size_t plus = rest.find('+'); plus != std::string::npos; plus = rest.find('+'))
    {
        const std::string modifier = Trimmed(rest.substr(0, plus));
        if (SameText(modifier, "Ctrl") || SameText(modifier, "Control"))
            key.ctrl = true;
        else if (SameText(modifier, "Shift"))
            key.shift = true;
        else if (SameText(modifier, "Alt"))
            key.alt = true;
        else
            return false;
        rest = rest.substr(plus + 1);
    }
    if (!ParseKeyName(Trimmed(rest), key.virtualKey))
        return false;
    out = key;
    return true;
}

namespace
{
bool ParseRegister(const std::string& text, unsigned registers, unsigned& out)
{
    std::string digits = text;
    if (!digits.empty() && (digits[0] == 'c' || digits[0] == 'C'))
        digits.erase(0, 1);
    if (digits.empty() || digits.size() > 3 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    out = static_cast<unsigned>(std::atoi(digits.c_str()));
    return out < registers;
}
}

bool ParseConstantRanges(const char* text, unsigned registers, ConstantRanges& out)
{
    ConstantRanges ranges = {};
    std::string rest = Trimmed(text ? text : "");
    if (SameText(rest, "none"))
    {
        out = ranges;
        return true;
    }
    for (char& c : rest)
        if (c == ',')
            c = ' ';
    size_t start = rest.find_first_not_of(' ');
    if (start == std::string::npos)
        return false;
    while (start != std::string::npos)
    {
        const size_t end = rest.find(' ', start);
        const std::string token = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t dash = token.find('-');
        unsigned first = 0;
        unsigned last = 0;
        if (!ParseRegister(token.substr(0, dash), registers, first) ||
            (dash != std::string::npos && !ParseRegister(token.substr(dash + 1), registers, last)))
            return false;
        if (dash == std::string::npos)
            last = first;
        if (last < first || ranges.count == kMaxConstantRanges)
            return false;
        ranges.first[ranges.count] = static_cast<unsigned short>(first);
        ranges.last[ranges.count] = static_cast<unsigned short>(last);
        ++ranges.count;
        start = end == std::string::npos ? end : rest.find_first_not_of(' ', end);
    }
    out = ranges;
    return true;
}

std::string ConstantRangesText(const ConstantRanges& ranges)
{
    if (ranges.count <= 0)
        return "none";
    std::string text;
    for (int i = 0; i < ranges.count; ++i)
    {
        if (i)
            text += ",";
        text += "c" + std::to_string(ranges.first[i]);
        if (ranges.last[i] != ranges.first[i])
            text += "-" + std::to_string(ranges.last[i]);
    }
    return text;
}

bool ConstantRangesInclude(const ConstantRanges& ranges, unsigned reg)
{
    for (int i = 0; i < ranges.count; ++i)
        if (reg >= ranges.first[i] && reg <= ranges.last[i])
            return true;
    return false;
}

bool ParseZoneList(const char* text, ZoneList& out)
{
    ZoneList zones = {};
    std::string rest = Trimmed(text ? text : "");
    if (SameText(rest, "none"))
    {
        out = zones;
        return true;
    }
    for (char& c : rest)
        if (c == ',' || c == ';' || c == '\t')
            c = ' ';
    size_t start = rest.find_first_not_of(' ');
    if (start == std::string::npos)
        return false;
    while (start != std::string::npos)
    {
        const size_t end = rest.find(' ', start);
        const std::string token = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        char* stop = nullptr;
        const long id = std::strtol(token.c_str(), &stop, 10);
        if (stop == token.c_str() || *stop != '\0' || id <= 0 || id > kMaxZoneId ||
            zones.count == kMaxZoneListZones)
            return false;
        if (!ZoneListIncludes(zones, static_cast<int>(id)))
            zones.ids[zones.count++] = static_cast<unsigned short>(id);
        start = end == std::string::npos ? end : rest.find_first_not_of(' ', end);
    }
    out = zones;
    return true;
}

std::string ZoneListText(const ZoneList& zones)
{
    if (zones.count <= 0)
        return "none";
    std::string text;
    for (int i = 0; i < zones.count; ++i)
    {
        if (i)
            text += ",";
        text += std::to_string(zones.ids[i]);
    }
    return text;
}

bool ZoneListIncludes(const ZoneList& zones, int zoneId)
{
    for (int i = 0; i < zones.count; ++i)
        if (zones.ids[i] == zoneId)
            return true;
    return false;
}

bool ZoneListAdd(ZoneList& zones, int zoneId)
{
    if (zoneId <= 0 || zoneId > kMaxZoneId)
        return false;
    if (ZoneListIncludes(zones, zoneId))
        return true;
    if (zones.count >= kMaxZoneListZones)
        return false;
    zones.ids[zones.count++] = static_cast<unsigned short>(zoneId);
    return true;
}

void ZoneListRemove(ZoneList& zones, int zoneId)
{
    int kept = 0;
    for (int i = 0; i < zones.count; ++i)
        if (zones.ids[i] != zoneId)
            zones.ids[kept++] = zones.ids[i];
    zones.count = kept;
}

bool SameZoneList(const ZoneList& a, const ZoneList& b)
{
    if (a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.ids[i] != b.ids[i])
            return false;
    return true;
}

bool ParseZoneFactors(const char* text, ZoneFactors& out)
{
    std::string rest = text ? text : "";
    for (char& c : rest)
        if (c == ',' || c == ';' || c == '\t')
            c = ' ';
    size_t start = rest.find_first_not_of(' ');
    if (start == std::string::npos)
        return false;
    ZoneFactors zones = {};
    if (rest.substr(start, rest.find_last_not_of(' ') + 1 - start) == "none")
    {
        out = zones;
        return true;
    }
    while (start != std::string::npos)
    {
        const size_t end = rest.find(' ', start);
        const std::string token = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t colon = token.find_first_of(":=");
        if (colon == std::string::npos)
            return false;
        const std::string idText = token.substr(0, colon);
        const std::string factorText = token.substr(colon + 1);
        char* stop = nullptr;
        const float factor = std::strtof(factorText.c_str(), &stop);
        if (stop == factorText.c_str() || *stop != '\0' || !(factor >= kMinZoneFactor) || !(factor <= kMaxZoneFactor))
            return false;
        start = end == std::string::npos ? end : rest.find_first_not_of(' ', end);
        if (idText == "*")
        {
            zones.otherZones = factor;
            continue;
        }
        stop = nullptr;
        const long id = std::strtol(idText.c_str(), &stop, 10);
        if (stop == idText.c_str() || *stop != '\0' || id <= 0 || id > kMaxZoneId)
            return false;
        int at = 0;
        while (at < zones.count && zones.ids[at] != id)
            ++at;
        if (at == zones.count)
        {
            if (zones.count == kMaxZoneFactors)
                return false;
            ++zones.count;
        }
        zones.ids[at] = static_cast<unsigned short>(id);
        zones.factors[at] = factor;
    }
    out = zones;
    return true;
}

std::string ZoneFactorsText(const ZoneFactors& zones)
{
    std::string text;
    if (zones.otherZones != 1.0f)
        text = "*:" + ExactSettingText(zones.otherZones);
    for (int i = 0; i < zones.count; ++i)
    {
        if (!text.empty())
            text += ",";
        text += std::to_string(zones.ids[i]) + ":" + ExactSettingText(zones.factors[i]);
    }
    return text.empty() ? "none" : text;
}

float ReliefClassShare(const Config& c, uint8_t reliefClass)
{
    if (!reliefclass::Valid(reliefClass) || !kReliefClassShares[reliefClass])
        return 0.0f;
    return c.*kReliefClassShares[reliefClass];
}

float* ReliefClassShareField(Config& c, uint8_t reliefClass)
{
    if (!reliefclass::Valid(reliefClass) || !kReliefClassShares[reliefClass])
        return nullptr;
    return &(c.*kReliefClassShares[reliefClass]);
}

float ReliefLayerDepth(const Config& c, float flatness, uint8_t reliefClass)
{
    return std::max(flatness, 0.0f) * ReliefClassShare(c, reliefClass) * c.reliefDepth;
}

std::string ReliefClassSharesText(const Config& c)
{
    std::string text;
    for (uint8_t id = 1; id < reliefclass::kCount; ++id)
    {
        char item[48];
        std::snprintf(item, sizeof item, "%s%s %.2f", id > 1 ? " " : "", reliefclass::Name(id), ReliefClassShare(c, id));
        text += item;
    }
    return text;
}

float ZoneFactorOf(const ZoneFactors& zones, int zoneId)
{
    for (int i = 0; i < zones.count; ++i)
        if (zones.ids[i] == zoneId)
            return zones.factors[i];
    return zones.otherZones;
}

bool SetZoneFactor(ZoneFactors& zones, int zoneId, float factor)
{
    if (zoneId <= 0 || zoneId > kMaxZoneId || !(factor >= kMinZoneFactor) || !(factor <= kMaxZoneFactor))
        return false;
    int at = 0;
    while (at < zones.count && zones.ids[at] != zoneId)
        ++at;
    if (SettingText(factor) == SettingText(zones.otherZones))
    {
        if (at < zones.count)
        {
            for (int i = at + 1; i < zones.count; ++i)
            {
                zones.ids[i - 1] = zones.ids[i];
                zones.factors[i - 1] = zones.factors[i];
            }
            --zones.count;
        }
        return true;
    }
    if (at == zones.count)
    {
        if (zones.count == kMaxZoneFactors)
            return false;
        ++zones.count;
    }
    zones.ids[at] = static_cast<unsigned short>(zoneId);
    zones.factors[at] = factor;
    return true;
}

bool SameZoneFactors(const ZoneFactors& a, const ZoneFactors& b)
{
    if (a.count != b.count || a.otherZones != b.otherZones)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.ids[i] != b.ids[i] || a.factors[i] != b.factors[i])
            return false;
    return true;
}

bool ClientShadowMapsWanted(const Config& c)
{
    return c.clientShadowMaps || c.debugView >= kDebugViewClientShadowReceivers;
}

std::string HotkeyName(const Hotkey& key)
{
    std::string name;
    if (key.ctrl)
        name += "Ctrl+";
    if (key.shift)
        name += "Shift+";
    if (key.alt)
        name += "Alt+";
    return name + KeyName(key.virtualKey);
}

std::string LookSettingsText(const Config& c)
{
    std::string text = std::string("[") + kSection + "]\r\n";
    for (const IntSetting& s : kIntSettings)
        if (!IsDiagnosticSetting(s.key))
            text += std::string(s.key) + "=" + std::to_string(c.*s.value) + "\r\n";
    for (const FloatSetting& s : kFloatSettings)
        text += std::string(s.key) + "=" + ExactSettingText(c.*s.value) + "\r\n";
    for (const BoolSetting& s : kBoolSettings)
        text += std::string(s.key) + "=" + (c.*s.value ? "1" : "0") + "\r\n";
    return text;
}

bool SameLiveSettings(const Config& a, const Config& b)
{
    for (const IntSetting& s : kIntSettings)
        if (a.*s.value != b.*s.value)
            return false;
    for (const FloatSetting& s : kFloatSettings)
        if (a.*s.value != b.*s.value)
            return false;
    for (const BoolSetting& s : kBoolSettings)
        if (a.*s.value != b.*s.value)
            return false;
    return SameZoneList(a.dryZones, b.dryZones) && SameZoneList(a.greenZones, b.greenZones) &&
           SameZoneFactors(a.zoneDensity, b.zoneDensity);
}

namespace
{
struct LiveSettingAccess
{
    LiveSettingInfo info;
    int Config::*intValue;
    float Config::*floatValue;
    bool Config::*boolValue;
};

bool IsGlobalOnlySetting(const char* key)
{
    for (const char* name : kGlobalOnlySettings)
        if (std::strcmp(name, key) == 0)
            return true;
    return false;
}

std::vector<LiveSettingAccess> BuildLiveSettings()
{
    std::vector<LiveSettingAccess> all;
    for (const IntSetting& s : kIntSettings)
        all.push_back({{s.key, SettingKind::Int, static_cast<float>(s.lo), static_cast<float>(s.hi),
                        !IsGlobalOnlySetting(s.key)},
                       s.value,
                       nullptr,
                       nullptr});
    for (const FloatSetting& s : kFloatSettings)
        all.push_back({{s.key, SettingKind::Float, s.lo, s.hi, !IsGlobalOnlySetting(s.key)},
                       nullptr,
                       s.value,
                       nullptr});
    for (const BoolSetting& s : kBoolSettings)
        all.push_back({{s.key, SettingKind::Bool, 0.0f, 1.0f, !IsGlobalOnlySetting(s.key)}, nullptr, nullptr, s.value});
    return all;
}

const std::vector<LiveSettingAccess>& LiveSettings()
{
    static const std::vector<LiveSettingAccess> settings = BuildLiveSettings();
    return settings;
}

float StoredFloat(float value, float lo, float hi)
{
    return std::strtof(SettingText(ClampedSetting(value, lo, hi)).c_str(), nullptr);
}

int StoredInt(float value, float lo, float hi)
{
    if (!(value == value))
        return static_cast<int>(lo);
    return static_cast<int>(std::lround(std::clamp(value, lo, hi)));
}
}

int LiveSettingCount()
{
    return static_cast<int>(LiveSettings().size());
}

const LiveSettingInfo& LiveSettingAt(int index)
{
    return LiveSettings()[static_cast<size_t>(index)].info;
}

int FindLiveSetting(const std::string& key)
{
    const std::vector<LiveSettingAccess>& settings = LiveSettings();
    for (size_t i = 0; i < settings.size(); ++i)
        if (SameText(key, settings[i].info.key))
            return static_cast<int>(i);
    return -1;
}

float LiveSettingValue(const Config& c, int index)
{
    const LiveSettingAccess& s = LiveSettings()[static_cast<size_t>(index)];
    switch (s.info.kind)
    {
    case SettingKind::Int:
        return static_cast<float>(c.*s.intValue);
    case SettingKind::Float:
        return c.*s.floatValue;
    default:
        return c.*s.boolValue ? 1.0f : 0.0f;
    }
}

void SetLiveSettingValue(Config& c, int index, float value)
{
    const LiveSettingAccess& s = LiveSettings()[static_cast<size_t>(index)];
    switch (s.info.kind)
    {
    case SettingKind::Int:
        c.*s.intValue = StoredInt(value, s.info.lo, s.info.hi);
        break;
    case SettingKind::Float:
        c.*s.floatValue = ClampedSetting(value, s.info.lo, s.info.hi);
        break;
    default:
        c.*s.boolValue = value >= 0.5f;
        break;
    }
}

float LiveSettingStored(int index, float value)
{
    const LiveSettingInfo& s = LiveSettingAt(index);
    switch (s.kind)
    {
    case SettingKind::Int:
        return static_cast<float>(StoredInt(value, s.lo, s.hi));
    case SettingKind::Float:
        return StoredFloat(value, s.lo, s.hi);
    default:
        return value >= 0.5f ? 1.0f : 0.0f;
    }
}

std::string LiveSettingText(int index, float value)
{
    const LiveSettingInfo& s = LiveSettingAt(index);
    switch (s.kind)
    {
    case SettingKind::Int:
        return std::to_string(StoredInt(value, s.lo, s.hi));
    case SettingKind::Float:
        return SettingText(ClampedSetting(value, s.lo, s.hi));
    default:
        return value >= 0.5f ? "1" : "0";
    }
}

LookLayer EmptyLookLayer()
{
    const size_t count = static_cast<size_t>(LiveSettingCount());
    return {std::vector<float>(count, 0.0f), std::vector<float>(count, 0.0f)};
}

bool LookLayerEmpty(const LookLayer& layer)
{
    for (float w : layer.weight)
        if (w > 0.0f)
            return false;
    return true;
}

Config ComposeLook(const Config& global, const LookLayer& layer)
{
    Config look = global;
    const int count = static_cast<int>(std::min({LiveSettings().size(), layer.weight.size(), layer.sum.size()}));
    for (int i = 0; i < count; ++i)
    {
        const float w = layer.weight[static_cast<size_t>(i)];
        const float sum = layer.sum[static_cast<size_t>(i)];
        if (!(w > 0.0f) || !LiveSettingAt(i).regional)
            continue;
        const float value = w >= 1.0f ? sum / w : (1.0f - w) * LiveSettingValue(global, i) + sum;
        SetLiveSettingValue(look, i, value);
    }
    return look;
}

void ConfigStore::Load(const std::string& path)
{
    m_path = path;
    m_stamp = FileStamp(path);
    Read();
}

bool ConfigStore::ReloadIfChanged()
{
    unsigned long long stamp = FileStamp(m_path);
    if (stamp == m_stamp)
        return false;
    m_stamp = stamp;
    ReadKeepingStartupSwitches();
    VF_LOG_INFO("config reloaded: density=%.2f zones=%s ceiling=%.0f haze=%.2f ground=%.2f far=%.2f aerial=%.2f "
                "stockfog=%d sun=%.2f "
                "rays=%.2f glow=%d farclipmax=%.0f quality=%d debug=%d colour=%.2f/%.2f/%.2f/%.2f balance=%.2f "
                "land=%.2f skyhaze=%.2f/%.2f/%.2f/%.2f whitebalance=%.2f/%.2f sky %.2f/%.2f skybrightness=%.2f",
                m_config.density, ZoneFactorsText(m_config.zoneDensity).c_str(), m_config.fogCeiling, m_config.haze,
                m_config.groundFog, m_config.farFog, m_config.aerialPerspective,
                m_config.stockFog,
                m_config.sunScatter, m_config.godRays, m_config.glowCompensation ? 1 : 0, m_config.farClipMax,
                m_config.quality, m_config.debugView, m_config.lightSaturation, m_config.hazeSaturation,
                m_config.saturation,
                m_config.skySaturation, m_config.lightBalance, m_config.landBrightness, m_config.skyHaze,
                m_config.skyHazeGlow, m_config.skyHazeZenith, m_config.skyHazeGlowGrade, m_config.temperature,
                m_config.tint, m_config.skyTemperature, m_config.skyTint, m_config.skyBrightness);
    VF_LOG_INFO("  shafts=%d terrain=%d surface=%.2f lowsun=%.2f/%.2f strength=%.2f glare=%.2f focus=%.2f moon=%.2f "
                "moonfocus=%.2f haze=%.2f range=%.0f length=%.0f depth=%.2f memory=%d",
                m_config.lightShafts ? 1 : 0, m_config.terrainShadows ? 1 : 0, m_config.surfaceShadows,
                m_config.lowSunLight, m_config.lowSunShafts, m_config.shafts, m_config.shaftGlare, m_config.shaftFocus,
                m_config.moonShafts, m_config.moonShaftFocus, m_config.shaftHaze, m_config.shaftRange,
                m_config.shaftLength, m_config.shaftDepth, m_config.shaftMemory ? 1 : 0);
    VF_LOG_INFO("  drift=%.2f size=%.0f detail=%.2f swirl=%.2f wind=%.1f storm=%.1f dir=%.0f mist=%.2f/%.0f "
                "evening=%.2f weather=%.2f/%.2f/%.2f/%.2f/%.2f twilight=%.2f skyboxmoons=%d sunandmoon=%.2f glow=%.2f/%.2f "
                "lamps=%.2f/%d/%.2f fires=%.2f indoors=%.2f light=%.2f day=%.2f shadows=%.2f carried=%.2f dark=%.2f "
                "windows=%.2f",
                m_config.drift, m_config.driftSize, m_config.driftDetail, m_config.driftSwirl,
                m_config.windSpeed, m_config.stormWind, m_config.windDirection,
                m_config.morningMist, m_config.mistHeight, m_config.eveningHaze, m_config.weatherFog,
                m_config.weatherShafts, m_config.weatherTint, m_config.weatherSky, m_config.rainInFront, m_config.twilight, m_config.skyboxMoons ? 1 : 0, m_config.sunAndMoon, m_config.lightGlow,
                m_config.glowThreshold, m_config.lampFog, m_config.lampCount, m_config.lampFlicker,
                m_config.fireStrength, m_config.lampIndoors, m_config.lampLight, m_config.lampDayLight, m_config.lampShadows, m_config.carriedShadows, m_config.nightDarkness, m_config.windowLight);
    VF_LOG_INFO("  water: reflect=%.2f glitter=%.2f clarity=%.2f smooth=%.2f bump=%.2f foam=%.2f waves=%.2f "
                "rain=%.2f memory=%d",
                m_config.waterReflect, m_config.waterGlitter, m_config.waterClarity, m_config.waterSmooth,
                m_config.waterBump, m_config.waterFoam, m_config.waterWaves, m_config.waterRain,
                m_config.waterMemory ? 1 : 0);
    VF_LOG_INFO("  wet surfaces: sheen=%.2f wet=%.2f puddles=%.2f materials=%d", m_config.surfaceSheen,
                m_config.wetSurfaces, m_config.puddles, m_config.terrainMaterials ? 1 : 0);
    VF_LOG_INFO("  lantern=%d light=%.2f halo=%.2f swing=%.2f key=%s effects=%.2f corpses=%.2f spotkey=%s",
                m_config.lanternMode, m_config.lanternLight, m_config.lanternHalo, m_config.lanternSwing,
                HotkeyName(m_config.lanternKey).c_str(), m_config.effectLight, m_config.corpseLight,
                HotkeyName(m_config.spotKey).c_str());
    VF_LOG_INFO("  open land: dust=%.2f humid=%.2f horizon=%.2f clouds=%.2f cover=%.2f size=%.0f farlights=%.2f "
                "dry=%s green=%s",
                m_config.dustHaze, m_config.humidHaze, m_config.horizonFog, m_config.cloudShadows,
                m_config.cloudCover, m_config.cloudSize, m_config.farLights, ZoneListText(m_config.dryZones).c_str(),
                ZoneListText(m_config.greenZones).c_str());
    VF_LOG_INFO("  relief=%d models=%d depth=%.3f light=%.2f shadows=%.2f distance=%.0f key=%s",
                m_config.relief ? 1 : 0, m_config.reliefModels ? 1 : 0,
                m_config.reliefDepth, m_config.reliefLight, m_config.reliefShadows, m_config.reliefDistance,
                HotkeyName(m_config.reliefKey).c_str());
    VF_LOG_INFO("  relief by texture type: %s", ReliefClassSharesText(m_config).c_str());
    VF_LOG_INFO("  regions: key=%s togglekey=%s", HotkeyName(m_config.regionKey).c_str(),
                HotkeyName(m_config.toggleKey).c_str());
    VF_LOG_INFO("  drawlog=%d vs %s ps %s clientshadowmaps=%d perflog=%d", m_config.drawLog,
                ConstantRangesText(m_config.drawLogVertexConstants).c_str(),
                ConstantRangesText(m_config.drawLogPixelConstants).c_str(), m_config.clientShadowMaps ? 1 : 0,
                m_config.perfLog);
    return true;
}

void ConfigStore::Apply(const Config& edited)
{
    Config c = edited;
    c.enable = m_config.enable;
    c.hooks = m_config.hooks;
    c.overlay = m_config.overlay;
    c.overlayKey = m_config.overlayKey;
    c.toggleKey = m_config.toggleKey;
    c.drawLogKey = m_config.drawLogKey;
    c.lanternKey = m_config.lanternKey;
    c.spotKey = m_config.spotKey;
    c.reliefKey = m_config.reliefKey;
    c.regionKey = m_config.regionKey;
    c.drawLogVertexConstants = m_config.drawLogVertexConstants;
    c.drawLogPixelConstants = m_config.drawLogPixelConstants;
    c.plainGame = false;
    ClampLiveSettings(c);
    m_config = c;
    LogSetLevel(c.logLevel);
    Recompose();
}

void ConfigStore::SetLookLayer(const LookLayer& layer)
{
    m_layer = layer;
    Recompose();
}

Config PlainGameLook(const Config& look)
{
    Config c = look;
    c.plainGame = true;
    c.stockFog = 0;
    c.liquidDepth = false;
    c.relief = false;
    c.terrainMaterials = false;
    c.skyboxMoons = true;
    c.sunAndMoon = 0.0f;
    c.farClipMax = kFarClipMaxKeepsClientCap;
    c.clientShadowMaps = false;
    c.debugView = 0;
    return c;
}

void ConfigStore::SetPlainGame(bool on)
{
    m_plainGame = on;
    Recompose();
}

void ConfigStore::Recompose()
{
    m_look = LookLayerEmpty(m_layer) ? m_config : ComposeLook(m_config, m_layer);
    if (m_plainGame)
        m_look = PlainGameLook(m_look);
}

bool ConfigStore::Save()
{
    const Config onDisk = ReadFile();
    Config merged = m_config;
    bool written = true;
    for (const IntSetting& s : kIntSettings)
    {
        if (m_config.*s.value == m_saved.*s.value)
            merged.*s.value = onDisk.*s.value;
        else if (m_config.*s.value != onDisk.*s.value)
            written = WriteSetting(m_path, s.key, std::to_string(m_config.*s.value)) && written;
    }
    for (const FloatSetting& s : kFloatSettings)
    {
        if (m_config.*s.value == m_saved.*s.value)
        {
            merged.*s.value = onDisk.*s.value;
            continue;
        }
        const std::string text = SettingText(m_config.*s.value);
        merged.*s.value = std::strtof(text.c_str(), nullptr);
        if (merged.*s.value != onDisk.*s.value)
            written = WriteSetting(m_path, s.key, text) && written;
    }
    for (const BoolSetting& s : kBoolSettings)
    {
        if (m_config.*s.value == m_saved.*s.value)
            merged.*s.value = onDisk.*s.value;
        else if (m_config.*s.value != onDisk.*s.value)
            written = WriteSetting(m_path, s.key, m_config.*s.value ? "1" : "0") && written;
    }
    const std::pair<const char*, ZoneList Config::*> zoneLists[] = {{"DryZones", &Config::dryZones},
                                                                    {"GreenZones", &Config::greenZones}};
    for (const auto& [key, list] : zoneLists)
    {
        if (SameZoneList(m_config.*list, m_saved.*list))
            merged.*list = onDisk.*list;
        else if (!SameZoneList(m_config.*list, onDisk.*list))
            written = WriteSetting(m_path, key, ZoneListText(m_config.*list)) && written;
    }
    if (SameZoneFactors(m_config.zoneDensity, m_saved.zoneDensity))
        merged.zoneDensity = onDisk.zoneDensity;
    else
    {
        ParseZoneFactors(ZoneFactorsText(m_config.zoneDensity).c_str(), merged.zoneDensity);
        if (!SameZoneFactors(merged.zoneDensity, onDisk.zoneDensity))
            written = WriteSetting(m_path, "ZoneDensity", ZoneFactorsText(merged.zoneDensity)) && written;
    }
    m_stamp = FileStamp(m_path);
    if (!written)
    {
        VF_LOG_ERROR("settings could not be written to %s (error %lu)", m_path.c_str(), GetLastError());
        return false;
    }
    m_config = merged;
    m_saved = merged;
    LogSetLevel(merged.logLevel);
    Recompose();
    VF_LOG_INFO("settings saved to %s", m_path.c_str());
    return true;
}

bool ConfigStore::SaveLanternMode(int mode)
{
    mode = std::clamp(mode, kLanternOff, kLanternOn);
    const bool written = !m_path.empty() && WriteSetting(m_path, "Lantern", std::to_string(mode));
    m_stamp = FileStamp(m_path);
    m_config.lanternMode = mode;
    m_saved.lanternMode = mode;
    Recompose();
    if (!written)
        VF_LOG_ERROR("Lantern=%d could not be written to %s (error %lu)", mode, m_path.c_str(), GetLastError());
    return written;
}

bool ConfigStore::SaveRelief(bool on)
{
    const bool written = !m_path.empty() && WriteSetting(m_path, "Relief", on ? "1" : "0");
    m_stamp = FileStamp(m_path);
    m_config.relief = on;
    m_saved.relief = on;
    Recompose();
    if (!written)
        VF_LOG_ERROR("Relief=%d could not be written to %s (error %lu)", on ? 1 : 0, m_path.c_str(), GetLastError());
    return written;
}

void ConfigStore::Revert()
{
    m_stamp = FileStamp(m_path);
    ReadKeepingStartupSwitches();
}

void ConfigStore::ReadKeepingStartupSwitches()
{
    bool enable = m_config.enable;
    bool hooks = m_config.hooks;
    Read();
    m_config.enable = enable;
    m_config.hooks = hooks;
    Recompose();
}

void ConfigStore::Read()
{
    const Config c = ReadFile();
    m_config = c;
    m_saved = c;
    ++m_generation;
    LogSetLevel(c.logLevel);
    Recompose();
}

Config ConfigStore::ReadFile() const
{
    Config c;
    const std::string& p = m_path;
    c.enable = ReadInt(p, "Enable", 1, 0, 1) != 0;
    c.hooks = ReadInt(p, "EngineHooks", 1, 0, 1) != 0;
    c.overlay = ReadInt(p, "Overlay", 1, 0, 1) != 0;
    c.overlayKey = ReadHotkey(p, "OverlayKey", c.overlayKey);
    c.toggleKey = ReadHotkey(p, "ToggleKey", c.toggleKey);
    c.drawLogKey = ReadHotkey(p, "DrawLogKey", c.drawLogKey);
    c.lanternKey = ReadHotkey(p, "LanternKey", c.lanternKey);
    c.spotKey = ReadHotkey(p, "SpotKey", c.spotKey);
    c.reliefKey = ReadHotkey(p, "ReliefKey", c.reliefKey);
    c.regionKey = ReadHotkey(p, "RegionKey", c.regionKey);
    c.dryZones = ReadZoneList(p, "DryZones", c.dryZones);
    c.greenZones = ReadZoneList(p, "GreenZones", c.greenZones);
    c.zoneDensity = ReadZoneFactors(p, "ZoneDensity", c.zoneDensity);
    c.drawLogVertexConstants = ReadConstantRanges(p, "DrawLogVertexConstants", c.drawLogVertexConstants,
                                                  kVertexShaderConstantRegisters);
    c.drawLogPixelConstants =
        ReadConstantRanges(p, "DrawLogPixelConstants", c.drawLogPixelConstants, kPixelShaderConstantRegisters);
    for (const IntSetting& s : kIntSettings)
        c.*s.value = ReadInt(p, s.key, c.*s.value, s.lo, s.hi);
    for (const FloatSetting& s : kFloatSettings)
        c.*s.value = ReadFloat(p, s.key, c.*s.value, s.lo, s.hi);
    for (const BoolSetting& s : kBoolSettings)
        c.*s.value = ReadInt(p, s.key, c.*s.value ? 1 : 0, 0, 1) != 0;
    ClampLiveSettings(c);
    return c;
}

ConfigStore& GlobalConfig()
{
    static ConfigStore store;
    return store;
}
