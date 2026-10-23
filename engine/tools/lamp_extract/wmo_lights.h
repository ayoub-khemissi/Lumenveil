#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include "geometry.h"
#include "lamp_rules.h"

class ArchiveSet;
class ModelLightCatalogue;

struct WmoFixture
{
    Vec3 position;
    Rgb color;
    float intensity = 1.0f;
    float reach = 12.0f;
    uint8_t type = 0;
    bool usesAttenuation = false;
    bool referencedIndoors = false;
    bool referencedOutdoors = false;
};

struct WmoDoodad
{
    std::string model;
    Vec3 position;
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float scale = 1.0f;
    bool referencedIndoors = false;
    bool referencedOutdoors = false;
};

struct WmoDoodadSet
{
    uint32_t start = 0;
    uint32_t count = 0;
};

struct WmoWindow
{
    Vec3 center;
    Vec3 normal;
    Vec3 halfWidth;
    float halfHeight = 0.0f;
    Rgb color;
    float area = 0.0f;
    bool indoor = false;
    bool closed = false;
    uint32_t group = 0;
};

struct WmoBox
{
    Vec3 low;
    Vec3 high;
};

struct WmoGroup
{
    uint32_t index = 0;
    uint32_t flags = 0;
    Vec3 low;
    Vec3 high;
    std::vector<uint16_t> doodads;
    std::vector<uint16_t> fixtures;
};

struct WmoInfo
{
    bool found = false;
    uint32_t groupCount = 0;
    uint32_t groupsRead = 0;
    uint32_t sidnMaterials = 0;
    uint32_t darkSidnBatches = 0;
    Vec3 boundsMin;
    Vec3 boundsMax;
    std::vector<WmoFixture> fixtures;
    std::vector<WmoDoodadSet> sets;
    std::vector<WmoDoodad> doodads;
    std::vector<WmoWindow> windows;
    std::vector<WmoWindow> lanterns;
    std::vector<WmoBox> interiors;
    std::vector<WmoGroup> groups;
};

class WmoCatalogue
{
public:
    explicit WmoCatalogue(const ArchiveSet& archives) : m_archives(archives) {}

    const WmoInfo& Of(const std::string& name);
    void Prepare(const std::vector<std::string>& names, const std::vector<const ArchiveSet*>& readers);
    const std::unordered_map<std::string, WmoInfo>& Entries() const { return m_wmos; }
    void Describe(const std::string& name, ModelLightCatalogue& models, std::ostream& report);

private:
    WmoInfo Analyse(const std::string& path) const;

    const ArchiveSet& m_archives;
    std::unordered_map<std::string, WmoInfo> m_wmos;
};
