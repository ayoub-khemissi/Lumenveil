#include "terrain_shadow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr int kRecentreCells = 32;
constexpr int kFillProbesPerFrame = 512;
constexpr int kRefreshProbesPerFrame = 16;
constexpr double kProbeSecondsPerFrame = 0.0005;
constexpr float kProbeReach = 1500.0f;
constexpr float kGroundClearance = 6.0f;
constexpr float kShadowSoftness = 8.0f;
constexpr float kSharpness = 1.0f / kShadowSoftness;
constexpr float kNoOccluder = -1.0e6f;
constexpr float kLitTexel = 1.0e5f;
constexpr float kMinHorizontalLight = 0.01f;
constexpr int kFramesPerSweep = 4;
constexpr float kResweepCos = 0.9999985f;
constexpr float kLightJumpCos = 0.9962f;
constexpr float kHeightChange = 0.25f;
constexpr float kCoverClearance = 3.0f;
constexpr float kMinUncoveredWeight = 1.0e-4f;
constexpr float kNoLiquid = -1.0e5f;
constexpr int kFramesPerFloors = 15;
constexpr float kLiquidChange = 0.05f;
constexpr int kFloorTableSide = TerrainShadowMap::kFloorSide + 1;
constexpr int kFloorBins = TerrainShadowMap::kFloorSide * TerrainShadowMap::kFloorSide;
constexpr float kCellsPerBin = TerrainShadowMap::kFloorBinCells * TerrainShadowMap::kFloorBinCells;
constexpr LiquidAt kDry = {kNoLiquid * 10.0f, 0.0f, 0.0f};

using Clock = std::chrono::steady_clock;

HeightSource g_source = {nullptr, nullptr};
LiquidSource g_liquids = {nullptr, nullptr};

bool LiquidChanged(const LiquidAt& a, const LiquidAt& b)
{
    return std::fabs(a.surface - b.surface) > kHeightChange || std::fabs(a.lake - b.lake) > kLiquidChange ||
           std::fabs(a.sea - b.sea) > kLiquidChange;
}

float Dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
}

void SetHeightSource(HeightProbe probe, void* context)
{
    g_source = {probe, context};
}

HeightSource CurrentHeightSource()
{
    return g_source;
}

void SetLiquidSource(LiquidProbe probe, void* context)
{
    g_liquids = {probe, context};
}

LiquidSource CurrentLiquidSource()
{
    return g_liquids;
}

TerrainShadowMap::TerrainShadowMap()
    : m_height(kSide * kSide, 0.0f), m_cells(kSide * kSide, Cell::Unknown), m_liquid(kSide * kSide, kDry),
      m_floors(kFloorBins, BankFloor{0.0f, 0.0f, 0.0f, 0.0f}), m_binSums(kFloorBins, 0.0), m_binCounts(kFloorBins, 0),
      m_binWater(2 * kFloorBins, 0.0f), m_floorSums(kFloorTableSide * kFloorTableSide, 0.0),
      m_floorCounts(kFloorTableSide * kFloorTableSide, 0),
      m_occluder(kSide * kSide, kNoOccluder),
      m_texels(kSide * kSide, kLitTexel), m_groundTexels(kSide * kSide, kLitTexel), m_order(kSide * kSide)
{
    for (int cell = 0; cell < kSide * kSide; ++cell)
        m_order[cell] = static_cast<uint32_t>(cell);
    auto distance = [](uint32_t cell) {
        const int di = static_cast<int>(cell % kSide) - kSide / 2;
        const int dj = static_cast<int>(cell / kSide) - kSide / 2;
        return di * di + dj * dj;
    };
    std::stable_sort(m_order.begin(), m_order.end(),
                     [&](uint32_t a, uint32_t b) { return distance(a) < distance(b); });
}

void TerrainShadowMap::Reset(int originX, int originY, int mapId)
{
    std::fill(m_height.begin(), m_height.end(), 0.0f);
    std::fill(m_cells.begin(), m_cells.end(), Cell::Unknown);
    std::fill(m_liquid.begin(), m_liquid.end(), kDry);
    m_valid = true;
    m_mapId = mapId;
    m_originX = originX;
    m_originY = originY;
    m_pass = Pass::Fill;
    m_cursor = 0;
    m_sweepDue = true;
    m_framesSinceSweep = kFramesPerSweep;
    m_floorsDue = true;
    m_floorsKnown = false;
    m_framesSinceFloors = kFramesPerFloors;
    ++m_stats.resets;
}

void TerrainShadowMap::Shift(int originX, int originY)
{
    const int dx = originX - m_originX;
    const int dy = originY - m_originY;
    std::vector<float> height(kSide * kSide, 0.0f);
    std::vector<Cell> cells(kSide * kSide, Cell::Unknown);
    std::vector<LiquidAt> liquid(kSide * kSide, kDry);
    for (int j = 0; j < kSide; ++j)
    {
        const int oldJ = j + dy;
        if (oldJ < 0 || oldJ >= kSide)
            continue;
        for (int i = 0; i < kSide; ++i)
        {
            const int oldI = i + dx;
            if (oldI < 0 || oldI >= kSide)
                continue;
            height[Index(i, j)] = m_height[Index(oldI, oldJ)];
            cells[Index(i, j)] = m_cells[Index(oldI, oldJ)];
            liquid[Index(i, j)] = m_liquid[Index(oldI, oldJ)];
        }
    }
    m_height.swap(height);
    m_cells.swap(cells);
    m_liquid.swap(liquid);
    m_originX = originX;
    m_originY = originY;
    m_pass = Pass::Fill;
    m_cursor = 0;
    m_sweepDue = true;
    m_floorsDue = true;
    m_framesSinceFloors = kFramesPerFloors;
    ++m_stats.shifts;
}

void TerrainShadowMap::Probe(float cameraZ, const HeightSource& source)
{
    const Clock::time_point start = Clock::now();
    const bool filling = m_pass == Pass::Fill;
    const int budget = filling ? kFillProbesPerFrame : kRefreshProbesPerFrame;
    int probes = 0;
    while (probes < budget)
    {
        if (m_cursor >= m_order.size())
        {
            m_cursor = 0;
            m_pass = Pass::Refresh;
            if (filling)
                break;
        }
        const int cell = m_order[m_cursor++];
        if (filling && m_cells[cell] != Cell::Unknown)
            continue;
        const float x = (m_originX + cell % kSide + 0.5f) * kCellYards;
        const float y = (m_originY + cell / kSide + 0.5f) * kCellYards;
        float height = 0.0f;
        const bool hit = source.probe(source.context, x, y, cameraZ + kProbeReach, cameraZ - kProbeReach, height) &&
                         std::isfinite(height);
        ++probes;
        const Cell state = hit ? Cell::Ground : Cell::Empty;
        if (state != m_cells[cell] || (hit && std::fabs(height - m_height[cell]) > kHeightChange))
        {
            m_sweepDue = true;
            m_floorsDue = true;
        }
        m_cells[cell] = state;
        m_height[cell] = hit ? height : 0.0f;
        LiquidAt liquid = kDry;
        if (m_liquids.probe && !(m_liquids.probe(m_liquids.context, m_mapId, x, y, liquid) &&
                                 std::isfinite(liquid.surface) && liquid.surface > kNoLiquid))
            liquid = kDry;
        if (LiquidChanged(liquid, m_liquid[cell]))
            m_floorsDue = true;
        m_liquid[cell] = liquid;
        if (std::chrono::duration<double>(Clock::now() - start).count() > kProbeSecondsPerFrame)
            break;
    }
    m_stats.probes += probes;
    m_stats.probeSeconds += std::chrono::duration<double>(Clock::now() - start).count();
}

bool TerrainShadowMap::LightMoved(const float* toLight) const
{
    return Dot3(toLight, m_light) < kResweepCos;
}

void TerrainShadowMap::Update(const float* camPos, int mapId, const float* toLight, const HeightSource& source,
                              const LiquidSource& liquids, bool floors)
{
    const int originX = static_cast<int>(std::floor(camPos[0] / kCellYards)) - kSide / 2;
    const int originY = static_cast<int>(std::floor(camPos[1] / kCellYards)) - kSide / 2;
    if (source.probe != m_source.probe || source.context != m_source.context ||
        liquids.probe != m_liquids.probe || liquids.context != m_liquids.context)
    {
        m_source = source;
        m_liquids = liquids;
        m_valid = false;
    }
    if (!m_valid || mapId != m_mapId || std::abs(originX - m_originX) >= kSide ||
        std::abs(originY - m_originY) >= kSide)
        Reset(originX, originY, mapId);
    else if (std::abs(originX - m_originX) > kRecentreCells || std::abs(originY - m_originY) > kRecentreCells)
        Shift(originX, originY);
    if (source.probe)
        Probe(camPos[2], source);
    ++m_framesSinceSweep;
    const bool lightJumped = Dot3(toLight, m_light) < kLightJumpCos;
    if (LightMoved(toLight))
        m_sweepDue = true;
    if (m_sweepDue && (m_framesSinceSweep >= kFramesPerSweep || lightJumped))
    {
        std::copy(toLight, toLight + 3, m_light);
        Sweep();
    }
    if (!floors)
        return;
    ++m_framesSinceFloors;
    if (m_floorsDue && m_framesSinceFloors >= kFramesPerFloors)
        ComputeFloors();
}

TerrainShadowMap::SweepGeometry TerrainShadowMap::GeometryToLight() const
{
    SweepGeometry g = {};
    g.alongX = std::fabs(m_light[0]) >= std::fabs(m_light[1]);
    const float major = g.alongX ? m_light[0] : m_light[1];
    const float minor = g.alongX ? m_light[1] : m_light[0];
    g.casts = m_light[2] > 0.0f && std::fabs(major) > kMinHorizontalLight;
    g.step = major > 0.0f ? 1 : -1;
    const float slope = g.casts ? minor / std::fabs(major) : 0.0f;
    g.drop = g.casts ? kCellYards * m_light[2] / std::fabs(major) : 0.0f;
    g.offset = static_cast<int>(std::floor(slope));
    g.fraction = slope - g.offset;
    return g;
}

void TerrainShadowMap::ShadeCell(int cell, float upstream)
{
    const float ground = m_cells[cell] == Cell::Ground ? m_height[cell] : kNoOccluder;
    m_occluder[cell] = std::max(ground, upstream);
    const float shadowTop = m_occluder[cell] - kGroundClearance;
    m_texels[cell] = std::clamp(0.5f - shadowTop * kSharpness, -kLitTexel, kLitTexel);
    const float coverTop = ground - kCoverClearance;
    m_groundTexels[cell] = std::clamp(1.0f - coverTop * kSharpness, -kLitTexel, kLitTexel);
}

const float* TerrainShadowMap::OccluderRow(int j) const
{
    return j >= 0 && j < kSide ? m_occluder.data() + j * kSide : nullptr;
}

void TerrainShadowMap::SweepAlongY(const SweepGeometry& g)
{
    for (int k = 0; k < kSide; ++k)
    {
        const int j = g.step > 0 ? kSide - 1 - k : k;
        const float* upstreamRow = g.casts && k > 0 ? OccluderRow(j + g.step) : nullptr;
        for (int i = 0; i < kSide; ++i)
        {
            float upstream = kNoOccluder;
            if (upstreamRow)
            {
                const int below = i + g.offset;
                const float o0 = below >= 0 && below < kSide ? upstreamRow[below] : kNoOccluder;
                const float o1 = below + 1 >= 0 && below + 1 < kSide ? upstreamRow[below + 1] : kNoOccluder;
                upstream = o0 + (o1 - o0) * g.fraction - g.drop;
            }
            ShadeCell(Index(i, j), upstream);
        }
    }
}

void TerrainShadowMap::SweepAlongX(const SweepGeometry& g)
{
    for (int n = 0; n < kSide; ++n)
    {
        const int j = g.offset >= 0 ? kSide - 1 - n : n;
        const float* lowerRow = OccluderRow(j + g.offset);
        const float* upperRow = OccluderRow(j + g.offset + 1);
        for (int k = 0; k < kSide; ++k)
        {
            const int i = g.step > 0 ? kSide - 1 - k : k;
            float upstream = kNoOccluder;
            if (g.casts && k > 0)
            {
                const float o0 = lowerRow ? lowerRow[i + g.step] : kNoOccluder;
                const float o1 = upperRow ? upperRow[i + g.step] : kNoOccluder;
                upstream = o0 + (o1 - o0) * g.fraction - g.drop;
            }
            ShadeCell(Index(i, j), upstream);
        }
    }
}

void TerrainShadowMap::Sweep()
{
    const Clock::time_point start = Clock::now();
    const SweepGeometry g = GeometryToLight();
    if (g.alongX)
        SweepAlongX(g);
    else
        SweepAlongY(g);
    for (int n = 0; n < kSide; ++n)
    {
        for (std::vector<float>* texels : {&m_texels, &m_groundTexels})
        {
            (*texels)[Index(n, 0)] = kLitTexel;
            (*texels)[Index(n, kSide - 1)] = kLitTexel;
            (*texels)[Index(0, n)] = kLitTexel;
            (*texels)[Index(kSide - 1, n)] = kLitTexel;
        }
    }
    m_sweepDue = false;
    m_framesSinceSweep = 0;
    ++m_version;
    ++m_stats.sweeps;
    m_stats.sweepSeconds += std::chrono::duration<double>(Clock::now() - start).count();
}

TerrainShadowConstants TerrainShadowMap::Constants(const float* camPos) const
{
    if (!m_valid)
        return kTerrainShadowsOff;
    const double originX = static_cast<double>(m_originX) * kCellYards;
    const double originY = static_cast<double>(m_originY) * kCellYards;
    return {{static_cast<float>((camPos[0] - originX) / kExtentYards),
             static_cast<float>((camPos[1] - originY) / kExtentYards)},
            1.0f / kExtentYards,
            kSharpness};
}

bool TerrainShadowMap::Footprint(float x, float y, int& i0, int& j0, float& fx, float& fy) const
{
    if (!m_valid)
        return false;
    const float tx = static_cast<float>((x - static_cast<double>(m_originX) * kCellYards) / kCellYards) - 0.5f;
    const float ty = static_cast<float>((y - static_cast<double>(m_originY) * kCellYards) / kCellYards) - 0.5f;
    i0 = static_cast<int>(std::floor(tx));
    j0 = static_cast<int>(std::floor(ty));
    fx = tx - i0;
    fy = ty - j0;
    return true;
}

float TerrainShadowMap::SurfaceShadow(float x, float y, float z, float outwardX, float outwardY) const
{
    int i0 = 0;
    int j0 = 0;
    float fx = 0.0f;
    float fy = 0.0f;
    const float lookupYards = kOutwardLookupCells * kCellYards;
    if (!Footprint(x + outwardX * lookupYards, y + outwardY * lookupYards, i0, j0, fx, fy))
        return 0.0f;
    float weightedLight = 0.0f;
    float uncoveredWeight = 0.0f;
    for (int corner = 0; corner < 4; ++corner)
    {
        const int di = corner & 1;
        const int dj = corner >> 1;
        const int cell = Index(std::clamp(i0 + di, 0, kSide - 1), std::clamp(j0 + dj, 0, kSide - 1));
        const float bilinear = (di ? fx : 1.0f - fx) * (dj ? fy : 1.0f - fy);
        const float uncovered = std::clamp(z * kSharpness + m_groundTexels[cell], 0.0f, 1.0f);
        const float light = std::clamp(z * kSharpness + m_texels[cell], 0.0f, 1.0f);
        weightedLight += bilinear * uncovered * light;
        uncoveredWeight += bilinear * uncovered;
    }
    const float light = uncoveredWeight > kMinUncoveredWeight ? weightedLight / uncoveredWeight : 1.0f;
    return (1.0f - light) * std::min(uncoveredWeight * kUncoveredGain, 1.0f);
}

float TerrainShadowMap::Light(float x, float y, float z) const
{
    int i0 = 0;
    int j0 = 0;
    float fx = 0.0f;
    float fy = 0.0f;
    if (!Footprint(x, y, i0, j0, fx, fy))
        return 1.0f;
    auto texel = [&](int i, int j) {
        return m_texels[Index(std::clamp(i, 0, kSide - 1), std::clamp(j, 0, kSide - 1))];
    };
    const float lower = texel(i0, j0) + (texel(i0 + 1, j0) - texel(i0, j0)) * fx;
    const float upper = texel(i0, j0 + 1) + (texel(i0 + 1, j0 + 1) - texel(i0, j0 + 1)) * fx;
    return std::clamp(z * kSharpness + lower + (upper - lower) * fy, 0.0f, 1.0f);
}

BankFloor TerrainShadowMap::FloorAt(float x, float y) const
{
    BankFloor out = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!m_floorsKnown || !m_valid)
        return out;
    const float tx = static_cast<float>((x - static_cast<double>(m_originX) * kCellYards) / kFloorBinYards) - 0.5f;
    const float ty = static_cast<float>((y - static_cast<double>(m_originY) * kCellYards) / kFloorBinYards) - 0.5f;
    const int i0 = static_cast<int>(std::floor(tx));
    const int j0 = static_cast<int>(std::floor(ty));
    const float fx = tx - i0;
    const float fy = ty - j0;
    for (int corner = 0; corner < 4; ++corner)
    {
        const int di = corner & 1;
        const int dj = corner >> 1;
        const int i = std::clamp(i0 + di, 0, kFloorSide - 1);
        const int j = std::clamp(j0 + dj, 0, kFloorSide - 1);
        const BankFloor& f = m_floors[j * kFloorSide + i];
        const float w = (di ? fx : 1.0f - fx) * (dj ? fy : 1.0f - fy);
        out.nearFloor += w * f.nearFloor;
        out.basin += w * f.basin;
        out.lake += w * f.lake;
        out.sea += w * f.sea;
    }
    return out;
}

void TerrainShadowMap::ComputeFloors()
{
    std::fill(m_binSums.begin(), m_binSums.end(), 0.0);
    std::fill(m_binCounts.begin(), m_binCounts.end(), 0);
    std::fill(m_binWater.begin(), m_binWater.end(), 0.0f);
    for (int j = 0; j < kSide; ++j)
    {
        const int binRow = (j / kFloorBinCells) * kFloorSide;
        for (int i = 0; i < kSide; ++i)
        {
            const int cell = Index(i, j);
            const LiquidAt& w = m_liquid[cell];
            const float wet = std::clamp(w.lake + w.sea, 0.0f, 1.0f);
            const bool ground = m_cells[cell] == Cell::Ground;
            const bool water = w.surface > kNoLiquid && wet > 0.0f;
            const int bin = binRow + i / kFloorBinCells;
            if (ground)
                m_binSums[bin] += m_height[cell] + (water ? wet * std::max(w.surface - m_height[cell], 0.0f) : 0.0f);
            else if (water)
                m_binSums[bin] += w.surface;
            m_binCounts[bin] += (ground || water) ? 1 : 0;
            m_binWater[2 * bin] += w.lake;
            m_binWater[2 * bin + 1] += w.sea;
        }
    }
    auto at = [](int i, int j) { return j * kFloorTableSide + i; };
    for (int j = 0; j < kFloorSide; ++j)
    {
        for (int i = 0; i < kFloorSide; ++i)
        {
            const int bin = j * kFloorSide + i;
            const int here = at(i + 1, j + 1);
            const int up = at(i + 1, j);
            const int left = at(i, j + 1);
            const int corner = at(i, j);
            m_floorSums[here] = m_binSums[bin] + m_floorSums[up] + m_floorSums[left] - m_floorSums[corner];
            m_floorCounts[here] = m_binCounts[bin] + m_floorCounts[up] + m_floorCounts[left] - m_floorCounts[corner];
        }
    }
    const int known = m_floorCounts[at(kFloorSide, kFloorSide)];
    m_floorsKnown = known > 0;
    const double everywhere = known > 0 ? m_floorSums[at(kFloorSide, kFloorSide)] / known : 0.0;
    auto box = [&](int i, int j, int r, double& mean) {
        const int i0 = std::max(i - r, 0);
        const int j0 = std::max(j - r, 0);
        const int i1 = std::min(i + r + 1, kFloorSide);
        const int j1 = std::min(j + r + 1, kFloorSide);
        const int count = m_floorCounts[at(i1, j1)] - m_floorCounts[at(i0, j1)] - m_floorCounts[at(i1, j0)] +
                          m_floorCounts[at(i0, j0)];
        if (count <= 0)
            return false;
        mean = (m_floorSums[at(i1, j1)] - m_floorSums[at(i0, j1)] - m_floorSums[at(i1, j0)] + m_floorSums[at(i0, j0)]) /
               count;
        return true;
    };
    for (int j = 0; j < kFloorSide; ++j)
    {
        for (int i = 0; i < kFloorSide; ++i)
        {
            const int bin = j * kFloorSide + i;
            double basin = everywhere;
            const bool basinKnown = box(i, j, kFloorBasinBins, basin);
            double nearFloor = basin;
            if (!box(i, j, kFloorNearBins, nearFloor) && !basinKnown)
                nearFloor = everywhere;
            m_floors[bin] = {static_cast<float>(nearFloor), static_cast<float>(basin),
                             m_binWater[2 * bin] / kCellsPerBin, m_binWater[2 * bin + 1] / kCellsPerBin};
        }
    }
    m_floorsDue = false;
    m_framesSinceFloors = 0;
    ++m_floorVersion;
}

TerrainShadowStats TerrainShadowMap::TakeStats()
{
    TerrainShadowStats stats = m_stats;
    stats.groundCells = static_cast<int>(std::count(m_cells.begin(), m_cells.end(), Cell::Ground));
    stats.emptyCells = static_cast<int>(std::count(m_cells.begin(), m_cells.end(), Cell::Empty));
    m_stats = {};
    return stats;
}
