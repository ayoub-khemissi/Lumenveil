#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "geometry.h"
#include "lamp_rules.h"

class ArchiveSet;

struct ModelLight
{
    Vec3 position;
    Rgb color;
    float intensity = 1.5f;
    float reach = 12.0f;
    lampfile::LampKind kind = lampfile::LampKind::Torch;
    uint16_t flags = 0;
};

enum class ModelRoute
{
    Missing,
    Unreadable,
    NoLight,
    AuthoredDark,
    Authored,
    Flame,
    Name,
    Emissive,
};

struct ModelLights
{
    ModelRoute route = ModelRoute::Missing;
    std::vector<ModelLight> lights;
    uint32_t emitters = 0;
    uint32_t fireEmitters = 0;
    uint32_t invalidEmitters = 0;
    uint32_t authoredBlocks = 0;
    uint32_t batches = 0;
    uint32_t unlitBatches = 0;
    float bestFireStrength = 0.0f;
    uint32_t clampedLights = 0;
    uint32_t rigLights = 0;
};

const char* RouteName(ModelRoute route);
std::string M2PathOf(std::string_view name);

class ModelLightCatalogue
{
public:
    explicit ModelLightCatalogue(const ArchiveSet& archives) : m_archives(archives) {}

    const ModelLights& Of(const std::string& modelName);
    const std::unordered_map<std::string, ModelLights>& Entries() const { return m_models; }
    void Describe(const std::string& modelName, std::ostream& report) const;

private:
    ModelLights Analyse(const std::string& path, std::ostream* report = nullptr) const;

    const ArchiveSet& m_archives;
    std::unordered_map<std::string, ModelLights> m_models;
};
