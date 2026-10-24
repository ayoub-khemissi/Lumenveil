#pragma once

#include <cstdint>

namespace lampfile
{

constexpr char kMagic[4] = {'V', 'F', 'L', 'D'};
constexpr uint32_t kVersion = 2;
constexpr uint32_t kVersionOne = 1;
constexpr float kWorldHalfExtent = 17066.666f;
constexpr float kCellSize = 128.0f;
constexpr uint32_t kCellsPerSide = 267;

enum class LampKind : uint8_t
{
    Candle,
    Torch,
    Brazier,
    Bonfire,
    Lamp,
    Chandelier,
    Fixture,
};

enum LampFlags : uint16_t
{
    kLampIndoor = 1 << 0,
    kLampAuthored = 1 << 1,
    kLampFlame = 1 << 2,
    kLampNameRoute = 1 << 3,
    kLampEmissiveRoute = 1 << 4,
    kLampWmoFixture = 1 << 5,
    kLampGameObject = 1 << 6,
    kLampWmoDoodad = 1 << 7,
    kLampFixtureNearLamp = 1 << 8,
    kLampBoundsFallback = 1 << 9,
    kLampClampedToModel = 1 << 10,
    kLampWmoLantern = 1 << 11,
};

enum WindowFlags : uint16_t
{
    kWindowIndoor = 1 << 0,
    kWindowFacesUp = 1 << 1,
};

#pragma pack(push, 1)

struct Header
{
    char magic[4];
    uint32_t version;
    uint32_t mapCount;
    uint32_t nameTableOffset;
    uint32_t nameTableSize;
    uint32_t objectModelCount;
    uint32_t objectModelOffset;
    uint32_t objectLampOffset;
};

struct MapEntry
{
    uint32_t mapId;
    uint32_t cellCount;
    uint32_t cellOffset;
    uint32_t lampCount;
    uint32_t lampOffset;
    uint32_t windowCount;
    uint32_t windowOffset;
    uint32_t reserved;
};

struct Cell
{
    uint32_t key;
    uint32_t firstLamp;
    uint32_t lampCount;
    uint32_t firstWindow;
    uint32_t windowCount;
};

struct Lamp
{
    float position[3];
    float color[3];
    float intensity;
    float reach;
    uint8_t kind;
    uint8_t reserved;
    uint16_t flags;
    uint32_t name;
};

struct ObjectModel
{
    uint32_t displayId;
    uint32_t firstLamp;
    uint32_t lampCount;
    uint32_t reserved;
};

struct Window
{
    float center[3];
    float normal[3];
    float halfWidth[3];
    float halfHeight;
    float color[3];
    float area;
    uint16_t flags;
    uint16_t reserved;
    uint32_t name;
};

#pragma pack(pop)

static_assert(sizeof(Header) == 32, "lampdata header");
static_assert(sizeof(MapEntry) == 32, "lampdata map entry");
static_assert(sizeof(Cell) == 20, "lampdata cell");
static_assert(sizeof(Lamp) == 40, "lampdata lamp");
static_assert(sizeof(Window) == 64, "lampdata window");
static_assert(sizeof(ObjectModel) == 16, "lampdata object model");

inline uint32_t CellCoordinate(float worldAxis)
{
    float cell = (worldAxis + kWorldHalfExtent) / kCellSize;
    if (cell < 0.0f)
        return 0;
    uint32_t index = static_cast<uint32_t>(cell);
    return index < kCellsPerSide ? index : kCellsPerSide - 1;
}

inline uint32_t CellKey(uint32_t cellX, uint32_t cellY)
{
    return cellX * 1024u + cellY;
}

}
