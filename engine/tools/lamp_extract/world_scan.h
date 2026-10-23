#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "geometry.h"
#include "lamp_data.h"

class ArchiveSet;
class LiquidScanner;
class ModelLightCatalogue;
class WmoCatalogue;
struct MapLiquids;
struct WmoBox;

struct PlacedInteriors
{
    Placement placement;
    Vec3 low;
    Vec3 high;
    const std::vector<WmoBox>* boxes = nullptr;
};

struct WorldLamp
{
    lampfile::Lamp record{};
    std::string source;
};

struct WorldWindow
{
    lampfile::Window record{};
    std::string source;
};

struct GameObjectSpawn
{
    uint32_t map = 0;
    Vec3 position;
    float orientation = 0.0f;
    float rotation[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t displayId = 0;
    float size = 1.0f;
    uint32_t entry = 0;
    std::string name;
};

struct MapLights
{
    uint32_t mapId = 0;
    std::string directory;
    std::vector<WorldLamp> lamps;
    std::vector<WorldWindow> windows;
    uint32_t tiles = 0;
    uint32_t missingTiles = 0;
    uint32_t doodadPlacements = 0;
    uint32_t wmoPlacements = 0;
    uint32_t gameObjects = 0;
    uint32_t gameObjectsWithoutModel = 0;
    uint32_t gameObjectWmos = 0;
    uint32_t gameObjectLampsIndoors = 0;
    uint32_t duplicateLamps = 0;
};

struct MapInfo
{
    uint32_t id = 0;
    std::string directory;
};

struct AdtDoodad
{
    uint32_t nameIndex = 0;
    uint32_t uniqueId = 0;
    Vec3 position;
    Vec3 rotation;
    uint16_t scale = 0;
};

struct AdtObject
{
    uint32_t nameIndex = 0;
    uint32_t uniqueId = 0;
    Vec3 position;
    Vec3 rotation;
    uint16_t doodadSet = 0;
};

struct AdtTile
{
    bool found = false;
    std::vector<std::string> models;
    std::vector<std::string> wmos;
    std::vector<AdtDoodad> doodads;
    std::vector<AdtObject> objects;
};

class WorldScanner
{
public:
    WorldScanner(const ArchiveSet& archives, ModelLightCatalogue& models, WmoCatalogue& wmos)
        : m_archives(archives), m_models(models), m_wmos(wmos)
    {
    }

    bool LoadTables();
    const std::vector<MapInfo>& Maps() const { return m_maps; }
    MapLights ScanMap(const MapInfo& map, const std::vector<GameObjectSpawn>& gameObjects,
                      const std::vector<const ArchiveSet*>& readers = {}, const LiquidScanner* liquids = nullptr,
                      MapLiquids* liquidOut = nullptr);
    const std::unordered_map<std::string, uint32_t>& ModelPlacements() const { return m_modelPlacements; }
    const std::unordered_map<std::string, uint32_t>& WmoPlacements() const { return m_wmoPlacements; }

private:
    void PlaceModel(const std::string& model, const Placement& placement, uint16_t extraFlags, MapLights& out);
    void PlaceWmo(const std::string& wmo, const Placement& placement, uint32_t doodadSet, MapLights& out);
    void PlaceTile(const AdtTile& tile, MapLights& out);
    void Prepare(const std::string& globalWmo, uint32_t globalSet, const std::vector<AdtTile>& tiles,
                 const std::vector<const ArchiveSet*>& readers);
    bool InsideAnInterior(Vec3 p) const;
    void Finish(MapLights& out);

    const ArchiveSet& m_archives;
    ModelLightCatalogue& m_models;
    WmoCatalogue& m_wmos;
    std::vector<MapInfo> m_maps;
    std::unordered_map<uint32_t, std::string> m_displayModels;
    std::unordered_set<uint32_t> m_seenDoodads;
    std::unordered_set<uint32_t> m_seenWmos;
    std::vector<PlacedInteriors> m_interiors;
    std::unordered_map<std::string, uint32_t> m_modelPlacements;
    std::unordered_map<std::string, uint32_t> m_wmoPlacements;
};
