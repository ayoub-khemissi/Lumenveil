#pragma once

#include <cstdint>

namespace reliefclass
{

enum Id : uint8_t
{
    kNone = 0,
    kRoads = 1,
    kRock = 2,
    kDirt = 3,
    kSand = 4,
    kSnow = 5,
    kGrass = 6,
    kWalls = 7,
    kRoofs = 8,
    kWood = 9,
    kMetal = 10,
    kBark = 11,
    kLeaves = 12,
    kCreatures = 13,
    kCharacters = 14,
    kArmour = 15,
    kProps = 16,
    kCount = 17,
};

enum class Group : uint8_t
{
    None,
    Ground,
    Buildings,
    Nature,
    Living,
    Other,
};

struct Info
{
    const char* key;
    const char* name;
    float share;
    Group group;
    const char* help;
};

constexpr float kMinShare = 0.0f;
constexpr float kMaxShare = 3.0f;
constexpr float kShareStep = 0.05f;

inline constexpr Info kInfo[kCount] = {
    {"", "no relief", 0.0f, Group::None, ""},
    {"ReliefRoads", "roads", 1.0f, Group::Ground,
     "Roads, paving, cobbles and paved floors, outdoors and in buildings"},
    {"ReliefRock", "rock", 1.0f, Group::Ground, "Rock, cliffs, gravel, boulders and caves"},
    {"ReliefDirt", "dirt", 0.6f, Group::Ground, "Dirt, mud, soil, ash, and ground of no other type"},
    {"ReliefSand", "sand", 0.3f, Group::Ground, "Sand, dunes and beaches"},
    {"ReliefSnow", "snow", 0.3f, Group::Ground, "Snow and ice"},
    {"ReliefGrass", "grass", 0.4f, Group::Ground, "Grass, moss, fallen leaves and crops on the ground"},
    {"ReliefWalls", "walls", 0.7f, Group::Buildings,
     "Walls of stone, brick and plaster, trim, pillars, ceilings and window frames"},
    {"ReliefRoofs", "roofs", 0.7f, Group::Buildings, "Roofs: shingles, tiles and thatch"},
    {"ReliefWood", "wood", 0.5f, Group::Buildings,
     "Planks, beams, wooden walls and floors, fences, crates and barrels"},
    {"ReliefMetal", "metal", 0.3f, Group::Buildings, "Metal and iron, chains, pipes and machines"},
    {"ReliefBark", "bark", 0.6f, Group::Nature, "Tree trunks, branches, roots and logs"},
    {"ReliefLeaves", "leaves", 0.2f, Group::Nature, "Leaves, bushes, plants and flowers on models"},
    {"ReliefCreatures", "creatures", 0.15f, Group::Living, "Creatures: fur, hide and scales"},
    {"ReliefCharacters", "characters", 0.1f, Group::Living, "Characters and the armour they wear on their body"},
    {"ReliefArmour", "armour", 0.1f, Group::Living, "Helmets, shoulders, weapons and shields"},
    {"ReliefProps", "props", 0.4f, Group::Other, "Props: cloth, banners, lamps, signs and everything else"},
};

inline bool Valid(uint8_t id)
{
    return id < kCount;
}

inline float DefaultShare(uint8_t id)
{
    return Valid(id) ? kInfo[id].share : 0.0f;
}

inline const char* Name(uint8_t id)
{
    return Valid(id) ? kInfo[id].name : "unknown";
}

inline uint8_t FromName(const char* name)
{
    for (uint8_t id = 0; id < kCount; ++id)
    {
        const char* a = kInfo[id].name;
        const char* b = name;
        while (*a && *a == *b)
            ++a, ++b;
        if (!*a && !*b)
            return id;
    }
    return kCount;
}

}
