#include "material_rules.h"

#include "material_data.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
struct WordRule
{
    uint8_t kind;
    std::vector<const char*> words;
};

const std::vector<const char*>& PlaceNames()
{
    static const std::vector<const char*> names = {
        "THOUSANDNEEDLES", "TEROKKARFOREST", "STRANGLETHORN", "WINTERSPRING", "EMERALDDREAM", "ZANGARMARSH",
        "NETHERSTORM",     "SHADOWOUTLAND", "SHADOWMOON",    "BONEWASTES",   "CRYSTALSONG",  "STONETALON",
        "DUSKWALLOW",      "BLADESEDGE",    "SILVERPINE",    "SILVERMYST",   "GHOSTLANDS",   "ASHENVALE",
        "DARKSHORE",       "IRONFORGE",     "BLOODMYST",     "TEROKKAR",     "SHOLAZAR",     "EVERSONG",
        "DUSKWOOD",        "FELWOOD",       "ASHZARA",       "AZSHARA",      "KALIDAR",      "ICEC_",
    };
    return names;
}

const std::vector<WordRule>& WordRules()
{
    static const std::vector<WordRule> rules = {
        {materialfile::kHot, {"LAVA", "MAGMA", "EMBER", "MOLTEN"}},
        {materialfile::kSnow, {"SNOW", "FROST", "SLUSH", "DRIFT"}},
        {materialfile::kIce, {"ICE", "FROZEN", "GLACIER"}},
        {materialfile::kStone, {"COBBLE", "PAVE", "BRICK", "TILE", "MASONRY", "PLAZA", "STREET", "MARBLE", "FLOOR"}},
        {materialfile::kSand, {"SAND", "DUNE", "BEACH", "SALTFLAT"}},
        {materialfile::kSoil, {"MUD", "MUCK", "DIRT", "SOIL", "CLAY", "ASH", "CHARCOAL", "SOOT"}},
        {materialfile::kGrass,
         {"GRASS", "MOSS", "LEAF", "LEAVES", "FLOWER", "PETAL", "FERN", "BUSH", "VINE", "WEED", "CLOVER", "NEEDLE",
          "BLOSSOM", "SHRUB", "BRUSH", "IVY", "CROP", "STRAW", "HAY", "FOLIAGE", "MEADOW", "HEDGE", "LAWN", "BRAMBLE",
          "THORN", "REED", "LICHEN", "SCRUB", "WHEAT", "FIELD", "FOREST", "JUNGLE", "PLANT"}},
        {materialfile::kStone,
         {"ROCK", "CLIFF", "STONE", "STONY", "GRAVEL", "PEBBL", "SLATE", "BOULDER", "RUBBLE", "SHALE", "CRYSTAL",
          "GRANITE", "BASALT", "MOUNTAIN", "CRAG", "RUIN", "WALL", "TITAN", "OBSIDIAN", "MINERAL", "BARNACLE"}},
        {materialfile::kWood, {"WOOD", "PLANK", "BARK", "LOGROAD", "TIMBER"}},
        {materialfile::kSoil,
         {"GROUND", "EARTH", "PATH", "ROAD", "CRACK", "ROOT", "DEBRIS", "SHORE", "BONE", "SKULL", "DUST", "TRAIL",
          "CREEP", "BLIGHT", "CORRUPT"}},
    };
    return rules;
}

struct NamedKind
{
    const char* base;
    uint8_t kind;
};

const std::vector<NamedKind>& NamedKinds()
{
    static const std::vector<NamedKind> named = {
        {"WESTPLAGUEDGRASSANDORHAL_S.BLP", materialfile::kGrass},
        {"KALIDARDARNROADNOGRASS_S.BLP", materialfile::kSoil},
        {"ASHZARABEACHROCKPINK_S.BLP", materialfile::kStone},
        {"ASHZARABEACHROCKPURPLEBARNACLE_S.BLP", materialfile::kStone},
        {"ASHZARABEACHROCKSOLIDBLUE_S.BLP", materialfile::kStone},
        {"WINTERSPRINGFOOTPRINTS_S.BLP", materialfile::kSnow},
        {"IRONFORGEFOOTPRINTSALPHA_S.BLP", materialfile::kSnow},
        {"ZD_LAKEICEC_S.BLP", materialfile::kIce},
        {"IG_DIRTYICEC_S.BLP", materialfile::kIce},
        {"DREAMFORESTPATH_S.BLP", materialfile::kSoil},
    };
    return named;
}

std::string WithoutSpecularSuffix(const std::string& base)
{
    static const std::string suffix = "_S.BLP";
    if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
        return base.substr(0, base.size() - suffix.size()) + ".BLP";
    return base;
}
}

std::string MaterialBaseName(const std::string& gamePath)
{
    const size_t slash = gamePath.find_last_of("\\/");
    std::string base = slash == std::string::npos ? gamePath : gamePath.substr(slash + 1);
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return base;
}

std::string MaterialPlaceStrippedName(const std::string& gamePath)
{
    std::string stripped = MaterialBaseName(gamePath);
    for (const char* place : PlaceNames())
        for (size_t at = stripped.find(place); at != std::string::npos; at = stripped.find(place))
            stripped.replace(at, std::char_traits<char>::length(place), "_");
    return stripped;
}

uint8_t MaterialKindOfName(const std::string& gamePath, std::string* word)
{
    const std::string base = MaterialBaseName(gamePath);
    for (const NamedKind& n : NamedKinds())
        if (base == n.base || WithoutSpecularSuffix(n.base) == base)
        {
            if (word)
                *word = "(named)";
            return n.kind;
        }
    const std::string stripped = MaterialPlaceStrippedName(gamePath);
    for (const WordRule& rule : WordRules())
        for (const char* w : rule.words)
            if (stripped.find(w) != std::string::npos)
            {
                if (word)
                    *word = w;
                return rule.kind;
            }
    if (word)
        word->clear();
    return materialfile::kUnknown;
}
