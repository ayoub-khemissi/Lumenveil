#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "archive_set.h"
#include "census.h"
#include "lamp_catalog.h"
#include "lamp_writer.h"
#include "liquid_map.h"
#include "liquid_scan.h"
#include "m2_lights.h"
#include "wmo_lights.h"
#include "world_scan.h"

namespace fs = std::filesystem;

namespace
{

constexpr size_t kMaxThreads = 16;

struct Options
{
    fs::path data;
    fs::path out;
    fs::path gameObjects;
    fs::path places;
    fs::path census;
    fs::path liquids;
    fs::path waterTextures;
    fs::path stamp;
    std::string locale = "enUS";
    std::vector<uint32_t> maps;
    std::vector<uint32_t> liquidMaps;
    size_t threads = 0;
    std::vector<std::string> models;
    bool background = false;
    bool stock = false;
};

void PrintUsage()
{
    std::cerr << "lamp_extract --data <client Data folder> --out <lampdata.bin> [--locale enUS]\n"
                 "             [--gameobjects <tsv: map x y z o r0 r1 r2 r3 displayId size entry name>]\n"
                 "             [--places <tsv: name map x y z>] [--census <report.txt>] [--maps 0,1,530]\n"
                 "             [--background]\n"
                 "lamp_extract --data <client Data folder> --model <path.m2 | path.wmo> [--model ...]\n"
                 "lamp_extract --data <client Data folder> --liquids <liquiddata.bin> [--maps 0,1,530]\n"
                 "             [--gameobjects <tsv>]\n"
                 "lamp_extract --data <client Data folder> --water-textures <folder>\n"
                 "lamp_extract --data <client Data folder> --out <lampdata.bin> [--liquids <liquiddata.bin>\n"
                 "             [--liquid-maps 0,1,530,571]] [--water-textures <folder>] [--stamp <file>]\n"
                 "             [--threads N] [--locale enUS] [--background]\n";
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--stock")
        {
            options.stock = true;
            continue;
        }
        if (arg == "--background")
        {
            options.background = true;
            continue;
        }
        const char* value = next();
        if (!value)
            return false;
        if (arg == "--data")
            options.data = value;
        else if (arg == "--out")
            options.out = value;
        else if (arg == "--locale")
            options.locale = value;
        else if (arg == "--gameobjects")
            options.gameObjects = value;
        else if (arg == "--places")
            options.places = value;
        else if (arg == "--census")
            options.census = value;
        else if (arg == "--liquids")
            options.liquids = value;
        else if (arg == "--water-textures")
            options.waterTextures = value;
        else if (arg == "--stamp")
            options.stamp = value;
        else if (arg == "--threads")
            options.threads = static_cast<size_t>(std::strtoul(value, nullptr, 10));
        else if (arg == "--liquid-maps")
        {
            std::stringstream list(value);
            std::string item;
            while (std::getline(list, item, ','))
                options.liquidMaps.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)));
        }
        else if (arg == "--model")
            options.models.push_back(value);
        else if (arg == "--maps")
        {
            std::stringstream list(value);
            std::string item;
            while (std::getline(list, item, ','))
                options.maps.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)));
        }
        else
            return false;
    }
    return !options.data.empty() && (!options.out.empty() || !options.models.empty() || !options.liquids.empty() ||
                                     !options.waterTextures.empty());
}

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t'))
        fields.push_back(field);
    return fields;
}

float Number(const std::string& text)
{
    return std::strtof(text.c_str(), nullptr);
}

std::vector<GameObjectSpawn> ReadGameObjects(const fs::path& path)
{
    std::vector<GameObjectSpawn> spawns;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> f = SplitTabs(line);
        if (f.size() < 12)
            continue;
        GameObjectSpawn spawn;
        spawn.map = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
        spawn.position = {Number(f[1]), Number(f[2]), Number(f[3])};
        spawn.orientation = Number(f[4]);
        for (int r = 0; r < 4; ++r)
            spawn.rotation[r] = Number(f[5 + r]);
        spawn.displayId = static_cast<uint32_t>(std::strtoul(f[9].c_str(), nullptr, 10));
        spawn.size = Number(f[10]);
        spawn.entry = static_cast<uint32_t>(std::strtoul(f[11].c_str(), nullptr, 10));
        if (f.size() > 12)
            spawn.name = f[12];
        spawns.push_back(std::move(spawn));
    }
    return spawns;
}

std::vector<CensusPlace> ReadPlaces(const fs::path& path)
{
    std::vector<CensusPlace> places;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> f = SplitTabs(line);
        if (f.size() < 5)
            continue;
        places.push_back({f[0], static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10)),
                          {Number(f[2]), Number(f[3]), Number(f[4])}});
    }
    return places;
}

bool Wanted(const Options& options, uint32_t mapId)
{
    return options.maps.empty() || std::find(options.maps.begin(), options.maps.end(), mapId) != options.maps.end();
}

bool LiquidsWanted(const Options& options, uint32_t mapId)
{
    return !options.liquids.empty() &&
           (options.liquidMaps.empty() ||
            std::find(options.liquidMaps.begin(), options.liquidMaps.end(), mapId) != options.liquidMaps.end());
}

std::string Signature(const ArchiveSet& archives, const Options& options)
{
    std::ostringstream text;
    text << "lamp_extract setup 1\n" << options.out.string() << "\n" << options.liquids.string() << "\n"
         << options.waterTextures.string() << "\n" << options.locale << "\n" << (options.stock ? 1 : 0) << "\n";
    for (uint32_t id : options.maps)
        text << id << ",";
    text << "\n";
    for (uint32_t id : options.liquidMaps)
        text << id << ",";
    text << "\n";
    for (const std::string& name : archives.OpenedNames())
    {
        std::error_code error;
        const auto size = fs::file_size(name, error);
        const auto written = fs::last_write_time(name, error).time_since_epoch().count();
        text << name << "|" << size << "|" << written << "\n";
    }
    return text.str();
}

std::string ReadWholeText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool OutputsPresent(const Options& options)
{
    std::error_code error;
    return fs::exists(options.out, error) && (options.liquids.empty() || fs::exists(options.liquids, error));
}

int WriteLiquids(const Options& options, const std::vector<MapLiquids>& maps)
{
    if (!WriteLiquidFile(options.liquids, maps))
    {
        std::cerr << "could not write " << options.liquids.string() << "\n";
        return 1;
    }
    LiquidMap reread;
    if (!reread.Load(options.liquids.string()))
    {
        std::cerr << "the fog DLL's reader rejects " << options.liquids.string() << "\n";
        return 1;
    }
    std::cout << "wrote " << options.liquids.string() << " (" << fs::file_size(options.liquids)
              << " bytes); the fog DLL's reader accepts it: " << reread.MapCount() << " maps, " << reread.ChunkCount()
              << " chunks\n";
    return 0;
}

void PrintLiquids(const MapInfo& map, const MapLiquids& found)
{
    size_t lakes = 0;
    for (const liquidfile::Chunk& c : found.chunks)
        lakes += c.kind == liquidfile::kLake ? 1 : 0;
    std::printf("map %4u %-28s tiles %4u (%u with water) chunks: %6zu lake %6zu sea (%u in the old format), "
                "%u magma or slime, %u unknown\n",
                map.id, map.directory.c_str(), found.tiles, found.tilesWithLiquid, lakes,
                found.chunks.size() - lakes, found.oldLiquidChunks, found.magmaOrSlimeChunks, found.unknownTypes);
    std::fflush(stdout);
}

bool FishingSpot(const std::string& name)
{
    for (const char* word : {"School", "Swarm", "Wreckage", "Debris", "Elemental Water"})
        if (name.find(word) != std::string::npos)
            return true;
    return false;
}

void CheckFishingSpots(const LiquidMap& liquids, const std::vector<GameObjectSpawn>& spawns)
{
    struct Tally
    {
        int spots = 0;
        int water = 0;
        int lake = 0;
        int sea = 0;
        double offSurface = 0.0;
    };
    std::map<std::string, Tally> byName;
    Tally all;
    for (const GameObjectSpawn& spawn : spawns)
    {
        if (!FishingSpot(spawn.name))
            continue;
        LiquidAt at = {};
        const bool wet = liquids.At(static_cast<int>(spawn.map), spawn.position.x, spawn.position.y, at);
        for (Tally* t : {&byName[spawn.name], &all})
        {
            ++t->spots;
            if (!wet)
                continue;
            ++t->water;
            t->lake += at.lake > 0.5f ? 1 : 0;
            t->sea += at.sea > 0.5f ? 1 : 0;
            t->offSurface += std::fabs(spawn.position.z - at.surface);
        }
    }
    for (const auto& [name, t] : byName)
        std::printf("  %-34s %5d spots, %5.1f%% on the liquid map (%d lake, %d sea), mean |z - surface| %.2f yd\n",
                    name.c_str(), t.spots, 100.0 * t.water / t.spots, t.lake, t.sea,
                    t.water ? t.offSurface / t.water : 0.0);
    std::printf("fishing spots: %d, %.1f%% on the liquid map, mean |z - surface| %.2f yd\n", all.spots,
                all.spots ? 100.0 * all.water / all.spots : 0.0, all.water ? all.offSurface / all.water : 0.0);
}

int RunWaterTextures(const Options& options, const ArchiveSet& archives)
{
    const std::pair<const char*, const char*> textures[] = {
        {"XTextures\\ocean\\newoceanbump.blp", "waterbump.blp"},
        {"XTextures\\foam\\waterchop7bw.blp", "waterfoam.blp"},
    };
    int copied = 0;
    for (const auto& [source, target] : textures)
    {
        const fs::path path = options.waterTextures / target;
        std::optional<std::vector<uint8_t>> bytes = archives.Read(source);
        if (!bytes)
        {
            std::error_code ignored;
            fs::remove(path, ignored);
            std::cout << source << ": not in the archives, no " << target << "\n";
            continue;
        }
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        if (!file)
        {
            std::cerr << "could not write " << path.string() << "\n";
            return 1;
        }
        std::cout << "wrote " << path.string() << " (" << bytes->size() << " bytes) from " << source << "\n";
        ++copied;
    }
    std::cout << copied << " of 2 water textures copied\n";
    return 0;
}

int RunLiquids(const Options& options, const ArchiveSet& archives)
{
    ModelLightCatalogue models(archives);
    WmoCatalogue wmos(archives);
    WorldScanner scanner(archives, models, wmos);
    LiquidScanner liquids(archives);
    if (!scanner.LoadTables() || !liquids.LoadTypes())
    {
        std::cerr << "Map.dbc or LiquidType.dbc could not be read\n";
        return 1;
    }
    std::vector<MapLiquids> maps;
    for (const MapInfo& map : scanner.Maps())
    {
        if (!Wanted(options, map.id))
            continue;
        MapLiquids found = liquids.ScanMap(map);
        if (found.tiles == 0)
            continue;
        size_t lakes = 0;
        for (const liquidfile::Chunk& c : found.chunks)
            lakes += c.kind == liquidfile::kLake ? 1 : 0;
        std::printf("map %4u %-28s tiles %4u (%u with water) chunks: %6zu lake %6zu sea (%u in the old format), "
                    "%u magma or slime, %u unknown\n",
                    map.id, map.directory.c_str(), found.tiles, found.tilesWithLiquid, lakes,
                    found.chunks.size() - lakes, found.oldLiquidChunks, found.magmaOrSlimeChunks, found.unknownTypes);
        std::fflush(stdout);
        maps.push_back(std::move(found));
    }
    if (!WriteLiquidFile(options.liquids, maps))
    {
        std::cerr << "could not write " << options.liquids.string() << "\n";
        return 1;
    }
    LiquidMap reread;
    if (!reread.Load(options.liquids.string()))
    {
        std::cerr << "the fog DLL's reader rejects " << options.liquids.string() << "\n";
        return 1;
    }
    std::cout << "wrote " << options.liquids.string() << " (" << fs::file_size(options.liquids)
              << " bytes); the fog DLL's reader accepts it: " << reread.MapCount() << " maps, " << reread.ChunkCount()
              << " chunks\n";
    if (!options.gameObjects.empty())
        CheckFishingSpots(reread, ReadGameObjects(options.gameObjects));
    return 0;
}

}

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage();
        return 2;
    }
    if (options.background)
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    auto started = std::chrono::steady_clock::now();
    ArchiveSet archives;
    if (!archives.Open(options.data, options.locale, !options.stock))
    {
        std::cerr << "no archives found in " << options.data.string() << "\n";
        return 1;
    }
    std::cout << archives.OpenedNames().size() << " archives opened\n";

    if (!options.models.empty())
    {
        ModelLightCatalogue catalogue(archives);
        WmoCatalogue wmoCatalogue(archives);
        for (const std::string& model : options.models)
        {
            std::string upper = UpperPath(model);
            if (upper.size() > 4 && upper.compare(upper.size() - 4, 4, ".WMO") == 0)
                wmoCatalogue.Describe(model, catalogue, std::cout);
            else
                catalogue.Describe(model, std::cout);
        }
        return 0;
    }

    if (!options.liquids.empty() && options.out.empty())
        return RunLiquids(options, archives);
    if (!options.waterTextures.empty() && options.out.empty())
        return RunWaterTextures(options, archives);

    const std::string signature = Signature(archives, options);
    if (!options.stamp.empty() && OutputsPresent(options) && ReadWholeText(options.stamp) == signature)
    {
        std::cout << "the game's archives and the options are as at the last setup: nothing to do\n";
        return 0;
    }
    const size_t wantedThreads =
        options.threads > 0 ? options.threads : static_cast<size_t>(std::thread::hardware_concurrency());
    const size_t threads = std::clamp<size_t>(wantedThreads, 1, kMaxThreads);
    std::vector<std::unique_ptr<ArchiveSet>> extraReaders;
    std::vector<const ArchiveSet*> readers{&archives};
    while (readers.size() < threads)
    {
        auto reader = std::make_unique<ArchiveSet>();
        if (!reader->Open(options.data, options.locale, !options.stock))
            break;
        readers.push_back(reader.get());
        extraReaders.push_back(std::move(reader));
    }
    std::cout << readers.size() << " threads\n";
    LiquidScanner liquids(archives);
    if (!options.liquids.empty() && !liquids.LoadTypes())
    {
        std::cerr << "LiquidType.dbc could not be read\n";
        return 1;
    }
    std::vector<MapLiquids> liquidMaps;

    ModelLightCatalogue models(archives);
    WmoCatalogue wmos(archives);
    WorldScanner scanner(archives, models, wmos);
    if (!scanner.LoadTables())
    {
        std::cerr << "Map.dbc could not be read\n";
        return 1;
    }
    std::vector<GameObjectSpawn> gameObjects =
        options.gameObjects.empty() ? std::vector<GameObjectSpawn>() : ReadGameObjects(options.gameObjects);
    std::vector<CensusPlace> places =
        options.places.empty() ? std::vector<CensusPlace>() : ReadPlaces(options.places);
    std::cout << scanner.Maps().size() << " maps, " << gameObjects.size() << " game object spawns, " << places.size()
              << " census places\n";

    std::vector<MapLights> maps;
    for (const MapInfo& map : scanner.Maps())
    {
        if (!Wanted(options, map.id))
            continue;
        auto mapStarted = std::chrono::steady_clock::now();
        const bool withLiquids = LiquidsWanted(options, map.id);
        MapLiquids found;
        MapLights lights = scanner.ScanMap(map, gameObjects, readers, withLiquids ? &liquids : nullptr,
                                           withLiquids ? &found : nullptr);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStarted).count();
        if (withLiquids && found.tiles > 0)
        {
            PrintLiquids(map, found);
            liquidMaps.push_back(std::move(found));
        }
        if (lights.lamps.empty() && lights.windows.empty() && lights.tiles == 0 && lights.wmoPlacements == 0)
            continue;
        std::printf("map %4u %-28s tiles %4u lights %6zu windows %6zu indoor game object lights %4u  %.1f s\n",
                    map.id, map.directory.c_str(), lights.tiles, lights.lamps.size(), lights.windows.size(),
                    lights.gameObjectLampsIndoors, seconds);
        std::fflush(stdout);
        if (!lights.lamps.empty() || !lights.windows.empty())
            maps.push_back(std::move(lights));
    }

    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!options.census.empty())
    {
        std::ofstream report(options.census);
        WriteCensus(report, {archives, models, wmos, scanner, maps, places, seconds});
    }
    if (!WriteLampFile(options.out, maps))
    {
        std::cerr << "could not write " << options.out.string() << "\n";
        return 1;
    }
    std::cout << "wrote " << options.out.string() << " (" << fs::file_size(options.out) << " bytes) in " << seconds
              << " s\n";
    LampCatalog reread;
    if (!reread.Load(options.out.string()))
    {
        std::cerr << "the fog DLL's reader rejects " << options.out.string() << "\n";
        return 1;
    }
    std::cout << "the fog DLL's reader accepts it: " << reread.LampTotal() << " lamps, " << reread.WindowTotal()
              << " windows\n";
    if (!options.liquids.empty() && WriteLiquids(options, liquidMaps) != 0)
        return 1;
    if (!options.waterTextures.empty() && RunWaterTextures(options, archives) != 0)
        return 1;
    if (!options.stamp.empty())
    {
        std::ofstream stampFile(options.stamp, std::ios::binary | std::ios::trunc);
        stampFile << signature;
    }
    std::cout << "setup done in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " s\n";
    return 0;
}
