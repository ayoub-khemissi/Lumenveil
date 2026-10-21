#pragma once

#include "liquid_data.h"
#include "terrain_shadow.h"

#include <cstddef>
#include <string>
#include <vector>

class LiquidMap
{
public:
    bool Load(const std::string& path);
    bool Parse(const void* bytes, size_t size, const char* source);
    void Clear();
    bool Loaded() const { return !m_chunks.empty(); }
    size_t MapCount() const { return m_maps.size(); }
    size_t ChunkCount() const { return m_chunks.size(); }
    bool At(int mapId, float x, float y, LiquidAt& liquid) const;
    static bool Probe(void* context, int mapId, float x, float y, LiquidAt& liquid);

private:
    std::vector<liquidfile::Map> m_maps;
    std::vector<liquidfile::Chunk> m_chunks;
};

LiquidMap& GlobalLiquidMap();
