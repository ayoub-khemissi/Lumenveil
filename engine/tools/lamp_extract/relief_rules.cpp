#include "relief_rules.h"

#include "material_data.h"
#include "material_rules.h"
#include "relief_classes.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
using namespace reliefclass;

enum class Family
{
    Ground,
    Creatures,
    Characters,
    Armour,
    DungeonTextures,
    Buildings,
    World,
    Other,
};

struct Prefix
{
    const char* path;
    Family family;
};

struct Words
{
    uint8_t reliefClass;
    std::vector<const char*> words;
};

struct Swap
{
    const char* from;
    const char* to;
};

struct FolderClass
{
    const char* folder;
    uint8_t reliefClass;
};

const std::vector<const char*>& ExcludedPrefixes()
{
    static const std::vector<const char*> prefixes = {
        "INTERFACE\\",          "TEXTURES\\MINIMAP\\",   "TEXTURES\\GUILDEMBLEMS\\", "TEXTURES\\SPELLCHAINEFFECTS\\",
        "TEXTURES\\WEATHER\\",  "TEXTURES\\FOOTSTEPS\\", "TEXTURES\\BLOODSPLATS\\",  "TEXTURES\\TAXIMAPS\\",
        "SPELLS\\",             "PARTICLES\\",           "ENVIRONMENTS\\",           "XTEXTURES\\",
    };
    return prefixes;
}

const std::vector<const char*>& ExcludedFolders()
{
    static const std::vector<const char*> folders = {
        "SKY",       "SKYBOX",     "SKYBOXES",       "PARTICLES",   "PARTICLEEMITTERS", "FX",
        "LIGHTFX",   "LIGHTS",     "VOLUMETRICLIGHTS", "SPELLPORTALS", "WATERFALLS",     "WATERFALL",
        "LAVA",      "CAUSTIC",    "CAUSTICS",       "PROCWATER",   "LIQUID",           "LIQUIDS",
        "GLOW",      "GLOWS",      "SPELLS",         "SPELLEFFECTS", "EFFECTS",       "EFFECT",
    };
    return folders;
}

const std::vector<Prefix>& Families()
{
    static const std::vector<Prefix> families = {
        {"TILESET\\", Family::Ground},
        {"CREATURE\\", Family::Creatures},
        {"WORLD\\CRITTER\\", Family::Creatures},
        {"CHARACTER\\", Family::Characters},
        {"TEXTURES\\BAKEDNPCTEXTURES\\", Family::Characters},
        {"ITEM\\TEXTURECOMPONENTS\\", Family::Characters},
        {"ITEM\\OBJECTCOMPONENTS\\", Family::Armour},
        {"DUNGEONS\\TEXTURES\\", Family::DungeonTextures},
        {"DUNGEONS\\", Family::Buildings},
        {"WORLD\\WMO\\", Family::Buildings},
        {"WORLD\\", Family::World},
    };
    return families;
}

const std::vector<Swap>& LookAlikes()
{
    static const std::vector<Swap> swaps = {
        {"ICECROWN", "_"},     {"DRAGONBLIGHT", "_"}, {"GOLDSHIRE", "_"},     {"BLACKROCK", "_"},
        {"HELLFIRE", "_"},     {"FIRELANDS", "_"},    {"MOLTENCORE", "_"},    {"SKYREACH", "_"},
        {"STORMWIND", "_"},    {"SILVERMOON", "_"},   {"STRATHOLME", "_"},    {"THUNDERBLUFF", "_"},
        {"HIGHLIGHT", "HILITE"}, {"LIGHTER", "PALER"}, {"LIGHTHOUSE", "TOWER"}, {"TWILIGHT", "DUSK"},
        {"BLIGHT", "BLITE"},   {"FLIGHT", "FLITE"},   {"SLIGHT", "SLITE"},    {"DELIGHT", "DELITE"},
        {"LIGHTBLUE", "PALEBLUE"}, {"LIGHTBROWN", "PALEBROWN"}, {"LIGHTGREEN", "PALEGREEN"},
        {"LIGHTGREY", "PALEGREY"}, {"LIGHTGRAY", "PALEGRAY"}, {"LIGHTRED", "PALERED"}, {"LIGHTTAN", "PALETAN"},
        {"LIGHTWOOD", "PALEWOOD"}, {"LIGHTPURPLE", "PALEPURPLE"}, {"LIGHTYELLOW", "PALEYELLOW"},
        {"FIREPLACE", "CHIMNEY"}, {"HEARTH", "CHIMNEY"}, {"FIREWOOD", "WOODPILE"}, {"FIREARM", "GUN"},
        {"HFIRE", "_"},        {"WATERWHEEL", "WHEEL"}, {"WATERMILL", "MILL"},  {"WATERTOWER", "TOWER"},
        {"UNDERWATER", "SUNKEN"}, {"WATERPUMP", "PUMP"}, {"WATERTROUGH", "TROUGH"}, {"STREETLAMP", "LAMP"}, {"STREET", "ROAD"},
        {"IRONHORDE", "_"},    {"HIGHMOUNTAIN", "_"},   {"EARTHEN", "_"},       {"FROSTWOLF", "_"},
        {"THOUSAND", "_"},     {"SANDSCROLL", "AND_SCROLL"}, {"SANDEGG", "AND_EGG"}, {"NETSAND", "NETS_AND"},
        {"JUNGLEFLOOR", "JUNGLESOIL"}, {"FORESTFLOOR", "FORESTSOIL"},
        {"TILESET", "_"},      {"ENVIRONMENT", "ENVIRO"},
    };
    return swaps;
}

const std::vector<const char*>& ExcludedWords()
{
    static const std::vector<const char*> words = {
        "GLOW",    "LIGHT",   "GLASS",   "WATER",    "LAVA",     "MAGMA",   "MOLTEN",   "SLIME",
        "FIRE",    "FLAME",   "SKY",     "CLOUD",    "SMOKE",    "CAUSTIC", "FOAM",     "PORTAL",
        "SPARK",   "HALO",    "AURA",    "REFLECT",  "ENVMAP",   "ENVIRO",  "CHROME",   "GRADIENT",
        "EMISSIVE", "ADDITIVE", "SWIRL", "PLASMA",   "ENERGY",   "BEAMS",   "SHINE",    "SHIMMER",
        "PARTICLE", "EMBER",  "WISPY",   "SPRAY",
    };
    return words;
}

const std::vector<const char*>& CreatureExcludedWords()
{
    static const std::vector<const char*> words = {
        "GLOW",   "REFLECT", "ENVMAP", "ENVIRO", "SMOKE",    "CLOUD",    "LIGHTNING", "SPARK",
        "FLAME",  "FIRE",    "PLASMA", "ENERGY", "SWIRL",    "HALO",     "AURA",      "PORTAL",
        "FOAM",   "CAUSTIC", "ADDITIVE", "EMISSIVE", "GRADIENT", "PARTICLE", "WISPY",   "LAVA",
        "MAGMA",  "SLIME",   "WATER",  "GLASS",
    };
    return words;
}

const std::vector<const char*>& HotGroundCrust()
{
    static const std::vector<const char*> words = {"COOL", "ROCK", "STONE", "CRUST"};
    return words;
}

const std::vector<const char*>& HotGroundWords()
{
    static const std::vector<const char*> words = {"LAVA", "MAGMA", "MOLTEN", "SLIME", "EMBER"};
    return words;
}

const std::vector<const char*>& ArmourExcludedWords()
{
    static const std::vector<const char*> words = {"GLOW", "REFLECT", "ENVIRO", "FIRE", "FLAME", "SPARK",
                                                   "SWIRL", "PLASMA", "ENERGY", "LIGHTNING"};
    return words;
}

const std::vector<const char*>& CharacterExcludedWords()
{
    static const std::vector<const char*> words = {"GLOW", "REFLECT", "ENVIRO"};
    return words;
}

const std::vector<Words>& GroundWords()
{
    static const std::vector<Words> rules = {
        {kRoads, {"ROAD", "PATH", "COBBLE", "PAVE"}},
        {kRoads, {"TILE", "BRICK", "FLOOR", "WALL", "MASONRY"}},
        {kWood, {"PLANK"}},
        {kRock, {"ROCK", "CLIFF", "STONE", "GRAVEL", "PEBBL", "SLATE", "BOULDER"}},
        {kDirt, {"DIRT", "MUD", "SOIL", "GROUND", "EARTH", "CLAY"}},
        {kSand, {"SAND", "DUNE"}},
        {kSnow, {"SNOW", "ICE"}},
        {kGrass,
         {"GRASS", "MOSS", "LEAF", "LEAVES", "FLOWER", "PETAL", "FERN", "ROOT", "BUSH", "VINE", "WEED", "CLOVER",
          "NEEDLE", "BLOSSOM"}},
        {kSnow, {"FROST", "SLUSH", "FROZEN", "GLACIER"}},
        {kRoads, {"PLAZA", "MARBLE"}},
        {kSand, {"BEACH", "SALTFLAT"}},
        {kGrass,
         {"SHRUB", "BRUSH", "IVY", "CROP", "STRAW", "HAY", "FOLIAGE", "MEADOW", "HEDGE", "LAWN", "BRAMBLE", "THORN",
          "REED", "LICHEN", "SCRUB", "WHEAT", "FIELD", "FOREST", "JUNGLE", "PLANT"}},
        {kRock,
         {"STONY", "RUBBLE", "SHALE", "CRYSTAL", "GRANITE", "BASALT", "MOUNTAIN", "CRAG", "RUIN", "TITAN", "OBSIDIAN",
          "MINERAL", "BARNACLE"}},
        {kBark, {"BARK", "TREE", "TRUNK"}},
        {kWood, {"WOOD", "LOGROAD", "TIMBER"}},
        {kDirt, {"MUCK", "ASH", "CHARCOAL", "SOOT", "CRACK", "DEBRIS", "SHORE", "BONE", "SKULL", "DUST", "TRAIL"}},
    };
    return rules;
}

const std::vector<FolderClass>& DungeonFolders()
{
    static const std::vector<FolderClass> folders = {
        {"WALLS", kWalls},  {"WALL", kWalls},     {"TRIM", kWalls},     {"PILLAR", kWalls},  {"PILLARS", kWalls},
        {"CEILING", kWalls}, {"CEILINGS", kWalls}, {"WINDOWS", kWalls},  {"WINDOW", kWalls},  {"BRICK", kWalls},
        {"BRICKS", kWalls}, {"FLOOR", kRoads},     {"FLOORS", kRoads},   {"ROOF", kRoofs},    {"ROOFS", kRoofs},
        {"WOOD", kWood},    {"METAL", kMetal},     {"DEVICES", kMetal},  {"ROCK", kRock},     {"ROCKS", kRock},
    };
    return folders;
}

const std::vector<FolderClass>& NatureFolders()
{
    static const std::vector<FolderClass> folders = {
        {"TREES", kBark},  {"TREE", kBark},      {"BUSHES", kLeaves}, {"BUSH", kLeaves},   {"PLANTS", kLeaves},
        {"FOLIAGE", kLeaves}, {"FLOWERS", kLeaves}, {"ROCKS", kRock}, {"DETAIL", kLeaves},
    };
    return folders;
}

const std::vector<Words>& ModelWords()
{
    static const std::vector<Words> rules = {
        {kRoofs, {"ROOF", "SHINGLE", "THATCH"}},
        {kLeaves,
         {"LEAF", "LEAVES", "BUSH", "FERN", "FLOWER", "VINE", "PLANT", "FOLIAGE", "CANOPY", "IVY", "GRASS", "HEDGE",
          "SHRUB", "PETAL", "BLOSSOM", "WEED", "REED", "KELP", "LILY", "CACTUS", "DETAILDOODAD"}},
        {kBark, {"TREE", "TRUNK", "BARK", "BRANCH", "ROOT", "STUMP", "TWIG"}},
        {kProps,
         {"CLOTH", "RUG", "CARPET", "BANNER", "TAPESTRY", "CURTAIN", "SAIL", "TENT", "FABRIC", "LAMP", "LANTERN",
          "CANDLE", "BOOK", "PAPER"}},
        {kRoads, {"COBBLE", "PAVE", "ROAD", "FLAGSTONE", "STAIR"}},
        {kWood,
         {"WOOD", "PLANK", "CRATE", "BARREL", "FENCE", "TIMBER", "LUMBER", "BOARD", "BEAM", "PALISADE", "LADDER",
          "SCAFFOLD", "TABLE", "CHAIR", "BENCH", "SHELF", "CABINET", "DOCK", "PIER"}},
        {kMetal,
         {"METAL", "IRON", "STEEL", "BRASS", "BRONZE", "COPPER", "CHAIN", "GEAR", "PIPE", "ANVIL", "CANNON",
          "MACHINE", "ENGINE", "MECH"}},
        {kWalls,
         {"BRICK", "WALL", "PLASTER", "MASONRY", "PILLAR", "COLUMN", "TRIM", "CEILING", "WINDOW", "FOUNDATION"}},
        {kRock,
         {"ROCK", "STONE", "BOULDER", "CLIFF", "CAVE", "GRAVEL", "PEBBL", "RUBBLE", "CRAG", "MARBLE", "GRANITE",
          "BASALT", "SLATE", "MOUNTAIN"}},
        {kDirt, {"DIRT", "MUD", "SOIL", "EARTH", "CLAY"}},
        {kSand, {"SAND", "DUNE"}},
        {kSnow, {"SNOW", "FROST", "GLACIER", "ICICLE"}},
    };
    return rules;
}

std::string UpperPath(const std::string& gamePath)
{
    std::string p = gamePath;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) {
        return c == '/' ? '\\' : static_cast<char>(std::toupper(c));
    });
    return p;
}

bool StartsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

std::vector<std::string> Folders(const std::string& path)
{
    std::vector<std::string> folders;
    size_t start = 0;
    for (size_t slash = path.find('\\'); slash != std::string::npos; slash = path.find('\\', start))
    {
        folders.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    return folders;
}

const char* FirstWord(const std::string& name, const std::vector<const char*>& words)
{
    for (const char* w : words)
        if (name.find(w) != std::string::npos)
            return w;
    return nullptr;
}

bool EffectFolder(const std::string& folder)
{
    for (const char* excluded : ExcludedFolders())
        if (folder == excluded)
            return true;
    return folder.size() > 3 && folder.compare(folder.size() - 3, 3, "_FX") == 0;
}

bool FxMark(const std::string& name)
{
    for (size_t at = name.find("FX"); at != std::string::npos; at = name.find("FX", at + 1))
    {
        const char before = at > 0 ? name[at - 1] : '_';
        const char beforeV = at > 1 ? name[at - 2] : '_';
        const char after = at + 2 < name.size() ? name[at + 2] : '.';
        const bool letterBefore = std::isalpha(static_cast<unsigned char>(before)) != 0;
        const bool vfx = before == 'V' && !std::isalpha(static_cast<unsigned char>(beforeV));
        if (!letterBefore || vfx || after == '_' || after == '.' ||
            std::isdigit(static_cast<unsigned char>(after)))
            return true;
    }
    return false;
}

uint8_t FromMaterial(uint8_t kind)
{
    switch (kind)
    {
    case materialfile::kGrass:
        return kGrass;
    case materialfile::kSoil:
        return kDirt;
    case materialfile::kStone:
    case materialfile::kHot:
        return kRock;
    case materialfile::kSand:
        return kSand;
    case materialfile::kSnow:
    case materialfile::kIce:
        return kSnow;
    case materialfile::kWood:
        return kWood;
    default:
        return kNone;
    }
}

uint8_t Decide(uint8_t reliefClass, std::string* why, const std::string& reason)
{
    if (why)
        *why = reason;
    return reliefClass;
}

bool MatchWords(const std::string& name, const std::vector<Words>& rules, uint8_t& reliefClass, const char*& word)
{
    for (const Words& rule : rules)
        if ((word = FirstWord(name, rule.words)) != nullptr)
        {
            reliefClass = rule.reliefClass;
            return true;
        }
    return false;
}

Family FamilyOf(const std::string& path)
{
    for (const Prefix& p : Families())
        if (StartsWith(path, p.path))
            return p.family;
    return Family::Other;
}

const char* WoodWordIn(const std::string& name)
{
    static const std::vector<const char*> wood = {"WOOD", "PLANK", "TIMBER", "BOARD", "LOG"};
    return FirstWord(name, wood);
}

uint8_t FamilyDefault(Family family, const std::vector<std::string>& folders, std::string* why)
{
    for (const std::string& folder : folders)
        for (const FolderClass& f : NatureFolders())
            if (folder == f.folder)
                return Decide(f.reliefClass, why, std::string("folder ") + f.folder);
    switch (family)
    {
    case Family::DungeonTextures:
    case Family::Buildings:
        return Decide(kWalls, why, "buildings by default");
    default:
        return Decide(kProps, why, "props by default");
    }
}
}

std::string ReliefNameWithoutLookAlikes(const std::string& gamePath)
{
    std::string name = MaterialPlaceStrippedName(gamePath);
    for (const Swap& s : LookAlikes())
        for (size_t at = name.find(s.from); at != std::string::npos; at = name.find(s.from, at + 1))
            name.replace(at, std::char_traits<char>::length(s.from), s.to);
    return name;
}

uint8_t ReliefClassOfName(const std::string& gamePath, std::string* why)
{
    const std::string path = UpperPath(gamePath);
    for (const char* prefix : ExcludedPrefixes())
        if (StartsWith(path, prefix))
            return Decide(kNone, why, std::string("folder ") + prefix);
    const std::vector<std::string> folders = Folders(path);
    for (const std::string& folder : folders)
        if (EffectFolder(folder))
            return Decide(kNone, why, "folder " + folder);
    const Family family = FamilyOf(path);
    const std::string name = ReliefNameWithoutLookAlikes(path);
    if (family == Family::Ground)
    {
        if (const char* hot = FirstWord(name, HotGroundWords()))
        {
            if (const char* crust = FirstWord(name, HotGroundCrust()))
                return Decide(kRock, why, std::string("ground word ") + hot + " with " + crust);
            return Decide(kNone, why, std::string("word ") + hot);
        }
    }
    else
    {
        const std::vector<const char*>& excluded = family == Family::Armour       ? ArmourExcludedWords()
                                                   : family == Family::Characters ? CharacterExcludedWords()
                                                   : family == Family::Creatures  ? CreatureExcludedWords()
                                                                                  : ExcludedWords();
        if (const char* w = FirstWord(name, excluded))
            return Decide(kNone, why, std::string("word ") + w);
        if (family != Family::Characters && FxMark(name))
            return Decide(kNone, why, "word FX");
    }
    if (family == Family::Creatures)
        return Decide(kCreatures, why, "creatures");
    if (family == Family::Characters)
        return Decide(kCharacters, why, "characters");
    if (family == Family::Armour)
        return Decide(kArmour, why, "armour");
    uint8_t reliefClass = kNone;
    const char* word = nullptr;
    if (family == Family::Ground)
    {
        std::string materialWord;
        const uint8_t material = FromMaterial(MaterialKindOfName(path, &materialWord));
        if (materialWord == "(named)" && material != kNone)
            return Decide(material, why, "ground named " + std::string(Name(material)));
        if (MatchWords(name, GroundWords(), reliefClass, word))
            return Decide(reliefClass, why, std::string("ground word ") + word);
        if (material != kNone)
            return Decide(material, why, "ground material word " + materialWord);
        return Decide(kDirt, why, "ground by default");
    }
    if (family == Family::DungeonTextures && folders.size() > 2)
        for (const FolderClass& f : DungeonFolders())
            if (folders[2] == f.folder)
            {
                if (f.reliefClass == kRoads)
                    if (const char* wood = WoodWordIn(name))
                        return Decide(kWood, why, std::string("folder FLOOR, word ") + wood);
                return Decide(f.reliefClass, why, std::string("folder ") + f.folder);
            }
    if (MatchWords(name, ModelWords(), reliefClass, word))
    {
        if (reliefClass == kRoads && std::string(word) != "ROAD" && WoodWordIn(name))
            return Decide(kWood, why, std::string("word ") + word + " of wood");
        return Decide(reliefClass, why, std::string("word ") + word);
    }
    if (name.find("FLOOR") != std::string::npos)
        return Decide(WoodWordIn(name) ? kWood : kRoads, why, "word FLOOR");
    return FamilyDefault(family, folders, why);
}

uint32_t ReliefMedianFor(bool groundSource, uint8_t reliefClass, uint32_t groundMedian, uint32_t buildingMedian)
{
    const bool groundType = reliefClass < reliefclass::kCount &&
                            reliefclass::kInfo[reliefClass].group == reliefclass::Group::Ground;
    return groundSource || groundType ? groundMedian : buildingMedian;
}
