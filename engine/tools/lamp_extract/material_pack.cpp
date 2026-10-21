#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "archive_set.h"
#include "blp_hash.h"
#include "material_catalog.h"
#include "material_data.h"
#include "material_rules.h"
#include "relief_data.h"

namespace fs = std::filesystem;

namespace
{

struct Options
{
    fs::path data;
    fs::path names;
    fs::path list;
    fs::path overrides;
    fs::path out;
    std::string locale = "enUS";
    std::string kind = "ground";
    bool stock = false;
};

struct Row
{
    std::string name;
    uint64_t hash = 0;
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t kind = materialfile::kUnknown;
    std::string word;
};

void PrintUsage()
{
    std::cerr << "material_pack --list <reliefdata.tsv> --out <materialdata.bin> [--overrides <name<TAB>kind tsv>]\n"
                 "material_pack --data <client Data folder> --names <tsv with kind and name columns>\n"
                 "              --out <materialdata.bin> [--kind ground|building|all] [--overrides <tsv>]\n"
                 "              [--locale enUS] [--stock]\n"
                 "kinds: grass soil stone sand snow hot\n";
}

bool ParseOptions(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--stock")
        {
            o.stock = true;
            continue;
        }
        if (i + 1 >= argc)
            return false;
        const std::string value = argv[++i];
        if (arg == "--data")
            o.data = value;
        else if (arg == "--names")
            o.names = value;
        else if (arg == "--list")
            o.list = value;
        else if (arg == "--overrides")
            o.overrides = value;
        else if (arg == "--out")
            o.out = value;
        else if (arg == "--locale")
            o.locale = value;
        else if (arg == "--kind")
            o.kind = value;
        else
            return false;
    }
    const bool fromList = !o.list.empty() && o.data.empty() && o.names.empty();
    const bool fromArchives = o.list.empty() && !o.data.empty() && !o.names.empty();
    return !o.out.empty() && (fromList || fromArchives);
}

std::vector<std::string> Split(const std::string& line)
{
    std::vector<std::string> cells;
    std::stringstream s(line);
    std::string cell;
    while (std::getline(s, cell, '\t'))
        cells.push_back(cell);
    return cells;
}

bool ReadTable(const fs::path& path, std::vector<std::map<std::string, std::string>>& rows)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    if (!std::getline(f, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    const std::vector<std::string> header = Split(line);
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const std::vector<std::string> cells = Split(line);
        std::map<std::string, std::string> row;
        for (size_t k = 0; k < header.size() && k < cells.size(); ++k)
            row[header[k]] = cells[k];
        rows.push_back(row);
    }
    return true;
}

uint32_t FormatOf(const std::string& name)
{
    if (name == "DXT1")
        return relieffile::kFormatDxt1;
    if (name == "DXT3")
        return relieffile::kFormatDxt3;
    if (name == "DXT5")
        return relieffile::kFormatDxt5;
    if (name == "ARGB")
        return relieffile::kFormatArgb;
    return 0;
}

bool ReadOverrides(const fs::path& path, std::map<std::string, uint8_t>& out)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        const std::vector<std::string> cells = Split(line);
        const uint8_t kind = cells.size() == 2 ? materialfile::KindFromName(cells[1].c_str()) : materialfile::kUnknown;
        if (kind == materialfile::kUnknown)
        {
            std::cerr << "override line not understood: " << line << "\n";
            return false;
        }
        out[MaterialBaseName(cells[0])] = kind;
    }
    return true;
}

std::string BaseFileOf(const std::string& name)
{
    static const std::string suffix = "_S.BLP";
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper.size() > suffix.size() && upper.compare(upper.size() - suffix.size(), suffix.size(), suffix) == 0)
        return name.substr(0, name.size() - suffix.size()) + ".BLP";
    return std::string();
}

void Classify(Row& row, const std::map<std::string, uint8_t>& overrides)
{
    auto it = overrides.find(MaterialBaseName(row.name));
    if (it != overrides.end())
    {
        row.kind = it->second;
        row.word = "(override)";
        return;
    }
    row.kind = MaterialKindOfName(row.name, &row.word);
}

}

int main(int argc, char** argv)
{
    Options o;
    if (!ParseOptions(argc, argv, o))
    {
        PrintUsage();
        return 2;
    }
    std::map<std::string, uint8_t> overrides;
    if (!o.overrides.empty() && !ReadOverrides(o.overrides, overrides))
    {
        std::cerr << "cannot read the overrides " << o.overrides.string() << "\n";
        return 1;
    }
    std::vector<Row> rows;
    std::map<std::string, int> refused;
    if (!o.list.empty())
    {
        std::vector<std::map<std::string, std::string>> table;
        if (!ReadTable(o.list, table))
        {
            std::cerr << "cannot read " << o.list.string() << "\n";
            return 1;
        }
        for (auto& r : table)
        {
            Row row;
            row.name = r["name"];
            row.hash = std::strtoull(r["hash"].c_str(), nullptr, 16);
            row.format = FormatOf(r["format"]);
            row.width = static_cast<uint32_t>(std::strtoul(r["level_w"].c_str(), nullptr, 10));
            row.height = static_cast<uint32_t>(std::strtoul(r["level_h"].c_str(), nullptr, 10));
            if (row.name.empty() || !row.format || !row.width || !row.height)
            {
                ++refused["incomplete list row"];
                continue;
            }
            rows.push_back(row);
        }
    }
    else
    {
        std::vector<std::map<std::string, std::string>> table;
        if (!ReadTable(o.names, table))
        {
            std::cerr << "cannot read " << o.names.string() << "\n";
            return 1;
        }
        ArchiveSet archives;
        if (!archives.Open(o.data, o.locale, !o.stock))
        {
            std::cerr << "cannot open the archives in " << o.data.string() << "\n";
            return 1;
        }
        for (auto& r : table)
        {
            if (o.kind != "all" && r["kind"] != o.kind)
                continue;
            std::vector<std::string> files = {r["name"]};
            const std::string base = BaseFileOf(r["name"]);
            if (!base.empty())
                files.push_back(base);
            for (const std::string& file : files)
            {
                const auto bytes = archives.Read(file);
                if (!bytes)
                {
                    ++refused[file == r["name"] ? "not in the archives" : "no file without _S"];
                    continue;
                }
                BlpLevel blp;
                std::string why;
                if (!HashBlp(*bytes, blp, why))
                {
                    ++refused[why];
                    continue;
                }
                Row row;
                row.name = file;
                row.hash = blp.hash;
                row.format = blp.format;
                row.width = blp.width;
                row.height = blp.height;
                rows.push_back(row);
            }
        }
    }
    std::map<std::string, int> kinds;
    std::vector<materialfile::Entry> entries;
    std::vector<const Row*> kept;
    for (Row& row : rows)
    {
        Classify(row, overrides);
        ++kinds[materialfile::KindName(row.kind)];
        if (row.kind == materialfile::kUnknown)
            continue;
        materialfile::Entry e = {};
        e.hash = row.hash;
        e.format = row.format;
        e.width = static_cast<uint16_t>(row.width);
        e.height = static_cast<uint16_t>(row.height);
        e.kind = row.kind;
        entries.push_back(e);
        kept.push_back(&row);
    }
    std::vector<size_t> order(entries.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return materialfile::KeyLess(entries[a], entries[b]); });
    std::vector<materialfile::Entry> unique;
    int duplicates = 0;
    int clashes = 0;
    for (size_t i : order)
    {
        if (!unique.empty() && !materialfile::KeyLess(unique.back(), entries[i]))
        {
            ++duplicates;
            if (unique.back().kind != entries[i].kind)
                ++clashes;
            continue;
        }
        unique.push_back(entries[i]);
    }
    if (unique.empty())
    {
        std::cerr << "no texture classified\n";
        return 1;
    }
    materialfile::Header h = {};
    std::memcpy(h.magic, materialfile::kMagic, 4);
    h.version = materialfile::kVersion;
    h.entryCount = static_cast<uint32_t>(unique.size());
    std::vector<uint8_t> file(sizeof h);
    std::memcpy(file.data(), &h, sizeof h);
    for (const materialfile::Entry& e : unique)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&e);
        file.insert(file.end(), p, p + sizeof e);
    }
    MaterialCatalog check;
    if (!check.Parse(file.data(), file.size(), "material_pack"))
    {
        std::cerr << "the fog DLL's reader rejects the file\n";
        return 1;
    }
    for (const Row* row : kept)
    {
        const materialfile::Entry* found = check.Find(row->hash, row->format, row->width, row->height);
        if (!found)
        {
            std::cerr << "the fog DLL's reader cannot find " << row->name << "\n";
            return 1;
        }
    }
    std::ofstream out(o.out, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out)
    {
        std::cerr << "cannot write " << o.out.string() << "\n";
        return 1;
    }
    out.close();
    fs::path listPath = o.out;
    listPath.replace_extension(".tsv");
    std::ofstream list(listPath);
    list << "hash\tformat\tlevel_w\tlevel_h\tkind\tword\tname\n";
    for (const Row& row : rows)
    {
        char hash[17];
        std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(row.hash));
        list << hash << '\t' << BlpFormatName(row.format) << '\t' << row.width << '\t' << row.height << '\t'
             << materialfile::KindName(row.kind) << '\t' << row.word << '\t' << row.name << '\n';
    }
    std::cout << rows.size() << " textures read, " << unique.size() << " classified in " << o.out.string() << " ("
              << duplicates << " duplicates, " << clashes << " of them with another kind)";
    for (const auto& [why, n] : refused)
        std::cout << ", " << n << " " << why;
    std::cout << "\nkinds:";
    for (const auto& [name, n] : kinds)
        std::cout << " " << name << " " << n;
    std::cout << "\nthe fog DLL's reader accepts it\n";
    return 0;
}
