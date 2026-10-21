#pragma once

#include <cstddef>
#include <cstdint>

namespace materialfile
{

constexpr char kMagic[4] = {'V', 'F', 'M', 'T'};
constexpr uint32_t kVersion = 1;

enum Kind : uint8_t
{
    kUnknown = 0,
    kGrass = 1,
    kSoil = 2,
    kStone = 3,
    kSand = 4,
    kSnow = 5,
    kHot = 6,
    kIce = 7,
    kWood = 8,
    kKindCount = 9,
};

#pragma pack(push, 1)

struct Header
{
    char magic[4];
    uint32_t version;
    uint32_t entryCount;
    uint32_t reserved;
};

struct Entry
{
    uint64_t hash;
    uint32_t format;
    uint16_t width;
    uint16_t height;
    uint8_t kind;
    uint8_t reserved[7];
};

#pragma pack(pop)

static_assert(sizeof(Header) == 16, "material file header");
static_assert(sizeof(Entry) == 24, "material file entry");

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

inline const char* KindName(uint8_t kind)
{
    switch (kind)
    {
    case kGrass:
        return "grass";
    case kSoil:
        return "soil";
    case kStone:
        return "stone";
    case kSand:
        return "sand";
    case kSnow:
        return "snow";
    case kHot:
        return "hot";
    case kIce:
        return "ice";
    case kWood:
        return "wood";
    default:
        return "unknown";
    }
}

inline uint8_t KindFromName(const char* name)
{
    for (uint8_t k = 1; k < kKindCount; ++k)
    {
        const char* a = KindName(k);
        const char* b = name;
        while (*a && *a == *b)
            ++a, ++b;
        if (!*a && !*b)
            return k;
    }
    return kUnknown;
}

}
