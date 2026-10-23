#include "liquid_scan.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>

#include "archive_set.h"
#include "byte_view.h"
#include "dbc_table.h"

namespace
{

constexpr uint32_t kMainHasAdt = 0x01;
constexpr size_t kMainEntrySize = 8;
constexpr uint32_t kLiquidTypeSoundBankField = 3;
constexpr uint32_t kSoundBankWater = 0;
constexpr uint32_t kSoundBankOcean = 1;
constexpr uint16_t kVertexFormatDepthOnly = 2;
constexpr size_t kHeaderEntrySize = 12;
constexpr size_t kInstanceSize = 24;
constexpr size_t kMcnkFlags = 0x00;
constexpr size_t kMcnkIndexX = 0x04;
constexpr size_t kMcnkIndexY = 0x08;
constexpr size_t kMcnkOldLiquidOffset = 0x60;
constexpr size_t kMcnkOldLiquidSize = 0x64;
constexpr uint32_t kEmptyOldLiquid = 8;
constexpr uint32_t kOldRiver = 0x04;
constexpr uint32_t kOldOcean = 0x08;
constexpr uint32_t kOldMagma = 0x10;
constexpr uint32_t kOldSlime = 0x20;
constexpr size_t kOldVertexSide = 9;
constexpr size_t kOldVertexSize = 8;
constexpr size_t kOldVertexHeight = 4;
constexpr size_t kOldVertices = 8;
constexpr size_t kOldTiles = kOldVertices + kOldVertexSide * kOldVertexSide * kOldVertexSize;
constexpr size_t kOldLayerSize = kOldTiles + 64 + 4 + 80;
constexpr uint8_t kOldTileHidden = 0x0F;

struct Instance
{
    uint16_t type;
    uint16_t vertexFormat;
    float minHeight;
    float maxHeight;
    uint8_t offsetX;
    uint8_t offsetY;
    uint8_t width;
    uint8_t height;
    uint32_t bitmapOffset;
    uint32_t vertexOffset;
};

Instance ReadInstance(ByteView water, size_t at)
{
    Instance i = {};
    i.type = water.Get<uint16_t>(at);
    i.vertexFormat = water.Get<uint16_t>(at + 2);
    i.minHeight = water.Get<float>(at + 4);
    i.maxHeight = water.Get<float>(at + 8);
    i.offsetX = water.Get<uint8_t>(at + 12);
    i.offsetY = water.Get<uint8_t>(at + 13);
    i.width = water.Get<uint8_t>(at + 14);
    i.height = water.Get<uint8_t>(at + 15);
    i.bitmapOffset = water.Get<uint32_t>(at + 16);
    i.vertexOffset = water.Get<uint32_t>(at + 20);
    return i;
}

uint64_t CellsOf(ByteView water, const Instance& i)
{
    uint64_t cells = 0;
    int bit = 0;
    for (int y = 0; y < i.height; ++y)
    {
        for (int x = 0; x < i.width; ++x, ++bit)
        {
            const bool exists =
                i.bitmapOffset == 0 || ((water.Get<uint8_t>(i.bitmapOffset + bit / 8) >> (bit % 8)) & 1);
            if (exists)
                cells |= 1ull << liquidfile::CellBit(i.offsetX + x, i.offsetY + y);
        }
    }
    return cells;
}

float OldSurface(ByteView layer)
{
    double sum = 0.0;
    int count = 0;
    for (size_t v = 0; v < kOldVertexSide * kOldVertexSide; ++v)
    {
        float h = 0.0f;
        if (layer.Read(kOldVertices + v * kOldVertexSize + kOldVertexHeight, h) && std::isfinite(h) &&
            std::fabs(h) < 1e5f)
        {
            sum += h;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.5f * (layer.Get<float>(0) + layer.Get<float>(4));
}

bool OldLiquid(ByteView mcnk, liquidfile::Chunk& out, MapLiquids& stats)
{
    const uint32_t size = mcnk.Get<uint32_t>(kMcnkOldLiquidSize);
    const uint32_t offset = mcnk.Get<uint32_t>(kMcnkOldLiquidOffset);
    if (size <= kEmptyOldLiquid || offset == 0)
        return false;
    const uint32_t flags = mcnk.Get<uint32_t>(kMcnkFlags);
    const ByteView data = mcnk.From(offset);
    size_t at = 0;
    for (uint32_t kind : {kOldRiver, kOldOcean, kOldMagma, kOldSlime})
    {
        if (!(flags & kind))
            continue;
        const ByteView layer = data.Slice(at, kOldLayerSize);
        at += kOldLayerSize;
        if (layer.Empty())
            return false;
        if (kind == kOldMagma || kind == kOldSlime)
        {
            ++stats.magmaOrSlimeChunks;
            continue;
        }
        uint64_t cells = 0;
        for (int y = 0; y < liquidfile::kCellsPerChunk; ++y)
            for (int x = 0; x < liquidfile::kCellsPerChunk; ++x)
                if ((layer.Get<uint8_t>(kOldTiles + y * liquidfile::kCellsPerChunk + x) & kOldTileHidden) !=
                    kOldTileHidden)
                    cells |= 1ull << liquidfile::CellBit(x, y);
        if (cells == 0)
            continue;
        out.surface = OldSurface(layer);
        out.cells = cells;
        out.kind = kind == kOldOcean ? liquidfile::kSea : liquidfile::kLake;
        return true;
    }
    return false;
}

float SurfaceOf(ByteView water, const Instance& i)
{
    if (i.vertexFormat != kVertexFormatDepthOnly && i.vertexOffset != 0)
    {
        const int vertices = (i.width + 1) * (i.height + 1);
        double sum = 0.0;
        int count = 0;
        for (int v = 0; v < vertices; ++v)
        {
            float h = 0.0f;
            if (water.Read(i.vertexOffset + static_cast<size_t>(v) * 4, h) && std::isfinite(h) && std::fabs(h) < 1e5f)
            {
                sum += h;
                ++count;
            }
        }
        if (count > 0)
            return static_cast<float>(sum / count);
    }
    return 0.5f * (i.minHeight + i.maxHeight);
}

}

bool LiquidScanner::LoadTypes()
{
    DbcTable types;
    std::optional<std::vector<uint8_t>> file = m_archives.Read("DBFilesClient\\LiquidType.dbc");
    if (!file || !types.Load(std::move(*file)) || types.Fields() <= kLiquidTypeSoundBankField)
        return false;
    for (uint32_t row = 0; row < types.Rows(); ++row)
        m_soundBanks[types.U32(row, 0)] = types.U32(row, kLiquidTypeSoundBankField);
    return true;
}

void LiquidScanner::ScanAdt(const std::string& path, int tileX, int tileY, MapLiquids& out) const
{
    std::optional<std::vector<uint8_t>> file = m_archives.Read(path);
    if (file)
        ScanAdtBytes(*file, tileX, tileY, out);
}

void AddTileLiquids(MapLiquids& map, const MapLiquids& tile)
{
    map.chunks.insert(map.chunks.end(), tile.chunks.begin(), tile.chunks.end());
    map.tiles += tile.tiles;
    map.tilesWithLiquid += tile.tilesWithLiquid;
    map.magmaOrSlimeChunks += tile.magmaOrSlimeChunks;
    map.oldLiquidChunks += tile.oldLiquidChunks;
    map.unknownTypes += tile.unknownTypes;
}

void SortLiquids(MapLiquids& map)
{
    std::sort(map.chunks.begin(), map.chunks.end(),
              [](const liquidfile::Chunk& a, const liquidfile::Chunk& b) { return a.key < b.key; });
}

void LiquidScanner::ScanAdtBytes(const std::vector<uint8_t>& file, int tileX, int tileY, MapLiquids& out) const
{
    ++out.tiles;
    ByteView water;
    const std::vector<Chunk> chunks = ChunksOf(ByteView(file));
    for (const Chunk& chunk : chunks)
        if (chunk.id == ChunkId("MH2O"))
            water = chunk.body;
    bool any = false;
    bool covered[liquidfile::kChunksPerTile * liquidfile::kChunksPerTile] = {};
    for (int index = 0; !water.Empty() && index < liquidfile::kChunksPerTile * liquidfile::kChunksPerTile; ++index)
    {
        const uint32_t instances = water.Get<uint32_t>(index * kHeaderEntrySize);
        const uint32_t layers = water.Get<uint32_t>(index * kHeaderEntrySize + 4);
        if (instances == 0 || layers == 0)
            continue;
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            const size_t at = instances + static_cast<size_t>(layer) * kInstanceSize;
            if (!water.Contains(at, kInstanceSize))
                break;
            const Instance i = ReadInstance(water, at);
            if (i.offsetX + i.width > liquidfile::kCellsPerChunk || i.offsetY + i.height > liquidfile::kCellsPerChunk)
                continue;
            const auto bank = m_soundBanks.find(i.type);
            if (bank == m_soundBanks.end() && i.vertexFormat != kVertexFormatDepthOnly)
            {
                ++out.unknownTypes;
                continue;
            }
            const bool sea = i.vertexFormat == kVertexFormatDepthOnly || bank->second == kSoundBankOcean;
            if (!sea && bank->second != kSoundBankWater)
            {
                ++out.magmaOrSlimeChunks;
                continue;
            }
            const uint64_t cells = CellsOf(water, i);
            if (cells == 0)
                continue;
            liquidfile::Chunk c = {};
            c.key = liquidfile::ChunkKey(tileX, tileY, index % liquidfile::kChunksPerTile,
                                         index / liquidfile::kChunksPerTile);
            c.surface = SurfaceOf(water, i);
            c.cells = cells;
            c.kind = sea ? liquidfile::kSea : liquidfile::kLake;
            out.chunks.push_back(c);
            covered[index] = true;
            any = true;
            break;
        }
    }
    for (const Chunk& chunk : chunks)
    {
        if (chunk.id != ChunkId("MCNK"))
            continue;
        const uint32_t x = chunk.body.Get<uint32_t>(kMcnkIndexX);
        const uint32_t y = chunk.body.Get<uint32_t>(kMcnkIndexY);
        if (x >= liquidfile::kChunksPerTile || y >= liquidfile::kChunksPerTile ||
            covered[y * liquidfile::kChunksPerTile + x])
            continue;
        liquidfile::Chunk c = {};
        if (!OldLiquid(chunk.body, c, out))
            continue;
        c.key = liquidfile::ChunkKey(tileX, tileY, static_cast<int>(x), static_cast<int>(y));
        out.chunks.push_back(c);
        ++out.oldLiquidChunks;
        any = true;
    }
    out.tilesWithLiquid += any ? 1 : 0;
}

MapLiquids LiquidScanner::ScanMap(const MapInfo& map) const
{
    MapLiquids out;
    out.mapId = map.id;
    out.directory = map.directory;
    const std::string base = "World\\Maps\\" + map.directory + "\\" + map.directory;
    std::optional<std::vector<uint8_t>> wdtFile = m_archives.Read(base + ".wdt");
    if (!wdtFile)
        return out;
    ByteView main = FindChunk(ChunksOf(ByteView(*wdtFile)), ChunkId("MAIN"));
    for (int y = 0; y < liquidfile::kTilesPerSide; ++y)
    {
        for (int x = 0; x < liquidfile::kTilesPerSide; ++x)
        {
            const size_t entry = static_cast<size_t>(y * liquidfile::kTilesPerSide + x) * kMainEntrySize;
            if (!(main.Get<uint32_t>(entry) & kMainHasAdt))
                continue;
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "_%d_%d.adt", x, y);
            ScanAdt(base + suffix, x, y, out);
        }
    }
    SortLiquids(out);
    return out;
}

bool WriteLiquidFile(const std::filesystem::path& path, const std::vector<MapLiquids>& maps)
{
    liquidfile::Header header = {};
    std::copy(std::begin(liquidfile::kMagic), std::end(liquidfile::kMagic), header.magic);
    header.version = liquidfile::kVersion;
    std::vector<liquidfile::Map> records;
    std::vector<liquidfile::Chunk> chunks;
    for (const MapLiquids& m : maps)
    {
        if (m.chunks.empty())
            continue;
        records.push_back({m.mapId, static_cast<uint32_t>(chunks.size()), static_cast<uint32_t>(m.chunks.size()), 0});
        chunks.insert(chunks.end(), m.chunks.begin(), m.chunks.end());
    }
    header.mapCount = static_cast<uint32_t>(records.size());
    header.chunkCount = static_cast<uint32_t>(chunks.size());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof header);
    file.write(reinterpret_cast<const char*>(records.data()),
               static_cast<std::streamsize>(records.size() * sizeof records[0]));
    file.write(reinterpret_cast<const char*>(chunks.data()),
               static_cast<std::streamsize>(chunks.size() * sizeof chunks[0]));
    return static_cast<bool>(file);
}
