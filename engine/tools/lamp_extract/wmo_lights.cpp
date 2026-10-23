#include "wmo_lights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <unordered_set>

#include "archive_set.h"
#include "byte_view.h"
#include "m2_lights.h"
#include "parallel.h"

namespace
{

constexpr size_t kMohdGroups = 0x04;
constexpr size_t kMohdBoundsMin = 0x24;
constexpr size_t kMohdBoundsMax = 0x30;

constexpr size_t kMaterialStride = 64;
constexpr size_t kMaterialSidnColor = 0x10;
constexpr uint32_t kMaterialSidn = 0x10;

constexpr size_t kLightStride = 48;
constexpr size_t kLightUseAttenuation = 0x01;
constexpr size_t kLightColor = 0x04;
constexpr size_t kLightPosition = 0x08;
constexpr size_t kLightIntensity = 0x14;
constexpr size_t kLightAttenuationEnd = 0x2C;

constexpr size_t kSetStride = 32;
constexpr size_t kSetStart = 0x14;
constexpr size_t kSetCount = 0x18;

constexpr size_t kDoodadStride = 40;
constexpr size_t kDoodadPosition = 0x04;
constexpr size_t kDoodadRotation = 0x10;
constexpr size_t kDoodadScale = 0x20;

constexpr size_t kGroupHeaderSize = 0x44;
constexpr size_t kGroupFlags = 0x08;
constexpr size_t kGroupBoundsMin = 0x0C;
constexpr size_t kGroupBoundsMax = 0x18;
constexpr uint32_t kGroupExterior = 0x8;
constexpr uint32_t kGroupExteriorLit = 0x40;
constexpr uint32_t kGroupInterior = 0x2000;

constexpr size_t kBatchStride = 24;
constexpr size_t kBatchStartIndex = 0x0C;
constexpr size_t kBatchIndexCount = 0x10;
constexpr size_t kBatchMaterial = 0x17;

constexpr float kMinWindowArea = 0.05f;
constexpr float kMinWindowGlow = 0.01f;
constexpr float kFacesUpNormalZ = 0.9f;
constexpr float kAmbiguousNormalRatio = 0.3f;
constexpr float kMaxLanternArea = 3.0f;
constexpr float kMaxLanternHalfExtent = 0.6f;

bool IsRoom(uint32_t groupFlags)
{
    return (groupFlags & kGroupInterior) && !(groupFlags & (kGroupExterior | kGroupExteriorLit));
}

Rgb BgraColor(ByteView view, size_t at)
{
    return {view.Get<uint8_t>(at + 2) / 255.0f, view.Get<uint8_t>(at + 1) / 255.0f, view.Get<uint8_t>(at) / 255.0f};
}

class UnionFind
{
public:
    explicit UnionFind(size_t size) : m_parent(size) { std::iota(m_parent.begin(), m_parent.end(), 0u); }

    uint32_t Find(uint32_t a)
    {
        while (m_parent[a] != a)
        {
            m_parent[a] = m_parent[m_parent[a]];
            a = m_parent[a];
        }
        return a;
    }

    void Join(uint32_t a, uint32_t b) { m_parent[Find(a)] = Find(b); }

private:
    std::vector<uint32_t> m_parent;
};

struct Triangle
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

uint64_t PositionKey(Vec3 p)
{
    auto quantize = [](float v) {
        return static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 100.0f)) & 0x1FFFFF);
    };
    return (quantize(p.x) << 42) | (quantize(p.y) << 21) | quantize(p.z);
}

std::optional<WmoWindow> WindowOf(const std::vector<Triangle>& triangles, const std::vector<Vec3>& vertices)
{
    Vec3 crossSum;
    Vec3 weightedCenter;
    float twiceArea = 0.0f;
    Vec3 largestNormal{0.0f, 0.0f, 1.0f};
    float largest = 0.0f;
    for (const Triangle& t : triangles)
    {
        Vec3 a = vertices[t.a], b = vertices[t.b], c = vertices[t.c];
        Vec3 cross = Cross(b - a, c - a);
        float length = Length(cross);
        crossSum = crossSum + cross;
        twiceArea += length;
        weightedCenter = weightedCenter + (a + b + c) * (length / 3.0f);
        if (length > largest)
        {
            largest = length;
            largestNormal = cross;
        }
    }
    float area = twiceArea * 0.5f;
    if (area < kMinWindowArea)
        return std::nullopt;
    WmoWindow window;
    window.area = area;
    Vec3 center = weightedCenter * (1.0f / twiceArea);
    window.closed = Length(crossSum) < kAmbiguousNormalRatio * twiceArea;
    window.normal = window.closed ? Normalized(largestNormal) : Normalized(crossSum);
    Vec3 up{0.0f, 0.0f, 1.0f};
    Vec3 u = std::fabs(window.normal.z) > kFacesUpNormalZ ? Vec3{1.0f, 0.0f, 0.0f}
                                                          : Normalized(Cross(up, window.normal));
    Vec3 v = Normalized(Cross(window.normal, u));
    float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
    for (const Triangle& t : triangles)
    {
        for (uint32_t index : {t.a, t.b, t.c})
        {
            Vec3 d = vertices[index] - center;
            minU = std::min(minU, Dot(d, u));
            maxU = std::max(maxU, Dot(d, u));
            minV = std::min(minV, Dot(d, v));
            maxV = std::max(maxV, Dot(d, v));
        }
    }
    window.center = center + u * ((minU + maxU) * 0.5f) + v * ((minV + maxV) * 0.5f);
    window.halfWidth = u * ((maxU - minU) * 0.5f);
    window.halfHeight = (maxV - minV) * 0.5f;
    return window;
}

bool IsLantern(const WmoWindow& window)
{
    return window.closed && window.area <= kMaxLanternArea && Length(window.halfWidth) <= kMaxLanternHalfExtent &&
           window.halfHeight <= kMaxLanternHalfExtent;
}

void AddWindows(const std::vector<Triangle>& triangles, const std::vector<Vec3>& vertices, Rgb color, bool indoor,
                uint32_t group, std::vector<WmoWindow>& out, std::vector<WmoWindow>& lanterns)
{
    UnionFind sets(vertices.size());
    std::unordered_map<uint64_t, uint32_t> byPosition;
    for (const Triangle& t : triangles)
    {
        sets.Join(t.a, t.b);
        sets.Join(t.b, t.c);
        for (uint32_t index : {t.a, t.b, t.c})
        {
            auto [it, inserted] = byPosition.emplace(PositionKey(vertices[index]), index);
            if (!inserted)
                sets.Join(index, it->second);
        }
    }
    std::unordered_map<uint32_t, std::vector<Triangle>> components;
    for (const Triangle& t : triangles)
        components[sets.Find(t.a)].push_back(t);
    for (const auto& [root, parts] : components)
    {
        std::optional<WmoWindow> window = WindowOf(parts, vertices);
        if (!window)
            continue;
        window->color = color;
        window->indoor = indoor;
        window->group = group;
        (IsLantern(*window) ? lanterns : out).push_back(*window);
    }
}

std::string GroupPath(const std::string& rootPath, uint32_t group)
{
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%03u.WMO", group);
    return rootPath.substr(0, rootPath.size() - 4) + suffix;
}

}

const WmoInfo& WmoCatalogue::Of(const std::string& name)
{
    std::string path = UpperPath(name);
    auto found = m_wmos.find(path);
    if (found != m_wmos.end())
        return found->second;
    return m_wmos.emplace(path, Analyse(path)).first->second;
}

void WmoCatalogue::Prepare(const std::vector<std::string>& names, const std::vector<const ArchiveSet*>& readers)
{
    std::vector<std::string> paths;
    std::unordered_set<std::string> wanted;
    for (const std::string& name : names)
    {
        std::string path = UpperPath(name);
        if (m_wmos.count(path) == 0 && wanted.insert(path).second)
            paths.push_back(std::move(path));
    }
    std::vector<WmoInfo> found(paths.size());
    ParallelFor(paths.size(), readers.size(), [&](size_t i, size_t worker) {
        found[i] = WmoCatalogue(*readers[worker]).Analyse(paths[i]);
    });
    for (size_t i = 0; i < paths.size(); ++i)
        m_wmos.emplace(paths[i], std::move(found[i]));
}

void WmoCatalogue::Describe(const std::string& name, ModelLightCatalogue& models, std::ostream& report)
{
    const WmoInfo& info = Of(name);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "%s: %s, groups %u of %u read, %zu fixtures, %zu doodads, %zu sets, %zu windows, %zu lanterns\n",
                  UpperPath(name).c_str(), info.found ? "found" : "not found", info.groupsRead, info.groupCount,
                  info.fixtures.size(), info.doodads.size(), info.sets.size(), info.windows.size(),
                  info.lanterns.size());
    report << line;
    std::vector<bool> lampDoodad(info.doodads.size());
    for (size_t i = 0; i < info.doodads.size(); ++i)
        lampDoodad[i] = !models.Of(info.doodads[i].model).lights.empty();
    for (const WmoGroup& group : info.groups)
    {
        size_t lamps = std::count_if(group.doodads.begin(), group.doodads.end(),
                                     [&](uint16_t index) { return lampDoodad[index]; });
        size_t windows = std::count_if(info.windows.begin(), info.windows.end(),
                                       [&](const WmoWindow& window) { return window.group == group.index; });
        std::snprintf(line, sizeof(line),
                      "  group %4u flags %08X %-7s box (%.1f %.1f %.1f)-(%.1f %.1f %.1f) doodads %zu lamps %zu "
                      "fixtures %zu windows %zu\n",
                      group.index, group.flags, IsRoom(group.flags) ? "room" : "outdoor", group.low.x, group.low.y,
                      group.low.z, group.high.x, group.high.y, group.high.z, group.doodads.size(), lamps,
                      group.fixtures.size(), windows);
        report << line;
    }
    for (size_t i = 0; i < info.doodads.size(); ++i)
    {
        if (!lampDoodad[i])
            continue;
        const WmoDoodad& doodad = info.doodads[i];
        std::snprintf(line, sizeof(line), "  lamp doodad %4zu at (%.1f %.1f %.1f) %s groups", i, doodad.position.x,
                      doodad.position.y, doodad.position.z, doodad.model.c_str());
        report << line;
        for (const WmoGroup& group : info.groups)
            if (std::find(group.doodads.begin(), group.doodads.end(), static_cast<uint16_t>(i)) != group.doodads.end())
                report << " " << group.index << (IsRoom(group.flags) ? "r" : "o");
        report << "\n";
    }
}

WmoInfo WmoCatalogue::Analyse(const std::string& path) const
{
    WmoInfo info;
    std::optional<std::vector<uint8_t>> file = m_archives.Read(path);
    if (!file)
        return info;
    ByteView root(*file);
    std::vector<Chunk> chunks = ChunksOf(root);
    ByteView header = FindChunk(chunks, ChunkId("MOHD"));
    if (header.Empty())
        return info;
    info.found = true;
    info.groupCount = std::min(header.Get<uint32_t>(kMohdGroups), 4096u);
    info.boundsMin = header.Get<Vec3>(kMohdBoundsMin);
    info.boundsMax = header.Get<Vec3>(kMohdBoundsMax);

    ByteView materials = FindChunk(chunks, ChunkId("MOMT"));
    size_t materialCount = materials.Size() / kMaterialStride;
    for (size_t i = 0; i < materialCount; ++i)
        if (materials.Get<uint32_t>(i * kMaterialStride) & kMaterialSidn)
            ++info.sidnMaterials;

    ByteView lights = FindChunk(chunks, ChunkId("MOLT"));
    for (size_t at = 0; at + kLightStride <= lights.Size(); at += kLightStride)
    {
        WmoFixture fixture;
        fixture.type = lights.Get<uint8_t>(at);
        fixture.usesAttenuation = lights.Get<uint8_t>(at + kLightUseAttenuation) != 0;
        fixture.color = BgraColor(lights, at + kLightColor);
        fixture.position = lights.Get<Vec3>(at + kLightPosition);
        fixture.intensity = lights.Get<float>(at + kLightIntensity);
        float end = lights.Get<float>(at + kLightAttenuationEnd);
        fixture.reach = fixture.usesAttenuation && std::isfinite(end) && end > 0.0f ? std::min(end, kMaxReach) : 12.0f;
        info.fixtures.push_back(fixture);
    }

    ByteView sets = FindChunk(chunks, ChunkId("MODS"));
    for (size_t at = 0; at + kSetStride <= sets.Size(); at += kSetStride)
        info.sets.push_back({sets.Get<uint32_t>(at + kSetStart), sets.Get<uint32_t>(at + kSetCount)});

    ByteView names = FindChunk(chunks, ChunkId("MODN"));
    ByteView doodads = FindChunk(chunks, ChunkId("MODD"));
    for (size_t at = 0; at + kDoodadStride <= doodads.Size(); at += kDoodadStride)
    {
        WmoDoodad doodad;
        uint32_t nameIndex = doodads.Get<uint32_t>(at) & 0x00FFFFFFu;
        doodad.model = names.CString(nameIndex);
        doodad.position = doodads.Get<Vec3>(at + kDoodadPosition);
        for (int i = 0; i < 4; ++i)
            doodad.rotation[i] = doodads.Get<float>(at + kDoodadRotation + static_cast<size_t>(i) * 4);
        doodad.scale = doodads.Get<float>(at + kDoodadScale);
        info.doodads.push_back(std::move(doodad));
    }

    for (uint32_t g = 0; g < info.groupCount; ++g)
    {
        std::optional<std::vector<uint8_t>> groupFile = m_archives.Read(GroupPath(path, g));
        if (!groupFile)
            continue;
        ByteView group = FindChunk(ChunksOf(ByteView(*groupFile)), ChunkId("MOGP"));
        if (group.Size() < kGroupHeaderSize)
            continue;
        ++info.groupsRead;
        WmoGroup& record = info.groups.emplace_back();
        record.index = g;
        record.flags = group.Get<uint32_t>(kGroupFlags);
        record.low = group.Get<Vec3>(kGroupBoundsMin);
        record.high = group.Get<Vec3>(kGroupBoundsMax);
        bool indoor = IsRoom(record.flags);
        if (indoor)
            info.interiors.push_back({record.low, record.high});
        std::vector<Chunk> parts = ChunksOf(group.From(kGroupHeaderSize));

        ByteView lightRefs = FindChunk(parts, ChunkId("MOLR"));
        for (size_t at = 0; at + 2 <= lightRefs.Size(); at += 2)
        {
            uint16_t index = lightRefs.Get<uint16_t>(at);
            if (index >= info.fixtures.size())
                continue;
            record.fixtures.push_back(index);
            (indoor ? info.fixtures[index].referencedIndoors : info.fixtures[index].referencedOutdoors) = true;
        }
        ByteView doodadRefs = FindChunk(parts, ChunkId("MODR"));
        for (size_t at = 0; at + 2 <= doodadRefs.Size(); at += 2)
        {
            uint16_t index = doodadRefs.Get<uint16_t>(at);
            if (index >= info.doodads.size())
                continue;
            record.doodads.push_back(index);
            (indoor ? info.doodads[index].referencedIndoors : info.doodads[index].referencedOutdoors) = true;
        }

        if (info.sidnMaterials == 0)
            continue;
        ByteView indices = FindChunk(parts, ChunkId("MOVI"));
        ByteView positions = FindChunk(parts, ChunkId("MOVT"));
        ByteView batches = FindChunk(parts, ChunkId("MOBA"));
        std::vector<Vec3> vertices(positions.Size() / sizeof(Vec3));
        for (size_t i = 0; i < vertices.size(); ++i)
            vertices[i] = positions.Get<Vec3>(i * sizeof(Vec3));
        size_t indexCount = indices.Size() / 2;
        for (size_t at = 0; at + kBatchStride <= batches.Size(); at += kBatchStride)
        {
            uint8_t material = batches.Get<uint8_t>(at + kBatchMaterial);
            if (material >= materialCount || !(materials.Get<uint32_t>(material * kMaterialStride) & kMaterialSidn))
                continue;
            Rgb glow = BgraColor(materials, material * kMaterialStride + kMaterialSidnColor);
            if (glow.r + glow.g + glow.b < kMinWindowGlow)
            {
                ++info.darkSidnBatches;
                continue;
            }
            uint32_t start = batches.Get<uint32_t>(at + kBatchStartIndex);
            uint32_t count = batches.Get<uint16_t>(at + kBatchIndexCount);
            std::vector<Triangle> triangles;
            for (uint32_t i = 0; i + 2 < count && start + i + 2 < indexCount; i += 3)
            {
                Triangle t{indices.Get<uint16_t>((start + i) * 2u), indices.Get<uint16_t>((start + i + 1) * 2u),
                           indices.Get<uint16_t>((start + i + 2) * 2u)};
                if (t.a < vertices.size() && t.b < vertices.size() && t.c < vertices.size())
                    triangles.push_back(t);
            }
            if (!triangles.empty())
                AddWindows(triangles, vertices, glow, indoor, g, info.windows, info.lanterns);
        }
    }
    return info;
}
