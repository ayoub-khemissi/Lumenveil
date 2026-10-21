#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "liquid_data.h"
#include "world_scan.h"

class ArchiveSet;

struct MapLiquids
{
    uint32_t mapId = 0;
    std::string directory;
    std::vector<liquidfile::Chunk> chunks;
    uint32_t tiles = 0;
    uint32_t tilesWithLiquid = 0;
    uint32_t magmaOrSlimeChunks = 0;
    uint32_t oldLiquidChunks = 0;
    uint32_t unknownTypes = 0;
};

class LiquidScanner
{
public:
    explicit LiquidScanner(const ArchiveSet& archives) : m_archives(archives) {}

    bool LoadTypes();
    size_t TypeCount() const { return m_soundBanks.size(); }
    MapLiquids ScanMap(const MapInfo& map) const;

private:
    void ScanAdt(const std::string& path, int tileX, int tileY, MapLiquids& out) const;

    const ArchiveSet& m_archives;
    std::unordered_map<uint32_t, uint32_t> m_soundBanks;
};

bool WriteLiquidFile(const std::filesystem::path& path, const std::vector<MapLiquids>& maps);
