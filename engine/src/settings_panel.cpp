#include "settings_panel.h"

#include "fog_model.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
constexpr float kMultiplierSliderMax = 4.0f;
constexpr float kPanelWidthInLines = 30.0f;
constexpr float kPanelMarginInLines = 2.0f;
const ImVec4 kDrawnColour = {0.45f, 0.85f, 0.45f, 1.0f};
const ImVec4 kSkippedColour = {0.95f, 0.75f, 0.35f, 1.0f};
const ImVec4 kErrorColour = {1.0f, 0.4f, 0.4f, 1.0f};

const char* const kQualityNames[] = {"Low: quarter resolution, 16 steps", "Medium: half resolution, 24 steps",
                                     "High: half resolution, 32 steps"};
const char* const kDebugViewNames[] = {"Off",
                                       "Fog radiance",
                                       "Transmittance",
                                       "Linear depth",
                                       "Surface shadows",
                                       "Lamp halos",
                                       "Lamp light on surfaces",
                                       "Client shadow maps (receiver matrices)",
                                       "Client shadow maps (caster matrices)"};
const char* const kLogLevelNames[] = {"Errors", "Info", "Debug"};
const char* const kLanternModeNames[] = {"Off", "In the dark", "Always"};
const char* const kBankModeNames[] = {"Off: even layers", "Everywhere, all day", "Valleys and water at dawn and dusk"};

template <int N>
bool Choice(const char* label, int& value, int first, const char* const (&names)[N], const char* help)
{
    int index = value - first;
    const bool changed = ImGui::Combo(label, &index, names, N);
    ImGui::SetItemTooltip("%s", help);
    if (changed)
        value = index + first;
    return changed;
}

class Section
{
public:
    explicit Section(const char* title, ImGuiTreeNodeFlags flags = 0)
        : m_title(title), m_open(ImGui::CollapsingHeader(title, flags))
    {
        if (m_open)
            ImGui::PushID(m_title);
    }
    ~Section()
    {
        if (m_open)
            ImGui::PopID();
    }
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;
    explicit operator bool() const { return m_open; }

private:
    const char* m_title;
    bool m_open;
};

bool Slider(const char* label, float& value, float lo, float hi, const char* format, const char* help,
            ImGuiSliderFlags flags = ImGuiSliderFlags_None)
{
    const bool changed = ImGui::SliderFloat(label, &value, lo, hi, format, flags);
    ImGui::SetItemTooltip("%s", help);
    return changed;
}

bool Multiplier(const char* label, float& value, const char* help)
{
    return Slider(label, value, 0.0f, kMultiplierSliderMax, "%.2f", help);
}

bool Count(const char* label, int& value, int lo, int hi, const char* help)
{
    const bool changed = ImGui::SliderInt(label, &value, lo, hi);
    ImGui::SetItemTooltip("%s", help);
    return changed;
}

bool Toggle(const char* label, bool& value, const char* help)
{
    const bool changed = ImGui::Checkbox(label, &value);
    ImGui::SetItemTooltip("%s", help);
    return changed;
}

bool Toggle(const char* label, int& value, const char* help)
{
    bool on = value != 0;
    const bool changed = Toggle(label, on, help);
    if (changed)
        value = on ? 1 : 0;
    return changed;
}

void DrawStatus(const FogFrameStatus& status)
{
    if (status.drawn)
        ImGui::TextColored(kDrawnColour, "Fog: Drawing");
    else
        ImGui::TextColored(kSkippedColour, "Fog: Not drawing (%s)", status.reason);
}

bool DrawQuality(Config& c)
{
    const Section section("Quality", ImGuiTreeNodeFlags_DefaultOpen);
    if (!section)
        return false;
    bool changed = Choice("Quality", c.quality, 1, kQualityNames, "Resolution and ray-march steps of the fog pass");
    changed |= Slider("Temporal filter", c.temporal, 0.0f, 0.97f, "%.2f",
                      "History weight of the temporal filter, 0 = off");
    return changed;
}

bool DrawZoneDensity(Config& c)
{
    const ZoneClimate& climate = GlobalZoneClimate();
    const int zone = climate.ZoneId();
    if (zone <= 0)
    {
        ImGui::TextDisabled("Density in this zone: zone not known yet");
        return false;
    }
    float factor = ZoneFactorOf(c.zoneDensity, zone);
    char label[64];
    std::snprintf(label, sizeof(label), "Density in zone %d##zoneDensity", zone);
    if (!Slider(label, factor, kMinZoneFactor, kMaxZoneFactor, "%.2f",
                "This zone's own multiplier of Density, kept for this zone only (ZoneDensity). At the factor of * "
                "in ZoneDensity (1 without one) the zone is taken off the list. A zone change fades over 4 seconds"))
        return false;
    return SetZoneFactor(c.zoneDensity, zone, factor);
}

bool DrawDensity(Config& c)
{
    const Section section("Density", ImGuiTreeNodeFlags_DefaultOpen);
    if (!section)
        return false;
    bool changed = Multiplier("Density", c.density, "Global density multiplier");
    changed |= DrawZoneDensity(c);
    changed |= Slider("Fog ceiling", c.fogCeiling, 0.0f, kMaxFogCeiling, "%.0f yd",
                      "While you fly or stand well above the ground, the fog keeps to this many yards over the ground "
                      "(fading over half as much again), so it lies in valleys and among the trees and the air above "
                      "it is clear; the sunlit air still shows the shafts. It comes in as the camera rises from 1.5 to "
                      "3 times this height over the ground under it, so nothing changes on foot. 0 = off");
    changed |= Multiplier("Haze", c.haze, "Distance haze where no Classic data exists");
    changed |= Multiplier("Ground fog", c.groundFog, "Low ground mist where no Classic data exists");
    changed |= Multiplier("Distance fog", c.farFog, "The distance fog that replaces the stock fog");
    changed |= Slider("Fog follows the view", c.farFogStretch, 0.0f, 1.0f, "%.2f",
                      "1: with the view distance lifted past the client's cap, the distance fog reaches as much "
                      "further as the view does, so land past the zone's fog end is not drawn as a flat fog-coloured "
                      "shape. 0: the fog ends at the zone's fog distance in yards");
    changed |= Slider("Aerial perspective", c.aerialPerspective, 0.0f, 1.0f, "%.2f",
                      "1: distant land fades into the zone's own fog colour, the sky at the horizon, and the sun "
                      "brightens the haze only around itself. 0: the haze is lit by the sun from every side, which "
                      "turns distant mountains pale");
    changed |= Toggle("Replace the stock fog", c.stockFog,
                      "On: the volumetric distance fog replaces the client's linear fog. Off: keep the stock fog");
    changed |= Toggle("Classic fog layers", c.dataMode,
                      "Use the Classic client's fog layers where its lights cover the map. Off: derived layers "
                      "everywhere");
    changed |= Slider("Fog range", c.maxDistance, 200.0f, 5000.0f, "%.0f yd",
                      "How far sky rays are integrated and the scale of the Classic distance curves");
    return changed;
}

bool DrawLight(Config& c)
{
    const Section section("Light", ImGuiTreeNodeFlags_DefaultOpen);
    if (!section)
        return false;
    bool changed = Multiplier("Sun scatter", c.sunScatter, "In-scattered sun or moon light: the halo and the shafts");
    changed |= Multiplier("Ambient", c.ambient, "Ambient fog brightness");
    changed |= Multiplier("Exposure", c.exposure, "Brightness of the layers used where no Classic data exists");
    changed |= Multiplier("Classic exposure", c.classicExposure, "Brightness of the Classic layers, 1 = as authored");
    changed |= Toggle("Linear light", c.colorSpace,
                      "Scatter and blend in linear light like the modern client, with a soft highlight roll-off. "
                      "Off: gamma");
    changed |= Toggle("Light shafts", c.lightShafts,
                      "Shadowed in-scattering: light shafts through trees, buildings and terrain");
    changed |= Toggle("Terrain shadows", c.terrainShadows,
                      "Mountains and buildings around you keep the sun and moon out of the fog in their shadow, "
                      "whether or not they are on screen");
    changed |= Slider("Surface shadows", c.surfaceShadows, 0.0f, 1.0f, "%.2f",
                      "Shadows of mountains and buildings on the ground, walls and characters, from the same height "
                      "map, whether or not the mountain is on screen. 1 = the sun's whole share of the light is taken "
                      "away, 0 = off");
    changed |= Slider("Low sun light", c.lowSunLight, 0.0f, 2.0f, "%.2f",
                      "While the sun is low, slopes and walls facing it, where it reaches, get a warm lift in its "
                      "colour, so the light lands on the far mountains at dawn and dusk. 1 = 35% brighter, 0 = off");
    changed |= Slider("God rays", c.godRays, 0.0f, 4.0f, "%.2f",
                      "Radial rays from the bright sky around the sun, 0 = off");
    changed |= Toggle("Glow compensation", c.glowCompensation,
                      "Pre-compensate the fog for the client's full-screen glow, which otherwise bleaches bright fog "
                      "around the sun to white");
    return changed;
}

bool DrawColour(Config& c)
{
    const Section section("Colour");
    if (!section)
        return false;
    bool changed = Slider("Light saturation", c.lightSaturation, 0.0f, kMaxLightSaturation, "%.2f",
                          "Colour of the sun's or moon's light in the fog, the shafts and the haze, at the same "
                          "brightness: 1 = the client's, 0 = white. The sky-lit part of the fog keeps its colour");
    changed |= Slider("Haze saturation", c.hazeSaturation, 0.0f, kMaxSaturation, "%.2f",
                      "Colour of the sky-lit part of the fog and the haze, the zone's fog colour, at the same "
                      "brightness: 1 = the client's, 0 = grey, 2 = twice as strong");
    changed |= Slider("Saturation", c.saturation, 0.0f, kMaxSaturation, "%.2f",
                      "Colour of the whole picture at the same brightness: 1 = unchanged, 0 = grey (needs Linear "
                      "light)");
    changed |= Slider("Sky saturation", c.skySaturation, 0.0f, kMaxSaturation, "%.2f",
                      "Colour of the sky alone, on top of Saturation: 1 = as the rest of the picture, 0 = grey");
    changed |= Slider("Temperature", c.temperature, -kMaxWhiteBalance, kMaxWhiteBalance, "%.2f",
                      "White balance of the whole picture at the same brightness: above 0 warmer (amber), below 0 "
                      "cooler (blue); 1 = red twice blue (needs Linear light)");
    changed |= Slider("Tint", c.tint, -kMaxWhiteBalance, kMaxWhiteBalance, "%.2f",
                      "White balance of the whole picture at the same brightness: above 0 toward magenta, below 0 "
                      "toward green (needs Linear light)");
    changed |= Slider("Sky temperature", c.skyTemperature, -kMaxWhiteBalance, kMaxWhiteBalance, "%.2f",
                      "Temperature of the sky alone, on top of Temperature: 0 = as the rest of the picture");
    changed |= Slider("Sky tint", c.skyTint, -kMaxWhiteBalance, kMaxWhiteBalance, "%.2f",
                      "Tint of the sky alone, on top of Tint: 0 = as the rest of the picture");
    changed |= Slider("Light balance", c.lightBalance, 0.0f, kMaxLightBalance, "%.2f",
                      "White balance of the land and everything on it against the client's sun and ambient light: "
                      "0 = the client's colour, 1 = a neutral light at the same brightness, 2 = past it. Not the sky, "
                      "not indoors (needs Linear light)");
    changed |= Slider("Land brightness", c.landBrightness, kMinLandBrightness, kMaxLandBrightness, "%.2f",
                      "Brightness of the land and everything on it, not the sky: 1 = the client's (needs Linear "
                      "light)");
    changed |= Slider("Sky brightness", c.skyBrightness, kMinSkyBrightness, kMaxSkyBrightness, "%.2f",
                      "Brightness of the sky alone, in linear light: 1 = the client's, 2 = twice as bright (needs "
                      "Linear light)",
                      ImGuiSliderFlags_Logarithmic);
    changed |= Slider("Sky haze", c.skyHaze, 0.0f, 1.0f, "%.2f",
                      "Share of the clear sky behind a haze in the horizon's colour, as rain covers it with "
                      "Overcast sky: 0 = the client's sky, 1 = all haze");
    changed |= Slider("Sun in the haze", c.skyHazeGlow, 0.0f, kMaxSkyHazeGlow, "%.2f",
                      "Brightness of the sun's disc and its halo in the sky haze, in the sun's colour");
    changed |= Slider("Haze overhead", c.skyHazeZenith, kMinSkyHazeZenith, 1.0f, "%.2f",
                      "Brightness of the sky haze straight overhead against the horizon, in linear light: 1 = even, "
                      "0.1 = a bright band at the horizon under a dark sky",
                      ImGuiSliderFlags_Logarithmic);
    changed |= Slider("Sun takes the sky's colour", c.skyHazeGlowGrade, 0.0f, 1.0f, "%.2f",
                      "How much of the sky's own colour, brightness and saturation the sun's glow in the haze takes: "
                      "1 = all of it, 0 = it keeps the sun's colour under a sky turned cool or grey");
    changed |= Slider("Haze takes the sky's colour", c.hazeFromSky, 0.0f, 1.0f, "%.2f",
                      "How far the haze's sky-lit light in front of the land takes the colour of the sky at the "
                      "horizon, as drawn: 1 = all the way (a bright sky gives bright distant hills, a dark one dark "
                      "ones), 0 = the haze's own colour. The sky and the sun's light in the haze are unchanged");
    changed |= Slider("Haze against the sky", c.hazeSkyLevel, kMinHazeSkyLevel, kMaxHazeSkyLevel, "%.2f",
                      "The brightness of that haze against the sky at the horizon, as displayed: 0.85 = a bit darker, "
                      "1 = as bright. Land seen through the haze keeps some of its own light, so snow can still "
                      "stand out brighter than the sky");
    return changed;
}

bool DrawShafts(Config& c)
{
    const Section section("Sun and moon shafts", ImGuiTreeNodeFlags_DefaultOpen);
    if (!section)
        return false;
    ImGui::BeginDisabled(!c.lightShafts);
    bool changed = Slider("Shafts", c.shafts, 0.0f, 4.0f, "%.2f",
                          "Extra sunlight scattered in the fog and the air wherever the sun reaches, traced through "
                          "trees, buildings and terrain whether or not the sun is on screen. 0 = only the fog's own "
                          "shadowed light");
    changed |= Slider("Focus", c.shaftFocus, 0.0f, 0.95f, "%.2f",
                      "0 = shafts as bright from every side; higher = brighter looking toward the sun, fainter "
                      "looking away");
    changed |= Slider("Low sun", c.lowSunShafts, 0.0f, 3.0f, "%.2f",
                      "Stronger shafts while the sun is low: 1 = twice as strong at dawn and dusk, fading out by "
                      "30 degrees up; 0 = the same all day");
    changed |= Slider("Sun glare", c.shaftGlare, kMinShaftGlare, kMaxShaftGlare, "%.2f",
                      "How bright the shafts' light grows before it grows only slowly: lower keeps the glow around a "
                      "low sun small while beams through trees and gaps keep their strength; 4 = almost no limit",
                      ImGuiSliderFlags_Logarithmic);
    changed |= Slider("Moon shafts", c.moonShafts, 0.0f, 8.0f, "%.2f",
                      "The same beams from the moon at night. Moonlight is dim and the moon stands "
                      "high, so they need more strength than the sun's. 0 = only the fog's own shadowed light");
    changed |= Slider("Moon focus", c.moonShaftFocus, 0.0f, 0.95f, "%.2f",
                      "Focus of the moon's shafts. The moon stands high, so a low focus keeps them visible when "
                      "looking level");
    changed |= Multiplier("Clear air", c.shaftHaze, "Air lit by the shafts, so they show where the fog is thin");
    changed |= Slider("Range", c.shaftRange, 30.0f, 600.0f, "%.0f yd", "How far from the camera shafts are traced");
    changed |= Toggle("Remember what was seen", c.shaftMemory,
                      "Trees and buildings shade the shafts from a shadow map, seen from the sun, of everything "
                      "the camera has seen so far. It stays fixed in the world, so turning the camera does not "
                      "change the shafts, and gaps between leaves stay open. Off = only what is on screen, with a "
                      "guess past its edges");
    ImGui::BeginDisabled(c.shaftMemory);
    changed |= Slider("Length", c.shaftLength, 10.0f, 300.0f, "%.0f yd",
                      "How far toward the sun each point looks for trees and buildings that shade it (only "
                      "without the memory, which looks all the way)");
    changed |= Slider("Occluder depth", c.shaftDepth, 0.1f, 4.0f, "%.2f",
                      "How deep trees and buildings are taken to be behind their visible face, per yard of their "
                      "distance (only without the memory)");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return changed;
}

bool DrawDriftAndMist(Config& c)
{
    const Section section("Drift and mist");
    if (!section)
        return false;
    bool changed = Slider("Drift", c.drift, 0.0f, 2.0f, "%.2f",
                          "How patchy the fog is. The patches drift with the wind and the shafts brighten in the "
                          "thick ones. 0 = even fog, as before");
    changed |= Slider("Patch size", c.driftSize, 10.0f, 300.0f, "%.0f yd", "Size of the drifting fog patches");
    changed |= Slider("Smoke", c.driftDetail, 0.0f, 1.0f, "%.2f",
                      "Share of fine, wispy smoke in the drift, a tenth of the patch size and smaller. 0 = only the "
                      "large patches");
    changed |= Slider("Swirl", c.driftSwirl, 0.0f, 3.0f, "%.2f",
                      "How far the large patches twist and stretch the smoke. 0 = straight wisps");
    changed |= Choice("Fog banks", c.banks, kBanksOff, kBankModeNames,
                      "Gathers the fog into separate banks, as big as the patches, that lie on the ground, fill "
                      "valleys and leave clear air between them, instead of even layers at your own height");
    ImGui::BeginDisabled(c.banks == kBanksOff);
    changed |= Slider("Bank cover", c.bankCover, 0.0f, 1.0f, "%.2f",
                      "Share of the ground under a fog bank; the banks are denser where they are fewer, so the fog "
                      "as a whole keeps its amount. 1 = one even blanket");
    changed |= Slider("Bank height", c.bankHeight, kMinBankHeight, kMaxBankHeight, "%.0f yd",
                      "How high the banks reach above the ground under them");
    changed |= Slider("Valleys", c.bankPool, 0.0f, 3.0f, "%.2f",
                      "How much the fog gathers in valleys and over lakes and leaves ridges and hilltops clear. 0 = "
                      "the same on every slope");
    changed |= Slider("Bank shade", c.bankShadow, 0.0f, 3.0f, "%.2f",
                      "How much a bank shades its own inside from the sun or moon, so it has a lit side and a dark "
                      "one");
    ImGui::EndDisabled();
    changed |= Slider("Wind", c.windSpeed, 0.0f, 10.0f, "%.1f yd/s", "Drift speed in clear weather");
    changed |= Slider("Storm wind", c.stormWind, 0.0f, 20.0f, "%.1f yd/s",
                      "Drift speed in full rain, snow or sandstorm; weaker weather lies in between");
    changed |= Slider("Wind direction", c.windDirection, 0.0f, 360.0f, "%.0f deg",
                      "Where the wind blows to: 0 north, 90 east, 180 south, 270 west");
    changed |= Slider("Morning mist", c.morningMist, 0.0f, 8.0f, "%.2f",
                      "Extra fog in low ground at dawn (full 05:00 to 07:30, gone by 10:00). 2 = three times the "
                      "fog near the ground; zones with little fog of their own get up to eight times more");
    changed |= Slider("Mist height", c.mistHeight, 2.0f, 60.0f, "%.0f yd",
                      "How far above your ground level the morning mist reaches");
    changed |= Slider("Evening haze", c.eveningHaze, 0.0f, 2.0f, "%.2f",
                      "Extra fog everywhere in the evening (full 20:00 to 22:00, gone by midnight)");
    return changed;
}

bool DrawWeather(Config& c)
{
    const Section section("Weather");
    if (!section)
        return false;
    bool changed = Slider("Weather fog", c.weatherFog, 0.0f, 4.0f, "%.2f",
                          "Extra fog in full rain, snow or sandstorm. 1 = twice the fog");
    changed |= Slider("Weather shafts", c.weatherShafts, 0.0f, 1.0f, "%.2f",
                      "Share of the shafts left in full weather, as clouds hide the sun");
    changed |= Slider("Weather tint", c.weatherTint, 0.0f, 1.0f, "%.2f",
                      "Fog turns grey in rain, white in snow and sandy in a sandstorm");
    changed |= Slider("Weather sky", c.weatherSky, 0.0f, 1.0f, "%.2f",
                      "The sky and its clouds turn the colour of the weather's fog, overcast. 1 = 85% in full "
                      "weather");
    changed |= Slider("Rain in front", c.rainInFront, 0.0f, 1.0f, "%.2f",
                      "Rain, snow and sand stay in front of the fog and the overcast sky, as near as they fall: "
                      "1 = fogged as if 25 yd away, 0 = as if as far as what is behind them");
    return changed;
}

bool DrawZoneKind(Config& c)
{
    const ZoneClimate& climate = GlobalZoneClimate();
    const int zone = climate.ZoneId();
    if (zone <= 0)
    {
        ImGui::TextDisabled("Zone: not known yet");
        return false;
    }
    const bool dry = ZoneListIncludes(c.dryZones, zone);
    const bool green = !dry && ZoneListIncludes(c.greenZones, zone);
    const ZoneClimateWeights now = climate.Weights();
    ImGui::Text("Zone %d (dry %.2f, green %.2f)", zone, now.dry, now.green);
    int kind = dry ? 1 : green ? 2 : 0;
    const int before = kind;
    ImGui::RadioButton("Neither", &kind, 0);
    ImGui::SetItemTooltip("This zone keeps the plain distance fog: no dust or humid haze, no cloud shadows");
    ImGui::SameLine();
    ImGui::RadioButton("Dry", &kind, 1);
    ImGui::SetItemTooltip("This zone counts as dry: warm dust haze, scattered clouds (DryZones)");
    ImGui::SameLine();
    ImGui::RadioButton("Green", &kind, 2);
    ImGui::SetItemTooltip("This zone counts as green open land: humid haze, more clouds (GreenZones)");
    if (kind == before)
        return false;
    ZoneListRemove(c.dryZones, zone);
    ZoneListRemove(c.greenZones, zone);
    if (kind == 1)
        ZoneListAdd(c.dryZones, zone);
    else if (kind == 2)
        ZoneListAdd(c.greenZones, zone);
    return true;
}

bool DrawOpenLand(Config& c)
{
    const Section section("Open land");
    if (!section)
        return false;
    bool changed = DrawZoneKind(c);
    changed |= Slider("Dust haze", c.dustHaze, 0.0f, 3.0f, "%.2f",
                      "Warm, sunlit dust in the air of dry zones, thickest in the afternoon and in wind. 0 = off");
    changed |= Slider("Humid haze", c.humidHaze, 0.0f, 3.0f, "%.2f",
                      "A soft, cool haze over green open land, thickest in the morning. 0 = off");
    changed |= Slider("Horizon fog", c.horizonFog, 0.0f, 1.0f, "%.2f",
                      "In dry and green zones, far land fades gradually into the colour of the sky at the horizon. "
                      "0 = the zone's own distance fog");
    changed |= Slider("Cloud shadows", c.cloudShadows, 0.0f, 1.0f, "%.2f",
                      "Shadows of clouds drifting with the wind over dry and green zones, on the ground and in the "
                      "haze; with a low sun they give beams between the clouds. 1 = a cloud takes the sun's whole "
                      "share of the light away, 0 = off");
    changed |= Slider("Cloud cover", c.cloudCover, 0.0f, 2.0f, "%.2f",
                      "How much of the sky the clouds cover: 1 = about a quarter in dry zones and nearly half in "
                      "green ones, at most 90%");
    changed |= Slider("Cloud size", c.cloudSize, 100.0f, 2000.0f, "%.0f yd", "How wide one cloud is, in yards");
    return changed;
}

bool DrawNight(Config& c)
{
    const Section section("Dusk and night");
    if (!section)
        return false;
    bool changed = Slider("Twilight", c.twilight, 0.0f, 2.0f, "%.2f",
                          "Blue tint of the fog around sunset and sunrise, while the sun is just below the horizon "
                          "and the moon is not up");
    changed |= Toggle("Skybox moons", c.skyboxMoons,
                      "Draw the moons the HD skyboxes paint on their sky. Off = only the game's own moon shows, the "
                      "one the moonlight, the moon shafts and the glitter on the water come from");
    changed |= Slider("Sun and moon over the sky", c.sunAndMoon, 0.0f, 1.0f, "%.2f",
                      "The game's own sun and moon drawn over the sky last, so an HD skybox or the sky haze cannot "
                      "hide them; land in front still does. 1 = as the client draws them, dimmer in thick fog and "
                      "rain; 0 = left to the client, under the skybox and the haze");
    changed |= Slider("Light glow", c.lightGlow, 0.0f, 8.0f, "%.2f",
                      "Lamps, windows and fires glow into the fog at night; the thicker the fog, the larger the "
                      "halo. 0 = off");
    changed |= Slider("Glow threshold", c.glowThreshold, 0.2f, 0.95f, "%.2f",
                      "How bright a pixel must be to glow; lower = more things glow");
    changed |= Slider("Lamp halos", c.lampFog, 0.0f, 4.0f, "%.2f",
                      "Torches, candles, braziers and lanterns placed in the world glow in the air, more in rain, "
                      "snow or sand; by day the By day share of it; none in the ghost world. Needs lampdata.bin. "
                      "0 = off");
    changed |= Count("Lamps", c.lampCount, 0, kMaxLampCount,
                     "How many lamps in view glow and light things, brightest first. Lamps reach 800 yd and fade from "
                     "650 yd; while halos or far lights show, up to 64 more lamps beyond 80 yd glow without lighting "
                     "things");
    changed |= Slider("Flicker", c.lampFlicker, 0.0f, 2.0f, "%.2f",
                      "How much flames flicker; lanterns behind glass stay steady. 0 = steady");
    changed |= Slider("Fires", c.fireStrength, 0.0f, 2.0f, "%.2f",
                      "Strength of open fires (braziers, bonfires, campfires and hearths), their light and halos, "
                      "against the other lamps. 1 = as the other lamps");
    changed |= Slider("Far lights", c.farLights, 0.0f, 4.0f, "%.2f",
                      "At night the lamps of towns and camps from 60 yd out show as small points of light, even "
                      "where their halos are too faint to see, so a town across a valley isn't dark. Hidden lamps "
                      "show none. 0 = off");
    changed |= Slider("Indoors", c.lampIndoors, 0.0f, 2.0f, "%.2f",
                      "Strength of lamps inside rooms (inns, houses, halls), and of every lamp while you stand "
                      "indoors, their light and halos. 1 = as outdoors");
    changed |= Slider("Lamp light", c.lampLight, 0.0f, 4.0f, "%.2f",
                      "The same lamps light the ground, walls, plants and characters around them in their own "
                      "colour, falling off like the game's own lights; by day too, most in shade; none in the "
                      "ghost world. 0 = off");
    changed |= Slider("By day", c.lampDayLight, 0.0f, 1.0f, "%.2f",
                      "Share of the lamps' light and halos by day, when the sun outshines them. 0 = none by "
                      "day, 1 = as at night");
    changed |= Slider("Window light", c.windowLight, 0.0f, 4.0f, "%.2f",
                      "Lit windows cast their light onto the street, walls and characters in front of them, up to "
                      "8 windows at a time; none by day and none in the ghost world. 0 = off");
    changed |= Slider("Spell light", c.effectLight, 0.0f, 4.0f, "%.2f",
                      "Glowing spells, projectiles, enchants and other bright effects on screen light the ground, "
                      "walls and characters around them in their own colour, up to 6 at a time; the placed lamps' "
                      "own flames are left to the lamps. In rain or snow only coloured glows count. By day the "
                      "lamps' By day share; none in the ghost world. 0 = off");
    changed |= Slider("Loot sparkle", c.corpseLight, 0.0f, 2.0f, "%.2f",
                      "Corpses you can loot give a small golden twinkle of light, the nearest 4 within 40 yd, from "
                      "the client's own list of what is lootable. By day the lamps' By day share; none in the ghost "
                      "world. 0 = off");
    changed |= Slider("Lamp shadows", c.lampShadows, 0.0f, 1.0f, "%.2f",
                      "Things on screen between a lamp and a surface block its light. 0 = no lamp shadows");
    changed |= Slider("Carried light shadows", c.carriedShadows, 0.0f, 1.0f, "%.2f",
                      "How much of the lamp shadows the lights that move with things keep: spells, projectiles, "
                      "enchants, the loot sparkle and the belt lantern. 0 = they cast no shadows, so no character's "
                      "shadow swings around with them; placed lamps keep theirs");
    changed |= Slider("Night darkness", c.nightDarkness, 0.0f, 0.9f, "%.2f",
                      "Darkens the moonlit scene at night outdoors, but not flames, lit windows, lit interiors or "
                      "what the lamps light, and not while you are indoors. 0 = the game's own night");
    return changed;
}

bool DrawLantern(Config& c)
{
    const Section section("Belt lantern");
    if (!section)
        return false;
    bool changed = Choice("Lantern", c.lanternMode, kLanternOff, kLanternModeNames,
                          "A lantern at your belt lights the ground, walls, plants and characters around you, and "
                          "what stands in its light casts shadows. In the dark it lights by itself at night and "
                          "indoors (caves, dungeons, buildings) and goes out by day; never while swimming or as a "
                          "ghost");
    changed |= Slider("Lantern light", c.lanternLight, 0.0f, 4.0f, "%.2f",
                      "How brightly the lantern lights things around you. 0 = no lantern");
    changed |= Slider("Lantern halo", c.lanternHalo, 0.0f, 2.0f, "%.2f",
                      "How strongly the lantern glows in the fog around it, on top of Lamp halos. 0 = no glow");
    changed |= Slider("Swing", c.lanternSwing, 0.0f, 2.0f, "%.2f",
                      "How much the lantern swings on its hook as you walk, run, turn and stop. 0 = it hangs still");
    ImGui::TextDisabled("%s switches it on or off and saves that.", HotkeyName(c.lanternKey).c_str());
    return changed;
}

struct ReliefTypeGroup
{
    reliefclass::Group group;
    const char* title;
};

constexpr ReliefTypeGroup kReliefTypeGroups[] = {
    {reliefclass::Group::Ground, "Ground"},
    {reliefclass::Group::Buildings, "Buildings"},
    {reliefclass::Group::Nature, "Nature"},
    {reliefclass::Group::Living, "Creatures and characters"},
    {reliefclass::Group::Other, "Other"},
};

bool ReliefShareSlider(const char* label, float& share, const char* help)
{
    if (!Slider(label, share, reliefclass::kMinShare, reliefclass::kMaxShare, "%.2f", help))
        return false;
    share = std::clamp(std::round(share / reliefclass::kShareStep) * reliefclass::kShareStep, reliefclass::kMinShare,
                       reliefclass::kMaxShare);
    return true;
}

bool DrawReliefByType(Config& c)
{
    if (!ImGui::TreeNode("Relief by texture type"))
        return false;
    ImGui::TextDisabled("Each type's share of Depth; 0 = flat, 1 = the full depth.");
    ImGui::TextDisabled("For now only the terrain has relief: the ground's types.");
    bool changed = false;
    for (const ReliefTypeGroup& g : kReliefTypeGroups)
    {
        ImGui::SeparatorText(g.title);
        for (uint8_t id = 1; id < reliefclass::kCount; ++id)
        {
            const reliefclass::Info& info = reliefclass::kInfo[id];
            float* share = ReliefClassShareField(c, id);
            if (info.group != g.group || !share)
                continue;
            char label[32];
            std::snprintf(label, sizeof label, "%c%s", std::toupper(static_cast<unsigned char>(info.name[0])),
                          info.name + 1);
            changed |= ReliefShareSlider(label, *share, info.help);
        }
    }
    ImGui::TreePop();
    return changed;
}

bool DrawRelief(Config& c)
{
    const Section section("Ground relief");
    if (!section)
        return false;
    bool changed = Toggle("Relief", c.relief,
                          "Paved roads, tiles, stones and rock on the ground get depth: they shift with the view "
                          "(parallax occlusion), catch the light on their sunlit side and shade themselves. Needs "
                          "reliefdata.bin and exterior shadows 3-5 (extShadowQuality)");
    changed |= Toggle("On models", c.reliefModels,
                      "Trees, creatures, characters, armour and props get relief too. Their height maps are made "
                      "while you play, on a thread of their own, the first time each texture is drawn");
    changed |= Slider("Depth", c.reliefDepth, 0.0f, 0.75f, "%.3f yd",
                      "How deep the relief is at its deepest, for stone and paving; each texture type gets its share "
                      "of it (Relief by texture type)");
    changed |= Slider("Light", c.reliefLight, 0.0f, 2.0f, "%.2f",
                      "How strongly the relief shapes the light (its sunlit and shaded sides). 0 = depth only");
    changed |= Slider("Self shadows", c.reliefShadows, 0.0f, 1.0f, "%.2f",
                      "How dark the shadows are that stones and ridges cast onto their own texture");
    changed |= Slider("Distance", c.reliefDistance, 5.0f, 1500.0f, "%.0f yd",
                      "Relief is drawn up to this distance and fades out over its last quarter",
                      ImGuiSliderFlags_Logarithmic);
    changed |= DrawReliefByType(c);
    ImGui::TextDisabled("%s switches it on or off and saves that.", HotkeyName(c.reliefKey).c_str());
    return changed;
}

bool DrawWater(Config& c)
{
    const Section section("Water");
    if (!section)
        return false;
    bool changed = Slider("Reflections", c.waterReflect, 0.0f, 2.0f, "%.2f",
                          "Water mirrors the sky, shores, trees and buildings on screen; strong when you look across "
                          "the water, weak when you look down into it. 0 = off");
    changed |= Slider("Glitter", c.waterGlitter, 0.0f, 4.0f, "%.2f",
                      "Sparkling path of the sun, and of the moon at night, on the waves; less under clouds and "
                      "none in the ghost world. 0 = off");
    changed |= Slider("Clarity", c.waterClarity, 0.0f, 1.0f, "%.2f",
                      "See the bottom through shallow water, and a deeper teal where the water gets deep. "
                      "0 = the game's own water colour");
    changed |= Slider("Smooth", c.waterSmooth, 0.0f, 1.0f, "%.2f",
                      "The game's animated water texture under the reflections is replaced by the zone's own water "
                      "colours, shallow to deep, rivers and lakes apart from the sea, so the water reads as one smooth "
                      "surface that the waves and reflections play on. 0 = the game's texture as drawn");
    changed |= Slider("Bump", c.waterBump, 0.0f, 2.0f, "%.2f",
                      "Fine ripples from the later clients' water normal map (waterbump.blp, copied from the game's "
                      "archives by Lumenveil-Setup.cmd) drift with the wind over the waves and bend the reflections. "
                      "0 = off");
    changed |= Slider("Foam", c.waterFoam, 0.0f, 2.0f, "%.2f",
                      "Foam where the water is shallow along the shores, from the later clients' foam texture "
                      "(waterfoam.blp, copied by Lumenveil-Setup.cmd), drifting with the wind. 0 = off");
    changed |= Slider("Waves", c.waterWaves, 0.0f, 2.0f, "%.2f",
                      "Small waves that break up the reflections and the glitter; higher in rain, snow or sand. "
                      "0 = a still mirror");
    changed |= Slider("Rain rings", c.waterRain, 0.0f, 2.0f, "%.2f",
                      "How strongly raindrops ring out on the water while it rains; how many follows the rain. "
                      "None in snow or sand. Far away they roughen the water instead. 0 = off");
    changed |= Toggle("Remember what was seen", c.waterMemory,
                      "Reflections of what left the screen come from a panorama of everything seen from here, so "
                      "turning the camera does not make them pop or vanish. Off = only what is on screen");
    ImGui::TextDisabled("Needs \"Water writes depth\". Lava and slime stay as the game draws them.");
    return changed;
}

bool DrawWetSurfaces(Config& c)
{
    const Section section("Wet and shiny surfaces");
    if (!section)
        return false;
    bool changed = Slider("Sheen", c.surfaceSheen, 0.0f, 2.0f, "%.2f",
                          "Stone, cobbles, rock, ice and other smooth surfaces catch the sun, the moon and the lamps "
                          "when you look along them toward the light, most of all under a low sun; snow sparkles; "
                          "grass, dirt and sand stay matt. 0 = off");
    changed |= Slider("Wet in rain", c.wetSurfaces, 0.0f, 2.0f, "%.2f",
                      "Rain wets what stands under the open sky within about a minute, each ground as its material "
                      "takes water: stone gets a glossy film at once, soil and sand darken first, grass beads into "
                      "droplets that glint but never mirror, snow does not darken. It dries over minutes once the "
                      "rain stops, slower at night. Nothing under roofs. 0 = never wet");
    changed |= Slider("Puddles", c.puddles, 0.0f, 2.0f, "%.2f",
                      "How much of the flat open ground pools into puddles in steady rain: still mirrors that fill "
                      "over a minute or two and outlast the wet ground. Never on grass or snow. 0 = no puddles");
    changed |= Toggle("Ground materials from textures", c.terrainMaterials,
                      "Tell the ground's material from its texture (materialdata.bin): grass, soil, stone, sand, "
                      "snow, ice, wood or lava. Off = judge every surface by its colour. Needs extShadowQuality 3+");
    ImGui::TextDisabled("None in the ghost world or indoors; needs ColorSpace=1.");
    return changed;
}

bool DrawViewDistance(Config& c)
{
    bool lifted = c.farClipMax > kFarClipMaxKeepsClientCap;
    bool changed = Toggle("Lift the continent view distance", lifted,
                          "The client caps the continents at 791 yd; the engine allows 1583. Costs about four times "
                          "the loaded terrain. Turning it on when it was off at start-up needs a client restart");
    if (changed)
        c.farClipMax = lifted ? kEngineFarClipMax : kFarClipMaxKeepsClientCap;
    if (lifted)
        changed |= Slider("View distance", c.farClipMax, std::ceil(kEngineFarClipMin), kEngineFarClipMax, "%.0f yd",
                          "Continent view distance, within the client's own farclip setting",
                          ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled("Applies at the next farclip change, map load or zone change.");
    return changed;
}

bool DrawWorld(Config& c)
{
    const Section section("World");
    if (!section)
        return false;
    bool changed = Toggle("Fog under water", c.underwater, "Keep the effect while the camera is under water");
    changed |= Toggle("Water writes depth", c.liquidDepth,
                      "Let water surfaces write depth so water is fogged by its own distance");
    changed |= DrawViewDistance(c);
    return changed;
}

bool DrawDebug(Config& c)
{
    const Section section("Debug");
    if (!section)
        return false;
    bool changed = Choice("View", c.debugView, 0, kDebugViewNames, "Show one input of the fog instead of the scene");
    changed |= Toggle("Sun marker", c.sunMarker, "Red marker where the sun direction projects on screen");
    changed |= Toggle("Client shadow maps", c.clientShadowMaps,
                      "Find the client's own sun shadow maps each frame and describe them in CoAVolFog.log");
    changed |= Choice("Log level", c.logLevel, 0, kLogLevelNames, "Detail written to CoAVolFog.log");
    return changed;
}
}

bool DrawLookSettings(Config& c)
{
    bool changed = DrawQuality(c);
    changed |= DrawDensity(c);
    changed |= DrawLight(c);
    changed |= DrawColour(c);
    changed |= DrawShafts(c);
    changed |= DrawDriftAndMist(c);
    changed |= DrawWeather(c);
    changed |= DrawNight(c);
    changed |= DrawLantern(c);
    changed |= DrawRelief(c);
    changed |= DrawWater(c);
    changed |= DrawWetSurfaces(c);
    return changed;
}

void SettingsPanel::Draw(ConfigStore& store, const FogFrameStatus& status, bool& open)
{
    const float line = ImGui::GetFontSize();
    const float width = kPanelWidthInLines * line;
    ImGui::SetNextWindowPos(ImVec2(kPanelMarginInLines * line, kPanelMarginInLines * line), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    if (!ImGui::Begin("CoAVolFog", &open,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoNavInputs))
    {
        ImGui::End();
        return;
    }
    DrawStatus(status);
    if (store.PlainGame())
        ImGui::TextColored(kSkippedColour, "The plain game is shown; %s brings the mod back.",
                           HotkeyName(store.Global().toggleKey).c_str());
    if (!LookLayerEmpty(store.Layer()))
        ImGui::TextColored(kSkippedColour, "Region settings change the look here; this window edits the global ones.");
    Config edited = store.Global();
    bool changed = DrawQuality(edited);
    changed |= DrawDensity(edited);
    changed |= DrawLight(edited);
    changed |= DrawColour(edited);
    changed |= DrawShafts(edited);
    changed |= DrawDriftAndMist(edited);
    changed |= DrawWeather(edited);
    changed |= DrawOpenLand(edited);
    changed |= DrawNight(edited);
    changed |= DrawLantern(edited);
    changed |= DrawRelief(edited);
    changed |= DrawWater(edited);
    changed |= DrawWetSurfaces(edited);
    changed |= DrawWorld(edited);
    changed |= DrawDebug(edited);
    if (changed)
        store.Apply(edited);
    ImGui::Separator();
    ImGui::TextDisabled("%s saves a spot to CoAVolFog-spots.txt.", HotkeyName(store.Global().spotKey).c_str());
    ImGui::TextDisabled("%s switches the whole mod off and on, to compare with the plain game.",
                        HotkeyName(store.Global().toggleKey).c_str());
    DrawSaveRow(store);
    ImGui::End();
}

void SettingsPanel::DrawSaveRow(ConfigStore& store)
{
    const bool unsaved = store.HasUnsavedChanges();
    ImGui::BeginDisabled(!unsaved);
    if (ImGui::Button("Save"))
        m_saveFailed = !store.Save();
    ImGui::SetItemTooltip("Write the changed settings to CoAVolFog.ini");
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        store.Revert();
        m_saveFailed = false;
    }
    ImGui::SetItemTooltip("Go back to the settings in CoAVolFog.ini");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (m_saveFailed)
        ImGui::TextColored(kErrorColour, "Could not write CoAVolFog.ini");
    else
        ImGui::TextDisabled("%s", unsaved ? "Unsaved changes" : "Matches CoAVolFog.ini");
}
