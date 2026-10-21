#include "region_settings.h"

#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace
{
constexpr float kHoursPerDay = 24.0f;
constexpr float kNightEnds = 4.5f;
constexpr float kDawnFull = 5.5f;
constexpr float kDawnEnds = 8.0f;
constexpr float kDayFull = 9.5f;
constexpr float kDayEnds = 17.5f;
constexpr float kDuskFull = 19.0f;
constexpr float kDuskEnds = 21.0f;
constexpr float kNightFull = 22.0f;
constexpr float kWeatherShareGain = 2.0f;
constexpr const char* kLineEnd = "\r\n";
constexpr const char* kProgressSection = "Progress";
constexpr const char* kSpotsSection = "Spots";
constexpr const char* kZoneWord = "Zone";
constexpr const char* kAllWord = "All";
constexpr const char* kAnyWord = "Any";
constexpr int kSlotChoices = 5;
constexpr int kOrderLength = 7;

const char* const kDayNames[kDaySlotCount] = {"Dawn", "Day", "Dusk", "Night"};
const char* const kSkyNames[kSkySlotCount] = {"Clear", "Rain", "Snow", "Sand"};

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool Same(const std::string& a, const char* b)
{
    return _stricmp(a.c_str(), b) == 0;
}

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t at = text.find(separator, start);
        parts.push_back(Trim(text.substr(start, at == std::string::npos ? std::string::npos : at - start)));
        if (at == std::string::npos)
            return parts;
        start = at + 1;
    }
}

std::vector<std::string> Words(const std::string& text)
{
    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t')
            ++j;
        if (j > i)
            words.push_back(text.substr(i, j - i));
        i = j;
    }
    return words;
}

bool ParseInt(const std::string& text, int& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool ParseFloat(const std::string& text, float& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || v != v)
        return false;
    out = v;
    return true;
}

int DayIndex(const std::string& word)
{
    for (int i = 0; i < kDaySlotCount; ++i)
        if (Same(word, kDayNames[i]))
            return i;
    return -2;
}

int SkyIndex(const std::string& word)
{
    for (int i = 0; i < kSkySlotCount; ++i)
        if (Same(word, kSkyNames[i]))
            return i;
    return -2;
}

enum class SectionKind
{
    None,
    Progress,
    Spots,
    Slot,
    Refused,
};

SectionKind ParseSection(const std::string& name, RegionSlot& slot, std::string& why)
{
    const std::vector<std::string> words = Words(name);
    if (words.empty())
    {
        why = "an empty section name";
        return SectionKind::Refused;
    }
    if (words.size() == 1 && Same(words[0], kProgressSection))
        return SectionKind::Progress;
    if (words.size() == 1 && Same(words[0], kSpotsSection))
        return SectionKind::Spots;
    slot = {kAllZones, kAnySlot, kAnySlot};
    size_t next = 1;
    if (Same(words[0], kZoneWord))
    {
        if (words.size() < 2 || !ParseInt(words[1], slot.zone) || slot.zone <= 0 || slot.zone > kMaxZoneId)
        {
            why = "\"Zone\" needs a zone id from 1 to 65535";
            return SectionKind::Refused;
        }
        next = 2;
    }
    else if (!Same(words[0], kAllWord))
    {
        why = "a section must be [Zone <id> ...], [All <time or weather>], [Progress] or [Spots]";
        return SectionKind::Refused;
    }
    bool dayGiven = false;
    bool skyGiven = false;
    for (size_t i = next; i < words.size(); ++i)
    {
        const int day = DayIndex(words[i]);
        const int sky = SkyIndex(words[i]);
        if (day >= 0 && !dayGiven)
        {
            slot.day = day;
            dayGiven = true;
        }
        else if (sky >= 0 && !skyGiven)
        {
            slot.sky = sky;
            skyGiven = true;
        }
        else
        {
            why = "\"" + words[i] + "\" is not Dawn, Day, Dusk, Night, Clear, Rain, Snow or Sand (each once)";
            return SectionKind::Refused;
        }
    }
    if (slot.zone == kAllZones && !dayGiven && !skyGiven)
    {
        why = "[All] alone would copy CoAVolFog.ini; give a time or a weather";
        return SectionKind::Refused;
    }
    return SectionKind::Slot;
}

std::string Format(const char* format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

std::string Line(int number, const std::string& text)
{
    return Format("line %d: ", number) + text;
}

bool ParseProgress(const std::string& text, ZoneProgress& out)
{
    if (Same(text, "done"))
        out = ZoneProgress::Done;
    else if (Same(text, "started"))
        out = ZoneProgress::Started;
    else if (Same(text, "todo") || Same(text, "not started"))
        out = ZoneProgress::NotStarted;
    else
        return false;
    return true;
}

bool ParseSpot(const std::string& text, ZoneSpot& out)
{
    const std::vector<std::string> parts = Split(text, '|');
    if (parts.size() != 6 || parts[0].empty())
        return false;
    out.name = parts[0];
    return ParseInt(parts[1], out.map) && ParseFloat(parts[2], out.x) && ParseFloat(parts[3], out.y) &&
           ParseFloat(parts[4], out.z) && ParseFloat(parts[5], out.o);
}

float SmoothStep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void Ramp(float hour, float from, float to, int a, int b, float (&out)[kDaySlotCount])
{
    const float t = SmoothStep((hour - from) / (to - from));
    out[a] = 1.0f - t;
    out[b] = t;
}

const RegionValues* FindSlot(const RegionBook& book, int zone, int day, int sky)
{
    const auto it = book.slots.find({zone, day, sky});
    return it == book.slots.end() ? nullptr : &it->second;
}

struct SlotTable
{
    const RegionValues* zone[kSlotChoices][kSlotChoices];
    const RegionValues* all[kSlotChoices][kSlotChoices];
};

void Order(const SlotTable& table, int day, int sky, const RegionValues* (&out)[kOrderLength])
{
    const int d = day + 1;
    const int s = sky + 1;
    out[0] = table.zone[d][s];
    out[1] = table.zone[0][s];
    out[2] = table.zone[d][0];
    out[3] = table.zone[0][0];
    out[4] = table.all[d][s];
    out[5] = table.all[0][s];
    out[6] = table.all[d][0];
}

bool Lookup(const RegionValues* (&order)[kOrderLength], int setting, float& value)
{
    for (const RegionValues* values : order)
    {
        if (!values)
            continue;
        const auto it = values->find(setting);
        if (it != values->end())
        {
            value = it->second;
            return true;
        }
    }
    return false;
}

SlotTable BuildSlotTable(const RegionBook& book, int zone, std::vector<int>& settings)
{
    SlotTable table = {};
    std::set<int> seen;
    for (int d = 0; d < kSlotChoices; ++d)
        for (int s = 0; s < kSlotChoices; ++s)
        {
            table.zone[d][s] = zone > 0 ? FindSlot(book, zone, d - 1, s - 1) : nullptr;
            table.all[d][s] = FindSlot(book, kAllZones, d - 1, s - 1);
            for (const RegionValues* values : {table.zone[d][s], table.all[d][s]})
                if (values)
                    for (const auto& kv : *values)
                        if (kv.first >= 0 && kv.first < LiveSettingCount() && LiveSettingAt(kv.first).regional)
                            seen.insert(kv.first);
        }
    settings.assign(seen.begin(), seen.end());
    return table;
}

void WriteValues(std::string& text, const RegionValues& values)
{
    for (const auto& kv : values)
    {
        if (kv.first < 0 || kv.first >= LiveSettingCount())
            continue;
        text += LiveSettingAt(kv.first).key;
        text += "=";
        text += LiveSettingText(kv.first, kv.second);
        text += kLineEnd;
    }
}

std::string SpotText(const ZoneSpot& spot)
{
    std::string name = spot.name;
    std::replace(name.begin(), name.end(), '|', '/');
    return Format("%s|%d|%.2f|%.2f|%.2f|%.3f", name.c_str(), spot.map, spot.x, spot.y, spot.z, spot.o);
}

void ApplyUnsavedKeys(const RegionBook& from, RegionBook& onto, const std::set<std::pair<RegionSlot, int>>& keys,
                      const std::set<int>& progress, const std::set<int>& spots)
{
    for (const auto& key : keys)
    {
        const auto source = from.slots.find(key.first);
        float value = 0.0f;
        bool present = false;
        if (source != from.slots.end())
        {
            const auto v = source->second.find(key.second);
            if (v != source->second.end())
            {
                value = v->second;
                present = true;
            }
        }
        if (present)
        {
            onto.slots[key.first][key.second] = value;
            continue;
        }
        const auto target = onto.slots.find(key.first);
        if (target == onto.slots.end())
            continue;
        target->second.erase(key.second);
        if (target->second.empty())
            onto.slots.erase(target);
    }
    for (int zone : progress)
    {
        const auto it = from.progress.find(zone);
        if (it == from.progress.end() || it->second == ZoneProgress::NotStarted)
            onto.progress.erase(zone);
        else
            onto.progress[zone] = it->second;
    }
    for (int zone : spots)
    {
        const auto it = from.spots.find(zone);
        if (it == from.spots.end() || it->second.empty())
            onto.spots.erase(zone);
        else
            onto.spots[zone] = it->second;
    }
}

bool ValidSlot(const RegionSlot& slot)
{
    if (slot.zone < 0 || slot.zone > kMaxZoneId)
        return false;
    if (slot.day < kAnySlot || slot.day >= kDaySlotCount || slot.sky < kAnySlot || slot.sky >= kSkySlotCount)
        return false;
    return !(slot.zone == kAllZones && slot.day == kAnySlot && slot.sky == kAnySlot);
}
}

const char* DaySlotName(int day)
{
    return day >= 0 && day < kDaySlotCount ? kDayNames[day] : kAnyWord;
}

const char* SkySlotName(int sky)
{
    return sky >= 0 && sky < kSkySlotCount ? kSkyNames[sky] : kAnyWord;
}

int SkySlotOf(WeatherKind weather)
{
    switch (weather)
    {
    case WeatherKind::Rain:
        return kSkyRain;
    case WeatherKind::Snow:
        return kSkySnow;
    case WeatherKind::Sand:
        return kSkySand;
    default:
        return kSkyClear;
    }
}

bool operator<(const RegionSlot& a, const RegionSlot& b)
{
    if (a.zone != b.zone)
        return a.zone < b.zone;
    if (a.day != b.day)
        return a.day < b.day;
    return a.sky < b.sky;
}

bool operator==(const RegionSlot& a, const RegionSlot& b)
{
    return a.zone == b.zone && a.day == b.day && a.sky == b.sky;
}

std::string RegionSlotName(const RegionSlot& slot)
{
    std::string name = slot.zone == kAllZones ? std::string(kAllWord) : Format("%s %d", kZoneWord, slot.zone);
    if (slot.day != kAnySlot)
        name += std::string(" ") + DaySlotName(slot.day);
    if (slot.sky != kAnySlot)
        name += std::string(" ") + SkySlotName(slot.sky);
    return name;
}

const char* ZoneProgressName(ZoneProgress progress)
{
    switch (progress)
    {
    case ZoneProgress::Started:
        return "started";
    case ZoneProgress::Done:
        return "done";
    default:
        return "todo";
    }
}

bool ParseRegionBook(const std::string& text, RegionBook& out, std::vector<std::string>& problems)
{
    out = RegionBook();
    SectionKind section = SectionKind::None;
    RegionSlot slot = {kAllZones, kAnySlot, kAnySlot};
    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    int number = 0;
    const size_t problemsBefore = problems.size();
    while (start <= text.size())
    {
        const size_t end = text.find('\n', start);
        const std::string line = Trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        ++number;
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[')
        {
            const size_t close = line.find(']');
            std::string why;
            if (close == std::string::npos)
            {
                section = SectionKind::Refused;
                problems.push_back(Line(number, "a section without \"]\"; its lines are ignored"));
                continue;
            }
            section = ParseSection(line.substr(1, close - 1), slot, why);
            if (section == SectionKind::Refused)
                problems.push_back(Line(number, why + "; its lines are ignored"));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            problems.push_back(Line(number, "\"" + line + "\" is not key=value"));
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        switch (section)
        {
        case SectionKind::None:
            problems.push_back(Line(number, "\"" + key + "\" comes before any section"));
            break;
        case SectionKind::Refused:
            break;
        case SectionKind::Progress: {
            int zone = 0;
            ZoneProgress progress = ZoneProgress::NotStarted;
            if (!ParseInt(key, zone) || zone <= 0 || zone > kMaxZoneId || !ParseProgress(value, progress))
                problems.push_back(Line(number, "progress lines are <zone id>=todo|started|done"));
            else if (progress == ZoneProgress::NotStarted)
                out.progress.erase(zone);
            else
                out.progress[zone] = progress;
            break;
        }
        case SectionKind::Spots: {
            int zone = 0;
            ZoneSpot spot = {};
            if (!ParseInt(key, zone) || zone <= 0 || zone > kMaxZoneId || !ParseSpot(value, spot))
                problems.push_back(Line(number, "spot lines are <zone id>=name|map|x|y|z|facing"));
            else
                out.spots[zone].push_back(spot);
            break;
        }
        case SectionKind::Slot: {
            const int setting = FindLiveSetting(key);
            float v = 0.0f;
            if (setting < 0)
                problems.push_back(Line(number, "\"" + key + "\" is not a setting"));
            else if (!LiveSettingAt(setting).regional)
                problems.push_back(Line(number, std::string(LiveSettingAt(setting).key) +
                                                    " is set for every region in CoAVolFog.ini only"));
            else if (!ParseFloat(value, v))
                problems.push_back(Line(number, "\"" + value + "\" is not a number"));
            else
                out.slots[slot][setting] = LiveSettingStored(setting, v);
            break;
        }
        }
    }
    return problems.size() == problemsBefore;
}

std::string RegionBookText(const RegionBook& book, const std::function<std::string(int)>& zoneName)
{
    std::string text;
    text += "; CoAVolFog region settings, written by the region panel (Ctrl+F3 by default). Hand edits are read";
    text += kLineEnd;
    text += "; within a second. [Zone <id> <Dawn|Day|Dusk|Night> <Clear|Rain|Snow|Sand>] sets keys of CoAVolFog.ini";
    text += kLineEnd;
    text += "; for one zone; time and weather are optional, [All <time and/or weather>] sets them everywhere.";
    text += kLineEnd;
    if (!book.progress.empty())
    {
        text += kLineEnd;
        text += std::string("[") + kProgressSection + "]" + kLineEnd;
        for (const auto& kv : book.progress)
        {
            if (kv.second == ZoneProgress::NotStarted)
                continue;
            text += Format("%d=%s", kv.first, ZoneProgressName(kv.second));
            text += kLineEnd;
        }
    }
    if (!book.spots.empty())
    {
        text += kLineEnd;
        text += std::string("[") + kSpotsSection + "]" + kLineEnd;
        for (const auto& kv : book.spots)
            for (const ZoneSpot& spot : kv.second)
            {
                text += Format("%d=", kv.first) + SpotText(spot);
                text += kLineEnd;
            }
    }
    int lastZone = -1;
    for (const auto& kv : book.slots)
    {
        if (kv.second.empty())
            continue;
        text += kLineEnd;
        if (kv.first.zone != lastZone && kv.first.zone != kAllZones && zoneName)
        {
            const std::string name = zoneName(kv.first.zone);
            if (!name.empty())
            {
                text += "; " + name;
                text += kLineEnd;
            }
        }
        lastZone = kv.first.zone;
        text += "[" + RegionSlotName(kv.first) + "]";
        text += kLineEnd;
        WriteValues(text, kv.second);
    }
    return text;
}

bool SameRegionValues(const RegionValues& a, const RegionValues& b)
{
    return a == b;
}

void DayWeights(float dayFraction, float (&out)[kDaySlotCount])
{
    for (float& w : out)
        w = 0.0f;
    float hour = dayFraction * kHoursPerDay;
    if (!(hour == hour))
        hour = 12.0f;
    hour = std::fmod(hour, kHoursPerDay);
    if (hour < 0.0f)
        hour += kHoursPerDay;
    if (hour >= kNightFull || hour < kNightEnds)
        out[kNight] = 1.0f;
    else if (hour < kDawnFull)
        Ramp(hour, kNightEnds, kDawnFull, kNight, kDawn, out);
    else if (hour < kDawnEnds)
        out[kDawn] = 1.0f;
    else if (hour < kDayFull)
        Ramp(hour, kDawnEnds, kDayFull, kDawn, kDay, out);
    else if (hour < kDayEnds)
        out[kDay] = 1.0f;
    else if (hour < kDuskFull)
        Ramp(hour, kDayEnds, kDuskFull, kDay, kDusk, out);
    else if (hour < kDuskEnds)
        out[kDusk] = 1.0f;
    else
        Ramp(hour, kDuskEnds, kNightFull, kDusk, kNight, out);
}

void SkyWeights(WeatherKind weather, float intensity, float (&out)[kSkySlotCount])
{
    for (float& w : out)
        w = 0.0f;
    const int sky = SkySlotOf(weather);
    const float share = sky == kSkyClear || !(intensity > 0.0f) ? 0.0f : std::min(1.0f, kWeatherShareGain * intensity);
    out[kSkyClear] = 1.0f - share;
    if (sky != kSkyClear)
        out[sky] += share;
}

SlotWeights SlotWeightsAt(float dayFraction, WeatherKind weather, float intensity)
{
    SlotWeights w = {};
    DayWeights(dayFraction, w.day);
    SkyWeights(weather, intensity, w.sky);
    return w;
}

int DominantDaySlot(float dayFraction)
{
    float w[kDaySlotCount];
    DayWeights(dayFraction, w);
    return static_cast<int>(std::max_element(w, w + kDaySlotCount) - w);
}

bool ResolveRegionValue(const RegionBook& book, int zone, int day, int sky, int setting, float& value)
{
    if (setting < 0 || setting >= LiveSettingCount() || !LiveSettingAt(setting).regional)
        return false;
    std::vector<int> settings;
    const SlotTable table = BuildSlotTable(book, zone, settings);
    const RegionValues* order[kOrderLength];
    Order(table, day, sky, order);
    return Lookup(order, setting, value);
}

LookLayer RegionLayer(const RegionBook& book, int zone, const SlotWeights& weights)
{
    LookLayer layer = EmptyLookLayer();
    std::vector<int> settings;
    const SlotTable table = BuildSlotTable(book, zone, settings);
    if (settings.empty())
        return layer;
    for (int d = 0; d < kDaySlotCount; ++d)
        for (int s = 0; s < kSkySlotCount; ++s)
        {
            const float w = weights.day[d] * weights.sky[s];
            if (!(w > 0.0f))
                continue;
            const RegionValues* order[kOrderLength];
            Order(table, d, s, order);
            for (int setting : settings)
            {
                float value = 0.0f;
                if (!Lookup(order, setting, value))
                    continue;
                layer.weight[static_cast<size_t>(setting)] += w;
                layer.sum[static_cast<size_t>(setting)] += w * value;
            }
        }
    return layer;
}

LookLayer BlendLookLayers(const LookLayer& from, const LookLayer& to, float t)
{
    LookLayer out = EmptyLookLayer();
    t = std::clamp(t, 0.0f, 1.0f);
    for (size_t i = 0; i < out.weight.size(); ++i)
    {
        const float fw = i < from.weight.size() ? from.weight[i] : 0.0f;
        const float fs = i < from.sum.size() ? from.sum[i] : 0.0f;
        const float tw = i < to.weight.size() ? to.weight[i] : 0.0f;
        const float ts = i < to.sum.size() ? to.sum[i] : 0.0f;
        out.weight[i] = fw + (tw - fw) * t;
        out.sum[i] = fs + (ts - fs) * t;
    }
    return out;
}

void ForceLookValues(LookLayer& layer, const RegionValues& values)
{
    for (const auto& kv : values)
    {
        if (kv.first < 0 || static_cast<size_t>(kv.first) >= layer.weight.size() ||
            !LiveSettingAt(kv.first).regional)
            continue;
        layer.weight[static_cast<size_t>(kv.first)] = 1.0f;
        layer.sum[static_cast<size_t>(kv.first)] = kv.second;
    }
}

LookLayer RegionBlender::Update(int zone, const LookLayer& target, double seconds)
{
    m_now = seconds;
    if (!m_started)
    {
        m_started = true;
        m_zone = zone;
        m_from = target;
        m_since = seconds - kRegionFadeSeconds;
        m_last = target;
        return target;
    }
    if (zone != m_zone)
    {
        m_from = m_last;
        m_zone = zone;
        m_since = seconds;
    }
    const float a = Progress();
    m_last = a >= 1.0f ? target : BlendLookLayers(m_from, target, a);
    return m_last;
}

float RegionBlender::Progress() const
{
    if (!m_started)
        return 1.0f;
    const double t = std::max(0.0, m_now - m_since) / kRegionFadeSeconds;
    return SmoothStep(static_cast<float>(std::min(1.0, t)));
}

void RegionBlender::Reset()
{
    m_started = false;
}

RegionSlot ChooseSlot(bool thisZone, int zone, int timeChoice, int skyChoice, float dayFraction, WeatherKind weather)
{
    RegionSlot slot = {thisZone && zone > 0 ? zone : kAllZones, kAnySlot, kAnySlot};
    if (timeChoice == kFollowChoice)
        slot.day = DominantDaySlot(dayFraction);
    else if (timeChoice >= kFirstSlotChoice && timeChoice < kFirstSlotChoice + kDaySlotCount)
        slot.day = timeChoice - kFirstSlotChoice;
    if (skyChoice == kFollowChoice)
        slot.sky = weather == WeatherKind::Clear ? kAnySlot : SkySlotOf(weather);
    else if (skyChoice >= kFirstSlotChoice && skyChoice < kFirstSlotChoice + kSkySlotCount)
        slot.sky = skyChoice - kFirstSlotChoice;
    return slot;
}

bool EditableSlot(const RegionSlot& slot)
{
    return ValidSlot(slot);
}

Config SlotLook(const RegionBook& book, const Config& global, const RegionSlot& slot, float dayFraction,
                WeatherKind weather)
{
    SlotWeights w = {};
    w.day[slot.day == kAnySlot ? DominantDaySlot(dayFraction) : slot.day] = 1.0f;
    w.sky[slot.sky == kAnySlot ? SkySlotOf(weather) : slot.sky] = 1.0f;
    LookLayer cell = RegionLayer(book, slot.zone, w);
    const auto it = book.slots.find(slot);
    if (it != book.slots.end())
        ForceLookValues(cell, it->second);
    return ComposeLook(global, cell);
}

int StoreLookEdits(RegionStore& store, const RegionSlot& slot, const Config& shown, const Config& edited,
                   std::string& globalOnlyKey)
{
    int stored = 0;
    for (int i = 0; i < LiveSettingCount(); ++i)
    {
        const float after = LiveSettingValue(edited, i);
        if (LiveSettingValue(shown, i) == after)
            continue;
        if (!LiveSettingAt(i).regional)
            globalOnlyKey = LiveSettingAt(i).key;
        else if (store.SetValue(slot, i, after))
            ++stored;
    }
    return stored;
}

bool ReadTextFile(const std::string& path, std::string& out)
{
    out.clear();
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    char buf[4096];
    DWORD got = 0;
    bool ok = true;
    for (;;)
    {
        if (!ReadFile(file, buf, sizeof(buf), &got, nullptr))
        {
            ok = false;
            break;
        }
        if (got == 0)
            break;
        out.append(buf, got);
    }
    CloseHandle(file);
    return ok;
}

bool WriteTextFileAtomically(const std::string& path, const std::string& text)
{
    const std::string temp = path + ".tmp";
    HANDLE file = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool wrote = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                       written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(temp.c_str());
        return false;
    }
    return true;
}

unsigned long long TextFileStamp(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    const unsigned long long high = data.ftLastWriteTime.dwHighDateTime;
    const unsigned long long time = (high << 32) | data.ftLastWriteTime.dwLowDateTime;
    return time ^ (static_cast<unsigned long long>(data.nFileSizeLow) << 1) ^ 1ull;
}

void RegionStore::Load(const std::string& path)
{
    m_path = path;
    ReadDisk();
    m_working = m_disk;
    m_dirtyKeys.clear();
    m_dirtyProgress.clear();
    m_dirtySpots.clear();
}

void RegionStore::ReadDisk()
{
    m_stamp = TextFileStamp(m_path);
    m_problems.clear();
    std::string text;
    if (ReadTextFile(m_path, text))
        ParseRegionBook(text, m_disk, m_problems);
    else
        m_disk = RegionBook();
    for (const std::string& problem : m_problems)
        VF_LOG_ERROR("regions: %s: %s", m_path.c_str(), problem.c_str());
    ++m_generation;
}

bool RegionStore::ReloadIfChanged()
{
    if (m_path.empty() || TextFileStamp(m_path) == m_stamp)
        return false;
    const RegionBook edited = m_working;
    ReadDisk();
    m_working = m_disk;
    ApplyUnsavedKeys(edited, m_working, m_dirtyKeys, m_dirtyProgress, m_dirtySpots);
    VF_LOG_INFO("regions: read %s (%zu slots, %zu zones with progress)", m_path.c_str(), m_disk.slots.size(),
                m_disk.progress.size());
    return true;
}

bool RegionStore::SetValue(const RegionSlot& slot, int setting, float value)
{
    if (!ValidSlot(slot) || setting < 0 || setting >= LiveSettingCount() || !LiveSettingAt(setting).regional)
        return false;
    m_working.slots[slot][setting] = LiveSettingStored(setting, value);
    m_dirtyKeys.insert({slot, setting});
    return true;
}

bool RegionStore::ClearValue(const RegionSlot& slot, int setting)
{
    const auto it = m_working.slots.find(slot);
    if (it == m_working.slots.end() || it->second.erase(setting) == 0)
        return false;
    if (it->second.empty())
        m_working.slots.erase(it);
    m_dirtyKeys.insert({slot, setting});
    return true;
}

bool RegionStore::ClearSlot(const RegionSlot& slot)
{
    const auto it = m_working.slots.find(slot);
    if (it == m_working.slots.end())
        return false;
    for (const auto& kv : it->second)
        m_dirtyKeys.insert({slot, kv.first});
    m_working.slots.erase(it);
    return true;
}

void RegionStore::SetProgress(int zone, ZoneProgress progress)
{
    if (zone <= 0 || zone > kMaxZoneId)
        return;
    if (progress == ZoneProgress::NotStarted)
        m_working.progress.erase(zone);
    else
        m_working.progress[zone] = progress;
    m_dirtyProgress.insert(zone);
}

ZoneProgress RegionStore::Progress(int zone) const
{
    const auto it = m_working.progress.find(zone);
    return it == m_working.progress.end() ? ZoneProgress::NotStarted : it->second;
}

void RegionStore::AddSpot(int zone, const ZoneSpot& spot)
{
    if (zone <= 0 || zone > kMaxZoneId)
        return;
    m_working.spots[zone].push_back(spot);
    m_dirtySpots.insert(zone);
}

bool RegionStore::RemoveSpot(int zone, size_t index)
{
    const auto it = m_working.spots.find(zone);
    if (it == m_working.spots.end() || index >= it->second.size())
        return false;
    it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
    if (it->second.empty())
        m_working.spots.erase(it);
    m_dirtySpots.insert(zone);
    return true;
}

bool RegionStore::HasUnsavedChanges() const
{
    return !m_dirtyKeys.empty() || !m_dirtyProgress.empty() || !m_dirtySpots.empty();
}

bool RegionStore::SlotUnsaved(const RegionSlot& slot) const
{
    const auto it = m_dirtyKeys.lower_bound({slot, -1});
    return it != m_dirtyKeys.end() && it->first == slot;
}

bool RegionStore::Save(const std::function<std::string(int)>& zoneName)
{
    return Write(zoneName, true);
}

bool RegionStore::SaveChecklist(const std::function<std::string(int)>& zoneName)
{
    return Write(zoneName, false);
}

bool RegionStore::ChecklistUnsaved() const
{
    return !m_dirtyProgress.empty() || !m_dirtySpots.empty();
}

bool RegionStore::Write(const std::function<std::string(int)>& zoneName, bool look)
{
    const std::set<std::pair<RegionSlot, int>> noKeys;
    const std::set<std::pair<RegionSlot, int>>& keys = look ? m_dirtyKeys : noKeys;
    RegionBook fresh;
    std::vector<std::string> problems;
    std::string text;
    if (ReadTextFile(m_path, text))
        ParseRegionBook(text, fresh, problems);
    ApplyUnsavedKeys(m_working, fresh, keys, m_dirtyProgress, m_dirtySpots);
    for (const auto& key : keys)
    {
        const int zone = key.first.zone;
        if (zone == kAllZones || m_dirtyProgress.count(zone) || fresh.slots.count(key.first) == 0)
            continue;
        if (fresh.progress.find(zone) == fresh.progress.end())
            fresh.progress[zone] = ZoneProgress::Started;
    }
    std::string out = RegionBookText(fresh, zoneName);
    for (const std::string& problem : problems)
        out += "; refused when the panel last saved, " + problem + kLineEnd;
    const RegionBook edited = m_working;
    if (!WriteTextFileAtomically(m_path, out))
    {
        VF_LOG_ERROR("regions: could not write %s (error %lu)", m_path.c_str(), GetLastError());
        return false;
    }
    m_disk = fresh;
    m_working = fresh;
    if (!look)
        ApplyUnsavedKeys(edited, m_working, m_dirtyKeys, {}, {});
    m_problems = problems;
    if (look)
        m_dirtyKeys.clear();
    m_dirtyProgress.clear();
    m_dirtySpots.clear();
    m_stamp = TextFileStamp(m_path);
    ++m_generation;
    VF_LOG_INFO("regions: saved %s", m_path.c_str());
    return true;
}

void RegionStore::Revert()
{
    m_working = m_disk;
    m_dirtyKeys.clear();
    m_dirtyProgress.clear();
    m_dirtySpots.clear();
}
