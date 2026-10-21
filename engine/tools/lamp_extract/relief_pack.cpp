#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "archive_set.h"
#include "blp_hash.h"
#include "png_reader.h"
#include "relief_catalog.h"
#include "relief_classes.h"
#include "relief_data.h"
#include "relief_rules.h"

namespace fs = std::filesystem;

namespace
{

constexpr float kFlatLumaSpread = 0.05f;
constexpr float kFullLumaSpread = 0.2f;
constexpr double kLumaSpreadFloor = 0.05;
constexpr uint32_t kGroundMedian = 13;
constexpr uint32_t kBuildingMedian = 5;
constexpr uint32_t kLargestMedian = 15;
constexpr uint32_t kSpreadSide = 1024;
constexpr uint8_t kOpaqueAlpha = 128;
constexpr double kLeastOpaqueShare = 0.1;
constexpr size_t kExamplesPerClass = 30;
constexpr size_t kReasonsPerClass = 25;
constexpr uint32_t kExampleSeed = 20261009;
constexpr float kDepthChange = 0.01f;
constexpr const char* kTypeOnly = "type only";
constexpr const char* kModelFolders[] = {"CREATURE\\", "ITEM\\", "WORLD\\"};

struct Options
{
    fs::path data;
    fs::path textures;
    fs::path heights;
    fs::path out;
    fs::path classesOnly;
    fs::path classify;
    fs::path report;
    fs::path compare;
    std::string locale = "enUS";
    std::vector<std::string> kinds = {"ground"};
    uint32_t size = 256;
    uint32_t groundMedian = kGroundMedian;
    uint32_t buildingMedian = kBuildingMedian;
    size_t limit = 0;
    bool stock = false;
    bool modelTypes = false;
    std::vector<std::string> hashOnly;
};

struct Row
{
    std::string id;
    std::string kind;
    std::string name;
};

struct Packed
{
    relieffile::Entry entry;
    std::string name;
    std::string source;
    float spread;
    uint32_t spooledIndex = relieffile::kNoHeights;
};

void PrintUsage()
{
    std::cerr
        << "relief_pack --data <client Data folder> --textures <relief-lab out/data/textures>\n"
           "            --heights <folder with heights.tsv and NNNNN-h.png> --out <reliefdata.bin>\n"
           "            [--kind ground|building|ground,building|all] [--classes-only <list of game paths>]\n"
           "            [--model-types] [--size 256] [--ground-median 13] [--building-median 5] [--median N]\n"
           "            [--limit N] [--locale enUS] [--stock]\n"
           "relief_pack --data <client Data folder> --out <reliefdata.bin> --classes-only <list> [--limit N]\n"
           "relief_pack --classify <list of game paths> --report <folder> [--compare <old reliefdata.tsv>]\n"
           "relief_pack --data <client Data folder> --hash <texture.blp> [--hash ...] [--locale enUS] [--stock]\n"
           "--model-types adds the type of every creature, item and world model texture the archives list, character\n"
           "skins left out\n"
           "--limit takes at most N rows of each kind and of the list\n";
}

bool MedianWindow(uint32_t median)
{
    return median <= 1 || (median % 2 == 1 && median <= kLargestMedian);
}

bool ParseKinds(const std::string& value, std::vector<std::string>& kinds)
{
    kinds.clear();
    if (value == "all")
    {
        kinds = {"ground", "building"};
        return true;
    }
    std::stringstream s(value);
    std::string kind;
    while (std::getline(s, kind, ','))
    {
        if (kind != "ground" && kind != "building")
            return false;
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
            kinds.push_back(kind);
    }
    return !kinds.empty();
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--stock")
        {
            options.stock = true;
            continue;
        }
        if (arg == "--model-types")
        {
            options.modelTypes = true;
            continue;
        }
        if (i + 1 >= argc)
            return false;
        const std::string value = argv[++i];
        if (arg == "--data")
            options.data = value;
        else if (arg == "--textures")
            options.textures = value;
        else if (arg == "--heights")
            options.heights = value;
        else if (arg == "--out")
            options.out = value;
        else if (arg == "--classes-only")
            options.classesOnly = value;
        else if (arg == "--classify")
            options.classify = value;
        else if (arg == "--report")
            options.report = value;
        else if (arg == "--compare")
            options.compare = value;
        else if (arg == "--locale")
            options.locale = value;
        else if (arg == "--kind")
        {
            if (!ParseKinds(value, options.kinds))
                return false;
        }
        else if (arg == "--size")
            options.size = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (arg == "--median")
            options.groundMedian = options.buildingMedian =
                static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (arg == "--ground-median")
            options.groundMedian = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (arg == "--building-median")
            options.buildingMedian = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (arg == "--limit")
            options.limit = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (arg == "--hash")
            options.hashOnly.push_back(value);
        else
            return false;
    }
    if (!options.classify.empty())
        return !options.report.empty();
    if (!options.hashOnly.empty())
        return !options.data.empty();
    const bool powerOfTwo = options.size >= 16 && options.size <= 1024 && !(options.size & (options.size - 1));
    const bool heightsGiven = !options.heights.empty() && !options.textures.empty();
    return !options.data.empty() && !options.out.empty() && (heightsGiven || !options.classesOnly.empty() || options.modelTypes) &&
           powerOfTwo && MedianWindow(options.groundMedian) && MedianWindow(options.buildingMedian);
}

std::vector<Row> ReadIndex(const fs::path& path, const std::vector<std::string>& kinds, size_t limit)
{
    std::vector<Row> rows;
    std::map<std::string, size_t> taken;
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::stringstream s(line);
        Row row;
        if (!std::getline(s, row.id, '\t') || !std::getline(s, row.kind, '\t') || !std::getline(s, row.name, '\t'))
            continue;
        if (std::find(kinds.begin(), kinds.end(), row.kind) == kinds.end())
            continue;
        if (limit && taken[row.kind] >= limit)
            continue;
        ++taken[row.kind];
        rows.push_back(row);
    }
    return rows;
}

std::vector<std::string> ReadList(const fs::path& path, size_t limit)
{
    std::vector<std::string> names;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::string name = line.substr(0, line.find('\t'));
        if (name.empty() || name == "name" || name == "path")
            continue;
        names.push_back(name);
        if (limit && names.size() >= limit)
            break;
    }
    return names;
}

float Wrapped(const PngImage& img, int x, int y)
{
    const int w = static_cast<int>(img.width);
    const int h = static_cast<int>(img.height);
    return img.At(static_cast<uint32_t>(((x % w) + w) % w), static_cast<uint32_t>(((y % h) + h) % h), 0);
}

float Resampled(const PngImage& img, uint32_t size, uint32_t x, uint32_t y)
{
    const bool boxX = img.width >= size && img.width % size == 0;
    const bool boxY = img.height >= size && img.height % size == 0;
    if (boxX && boxY)
    {
        const uint32_t fx = img.width / size;
        const uint32_t fy = img.height / size;
        double sum = 0.0;
        for (uint32_t j = 0; j < fy; ++j)
            for (uint32_t i = 0; i < fx; ++i)
                sum += img.At(x * fx + i, y * fy + j, 0);
        return static_cast<float>(sum / (fx * fy));
    }
    const float u = (x + 0.5f) * img.width / size - 0.5f;
    const float v = (y + 0.5f) * img.height / size - 0.5f;
    const int x0 = static_cast<int>(std::floor(u));
    const int y0 = static_cast<int>(std::floor(v));
    const float a = u - x0;
    const float c = v - y0;
    return (Wrapped(img, x0, y0) * (1 - a) + Wrapped(img, x0 + 1, y0) * a) * (1 - c) +
           (Wrapped(img, x0, y0 + 1) * (1 - a) + Wrapped(img, x0 + 1, y0 + 1) * a) * c;
}

uint32_t LevelsFor(uint32_t size)
{
    uint32_t levels = 1;
    while ((size >> (levels - 1)) > 1)
        ++levels;
    return levels;
}

std::vector<float> MedianWrapped(const std::vector<float>& level, uint32_t size, uint32_t window)
{
    if (window < 3)
        return level;
    const int r = static_cast<int>(window / 2);
    const int n = static_cast<int>(size);
    std::vector<float> out(level.size());
    std::vector<float> around(static_cast<size_t>(window) * window);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            size_t k = 0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    around[k++] =
                        level[static_cast<size_t>((y + dy + n) % n) * size + static_cast<size_t>((x + dx + n) % n)];
            const auto middle = around.begin() + static_cast<std::ptrdiff_t>(around.size() / 2);
            std::nth_element(around.begin(), middle, around.end());
            out[static_cast<size_t>(y) * size + x] = *middle;
        }
    return out;
}

std::vector<uint8_t> HeightLevels(const PngImage& img, uint32_t size, uint32_t median)
{
    std::vector<float> level(static_cast<size_t>(size) * size);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
            level[static_cast<size_t>(y) * size + x] = Resampled(img, size, x, y);
    level = MedianWrapped(level, size, median);
    std::vector<uint8_t> out;
    out.reserve(relieffile::LevelBytes(size, LevelsFor(size)));
    for (uint32_t side = size;; side /= 2)
    {
        for (float h : level)
            out.push_back(static_cast<uint8_t>(std::lround(std::clamp(h, 0.0f, 1.0f) * 255.0f)));
        if (side == 1)
            break;
        const uint32_t half = side / 2;
        std::vector<float> next(static_cast<size_t>(half) * half);
        for (uint32_t y = 0; y < half; ++y)
            for (uint32_t x = 0; x < half; ++x)
            {
                const size_t a = static_cast<size_t>(2 * y) * side + 2 * x;
                next[static_cast<size_t>(y) * half + x] =
                    0.25f * (level[a] + level[a + 1] + level[a + side] + level[a + side + 1]);
            }
        level.swap(next);
    }
    return out;
}

struct SpreadSums
{
    double sum = 0.0;
    double squares = 0.0;
    size_t n = 0;

    void Add(double luma)
    {
        sum += luma;
        squares += luma * luma;
        ++n;
    }

    float Spread() const
    {
        if (!n)
            return 0.0f;
        const double mean = sum / n;
        return static_cast<float>(std::sqrt(std::max(squares / n - mean * mean, 0.0)) / (mean + kLumaSpreadFloor));
    }
};

double Luma(double r, double g, double b)
{
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

float LumaSpread(const PngImage& img)
{
    if (img.channels < 3)
        return 0.0f;
    SpreadSums s;
    const size_t n = static_cast<size_t>(img.width) * img.height;
    for (size_t i = 0; i < n; ++i)
    {
        const float* p = &img.values[i * img.channels];
        s.Add(Luma(p[0], p[1], p[2]));
    }
    return s.Spread();
}

float LumaSpread(const BlpPixels& px, bool opaqueOnly)
{
    SpreadSums all;
    SpreadSums opaque;
    const size_t n = static_cast<size_t>(px.width) * px.height;
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t* p = &px.rgba[i * 4];
        const double luma = Luma(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0);
        all.Add(luma);
        if (p[3] >= kOpaqueAlpha)
            opaque.Add(luma);
    }
    const bool useOpaque = opaqueOnly && opaque.n >= static_cast<size_t>(kLeastOpaqueShare * n);
    return useOpaque ? opaque.Spread() : all.Spread();
}

float Smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Flatness(float spread)
{
    return Smoothstep(kFlatLumaSpread, kFullLumaSpread, spread);
}

std::string HashText(uint64_t hash)
{
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

int PrintHashes(const Options& options)
{
    ArchiveSet archives;
    if (!archives.Open(options.data, options.locale, !options.stock))
    {
        std::cerr << "cannot open the archives in " << options.data.string() << "\n";
        return 1;
    }
    int failed = 0;
    for (const std::string& name : options.hashOnly)
    {
        const auto bytes = archives.Read(name);
        BlpLevel blp;
        std::string why = "not in the archives";
        if (!bytes || !HashBlp(*bytes, blp, why))
        {
            std::cout << "-\t-\t-\t-\t" << name << "\t" << why << "\n";
            ++failed;
            continue;
        }
        std::cout << HashText(blp.hash) << '\t' << BlpFormatName(blp.format) << '\t' << blp.width << '\t'
                  << blp.height << '\t' << name << '\t' << reliefclass::Name(ReliefClassOfName(name)) << '\n';
    }
    return failed ? 1 : 0;
}

std::vector<std::map<std::string, std::string>> ReadTable(const fs::path& path)
{
    std::vector<std::map<std::string, std::string>> rows;
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line))
        return rows;
    auto split = [](std::string text) {
        if (!text.empty() && text.back() == '\r')
            text.pop_back();
        std::vector<std::string> cells;
        std::stringstream s(text);
        std::string cell;
        while (std::getline(s, cell, '\t'))
            cells.push_back(cell);
        return cells;
    };
    const std::vector<std::string> header = split(line);
    while (std::getline(f, line))
    {
        const std::vector<std::string> cells = split(line);
        std::map<std::string, std::string> row;
        for (size_t k = 0; k < header.size() && k < cells.size(); ++k)
            row[header[k]] = cells[k];
        if (!row.empty())
            rows.push_back(row);
    }
    return rows;
}

void CompareWithOld(const Options& o, std::ostream& summary)
{
    auto table = ReadTable(o.compare);
    std::ofstream changes(o.report / "compare.tsv");
    changes << "old_kind\told_depth\tnew_class\tnew_depth\tluma_spread\twhy\tname\n";
    std::map<std::string, int> moves;
    int changed = 0;
    double oldSum = 0.0;
    double newSum = 0.0;
    for (auto& r : table)
    {
        const float spread = std::strtof(r["luma_spread"].c_str(), nullptr);
        const float oldDepth = std::strtof(r["depth"].c_str(), nullptr);
        std::string why;
        const uint8_t cls = ReliefClassOfName(r["name"], &why);
        const float newDepth = reliefclass::DefaultShare(cls) * Flatness(spread);
        ++moves[r["kind"] + " -> " + reliefclass::Name(cls)];
        oldSum += oldDepth;
        newSum += newDepth;
        if (std::fabs(newDepth - oldDepth) > kDepthChange)
        {
            ++changed;
            changes << r["kind"] << '\t' << oldDepth << '\t' << reliefclass::Name(cls) << '\t' << newDepth << '\t'
                    << spread << '\t' << why << '\t' << r["name"] << '\n';
        }
    }
    summary << "\nagainst " << o.compare.string() << ": " << table.size() << " ground textures, " << changed
            << " change their depth at the default shares by more than " << kDepthChange << " (compare.tsv); mean "
            << "depth " << (table.empty() ? 0.0 : oldSum / table.size()) << " before, "
            << (table.empty() ? 0.0 : newSum / table.size()) << " now\n";
    for (const auto& [move, n] : moves)
        summary << "  " << n << "\t" << move << "\n";
}

int Classify(const Options& o)
{
    const std::vector<std::string> names = ReadList(o.classify, o.limit);
    if (names.empty())
    {
        std::cerr << "no names in " << o.classify.string() << "\n";
        return 1;
    }
    std::error_code ec;
    fs::create_directories(o.report, ec);
    std::vector<std::vector<std::pair<std::string, std::string>>> byClass(reliefclass::kCount);
    std::vector<std::map<std::string, int>> reasons(reliefclass::kCount);
    std::ofstream all(o.report / "classes.tsv");
    all << "class\twhy\tname\n";
    for (const std::string& name : names)
    {
        std::string why;
        const uint8_t cls = ReliefClassOfName(name, &why);
        byClass[cls].emplace_back(name, why);
        ++reasons[cls][why];
        all << reliefclass::Name(cls) << '\t' << why << '\t' << name << '\n';
    }
    std::ofstream summary(o.report / "summary.txt");
    summary << names.size() << " texture names from " << o.classify.string() << "\n";
    for (uint8_t cls = 0; cls < reliefclass::kCount; ++cls)
        summary << "  " << byClass[cls].size() << "\t" << reliefclass::Name(cls) << "\n";
    std::mt19937 random(kExampleSeed);
    for (uint8_t cls = 0; cls < reliefclass::kCount; ++cls)
    {
        auto list = byClass[cls];
        summary << "\n" << reliefclass::Name(cls) << ": " << list.size() << " textures\n  most common reasons:\n";
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto& [why, n] : reasons[cls])
            sorted.emplace_back(n, why);
        std::sort(sorted.rbegin(), sorted.rend());
        for (size_t k = 0; k < sorted.size() && k < kReasonsPerClass; ++k)
            summary << "    " << sorted[k].first << "\t" << sorted[k].second << "\n";
        std::shuffle(list.begin(), list.end(), random);
        summary << "  examples:\n";
        for (size_t k = 0; k < list.size() && k < kExamplesPerClass; ++k)
            summary << "    " << list[k].first << "\t(" << list[k].second << ")\n";
    }
    if (!o.compare.empty())
        CompareWithOld(o, summary);
    summary.close();
    std::cout << names.size() << " names classified:";
    for (uint8_t cls = 0; cls < reliefclass::kCount; ++cls)
        std::cout << " " << reliefclass::Name(cls) << " " << byClass[cls].size() << ";";
    std::cout << "\nreport in " << o.report.string() << "\n";
    return 0;
}

uint32_t MedianFor(const Options& o, const std::string& kind, uint8_t reliefClass)
{
    return ReliefMedianFor(kind == "ground", reliefClass, o.groundMedian, o.buildingMedian);
}

std::string MedianText(uint32_t median)
{
    return median >= 3 ? std::to_string(median) + "x" + std::to_string(median) : std::string("none");
}

int Pack(const Options& options)
{
    const auto started = std::chrono::steady_clock::now();
    std::vector<Row> rows;
    if (!options.heights.empty())
    {
        rows = ReadIndex(options.heights / "heights.tsv", options.kinds, options.limit);
        if (rows.empty())
        {
            std::cerr << "no rows of the asked kinds in " << (options.heights / "heights.tsv").string() << "\n";
            return 1;
        }
    }
    std::vector<std::string> typeOnly =
        options.classesOnly.empty() ? std::vector<std::string>() : ReadList(options.classesOnly, options.limit);
    ArchiveSet archives;
    if (!archives.Open(options.data, options.locale, !options.stock))
    {
        std::cerr << "cannot open the archives in " << options.data.string() << "\n";
        return 1;
    }
    if (options.modelTypes)
    {
        size_t listed = 0;
        size_t added = 0;
        for (const std::string& name : archives.ListedNames())
        {
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".BLP") != 0)
                continue;
            bool modelFolder = false;
            for (const char* folder : kModelFolders)
                modelFolder = modelFolder || name.rfind(folder, 0) == 0;
            if (!modelFolder)
                continue;
            ++listed;
            const uint8_t cls = ReliefClassOfName(name, nullptr);
            if (cls == reliefclass::kNone || cls == reliefclass::kCharacters)
                continue;
            typeOnly.push_back(name);
            ++added;
            if (options.limit && added >= options.limit)
                break;
        }
        std::cout << "model textures: " << listed << " listed in the creature, item and world folders, " << added
                  << " with a relief type other than characters\n";
    }
    const uint32_t levels = LevelsFor(options.size);
    const uint32_t heightBytes = relieffile::LevelBytes(options.size, levels);
    fs::path spoolPath = options.out;
    spoolPath += ".heights.tmp";
    std::fstream spool(spoolPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!spool)
    {
        std::cerr << "cannot write " << spoolPath.string() << "\n";
        return 1;
    }
    std::vector<Packed> packed;
    uint32_t spooled = 0;
    std::vector<uint64_t> spooledHashes;
    std::map<std::string, int> refused;
    std::map<std::string, int> formats;
    std::map<std::string, int> sources;
    std::map<uint32_t, int> medians;
    float spreadMiss = 0.0f;
    std::string spreadMissName;
    int spreadChecked = 0;
    for (const Row& row : rows)
    {
        std::string why;
        const uint8_t cls = ReliefClassOfName(row.name, &why);
        if (cls == reliefclass::kNone)
        {
            ++refused["no relief type"];
            continue;
        }
        const auto bytes = archives.Read(row.name);
        if (!bytes)
        {
            ++refused["not in the archives"];
            continue;
        }
        BlpLevel blp;
        if (!HashBlp(*bytes, blp, why))
        {
            ++refused[why];
            continue;
        }
        PngImage height;
        PngImage colour;
        if (!ReadPng((options.heights / (row.id + "-h.png")).string(), height, &why))
        {
            ++refused["height: " + why];
            continue;
        }
        if (!ReadPng((options.textures / (row.id + ".png")).string(), colour, &why))
        {
            ++refused["colour: " + why];
            continue;
        }
        Packed p = {};
        p.entry.hash = blp.hash;
        p.entry.format = blp.format;
        p.entry.width = static_cast<uint16_t>(blp.width);
        p.entry.height = static_cast<uint16_t>(blp.height);
        p.entry.heightIndex = spooled;
        p.spooledIndex = spooled;
        p.entry.reliefClass = cls;
        p.spread = LumaSpread(colour);
        p.entry.flatness = Flatness(p.spread);
        p.name = row.name;
        p.source = row.kind;
        BlpPixels pixels;
        if (DecodeBlp(*bytes, kSpreadSide, pixels, why))
        {
            const float miss = std::fabs(LumaSpread(pixels, false) - p.spread);
            ++spreadChecked;
            if (miss > spreadMiss)
            {
                spreadMiss = miss;
                spreadMissName = row.name;
            }
        }
        const uint32_t median = MedianFor(options, row.kind, cls);
        ++medians[median];
        const std::vector<uint8_t> levelsOut = HeightLevels(height, options.size, median);
        if (levelsOut.size() != heightBytes)
        {
            std::cerr << "a height map has " << levelsOut.size() << " bytes, expected " << heightBytes << "\n";
            return 1;
        }
        spool.write(reinterpret_cast<const char*>(levelsOut.data()), static_cast<std::streamsize>(heightBytes));
        spooledHashes.push_back(relieffile::Fnv1a(levelsOut.data(), levelsOut.size()));
        ++spooled;
        packed.push_back(p);
        ++formats[BlpFormatName(blp.format)];
        ++sources[row.kind];
    }
    for (const std::string& name : typeOnly)
    {
        std::string why;
        const uint8_t cls = ReliefClassOfName(name, &why);
        if (cls == reliefclass::kNone)
        {
            ++refused["type only: no relief type"];
            continue;
        }
        const auto bytes = archives.Read(name);
        if (!bytes)
        {
            ++refused["type only: not in the archives"];
            continue;
        }
        BlpLevel blp;
        BlpPixels pixels;
        if (!HashBlp(*bytes, blp, why) || !DecodeBlp(*bytes, kSpreadSide, pixels, why))
        {
            ++refused["type only: " + why];
            continue;
        }
        Packed p = {};
        p.entry.hash = blp.hash;
        p.entry.format = blp.format;
        p.entry.width = static_cast<uint16_t>(blp.width);
        p.entry.height = static_cast<uint16_t>(blp.height);
        p.entry.heightIndex = relieffile::kNoHeights;
        p.entry.reliefClass = cls;
        p.spread = LumaSpread(pixels, true);
        p.entry.flatness = Flatness(p.spread);
        p.name = name;
        p.source = kTypeOnly;
        packed.push_back(p);
        ++formats[BlpFormatName(blp.format)];
        ++sources[kTypeOnly];
    }
    if (!spool)
    {
        std::cerr << "cannot write " << spoolPath.string() << "\n";
        return 1;
    }
    std::stable_sort(packed.begin(), packed.end(),
                     [](const Packed& a, const Packed& b) { return relieffile::KeyLess(a.entry, b.entry); });
    std::vector<Packed> unique;
    int duplicates = 0;
    for (const Packed& p : packed)
    {
        if (!unique.empty() && !relieffile::KeyLess(unique.back().entry, p.entry))
        {
            ++duplicates;
            continue;
        }
        unique.push_back(p);
    }
    std::vector<uint32_t> remap(spooled, relieffile::kNoHeights);
    std::vector<uint32_t> keptFrom;
    for (Packed& p : unique)
    {
        if (!relieffile::HasHeights(p.entry))
            continue;
        uint32_t& index = remap[p.entry.heightIndex];
        if (index == relieffile::kNoHeights)
        {
            index = static_cast<uint32_t>(keptFrom.size());
            keptFrom.push_back(p.entry.heightIndex);
        }
        p.entry.heightIndex = index;
    }
    relieffile::Header header = {};
    std::memcpy(header.magic, relieffile::kMagic, 4);
    header.version = relieffile::kVersion;
    header.entryCount = static_cast<uint32_t>(unique.size());
    header.heightCount = static_cast<uint32_t>(keptFrom.size());
    header.heightSize = options.size;
    header.levelCount = levels;
    header.heightBytes = heightBytes;
    std::vector<uint64_t> mapHashes;
    {
        std::ofstream out(options.out, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header), sizeof header);
        for (const Packed& p : unique)
            out.write(reinterpret_cast<const char*>(&p.entry), sizeof p.entry);
        std::vector<uint8_t> map(heightBytes);
        for (uint32_t from : keptFrom)
        {
            spool.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(from) * heightBytes));
            spool.read(reinterpret_cast<char*>(map.data()), static_cast<std::streamsize>(heightBytes));
            mapHashes.push_back(relieffile::Fnv1a(map.data(), map.size()));
            out.write(reinterpret_cast<const char*>(map.data()), static_cast<std::streamsize>(heightBytes));
        }
        if (!spool || !out)
        {
            std::cerr << "cannot write " << options.out.string() << "\n";
            return 1;
        }
    }
    spool.close();
    std::error_code ec;
    fs::remove(spoolPath, ec);
    ReliefCatalog check;
    if (!check.Load(options.out.string()))
    {
        std::cerr << "the fog DLL's reader rejects the file\n";
        return 1;
    }
    std::vector<uint8_t> back;
    for (const Packed& p : unique)
    {
        const relieffile::Entry* e = check.Find(p.entry.hash, p.entry.format, p.entry.width, p.entry.height);
        if (!e || e->reliefClass != p.entry.reliefClass || e->flatness != p.entry.flatness ||
            relieffile::HasHeights(*e) != (p.spooledIndex != relieffile::kNoHeights))
        {
            std::cerr << "the fog DLL's reader cannot find " << p.name << "\n";
            return 1;
        }
        if (relieffile::HasHeights(*e) &&
            (!check.ReadHeights(e->heightIndex, back) ||
             relieffile::Fnv1a(back.data(), back.size()) != spooledHashes[p.spooledIndex]))
        {
            std::cerr << "the fog DLL's reader finds another height map for " << p.name << "\n";
            return 1;
        }
    }
    for (uint32_t k = 0; k < mapHashes.size(); ++k)
        if (!check.ReadHeights(k, back) || relieffile::Fnv1a(back.data(), back.size()) != mapHashes[k])
        {
            std::cerr << "the fog DLL's reader reads height map " << k << " wrong\n";
            return 1;
        }
    fs::path listPath = options.out;
    listPath.replace_extension(".tsv");
    std::ofstream list(listPath);
    list << "hash\tformat\tlevel_w\tlevel_h\tsource\tclass\tluma_spread\tflatness\tdepth\theight\tname\n";
    std::map<std::string, int> classes;
    for (const Packed& p : unique)
    {
        list << HashText(p.entry.hash) << '\t' << BlpFormatName(p.entry.format) << '\t' << p.entry.width << '\t'
             << p.entry.height << '\t' << p.source << '\t' << reliefclass::Name(p.entry.reliefClass) << '\t'
             << p.spread << '\t' << p.entry.flatness << '\t'
             << p.entry.flatness * reliefclass::DefaultShare(p.entry.reliefClass) << '\t';
        if (relieffile::HasHeights(p.entry))
            list << p.entry.heightIndex;
        else
            list << '-';
        list << '\t' << p.name << '\n';
        ++classes[reliefclass::Name(p.entry.reliefClass)];
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << rows.size() + typeOnly.size() << " textures asked: " << unique.size() << " packed, " << duplicates
              << " duplicates of a packed one";
    for (const auto& [why, n] : refused)
        std::cout << ", " << n << " " << why;
    std::cout << "\nsources:";
    for (const auto& [name, n] : sources)
        std::cout << " " << name << " " << n;
    std::cout << "\nformats:";
    for (const auto& [name, n] : formats)
        std::cout << " " << name << " " << n;
    std::cout << "\ntypes:";
    for (const auto& [name, n] : classes)
        std::cout << " " << name << " " << n;
    std::cout << "\n" << header.heightCount << " height maps of " << options.size << "x" << options.size << " ("
              << levels << " levels), median " << MedianText(options.groundMedian)
              << " on the ground and on building textures of a ground type, " << MedianText(options.buildingMedian)
              << " on the other building textures (" << medians[options.groundMedian] << " and "
              << (options.groundMedian == options.buildingMedian ? 0 : medians[options.buildingMedian]) << " maps), "
              << (fs::file_size(options.out, ec) + 512 * 1024) / (1024 * 1024) << " MB in " << options.out.string()
              << "\nthe colour's luma spread from the BLP's own pixels against the relief-lab PNG: largest miss "
              << spreadMiss << " over " << spreadChecked << " textures" << (spreadMissName.empty() ? "" : " (")
              << spreadMissName << (spreadMissName.empty() ? "" : ")") << "\n"
              << archives.FilesRead() << " files, " << archives.BytesRead() / (1024 * 1024) << " MB read from the "
              << "archives in " << seconds << " s\nthe fog DLL's reader accepts it\n";
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
    if (!options.classify.empty())
        return Classify(options);
    if (!options.hashOnly.empty())
        return PrintHashes(options);
    return Pack(options);
}
