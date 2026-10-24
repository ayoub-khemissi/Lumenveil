#include "world_scan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "archive_set.h"
#include "byte_view.h"
#include "dbc_table.h"
#include "liquid_scan.h"
#include "m2_lights.h"
#include "parallel.h"
#include "wmo_lights.h"

namespace
{

constexpr uint32_t kWdtGlobalWmo = 0x01;
constexpr uint32_t kMainHasAdt = 0x01;
constexpr int kTilesPerSide = 64;
constexpr size_t kMainEntrySize = 8;

constexpr size_t kMddfStride = 36;
constexpr size_t kMddfUniqueId = 0x04;
constexpr size_t kMddfPosition = 0x08;
constexpr size_t kMddfRotation = 0x14;
constexpr size_t kMddfScale = 0x20;

constexpr size_t kModfStride = 64;
constexpr size_t kModfUniqueId = 0x04;
constexpr size_t kModfPosition = 0x08;
constexpr size_t kModfRotation = 0x14;
constexpr size_t kModfDoodadSet = 0x3A;

constexpr float kDuplicateDistance = 0.5f;
constexpr float kFixtureNearLampDistance = 3.0f;

bool IsWmoPath(const std::string& path)
{
    std::string upper = UpperPath(path);
    return upper.size() > 4 && upper.compare(upper.size() - 4, 4, ".WMO") == 0;
}

std::vector<std::string> NameTable(ByteView names, ByteView offsets)
{
    std::vector<std::string> table;
    for (size_t at = 0; at + 4 <= offsets.Size(); at += 4)
        table.push_back(names.CString(offsets.Get<uint32_t>(at)));
    return table;
}

lampfile::Lamp LampRecord(Vec3 position, Rgb color, float intensity, float reach, lampfile::LampKind kind,
                          uint16_t flags)
{
    lampfile::Lamp lamp{};
    lamp.position[0] = position.x;
    lamp.position[1] = position.y;
    lamp.position[2] = position.z;
    lamp.color[0] = color.r;
    lamp.color[1] = color.g;
    lamp.color[2] = color.b;
    lamp.intensity = intensity;
    lamp.reach = reach;
    lamp.kind = static_cast<uint8_t>(kind);
    lamp.flags = flags;
    return lamp;
}

Vec3 LampPosition(const lampfile::Lamp& lamp)
{
    return {lamp.position[0], lamp.position[1], lamp.position[2]};
}

int64_t GridKey(Vec3 p, float cell)
{
    int64_t x = static_cast<int64_t>(std::floor(p.x / cell)) + (1 << 20);
    int64_t y = static_cast<int64_t>(std::floor(p.y / cell)) + (1 << 20);
    int64_t z = static_cast<int64_t>(std::floor(p.z / cell)) + (1 << 20);
    return (x << 42) | (y << 21) | z;
}

PlacedInteriors InteriorsOf(const WmoInfo& info, const Placement& placement)
{
    PlacedInteriors placed;
    placed.placement = placement;
    placed.boxes = &info.interiors;
    placed.low = {INFINITY, INFINITY, INFINITY};
    placed.high = {-INFINITY, -INFINITY, -INFINITY};
    for (const WmoBox& box : info.interiors)
        for (int corner = 0; corner < 8; ++corner)
        {
            Vec3 local{(corner & 1) ? box.high.x : box.low.x, (corner & 2) ? box.high.y : box.low.y,
                       (corner & 4) ? box.high.z : box.low.z};
            Vec3 world = placement.Point(local);
            placed.low = {std::min(placed.low.x, world.x), std::min(placed.low.y, world.y),
                          std::min(placed.low.z, world.z)};
            placed.high = {std::max(placed.high.x, world.x), std::max(placed.high.y, world.y),
                           std::max(placed.high.z, world.z)};
        }
    return placed;
}

bool Inside(Vec3 p, Vec3 low, Vec3 high)
{
    return p.x >= low.x && p.y >= low.y && p.z >= low.z && p.x <= high.x && p.y <= high.y && p.z <= high.z;
}

Vec3 ToLocal(const Placement& placement, Vec3 world)
{
    const Vec3 d = world - placement.origin;
    const float (&m)[3][3] = placement.rotation.m;
    const float inverseScale = placement.scale > 0.0f ? 1.0f / placement.scale : 1.0f;
    return Vec3{m[0][0] * d.x + m[1][0] * d.y + m[2][0] * d.z, m[0][1] * d.x + m[1][1] * d.y + m[2][1] * d.z,
                m[0][2] * d.x + m[1][2] * d.y + m[2][2] * d.z} *
           inverseScale;
}

template <typename Visit>
void ForNeighbours(const std::unordered_map<int64_t, std::vector<size_t>>& grid, Vec3 p, float cell, Visit visit)
{
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
            {
                auto it = grid.find(GridKey(p + Vec3{dx * cell, dy * cell, dz * cell}, cell));
                if (it == grid.end())
                    continue;
                for (size_t index : it->second)
                    visit(index);
            }
}

AdtTile ParseAdtTile(const std::vector<uint8_t>& file)
{
    AdtTile tile;
    tile.found = true;
    std::vector<Chunk> chunks;
    for (const Chunk& chunk : ChunksOf(ByteView(file)))
    {
        if (chunk.id == ChunkId("MCNK"))
            break;
        chunks.push_back(chunk);
    }
    tile.models = NameTable(FindChunk(chunks, ChunkId("MMDX")), FindChunk(chunks, ChunkId("MMID")));
    tile.wmos = NameTable(FindChunk(chunks, ChunkId("MWMO")), FindChunk(chunks, ChunkId("MWID")));
    ByteView doodads = FindChunk(chunks, ChunkId("MDDF"));
    for (size_t at = 0; at + kMddfStride <= doodads.Size(); at += kMddfStride)
        tile.doodads.push_back({doodads.Get<uint32_t>(at), doodads.Get<uint32_t>(at + kMddfUniqueId),
                                doodads.Get<Vec3>(at + kMddfPosition), doodads.Get<Vec3>(at + kMddfRotation),
                                doodads.Get<uint16_t>(at + kMddfScale)});
    ByteView objects = FindChunk(chunks, ChunkId("MODF"));
    for (size_t at = 0; at + kModfStride <= objects.Size(); at += kModfStride)
        tile.objects.push_back({objects.Get<uint32_t>(at), objects.Get<uint32_t>(at + kModfUniqueId),
                                objects.Get<Vec3>(at + kModfPosition), objects.Get<Vec3>(at + kModfRotation),
                                objects.Get<uint16_t>(at + kModfDoodadSet)});
    return tile;
}
}

bool WorldScanner::LoadTables()
{
    DbcTable maps;
    std::optional<std::vector<uint8_t>> mapFile = m_archives.Read("DBFilesClient\\Map.dbc");
    if (!mapFile || !maps.Load(std::move(*mapFile)))
        return false;
    for (uint32_t row = 0; row < maps.Rows(); ++row)
        m_maps.push_back({maps.U32(row, 0), maps.String(row, 1)});

    DbcTable displays;
    std::optional<std::vector<uint8_t>> displayFile = m_archives.Read("DBFilesClient\\GameObjectDisplayInfo.dbc");
    if (displayFile && displays.Load(std::move(*displayFile)))
        for (uint32_t row = 0; row < displays.Rows(); ++row)
            m_displayModels[displays.U32(row, 0)] = displays.String(row, 1);
    return true;
}

void WorldScanner::PlaceModel(const std::string& model, const Placement& placement, uint16_t extraFlags,
                              MapLights& out)
{
    if (model.empty())
        return;
    ++m_modelPlacements[M2PathOf(model)];
    const ModelLights& lights = m_models.Of(model);
    for (const ModelLight& light : lights.lights)
    {
        WorldLamp lamp;
        lamp.record = LampRecord(placement.Point(light.position), light.color, light.intensity, light.reach,
                                 light.kind, static_cast<uint16_t>(light.flags | extraFlags));
        lamp.source = M2PathOf(model);
        out.lamps.push_back(std::move(lamp));
    }
}

void WorldScanner::PlaceWmo(const std::string& wmo, const Placement& placement, uint32_t doodadSet, MapLights& out)
{
    const WmoInfo& info = m_wmos.Of(wmo);
    std::string source = UpperPath(wmo);
    ++m_wmoPlacements[source];
    if (!info.found)
        return;
    if (!info.interiors.empty())
        m_interiors.push_back(InteriorsOf(info, placement));

    for (const WmoFixture& fixture : info.fixtures)
    {
        if (!fixture.referencedIndoors && !fixture.referencedOutdoors)
            continue;
        uint16_t flags = lampfile::kLampWmoFixture;
        if (fixture.referencedIndoors && !fixture.referencedOutdoors)
            flags |= lampfile::kLampIndoor;
        float peak = 0.0f;
        Rgb color = PeakNormalized(fixture.color, &peak);
        WorldLamp lamp;
        lamp.record = LampRecord(placement.Point(fixture.position), color, fixture.intensity * peak, fixture.reach,
                                 lampfile::LampKind::Fixture, flags);
        lamp.source = source;
        out.lamps.push_back(std::move(lamp));
    }

    const float lanternIntensity = LampIntensity(NameKind::Lamp);
    for (const WmoWindow& lantern : info.lanterns)
    {
        const uint16_t flags =
            static_cast<uint16_t>(lampfile::kLampWmoLantern | (lantern.indoor ? lampfile::kLampIndoor : 0));
        WorldLamp lamp;
        lamp.record = LampRecord(placement.Point(lantern.center),
                                 SourceColor(source, lampfile::LampKind::Lamp, lantern.color), lanternIntensity,
                                 ReachFromIntensity(lanternIntensity), lampfile::LampKind::Lamp, flags);
        lamp.source = source;
        out.lamps.push_back(std::move(lamp));
    }

    std::vector<uint32_t> activeSets{0};
    if (doodadSet != 0 && doodadSet < info.sets.size())
        activeSets.push_back(doodadSet);
    for (uint32_t set : activeSets)
    {
        if (set >= info.sets.size())
            continue;
        const WmoDoodadSet& range = info.sets[set];
        for (uint32_t i = range.start; i < range.start + range.count && i < info.doodads.size(); ++i)
        {
            const WmoDoodad& doodad = info.doodads[i];
            if (!doodad.referencedIndoors && !doodad.referencedOutdoors)
                continue;
            Placement local;
            local.origin = doodad.position;
            local.rotation =
                QuaternionMatrix(doodad.rotation[0], doodad.rotation[1], doodad.rotation[2], doodad.rotation[3]);
            local.scale = doodad.scale > 0.0f && std::isfinite(doodad.scale) ? doodad.scale : 1.0f;
            uint16_t flags = lampfile::kLampWmoDoodad;
            if (doodad.referencedIndoors && !doodad.referencedOutdoors)
                flags |= lampfile::kLampIndoor;
            PlaceModel(doodad.model, placement.Then(local), flags, out);
        }
    }

    for (const WmoWindow& window : info.windows)
    {
        Vec3 center = placement.Point(window.center);
        Vec3 normal = Normalized(placement.Direction(window.normal));
        Vec3 halfWidth = placement.Direction(window.halfWidth) * placement.scale;
        WorldWindow out_window;
        lampfile::Window& record = out_window.record;
        record.center[0] = center.x;
        record.center[1] = center.y;
        record.center[2] = center.z;
        record.normal[0] = normal.x;
        record.normal[1] = normal.y;
        record.normal[2] = normal.z;
        record.halfWidth[0] = halfWidth.x;
        record.halfWidth[1] = halfWidth.y;
        record.halfWidth[2] = halfWidth.z;
        record.halfHeight = window.halfHeight * placement.scale;
        record.color[0] = window.color.r;
        record.color[1] = window.color.g;
        record.color[2] = window.color.b;
        record.area = window.area * placement.scale * placement.scale;
        record.flags = static_cast<uint16_t>((window.indoor ? lampfile::kWindowIndoor : 0) |
                                             (std::fabs(normal.z) > 0.9f ? lampfile::kWindowFacesUp : 0));
        out_window.source = source;
        out.windows.push_back(std::move(out_window));
    }
}

void WorldScanner::PlaceTile(const AdtTile& tile, MapLights& out)
{
    if (!tile.found)
    {
        ++out.missingTiles;
        return;
    }
    ++out.tiles;
    for (const AdtDoodad& doodad : tile.doodads)
    {
        if (doodad.nameIndex >= tile.models.size() || !m_seenDoodads.insert(doodad.uniqueId).second)
            continue;
        ++out.doodadPlacements;
        float scale = doodad.scale / 1024.0f;
        Placement placement = AdtPlacement(doodad.position, doodad.rotation, scale > 0.0f ? scale : 1.0f, false);
        PlaceModel(tile.models[doodad.nameIndex], placement, 0, out);
    }
    for (const AdtObject& object : tile.objects)
    {
        if (object.nameIndex >= tile.wmos.size() || !m_seenWmos.insert(object.uniqueId).second)
            continue;
        ++out.wmoPlacements;
        Placement placement = AdtPlacement(object.position, object.rotation, 1.0f, false);
        PlaceWmo(tile.wmos[object.nameIndex], placement, object.doodadSet, out);
    }
}

void WorldScanner::Prepare(const std::string& globalWmo, uint32_t globalSet, const std::vector<AdtTile>& tiles,
                           const std::vector<const ArchiveSet*>& readers)
{
    std::vector<std::pair<std::string, uint32_t>> wmoPlacements;
    std::unordered_set<std::string> seenWmoSets;
    auto addWmo = [&](const std::string& name, uint32_t set) {
        if (seenWmoSets.insert(name + "#" + std::to_string(set)).second)
            wmoPlacements.emplace_back(name, set);
    };
    if (!globalWmo.empty())
        addWmo(globalWmo, globalSet);
    std::vector<std::string> models;
    for (const AdtTile& tile : tiles)
    {
        for (const AdtDoodad& doodad : tile.doodads)
            if (doodad.nameIndex < tile.models.size())
                models.push_back(tile.models[doodad.nameIndex]);
        for (const AdtObject& object : tile.objects)
            if (object.nameIndex < tile.wmos.size())
                addWmo(tile.wmos[object.nameIndex], object.doodadSet);
    }
    std::vector<std::string> wmoNames;
    for (const auto& placement : wmoPlacements)
        wmoNames.push_back(placement.first);
    m_wmos.Prepare(wmoNames, readers);
    for (const auto& [name, doodadSet] : wmoPlacements)
    {
        const WmoInfo& info = m_wmos.Of(name);
        if (!info.found)
            continue;
        std::vector<uint32_t> activeSets{0};
        if (doodadSet != 0 && doodadSet < info.sets.size())
            activeSets.push_back(doodadSet);
        for (uint32_t set : activeSets)
        {
            if (set >= info.sets.size())
                continue;
            const WmoDoodadSet& range = info.sets[set];
            for (uint32_t i = range.start; i < range.start + range.count && i < info.doodads.size(); ++i)
                if (info.doodads[i].referencedIndoors || info.doodads[i].referencedOutdoors)
                    models.push_back(info.doodads[i].model);
        }
    }
    m_models.Prepare(models, readers);
}

MapLights WorldScanner::ScanMap(const MapInfo& map, const std::vector<GameObjectSpawn>& gameObjects,
                                const std::vector<const ArchiveSet*>& readers, const LiquidScanner* liquids,
                                MapLiquids* liquidOut)
{
    MapLights out;
    out.mapId = map.id;
    out.directory = map.directory;
    m_seenDoodads.clear();
    m_seenWmos.clear();
    m_interiors.clear();

    const std::vector<const ArchiveSet*> workers =
        readers.empty() ? std::vector<const ArchiveSet*>{&m_archives} : readers;
    const bool scanLiquids = liquids != nullptr && liquidOut != nullptr;
    std::string base = "World\\Maps\\" + map.directory + "\\" + map.directory;
    std::optional<std::vector<uint8_t>> wdtFile = m_archives.Read(base + ".wdt");
    if (wdtFile)
    {
        std::vector<Chunk> chunks = ChunksOf(ByteView(*wdtFile));
        uint32_t flags = FindChunk(chunks, ChunkId("MPHD")).Get<uint32_t>(0);
        std::string globalWmo;
        ByteView globalPlacement;
        if (flags & kWdtGlobalWmo)
        {
            globalWmo = FindChunk(chunks, ChunkId("MWMO")).CString(0);
            globalPlacement = FindChunk(chunks, ChunkId("MODF"));
        }
        const bool global = !globalWmo.empty() && globalPlacement.Size() >= kModfStride;
        ByteView main = FindChunk(chunks, ChunkId("MAIN"));
        std::vector<std::pair<int, int>> spots;
        for (int y = 0; y < kTilesPerSide; ++y)
            for (int x = 0; x < kTilesPerSide; ++x)
                if (main.Get<uint32_t>(static_cast<size_t>(y * kTilesPerSide + x) * kMainEntrySize) & kMainHasAdt)
                    spots.emplace_back(x, y);
        std::vector<AdtTile> tiles(spots.size());
        std::vector<MapLiquids> tileLiquids(scanLiquids ? spots.size() : 0);
        ParallelFor(spots.size(), workers.size(), [&](size_t i, size_t worker) {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "_%d_%d.adt", spots[i].first, spots[i].second);
            std::optional<std::vector<uint8_t>> file = workers[worker]->Read(base + suffix);
            if (!file)
                return;
            tiles[i] = ParseAdtTile(*file);
            if (scanLiquids)
                liquids->ScanAdtBytes(*file, spots[i].first, spots[i].second, tileLiquids[i]);
        });
        Prepare(global ? globalWmo : std::string(), global ? globalPlacement.Get<uint16_t>(kModfDoodadSet) : 0u,
                tiles, workers);
        if (global)
        {
            ++out.wmoPlacements;
            PlaceWmo(globalWmo,
                     AdtPlacement(globalPlacement.Get<Vec3>(kModfPosition), globalPlacement.Get<Vec3>(kModfRotation),
                                  1.0f, true),
                     globalPlacement.Get<uint16_t>(kModfDoodadSet), out);
        }
        for (const AdtTile& tile : tiles)
            PlaceTile(tile, out);
        if (scanLiquids)
        {
            liquidOut->mapId = map.id;
            liquidOut->directory = map.directory;
            for (const MapLiquids& tile : tileLiquids)
                AddTileLiquids(*liquidOut, tile);
            SortLiquids(*liquidOut);
        }
    }

    for (const GameObjectSpawn& spawn : gameObjects)
    {
        if (spawn.map != map.id)
            continue;
        ++out.gameObjects;
        auto model = m_displayModels.find(spawn.displayId);
        if (model == m_displayModels.end() || model->second.empty())
        {
            ++out.gameObjectsWithoutModel;
            continue;
        }
        if (IsWmoPath(model->second))
        {
            ++out.gameObjectWmos;
            continue;
        }
        Placement placement;
        placement.origin = spawn.position;
        float norm = std::sqrt(spawn.rotation[0] * spawn.rotation[0] + spawn.rotation[1] * spawn.rotation[1] +
                               spawn.rotation[2] * spawn.rotation[2] + spawn.rotation[3] * spawn.rotation[3]);
        placement.rotation = norm > 0.5f ? QuaternionMatrix(spawn.rotation[0], spawn.rotation[1], spawn.rotation[2],
                                                            spawn.rotation[3])
                                         : RotationZ(spawn.orientation);
        placement.scale = spawn.size > 0.0f ? spawn.size : 1.0f;
        const size_t first = out.lamps.size();
        PlaceModel(model->second, placement, lampfile::kLampGameObject, out);
        for (size_t i = first; i < out.lamps.size(); ++i)
            if (InsideAnInterior(LampPosition(out.lamps[i].record)))
            {
                out.lamps[i].record.flags |= lampfile::kLampIndoor;
                ++out.gameObjectLampsIndoors;
            }
    }

    Finish(out);
    return out;
}

std::vector<ObjectModelLights> WorldScanner::ObjectModels(const std::vector<const ArchiveSet*>& readers)
{
    std::vector<std::pair<uint32_t, std::string>> displays;
    for (const auto& [displayId, model] : m_displayModels)
        if (!model.empty() && !IsWmoPath(model))
            displays.emplace_back(displayId, model);
    std::sort(displays.begin(), displays.end());
    std::vector<std::string> models;
    for (const auto& display : displays)
        models.push_back(display.second);
    m_models.Prepare(models, readers.empty() ? std::vector<const ArchiveSet*>{&m_archives} : readers);
    std::vector<ObjectModelLights> out;
    for (const auto& [displayId, model] : displays)
    {
        ObjectModelLights object;
        object.displayId = displayId;
        for (const ModelLight& light : m_models.Of(model).lights)
        {
            const lampfile::Lamp record =
                LampRecord(light.position, light.color, light.intensity, light.reach, light.kind,
                           static_cast<uint16_t>(light.flags | lampfile::kLampGameObject));
            bool duplicate = false;
            for (const WorldLamp& kept : object.lamps)
                duplicate = duplicate ||
                            (kept.record.kind == record.kind &&
                             Length(LampPosition(kept.record) - LampPosition(record)) < kDuplicateDistance);
            if (!duplicate)
                object.lamps.push_back({record, M2PathOf(model)});
        }
        if (!object.lamps.empty())
            out.push_back(std::move(object));
    }
    return out;
}

bool WorldScanner::InsideAnInterior(Vec3 p) const
{
    for (const PlacedInteriors& placed : m_interiors)
    {
        if (!Inside(p, placed.low, placed.high))
            continue;
        const Vec3 local = ToLocal(placed.placement, p);
        for (const WmoBox& box : *placed.boxes)
            if (Inside(local, box.low, box.high))
                return true;
    }
    return false;
}

void WorldScanner::Finish(MapLights& out)
{
    const float cell = kFixtureNearLampDistance;
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    std::vector<WorldLamp> kept;
    kept.reserve(out.lamps.size());
    for (WorldLamp& lamp : out.lamps)
    {
        Vec3 p = LampPosition(lamp.record);
        bool fixture = (lamp.record.flags & lampfile::kLampWmoFixture) != 0;
        bool duplicate = false;
        if (!fixture)
        {
            ForNeighbours(grid, p, cell, [&](size_t index) {
                const lampfile::Lamp& other = kept[index].record;
                if (!(other.flags & lampfile::kLampWmoFixture) && other.kind == lamp.record.kind &&
                    Length(LampPosition(other) - p) < kDuplicateDistance)
                    duplicate = true;
            });
        }
        if (duplicate)
        {
            ++out.duplicateLamps;
            continue;
        }
        grid[GridKey(p, cell)].push_back(kept.size());
        kept.push_back(std::move(lamp));
    }
    for (WorldLamp& lamp : kept)
    {
        if (!(lamp.record.flags & lampfile::kLampWmoFixture))
            continue;
        Vec3 p = LampPosition(lamp.record);
        ForNeighbours(grid, p, cell, [&](size_t index) {
            const lampfile::Lamp& other = kept[index].record;
            if (!(other.flags & lampfile::kLampWmoFixture) &&
                Length(LampPosition(other) - p) < kFixtureNearLampDistance)
                lamp.record.flags |= lampfile::kLampFixtureNearLamp;
        });
    }
    out.lamps = std::move(kept);
}
