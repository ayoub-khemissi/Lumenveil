#pragma once

#include "relief_classes.h"

#include <cstddef>
#include <cstdint>

namespace relieffile
{

constexpr char kMagic[4] = {'V', 'F', 'R', 'L'};
constexpr uint32_t kVersion = 2;
constexpr uint32_t kVersionOne = 1;
constexpr uint32_t kNoHeights = 0xFFFFFFFFu;
constexpr float kMaxVersionOneDepth = 4.0f;
constexpr uint32_t kHashedLevelSize = 64;
constexpr uint32_t kFormatDxt1 = 0x31545844u;
constexpr uint32_t kFormatDxt3 = 0x33545844u;
constexpr uint32_t kFormatDxt5 = 0x35545844u;
constexpr uint32_t kFormatArgb = 21u;

#pragma pack(push, 1)

struct Header
{
    char magic[4];
    uint32_t version;
    uint32_t entryCount;
    uint32_t heightCount;
    uint32_t heightSize;
    uint32_t levelCount;
    uint32_t heightBytes;
    uint32_t reserved;
};

struct EntryVersionOne
{
    uint64_t hash;
    uint32_t format;
    uint16_t width;
    uint16_t height;
    uint32_t heightIndex;
    float depth;
};

struct Entry
{
    uint64_t hash;
    uint32_t format;
    uint16_t width;
    uint16_t height;
    uint32_t heightIndex;
    float flatness;
    uint8_t reliefClass;
    uint8_t reserved[7];
};

#pragma pack(pop)

static_assert(sizeof(Header) == 32, "relief file header");
static_assert(sizeof(EntryVersionOne) == 24, "relief file entry, version 1");
static_assert(sizeof(Entry) == 32, "relief file entry");

inline size_t EntryBytes(uint32_t version)
{
    return version == kVersionOne ? sizeof(EntryVersionOne) : version == kVersion ? sizeof(Entry) : 0;
}

inline bool HasHeights(const Entry& e)
{
    return e.heightIndex != kNoHeights;
}

struct VersionOneKind
{
    float depth;
    uint8_t reliefClass;
};

inline constexpr VersionOneKind kVersionOneKinds[] = {
    {0.3f, reliefclass::kSand}, {0.4f, reliefclass::kGrass}, {0.6f, reliefclass::kDirt},
    {0.7f, reliefclass::kDirt}, {1.0f, reliefclass::kRoads},
};

constexpr float kVersionOneDepthTolerance = 1e-4f;

inline Entry FromVersionOne(const EntryVersionOne& old)
{
    Entry e = {};
    e.hash = old.hash;
    e.format = old.format;
    e.width = old.width;
    e.height = old.height;
    e.heightIndex = old.heightIndex;
    e.reliefClass = reliefclass::kRoads;
    for (const VersionOneKind& kind : kVersionOneKinds)
        if (old.depth <= kind.depth + kVersionOneDepthTolerance)
        {
            e.reliefClass = kind.reliefClass;
            break;
        }
    e.flatness = old.depth / reliefclass::DefaultShare(e.reliefClass);
    return e;
}

inline uint64_t Fnv1a(const void* data, size_t size, uint64_t h = 14695981039346656037ull)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ bytes[i]) * 1099511628211ull;
    return h;
}

inline uint32_t BlockBytes(uint32_t format)
{
    switch (format)
    {
    case kFormatDxt1:
        return 8;
    case kFormatDxt3:
    case kFormatDxt5:
        return 16;
    default:
        return 0;
    }
}

inline uint32_t RowBytes(uint32_t format, uint32_t width)
{
    if (const uint32_t block = BlockBytes(format))
        return ((width + 3) / 4) * block;
    return format == kFormatArgb ? width * 4 : 0;
}

inline uint32_t RowCount(uint32_t format, uint32_t height)
{
    return BlockBytes(format) ? (height + 3) / 4 : height;
}

inline uint32_t HashedLevel(uint32_t width, uint32_t height, uint32_t levels)
{
    uint32_t level = 0;
    while (level + 1 < levels && (width > kHashedLevelSize || height > kHashedLevelSize))
    {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++level;
    }
    return level;
}

inline uint32_t LevelBytes(uint32_t size, uint32_t levels)
{
    uint32_t total = 0;
    for (uint32_t k = 0; k < levels; ++k)
    {
        const uint32_t side = size >> k ? size >> k : 1;
        total += side * side;
    }
    return total;
}

inline bool KeyLess(const Entry& a, const Entry& b)
{
    if (a.hash != b.hash)
        return a.hash < b.hash;
    if (a.format != b.format)
        return a.format < b.format;
    if (a.width != b.width)
        return a.width < b.width;
    return a.height < b.height;
}

}
