#include "liquid_map.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
bool HeaderValid(const liquidfile::Header& h, size_t size)
{
    return std::memcmp(h.magic, liquidfile::kMagic, 4) == 0 && h.version == liquidfile::kVersion &&
           size == sizeof h + static_cast<size_t>(h.mapCount) * sizeof(liquidfile::Map) +
                       static_cast<size_t>(h.chunkCount) * sizeof(liquidfile::Chunk);
}

bool ChunksValid(const std::vector<liquidfile::Map>& maps, const std::vector<liquidfile::Chunk>& chunks)
{
    const uint32_t keys = liquidfile::kChunksPerSide * liquidfile::kChunksPerSide;
    for (const liquidfile::Map& m : maps)
    {
        if (m.firstChunk > chunks.size() || m.chunkCount > chunks.size() - m.firstChunk)
            return false;
        for (uint32_t i = m.firstChunk; i < m.firstChunk + m.chunkCount; ++i)
        {
            const liquidfile::Chunk& c = chunks[i];
            if (c.key >= keys || (c.kind != liquidfile::kLake && c.kind != liquidfile::kSea) ||
                !std::isfinite(c.surface) || c.cells == 0)
                return false;
            if (i > m.firstChunk && !(chunks[i - 1].key < c.key))
                return false;
        }
    }
    return true;
}
}

bool LiquidMap::Load(const std::string& path)
{
    Clear();
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        VF_LOG_INFO("no liquid data at %s; fog banks take all ground for land", path.c_str());
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!Parse(bytes.data(), bytes.size(), path.c_str()))
    {
        VF_LOG_ERROR("liquid data %s is unknown, truncated or out of order; fog banks take all ground for land",
                     path.c_str());
        return false;
    }
    size_t lakes = 0;
    for (const liquidfile::Chunk& c : m_chunks)
        lakes += c.kind == liquidfile::kLake ? 1 : 0;
    VF_LOG_INFO("liquid data: %zu maps, %zu chunks with water (%zu lakes and rivers, %zu sea) from %s", m_maps.size(),
                m_chunks.size(), lakes, m_chunks.size() - lakes, path.c_str());
    return true;
}

bool LiquidMap::Parse(const void* bytes, size_t size, const char* source)
{
    Clear();
    liquidfile::Header h = {};
    if (!bytes || size < sizeof h)
        return false;
    std::memcpy(&h, bytes, sizeof h);
    if (!HeaderValid(h, size))
        return false;
    const auto* at = static_cast<const uint8_t*>(bytes) + sizeof h;
    std::vector<liquidfile::Map> maps(h.mapCount);
    std::memcpy(maps.data(), at, maps.size() * sizeof maps[0]);
    at += maps.size() * sizeof maps[0];
    std::vector<liquidfile::Chunk> chunks(h.chunkCount);
    std::memcpy(chunks.data(), at, chunks.size() * sizeof chunks[0]);
    if (!ChunksValid(maps, chunks))
        return false;
    m_maps = std::move(maps);
    m_chunks = std::move(chunks);
    (void)source;
    return true;
}

void LiquidMap::Clear()
{
    m_maps.clear();
    m_chunks.clear();
}

bool LiquidMap::At(int mapId, float x, float y, LiquidAt& liquid) const
{
    liquidfile::Place place = {};
    if (!liquidfile::PlaceOf(x, y, place))
        return false;
    for (const liquidfile::Map& m : m_maps)
    {
        if (static_cast<int>(m.mapId) != mapId)
            continue;
        const auto first = m_chunks.begin() + m.firstChunk;
        const auto last = first + m.chunkCount;
        const auto found = std::lower_bound(
            first, last, place.key, [](const liquidfile::Chunk& c, uint32_t key) { return c.key < key; });
        if (found == last || found->key != place.key || !((found->cells >> place.bit) & 1u))
            return false;
        liquid.surface = found->surface;
        liquid.lake = found->kind == liquidfile::kLake ? 1.0f : 0.0f;
        liquid.sea = found->kind == liquidfile::kSea ? 1.0f : 0.0f;
        return true;
    }
    return false;
}

bool LiquidMap::Probe(void* context, int mapId, float x, float y, LiquidAt& liquid)
{
    return static_cast<const LiquidMap*>(context)->At(mapId, x, y, liquid);
}

LiquidMap& GlobalLiquidMap()
{
    static LiquidMap map;
    return map;
}
