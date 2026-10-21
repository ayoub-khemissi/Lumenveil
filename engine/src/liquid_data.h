#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace liquidfile
{

constexpr char kMagic[4] = {'V', 'F', 'L', 'Q'};
constexpr uint32_t kVersion = 1;
constexpr int kTilesPerSide = 64;
constexpr int kChunksPerTile = 16;
constexpr int kCellsPerChunk = 8;
constexpr int kChunksPerSide = kTilesPerSide * kChunksPerTile;
constexpr double kTileYards = 1600.0 / 3.0;
constexpr double kMapHalfExtent = kTileYards * kTilesPerSide / 2.0;
constexpr double kChunkYards = kTileYards / kChunksPerTile;
constexpr double kCellYards = kChunkYards / kCellsPerChunk;
constexpr uint8_t kLake = 1;
constexpr uint8_t kSea = 2;

#pragma pack(push, 1)

struct Header
{
    char magic[4];
    uint32_t version;
    uint32_t mapCount;
    uint32_t chunkCount;
};

struct Map
{
    uint32_t mapId;
    uint32_t firstChunk;
    uint32_t chunkCount;
    uint32_t reserved;
};

struct Chunk
{
    uint32_t key;
    float surface;
    uint64_t cells;
    uint8_t kind;
    uint8_t reserved[3];
};

#pragma pack(pop)

static_assert(sizeof(Header) == 16, "liquid file header");
static_assert(sizeof(Map) == 16, "liquid file map");
static_assert(sizeof(Chunk) == 20, "liquid file chunk");

inline uint32_t ChunkKey(int tileX, int tileY, int chunkX, int chunkY)
{
    return static_cast<uint32_t>(((tileY * kTilesPerSide + tileX) * kChunksPerTile + chunkY) * kChunksPerTile + chunkX);
}

inline int CellBit(int cellX, int cellY)
{
    return cellY * kCellsPerChunk + cellX;
}

struct Place
{
    uint32_t key;
    int bit;
};

inline bool PlaceOf(float worldX, float worldY, Place& place)
{
    const double rows = (kMapHalfExtent - worldX) / kChunkYards;
    const double columns = (kMapHalfExtent - worldY) / kChunkYards;
    if (!(rows >= 0.0 && columns >= 0.0 && rows < kChunksPerSide && columns < kChunksPerSide))
        return false;
    const int row = static_cast<int>(std::floor(rows));
    const int column = static_cast<int>(std::floor(columns));
    const int cellY = std::min(static_cast<int>((rows - row) * kCellsPerChunk), kCellsPerChunk - 1);
    const int cellX = std::min(static_cast<int>((columns - column) * kCellsPerChunk), kCellsPerChunk - 1);
    place.key = ChunkKey(column / kChunksPerTile, row / kChunksPerTile, column % kChunksPerTile, row % kChunksPerTile);
    place.bit = CellBit(cellX, cellY);
    return true;
}

inline void CellCentre(uint32_t key, int bit, float& worldX, float& worldY)
{
    const int chunkX = static_cast<int>(key % kChunksPerTile);
    const int chunkY = static_cast<int>(key / kChunksPerTile % kChunksPerTile);
    const int tile = static_cast<int>(key / (kChunksPerTile * kChunksPerTile));
    const int tileX = tile % kTilesPerSide;
    const int tileY = tile / kTilesPerSide;
    const double column = tileX * kChunksPerTile + chunkX + (bit % kCellsPerChunk + 0.5) / kCellsPerChunk;
    const double row = tileY * kChunksPerTile + chunkY + (bit / kCellsPerChunk + 0.5) / kCellsPerChunk;
    worldX = static_cast<float>(kMapHalfExtent - row * kChunkYards);
    worldY = static_cast<float>(kMapHalfExtent - column * kChunkYards);
}

}
