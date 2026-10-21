#pragma once

#include "region_settings.h"

#include <map>
#include <string>
#include <vector>

constexpr int kEasternKingdoms = 0;
constexpr int kKalimdor = 1;
constexpr int kOutland = 530;
constexpr int kNorthrend = 571;

const char* ContinentName(int continent);

struct AtlasSpot
{
    ZoneSpot spot;
    bool ground;
};

struct AtlasZone
{
    int id;
    int map;
    int continent;
    std::string name;
    std::vector<AtlasSpot> spots;
};

struct ChecklistCount
{
    int total;
    int started;
    int done;
};

class ZoneAtlas
{
public:
    bool Parse(const std::string& text, std::vector<std::string>& problems);
    bool Load(const std::string& path, std::vector<std::string>& problems);
    bool Empty() const { return m_zones.empty(); }
    size_t ZoneCount() const { return m_zones.size(); }
    size_t AreaCount() const { return m_areas.size(); }
    const AtlasZone* Zone(int id) const;
    std::string ZoneName(int id) const;
    std::string AreaName(int id) const;
    int ZoneOfArea(int area) const;
    std::vector<int> Continents() const;
    std::vector<int> ZonesOn(int continent) const;

private:
    std::map<int, AtlasZone> m_zones;
    std::map<int, std::pair<int, std::string>> m_areas;
};

ChecklistCount CountChecklist(const ZoneAtlas& atlas, int continent, const RegionBook& book);
std::vector<int> ZonesWithoutAtlas(const RegionBook& book, int currentZone);
bool ZoneHasSettings(const RegionBook& book, int zone);
