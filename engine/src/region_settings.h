#pragma once

#include "config.h"
#include "engine.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

constexpr int kAllZones = 0;
constexpr int kAnySlot = -1;
constexpr int kDaySlotCount = 4;
constexpr int kSkySlotCount = 4;
constexpr int kDawn = 0;
constexpr int kDay = 1;
constexpr int kDusk = 2;
constexpr int kNight = 3;
constexpr int kSkyClear = 0;
constexpr int kSkyRain = 1;
constexpr int kSkySnow = 2;
constexpr int kSkySand = 3;
constexpr double kRegionFadeSeconds = 4.0;
constexpr int kFollowChoice = 0;
constexpr int kAnyChoice = 1;
constexpr int kFirstSlotChoice = 2;

const char* DaySlotName(int day);
const char* SkySlotName(int sky);
int SkySlotOf(WeatherKind weather);

struct RegionSlot
{
    int zone;
    int day;
    int sky;
};

bool operator<(const RegionSlot& a, const RegionSlot& b);
bool operator==(const RegionSlot& a, const RegionSlot& b);
std::string RegionSlotName(const RegionSlot& slot);

using RegionValues = std::map<int, float>;

enum class ZoneProgress
{
    NotStarted,
    Started,
    Done,
};

const char* ZoneProgressName(ZoneProgress progress);

struct ZoneSpot
{
    std::string name;
    int map;
    float x;
    float y;
    float z;
    float o;
};

struct RegionBook
{
    std::map<RegionSlot, RegionValues> slots;
    std::map<int, ZoneProgress> progress;
    std::map<int, std::vector<ZoneSpot>> spots;
};

bool ParseRegionBook(const std::string& text, RegionBook& out, std::vector<std::string>& problems);
std::string RegionBookText(const RegionBook& book, const std::function<std::string(int)>& zoneName);
bool SameRegionValues(const RegionValues& a, const RegionValues& b);

struct SlotWeights
{
    float day[kDaySlotCount];
    float sky[kSkySlotCount];
};

void DayWeights(float dayFraction, float (&out)[kDaySlotCount]);
void SkyWeights(WeatherKind weather, float intensity, float (&out)[kSkySlotCount]);
SlotWeights SlotWeightsAt(float dayFraction, WeatherKind weather, float intensity);
int DominantDaySlot(float dayFraction);

bool ResolveRegionValue(const RegionBook& book, int zone, int day, int sky, int setting, float& value);
LookLayer RegionLayer(const RegionBook& book, int zone, const SlotWeights& weights);
LookLayer BlendLookLayers(const LookLayer& from, const LookLayer& to, float t);
void ForceLookValues(LookLayer& layer, const RegionValues& values);

class RegionBlender
{
public:
    LookLayer Update(int zone, const LookLayer& target, double seconds);
    int Zone() const { return m_zone; }
    float Progress() const;
    void Reset();

private:
    bool m_started = false;
    int m_zone = 0;
    LookLayer m_from;
    LookLayer m_last;
    double m_since = 0.0;
    double m_now = 0.0;
};

class RegionStore
{
public:
    void Load(const std::string& path);
    bool ReloadIfChanged();
    const RegionBook& Book() const { return m_working; }
    const std::string& Path() const { return m_path; }
    const std::vector<std::string>& Problems() const { return m_problems; }
    unsigned Generation() const { return m_generation; }

    bool SetValue(const RegionSlot& slot, int setting, float value);
    bool ClearValue(const RegionSlot& slot, int setting);
    bool ClearSlot(const RegionSlot& slot);
    void SetProgress(int zone, ZoneProgress progress);
    ZoneProgress Progress(int zone) const;
    void AddSpot(int zone, const ZoneSpot& spot);
    bool RemoveSpot(int zone, size_t index);

    bool HasUnsavedChanges() const;
    bool SlotUnsaved(const RegionSlot& slot) const;
    bool Save(const std::function<std::string(int)>& zoneName);
    bool SaveChecklist(const std::function<std::string(int)>& zoneName);
    bool ChecklistUnsaved() const;
    void Revert();

private:
    void ReadDisk();
    bool Write(const std::function<std::string(int)>& zoneName, bool look);

    std::string m_path;
    unsigned long long m_stamp = 0;
    RegionBook m_disk;
    RegionBook m_working;
    std::set<std::pair<RegionSlot, int>> m_dirtyKeys;
    std::set<int> m_dirtyProgress;
    std::set<int> m_dirtySpots;
    std::vector<std::string> m_problems;
    unsigned m_generation = 0;
};

RegionSlot ChooseSlot(bool thisZone, int zone, int timeChoice, int skyChoice, float dayFraction, WeatherKind weather);
bool EditableSlot(const RegionSlot& slot);
Config SlotLook(const RegionBook& book, const Config& global, const RegionSlot& slot, float dayFraction,
                WeatherKind weather);
int StoreLookEdits(RegionStore& store, const RegionSlot& slot, const Config& shown, const Config& edited,
                   std::string& globalOnlyKey);

bool WriteTextFileAtomically(const std::string& path, const std::string& text);
bool ReadTextFile(const std::string& path, std::string& out);
unsigned long long TextFileStamp(const std::string& path);
