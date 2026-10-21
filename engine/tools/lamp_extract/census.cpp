#include "census.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>

#include "archive_set.h"
#include "m2_lights.h"
#include "wmo_lights.h"

namespace
{

constexpr float kPlaceRadius = 60.0f;
constexpr size_t kModelRows = 120;
constexpr size_t kMissedRows = 60;
constexpr size_t kPlaceRows = 40;

const char* KindName(uint8_t kind)
{
    static const std::array<const char*, 7> names = {"candle", "torch", "brazier", "bonfire",
                                                     "lamp",   "chandelier", "fixture"};
    return kind < names.size() ? names[kind] : "?";
}

std::string FlagText(uint16_t flags)
{
    std::string text;
    auto add = [&](uint16_t bit, const char* name) {
        if (flags & bit)
            text += text.empty() ? name : std::string(",") + name;
    };
    add(lampfile::kLampAuthored, "authored");
    add(lampfile::kLampFlame, "flame");
    add(lampfile::kLampNameRoute, "name");
    add(lampfile::kLampEmissiveRoute, "emissive");
    add(lampfile::kLampWmoFixture, "molt");
    add(lampfile::kLampGameObject, "go");
    add(lampfile::kLampWmoDoodad, "wmo-doodad");
    add(lampfile::kLampWmoLantern, "lantern");
    add(lampfile::kLampIndoor, "indoor");
    add(lampfile::kLampFixtureNearLamp, "near-lamp");
    add(lampfile::kLampBoundsFallback, "bbox");
    add(lampfile::kLampClampedToModel, "clamped");
    return text.empty() ? "-" : text;
}

std::string Stem(const std::string& path)
{
    size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Vec3 LampAt(const lampfile::Lamp& lamp)
{
    return {lamp.position[0], lamp.position[1], lamp.position[2]};
}

void WriteMaps(std::ostream& out, const std::vector<MapLights>& maps)
{
    out << "\n== Maps (only maps with lights or windows) ==\n";
    out << "map  directory                 tiles miss doodads wmos  go(nomodel/wmo) | lamps: candle torch brazier"
           " bonfire lamp chandel. | molt(in/near) | windows(in/up) dup\n";
    for (const MapLights& map : maps)
    {
        if (map.lamps.empty() && map.windows.empty())
            continue;
        std::array<uint32_t, 7> kinds{};
        uint32_t fixtures = 0, fixturesIndoor = 0, fixturesNear = 0;
        for (const WorldLamp& lamp : map.lamps)
        {
            if (lamp.record.flags & lampfile::kLampWmoFixture)
            {
                ++fixtures;
                fixturesIndoor += (lamp.record.flags & lampfile::kLampIndoor) ? 1 : 0;
                fixturesNear += (lamp.record.flags & lampfile::kLampFixtureNearLamp) ? 1 : 0;
                continue;
            }
            if (lamp.record.kind < kinds.size())
                ++kinds[lamp.record.kind];
        }
        uint32_t windowsIndoor = 0, windowsUp = 0;
        for (const WorldWindow& window : map.windows)
        {
            windowsIndoor += (window.record.flags & lampfile::kWindowIndoor) ? 1 : 0;
            windowsUp += (window.record.flags & lampfile::kWindowFacesUp) ? 1 : 0;
        }
        out << std::left << std::setw(5) << map.mapId << std::setw(26) << map.directory.substr(0, 25) << std::right
            << std::setw(5) << map.tiles << std::setw(5) << map.missingTiles << std::setw(8) << map.doodadPlacements
            << std::setw(6) << map.wmoPlacements << std::setw(6) << map.gameObjects << "("
            << map.gameObjectsWithoutModel << "/" << map.gameObjectWmos
            << ") |" << std::setw(12) << kinds[0] << std::setw(6) << kinds[1] << std::setw(8) << kinds[2]
            << std::setw(8) << kinds[3] << std::setw(5) << kinds[4] << std::setw(9) << kinds[5] << " | "
            << fixtures << "(" << fixturesIndoor << "/" << fixturesNear << ") | " << map.windows.size() << "("
            << windowsIndoor << "/" << windowsUp << ") " << map.duplicateLamps << "\n";
    }
}

void WriteModels(std::ostream& out, const CensusInputs& in)
{
    const auto& placements = in.scanner.ModelPlacements();
    std::vector<std::pair<std::string, const ModelLights*>> lit;
    uint32_t emitters = 0, invalid = 0;
    std::map<ModelRoute, uint32_t> routes;
    for (const auto& [path, lights] : in.models.Entries())
    {
        emitters += lights.emitters;
        invalid += lights.invalidEmitters;
        ++routes[lights.route];
        if (!lights.lights.empty())
            lit.emplace_back(path, &lights);
    }
    auto count = [&](const std::string& path) {
        auto it = placements.find(path);
        return it == placements.end() ? 0u : it->second;
    };
    out << "\n== Models ==\n" << in.models.Entries().size() << " distinct M2 models placed; emitters " << emitters
        << ", of which failed the layout check " << invalid << "\n";
    for (const auto& [route, n] : routes)
        out << "  route " << RouteName(route) << ": " << n << " models\n";

    std::sort(lit.begin(), lit.end(), [&](const auto& a, const auto& b) { return count(a.first) > count(b.first); });
    out << "\nLight-giving models by placements (top " << kModelRows << " of " << lit.size() << ")\n";
    out << "  placed route      kind       int  reach  rgb              local xyz               flags  model\n";
    for (size_t i = 0; i < lit.size() && i < kModelRows; ++i)
    {
        const auto& [path, lights] = lit[i];
        const ModelLight& light = lights->lights.front();
        out << std::setw(8) << count(path) << " " << std::left << std::setw(10) << RouteName(lights->route)
            << std::setw(11) << KindName(static_cast<uint8_t>(light.kind)) << std::right << std::fixed
            << std::setprecision(1) << std::setw(4) << light.intensity << std::setw(6) << light.reach << "  "
            << std::setprecision(2) << light.color.r << " " << light.color.g << " " << light.color.b << "  "
            << std::setprecision(2) << std::setw(6) << light.position.x << " " << std::setw(6) << light.position.y
            << " " << std::setw(6) << light.position.z << "  " << lights->lights.size() << "x "
            << FlagText(light.flags) << "  " << path << "\n";
    }

    std::vector<std::pair<std::string, uint32_t>> missed;
    for (const auto& [path, n] : placements)
    {
        auto entry = in.models.Entries().find(path);
        if (entry == in.models.Entries().end() || !entry->second.lights.empty() || !LooksLikeLightSource(path))
            continue;
        missed.emplace_back(path, n);
    }
    std::sort(missed.begin(), missed.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    out << "\nLight-like names that give no light (top " << kMissedRows << " of " << missed.size() << ")\n";
    for (size_t i = 0; i < missed.size() && i < kMissedRows; ++i)
    {
        const ModelLights& lights = in.models.Entries().at(missed[i].first);
        out << std::setw(8) << missed[i].second << " " << std::left << std::setw(14) << RouteName(lights.route)
            << std::right << "emitters " << lights.emitters << " fire " << lights.fireEmitters << " unlit "
            << lights.unlitBatches << "/" << lights.batches << "  " << missed[i].first << "\n";
    }

    std::vector<std::pair<std::string, const ModelLights*>> broken;
    for (const auto& [path, lights] : in.models.Entries())
        if (lights.route == ModelRoute::Missing || lights.route == ModelRoute::Unreadable)
            broken.emplace_back(path, &lights);
    auto byPlacements = [&](const auto& a, const auto& b) { return count(a.first) > count(b.first); };
    std::sort(broken.begin(), broken.end(), byPlacements);
    out << "\nMissing or unreadable models (" << broken.size() << ")\n";
    for (const auto& [path, lights] : broken)
        out << std::setw(8) << count(path) << "  " << std::left << std::setw(11) << RouteName(lights->route)
            << std::right << path << "\n";

    std::vector<std::pair<std::string, uint32_t>> clamped;
    for (const auto& [path, lights] : lit)
        if (lights->clampedLights > 0)
            clamped.emplace_back(path, count(path));
    out << "\nModels whose flame lay outside their geometry and was clamped into it (" << clamped.size() << ")\n";
    for (const auto& [path, n] : clamped)
        out << std::setw(8) << n << "  " << path << "\n";

    std::vector<std::pair<std::string, const ModelLights*>> rigs;
    for (const auto& [path, lights] : in.models.Entries())
        if (lights.rigLights > 0)
            rigs.emplace_back(path, &lights);
    std::sort(rigs.begin(), rigs.end(), byPlacements);
    out << "\nModels with authored lights far outside their geometry, dropped as lighting rigs (" << rigs.size()
        << ")\n";
    for (const auto& [path, lights] : rigs)
        out << std::setw(8) << count(path) << "  dropped " << lights->rigLights << ", kept " << lights->lights.size()
            << "  " << path << "\n";
}

void WriteWindows(std::ostream& out, const CensusInputs& in)
{
    uint32_t windows = 0, outward = 0, dark = 0, wmosWithWindows = 0;
    std::vector<std::tuple<uint32_t, float, std::string>> rows;
    for (const auto& [path, info] : in.wmos.Entries())
    {
        Vec3 middle = (info.boundsMin + info.boundsMax) * 0.5f;
        dark += info.darkSidnBatches;
        uint32_t exterior = 0, out_count = 0;
        for (const WmoWindow& window : info.windows)
        {
            ++windows;
            if (window.indoor)
                continue;
            ++exterior;
            if (Dot(window.normal, window.center - middle) > 0.0f)
                ++out_count;
        }
        outward += out_count;
        if (!info.windows.empty())
            ++wmosWithWindows;
        if (exterior >= 4)
            rows.emplace_back(exterior, static_cast<float>(out_count) / static_cast<float>(exterior), path);
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    out << "\n== Night windows (WMO material flag 0x10) ==\n" << windows << " panes in " << wmosWithWindows
        << " WMOs; batches skipped for a black night colour " << dark << "\n";
    out << "Exterior panes whose normal points away from the WMO's box centre, per WMO (top 30)\n";
    for (size_t i = 0; i < rows.size() && i < 30; ++i)
        out << std::setw(8) << std::get<0>(rows[i]) << "  " << std::fixed << std::setprecision(2)
            << std::get<1>(rows[i]) << "  " << std::get<2>(rows[i]) << "\n";

    std::vector<std::pair<size_t, std::string>> lanterns;
    size_t lanternTotal = 0;
    for (const auto& [path, info] : in.wmos.Entries())
        if (!info.lanterns.empty())
        {
            lanterns.emplace_back(info.lanterns.size(), path);
            lanternTotal += info.lanterns.size();
        }
    std::sort(lanterns.begin(), lanterns.end(), std::greater<>());
    out << "Closed glass boxes of night colour, placed as lamps (lanterns), per WMO (top 30 of " << lanterns.size()
        << ", " << lanternTotal << " in all)\n";
    for (size_t i = 0; i < lanterns.size() && i < 30; ++i)
        out << std::setw(8) << lanterns[i].first << "  " << lanterns[i].second << "\n";
}

void WritePlaces(std::ostream& out, const CensusInputs& in)
{
    out << "\n== Places (lights within " << kPlaceRadius << " yd) ==\n";
    for (const CensusPlace& place : in.places)
    {
        const MapLights* map = nullptr;
        for (const MapLights& candidate : in.maps)
            if (candidate.mapId == place.map)
                map = &candidate;
        out << "\n-- " << place.name << " (map " << place.map << " " << std::fixed << std::setprecision(1)
            << place.position.x << " " << place.position.y << " " << place.position.z << ")\n";
        if (!map)
        {
            out << "   map not scanned\n";
            continue;
        }
        std::vector<std::pair<float, const WorldLamp*>> near;
        for (const WorldLamp& lamp : map->lamps)
        {
            float distance = Length(LampAt(lamp.record) - place.position);
            if (distance <= kPlaceRadius)
                near.emplace_back(distance, &lamp);
        }
        std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        uint32_t windows = 0, exteriorWindows = 0;
        for (const WorldWindow& window : map->windows)
        {
            Vec3 c{window.record.center[0], window.record.center[1], window.record.center[2]};
            if (Length(c - place.position) > kPlaceRadius)
                continue;
            ++windows;
            exteriorWindows += (window.record.flags & lampfile::kWindowIndoor) ? 0 : 1;
        }
        out << "   " << near.size() << " lights, " << windows << " window panes (" << exteriorWindows << " exterior)\n";
        for (size_t i = 0; i < near.size() && i < kPlaceRows; ++i)
        {
            const lampfile::Lamp& lamp = near[i].second->record;
            out << "   " << std::setw(5) << std::setprecision(1) << near[i].first << " yd  " << std::setw(9)
                << lamp.position[0] << std::setw(9) << lamp.position[1] << std::setw(8) << lamp.position[2] << "  "
                << std::left << std::setw(11) << KindName(lamp.kind) << std::right << std::setprecision(1)
                << lamp.intensity << "/" << std::setprecision(0) << lamp.reach << "yd  " << FlagText(lamp.flags) << "  "
                << Stem(near[i].second->source) << "\n";
        }
    }
}

}

void WriteCensus(std::ostream& out, const CensusInputs& in)
{
    out << "lamp_extract census\n";
    out << "archives (highest priority first):\n";
    for (const std::string& name : in.archives.OpenedNames())
        out << "  " << name << "\n";
    out << "files read " << in.archives.FilesRead() << ", " << (in.archives.BytesRead() >> 20) << " MB, "
        << std::fixed << std::setprecision(1) << in.seconds << " s\n";
    uint64_t lamps = 0, windows = 0;
    for (const MapLights& map : in.maps)
    {
        lamps += map.lamps.size();
        windows += map.windows.size();
    }
    out << "total lights " << lamps << ", window panes " << windows << "\n";
    WriteMaps(out, in.maps);
    WriteModels(out, in);
    WriteWindows(out, in);
    WritePlaces(out, in);
}
