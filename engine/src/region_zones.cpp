#include "region_zones.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace
{
std::vector<std::string> Fields(const std::string& line)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t at = line.find('|', start);
        parts.push_back(line.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos)
            return parts;
        start = at + 1;
    }
}

bool Int(const std::string& text, int& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (*end)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool Float(const std::string& text, float& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (*end || v != v)
        return false;
    out = v;
    return true;
}

std::string Problem(int line, const char* what)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "line %d: %s", line, what);
    return buf;
}
}

const char* ContinentName(int continent)
{
    switch (continent)
    {
    case kEasternKingdoms:
        return "Eastern Kingdoms";
    case kKalimdor:
        return "Kalimdor";
    case kOutland:
        return "Outland";
    case kNorthrend:
        return "Northrend";
    default:
        return "Elsewhere";
    }
}

bool ZoneAtlas::Parse(const std::string& text, std::vector<std::string>& problems)
{
    m_zones.clear();
    m_areas.clear();
    std::vector<std::pair<int, AtlasSpot>> spots;
    const size_t before = problems.size();
    size_t start = 0;
    int number = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        const std::vector<std::string> f = Fields(line);
        if (f[0] == "zone" && f.size() == 5)
        {
            AtlasZone zone = {};
            if (Int(f[1], zone.id) && Int(f[2], zone.map) && Int(f[3], zone.continent) && zone.id > 0 && !f[4].empty())
            {
                zone.name = f[4];
                m_zones[zone.id] = zone;
                continue;
            }
        }
        else if (f[0] == "area" && f.size() == 4)
        {
            int id = 0;
            int zone = 0;
            if (Int(f[1], id) && Int(f[2], zone) && id > 0 && !f[3].empty())
            {
                m_areas[id] = {zone, f[3]};
                continue;
            }
        }
        else if (f[0] == "spot" && f.size() == 9)
        {
            int zone = 0;
            int ground = 0;
            AtlasSpot s = {};
            if (Int(f[1], zone) && Int(f[2], s.spot.map) && Float(f[3], s.spot.x) && Float(f[4], s.spot.y) &&
                Float(f[5], s.spot.z) && Float(f[6], s.spot.o) && Int(f[7], ground) && !f[8].empty())
            {
                s.ground = ground != 0;
                s.spot.name = f[8];
                spots.push_back({zone, s});
                continue;
            }
        }
        problems.push_back(Problem(number, "not zone|id|map|continent|name, area|id|zone|name or "
                                           "spot|zone|map|x|y|z|facing|ground|name"));
    }
    for (const auto& s : spots)
    {
        const auto it = m_zones.find(s.first);
        if (it != m_zones.end())
            it->second.spots.push_back(s.second);
    }
    return problems.size() == before;
}

bool ZoneAtlas::Load(const std::string& path, std::vector<std::string>& problems)
{
    std::string text;
    if (!ReadTextFile(path, text))
    {
        m_zones.clear();
        m_areas.clear();
        return false;
    }
    return Parse(text, problems);
}

const AtlasZone* ZoneAtlas::Zone(int id) const
{
    const auto it = m_zones.find(id);
    return it == m_zones.end() ? nullptr : &it->second;
}

std::string ZoneAtlas::ZoneName(int id) const
{
    const AtlasZone* zone = Zone(id);
    if (zone)
        return zone->name;
    const auto area = m_areas.find(id);
    return area == m_areas.end() ? std::string() : area->second.second;
}

std::string ZoneAtlas::AreaName(int id) const
{
    const auto it = m_areas.find(id);
    if (it != m_areas.end())
        return it->second.second;
    const AtlasZone* zone = Zone(id);
    return zone ? zone->name : std::string();
}

int ZoneAtlas::ZoneOfArea(int area) const
{
    const auto it = m_areas.find(area);
    return it == m_areas.end() ? 0 : it->second.first;
}

std::vector<int> ZoneAtlas::Continents() const
{
    std::set<int> found;
    for (const auto& kv : m_zones)
        found.insert(kv.second.continent);
    std::vector<int> ordered;
    for (int c : {kEasternKingdoms, kKalimdor, kOutland, kNorthrend})
        if (found.erase(c))
            ordered.push_back(c);
    ordered.insert(ordered.end(), found.begin(), found.end());
    return ordered;
}

std::vector<int> ZoneAtlas::ZonesOn(int continent) const
{
    std::vector<int> ids;
    for (const auto& kv : m_zones)
        if (kv.second.continent == continent)
            ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end(), [this](int a, int b) {
        const std::string& na = m_zones.at(a).name;
        const std::string& nb = m_zones.at(b).name;
        return na == nb ? a < b : _stricmp(na.c_str(), nb.c_str()) < 0;
    });
    return ids;
}

bool ZoneHasSettings(const RegionBook& book, int zone)
{
    const auto it = book.slots.lower_bound({zone, kAnySlot - 1, kAnySlot - 1});
    return it != book.slots.end() && it->first.zone == zone;
}

ChecklistCount CountChecklist(const ZoneAtlas& atlas, int continent, const RegionBook& book)
{
    ChecklistCount count = {};
    for (int zone : atlas.ZonesOn(continent))
    {
        ++count.total;
        const auto it = book.progress.find(zone);
        const ZoneProgress progress = it == book.progress.end() ? ZoneProgress::NotStarted : it->second;
        if (progress == ZoneProgress::Done)
            ++count.done;
        else if (progress == ZoneProgress::Started)
            ++count.started;
    }
    return count;
}

std::vector<int> ZonesWithoutAtlas(const RegionBook& book, int currentZone)
{
    std::set<int> zones;
    for (const auto& kv : book.slots)
        if (kv.first.zone != kAllZones)
            zones.insert(kv.first.zone);
    for (const auto& kv : book.progress)
        zones.insert(kv.first);
    for (const auto& kv : book.spots)
        zones.insert(kv.first);
    if (currentZone > 0)
        zones.insert(currentZone);
    return std::vector<int>(zones.begin(), zones.end());
}
