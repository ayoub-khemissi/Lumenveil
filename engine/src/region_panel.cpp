#include "region_panel.h"

#include "client_world.h"
#include "config.h"
#include "regions.h"
#include "settings_panel.h"

#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
constexpr float kPanelWidthInLines = 34.0f;
constexpr float kPanelLeftInLines = 34.0f;
constexpr float kPanelTopInLines = 2.0f;
constexpr float kPanelHeightShare = 0.85f;
constexpr float kNarrowInLines = 7.0f;
constexpr float kMinTimesRealTime = 1.0f;
constexpr float kMaxTimesRealTime = 3600.0f;
constexpr int kLastMinute = kGameMinutesPerDay - 1;
constexpr int kMinutesPerHour = 60;
constexpr float kHeadlessFrameSeconds = 1.0f / 60.0f;
const ImVec4 kWarnColour = {0.95f, 0.75f, 0.35f, 1.0f};
const ImVec4 kErrorColour = {1.0f, 0.4f, 0.4f, 1.0f};
const ImVec4 kDoneColour = {0.45f, 0.85f, 0.45f, 1.0f};
const ImVec4 kStartedColour = {0.95f, 0.85f, 0.4f, 1.0f};

const char* const kTimeChoices[] = {"Follow the clock", "Any time", "Dawn", "Day", "Dusk", "Night"};
const char* const kSkyChoices[] = {"Follow the weather", "Any weather", "Clear only", "Rain", "Snow", "Sand"};
const char* const kWeatherNames[] = {"Clear", "Rain", "Snow", "Sand"};
const char* const kProgressNames[] = {"todo", "started", "done"};

struct PresetTime
{
    const char* label;
    int minute;
};

const PresetTime kPresetTimes[] = {
    {"06:00", 6 * 60}, {"12:00", 12 * 60}, {"19:45", 19 * 60 + 45}, {"23:30", 23 * 60 + 30}};

struct PanelState
{
    int minute = 12 * 60;
    float timesRealTime = 60.0f;
    int weatherType = kWeatherTypeRain;
    float weatherGrade = 0.8f;
    bool instantWeather = true;
    std::string globalOnlyKey;
};

PanelState g_panel;

std::string ClockText(int minute)
{
    char buf[16];
    minute = WrapMinute(minute);
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minute / kMinutesPerHour, minute % kMinutesPerHour);
    return buf;
}

int MinuteOf(float dayFraction)
{
    return WrapMinute(static_cast<int>(std::lround(dayFraction * kGameMinutesPerDay)));
}

std::string ZoneLabel(const RegionsRuntime& r, int zone)
{
    const std::string name = r.atlas.ZoneName(zone);
    char buf[160];
    if (name.empty())
        std::snprintf(buf, sizeof(buf), "zone %d", zone);
    else
        std::snprintf(buf, sizeof(buf), "%s (%d)", name.c_str(), zone);
    return buf;
}


void DrawHere(const RegionsRuntime& r)
{
    if (!r.live.known)
    {
        ImGui::TextDisabled("Waiting for the world to be drawn.");
        return;
    }
    if (r.live.zone > 0)
    {
        const std::string area = r.live.area > 0 && r.live.area != r.live.zone ? r.atlas.AreaName(r.live.area) : "";
        ImGui::Text("Here: %s%s%s", ZoneLabel(r, r.live.zone).c_str(), area.empty() ? "" : ", ",
                    area.c_str());
    }
    else
        ImGui::TextColored(kWarnColour, "The zone is unknown; only the all-zone settings apply.");
    const int weather = WeatherTypeOf(r.live.weather);
    ImGui::Text("Game time %s, %s %.2f, map %d", ClockText(MinuteOf(r.live.dayFraction)).c_str(),
                WeatherTypeName(weather), r.live.weatherIntensity, r.live.map);
    if (r.keysInEffect > 0)
        ImGui::Text("Region settings change %d key%s here%s", r.keysInEffect, r.keysInEffect == 1 ? "" : "s",
                    r.fade < 1.0f ? " (fading in)" : "");
    else
        ImGui::TextDisabled("Only the global settings (CoAVolFog.ini) apply here.");
}

void DrawTime(RegionsRuntime& r)
{
    if (!ImGui::CollapsingHeader("Time of day", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("time");
    const double now = RegionsNow();
    if (!r.clientControls)
        ImGui::TextColored(kWarnColour, "The client's clock functions did not match; the time cannot be set.");
    switch (r.clock.Mode())
    {
    case ClockMode::Hold:
        ImGui::Text("Held at %s (your client only)", ClockText(r.clock.TargetMinute(now)).c_str());
        break;
    case ClockMode::Run:
        ImGui::Text("Running at x%.0f real time, now %s (your client only)", r.clock.TimesRealTime(),
                    ClockText(r.clock.TargetMinute(now)).c_str());
        break;
    default:
        ImGui::Text("The server's clock (x%.0f real time)", r.clock.ServerSpeed() / kRealTimeClockSpeed);
        break;
    }
    ImGui::BeginDisabled(!r.clientControls);
    const std::string clock = ClockText(g_panel.minute);
    ImGui::SliderInt("Time", &g_panel.minute, 0, kLastMinute, clock.c_str(), ImGuiSliderFlags_AlwaysClamp);
    for (const PresetTime& p : kPresetTimes)
    {
        if (ImGui::Button(p.label))
            g_panel.minute = p.minute;
        ImGui::SameLine();
    }
    if (ImGui::Button("Now"))
        g_panel.minute = MinuteOf(r.live.dayFraction);
    ImGui::SetItemTooltip("Take the game time shown above");
    ImGui::SliderFloat("Speed", &g_panel.timesRealTime, kMinTimesRealTime, kMaxTimesRealTime, "x%.0f real time",
                       ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("x1 is real time, x20 the server's clock, x60 one game minute a second");
    if (ImGui::Button("Hold"))
        r.clock.Hold(g_panel.minute, now);
    ImGui::SetItemTooltip("Stop the sky at the chosen time");
    ImGui::SameLine();
    if (ImGui::Button("Run"))
        r.clock.Run(g_panel.minute, g_panel.timesRealTime, now);
    ImGui::SetItemTooltip("Start at the chosen time and run at the chosen speed");
    ImGui::SameLine();
    ImGui::BeginDisabled(r.clock.Mode() == ClockMode::Server);
    if (ImGui::Button("Server clock"))
        r.clock.Release();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Go back to the server's time and speed");
    ImGui::EndDisabled();
    ImGui::PopID();
}

void DrawWeather(RegionsRuntime& r)
{
    if (!ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("weather");
    if (!r.clientControls)
        ImGui::TextColored(kWarnColour, "The client's weather functions did not match; the weather cannot be set.");
    if (r.weather.Mode() == WeatherMode::Forced)
        ImGui::Text("Forced: %s %.2f (your client only)", WeatherTypeName(r.weather.ForcedType()),
                    r.weather.ForcedGrade());
    else
        ImGui::Text("The server's weather");
    ImGui::BeginDisabled(!r.clientControls);
    ImGui::Combo("Weather", &g_panel.weatherType, kWeatherNames, IM_ARRAYSIZE(kWeatherNames));
    if (g_panel.weatherType != kWeatherTypeClear)
        ImGui::SliderFloat("Intensity", &g_panel.weatherGrade, kMinForcedWeather, 1.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Change at once", &g_panel.instantWeather);
    ImGui::SetItemTooltip("Off: the weather fades in as it does when the server changes it");
    if (ImGui::Button("Force"))
        r.weather.Force(g_panel.weatherType, g_panel.weatherGrade, g_panel.instantWeather);
    ImGui::SameLine();
    ImGui::BeginDisabled(r.weather.Mode() == WeatherMode::Server);
    if (ImGui::Button("Server weather"))
        r.weather.Release(g_panel.instantWeather);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PopID();
}



void DrawSlotKeys(RegionsRuntime& r, const RegionSlot& slot)
{
    const auto it = r.store.Book().slots.find(slot);
    if (it == r.store.Book().slots.end() || it->second.empty())
    {
        ImGui::TextDisabled("Nothing set in this slot yet; move a slider below.");
        return;
    }
    int clear = -1;
    for (const auto& kv : it->second)
    {
        ImGui::PushID(kv.first);
        if (ImGui::SmallButton("x"))
            clear = kv.first;
        ImGui::SetItemTooltip("Back to what applies without this slot");
        ImGui::SameLine();
        ImGui::Text("%s = %s", LiveSettingAt(kv.first).key, LiveSettingText(kv.first, kv.second).c_str());
        ImGui::PopID();
    }
    if (clear >= 0)
        r.store.ClearValue(slot, clear);
    if (ImGui::SmallButton("Clear the whole slot"))
        r.store.ClearSlot(slot);
}

void DrawSaveRow(RegionsRuntime& r)
{
    const bool unsaved = r.store.HasUnsavedChanges();
    ImGui::BeginDisabled(!unsaved);
    if (ImGui::Button("Save"))
        r.saveFailed = !r.store.Save(RegionZoneName);
    ImGui::SetItemTooltip("Write the region settings to CoAVolFog-regions.ini (CoAVolFog.ini is not touched)");
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        r.store.Revert();
        r.saveFailed = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (r.saveFailed)
        ImGui::TextColored(kErrorColour, "Could not write CoAVolFog-regions.ini");
    else
        ImGui::TextDisabled("%s", unsaved ? "Unsaved changes" : "Matches CoAVolFog-regions.ini");
    if (!r.store.Problems().empty())
        ImGui::TextColored(kWarnColour, "%zu line(s) of CoAVolFog-regions.ini were refused; see CoAVolFog.log",
                           r.store.Problems().size());
}

void DrawRegionSettings(RegionsRuntime& r)
{
    if (!ImGui::CollapsingHeader("Settings for this region", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("slot");
    int zoneChoice = r.editZone == EditZone::Here ? 0 : 1;
    const std::string here = r.live.zone > 0 ? "This zone, " + ZoneLabel(r, r.live.zone) : "This zone (unknown)";
    if (ImGui::RadioButton(here.c_str(), zoneChoice == 0))
        zoneChoice = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("All zones", zoneChoice == 1))
        zoneChoice = 1;
    r.editZone = zoneChoice == 0 ? EditZone::Here : EditZone::All;
    ImGui::Combo("Time", &r.editTime, kTimeChoices, IM_ARRAYSIZE(kTimeChoices));
    ImGui::SetItemTooltip("Follow the clock edits the dawn, day, dusk or night slot that applies most right now");
    ImGui::Combo("Weather", &r.editSky, kSkyChoices, IM_ARRAYSIZE(kSkyChoices));
    ImGui::SetItemTooltip("Follow the weather edits the settings for any weather while it is clear, and the rain, "
                          "snow or sand settings while that falls");
    const RegionSlot slot = EditedSlot(r);
    if (r.editZone == EditZone::Here && r.live.zone <= 0)
        ImGui::TextColored(kWarnColour, "The zone is unknown here; edits go to all zones.");
    if (!EditableSlot(slot))
    {
        ImGui::TextColored(kWarnColour, "For all zones at any time in any weather, use the CoAVolFog window "
                                        "(Ctrl+F7); pick a time or a weather here.");
        DrawSaveRow(r);
        ImGui::PopID();
        return;
    }
    ImGui::Text("Editing [%s]%s", RegionSlotName(slot).c_str(), r.store.SlotUnsaved(slot) ? ", unsaved" : "");
    ImGui::Checkbox("Show this slot now", &r.preview);
    ImGui::SetItemTooltip("Draw the edited slot's settings at full strength whatever the hour or weather");
    DrawSlotKeys(r, slot);
    DrawSaveRow(r);
    ImGui::Separator();
    const Config shown = SlotLook(r.store.Book(), GlobalConfig().Global(), slot, r.live.dayFraction, r.live.weather);
    Config edited = shown;
    if (DrawLookSettings(edited))
        StoreLookEdits(r.store, slot, shown, edited, g_panel.globalOnlyKey);
    if (!g_panel.globalOnlyKey.empty())
        ImGui::TextColored(kWarnColour, "%s is the same in every region; change it in the CoAVolFog window (Ctrl+F7).",
                           g_panel.globalOnlyKey.c_str());
    ImGui::PopID();
}

int DrawFrames(int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        ImGui::NewFrame();
        bool open = true;
        DrawRegionPanel(open);
        ImGui::Render();
    }
    return frames;
}

int DrawFramesGuarded(int frames)
{
    __try
    {
        return DrawFrames(frames);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

void DrawProgressCombo(RegionsRuntime& r, int zone)
{
    int progress = static_cast<int>(r.store.Progress(zone));
    ImGui::SetNextItemWidth(kNarrowInLines * ImGui::GetFontSize());
    if (ImGui::Combo("##progress", &progress, kProgressNames, IM_ARRAYSIZE(kProgressNames)))
    {
        r.store.SetProgress(zone, static_cast<ZoneProgress>(progress));
        r.saveFailed = !r.store.SaveChecklist(RegionZoneName);
    }
}


void DrawZoneRow(RegionsRuntime& r, int zone)
{
    ImGui::PushID(zone);
    DrawProgressCombo(r, zone);
    ImGui::SameLine();
    const ZoneProgress progress = r.store.Progress(zone);
    const std::string label = ZoneLabel(r, zone) + (ZoneHasSettings(r.store.Book(), zone) ? " *" : "") +
                              (zone == r.live.zone ? "  (here)" : "");
    if (progress == ZoneProgress::Done)
        ImGui::TextColored(kDoneColour, "%s", label.c_str());
    else if (progress == ZoneProgress::Started)
        ImGui::TextColored(kStartedColour, "%s", label.c_str());
    else
        ImGui::TextUnformatted(label.c_str());
    ImGui::PopID();
}

void DrawChecklist(RegionsRuntime& r)
{
    if (!ImGui::CollapsingHeader("Checklist", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("checklist");
    ImGui::TextDisabled("* = has region settings. Status changes are saved at once.");
    if (r.atlas.Empty())
    {
        ImGui::TextColored(kWarnColour, "No CoAVolFog-zones.txt next to the DLL; listing the zones you have set.");
        for (int zone : ZonesWithoutAtlas(r.store.Book(), r.live.zone))
            DrawZoneRow(r, zone);
        ImGui::PopID();
        return;
    }
    for (int continent : r.atlas.Continents())
    {
        const ChecklistCount count = CountChecklist(r.atlas, continent, r.store.Book());
        char title[128];
        std::snprintf(title, sizeof(title), "%s: %d of %d done, %d started###%d", ContinentName(continent),
                      count.done, count.total, count.started, continent);
        const std::vector<int> zones = r.atlas.ZonesOn(continent);
        const bool here = std::find(zones.begin(), zones.end(), r.live.zone) != zones.end();
        if (!ImGui::TreeNodeEx(title, here ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
            continue;
        for (int zone : zones)
            DrawZoneRow(r, zone);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
}

void DrawRegionPanel(bool& open)
{
    RegionsRuntime& r = Regions();
    r.panelSeenAt = RegionsNow();
    const float line = ImGui::GetFontSize();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(kPanelLeftInLines * line, kPanelTopInLines * line), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidthInLines * line, kPanelHeightShare * display.y),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CoAVolFog regions", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavInputs))
    {
        ImGui::End();
        return;
    }
    DrawHere(r);
    DrawTime(r);
    DrawWeather(r);
    DrawRegionSettings(r);
    DrawChecklist(r);
    ImGui::Separator();
    ImGui::TextDisabled("%s opens and closes this window.", HotkeyName(GlobalConfig().Global().regionKey).c_str());
    ImGui::End();
}

int DrawRegionPanelWithoutDevice(int frames, float width, float height)
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = kHeadlessFrameSeconds;
    const int drawn = DrawFramesGuarded(frames);
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
    return drawn;
}
