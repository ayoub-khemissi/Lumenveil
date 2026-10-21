#pragma once

#include <cstdint>
#include <vector>

using HeightProbe = bool (*)(void* context, float x, float y, float top, float bottom, float& height);

struct HeightSource
{
    HeightProbe probe;
    void* context;
};

void SetHeightSource(HeightProbe probe, void* context);
HeightSource CurrentHeightSource();

struct LiquidAt
{
    float surface;
    float lake;
    float sea;
};

using LiquidProbe = bool (*)(void* context, int mapId, float x, float y, LiquidAt& liquid);

struct LiquidSource
{
    LiquidProbe probe;
    void* context;
};

void SetLiquidSource(LiquidProbe probe, void* context);
LiquidSource CurrentLiquidSource();

struct BankFloor
{
    float nearFloor;
    float basin;
    float lake;
    float sea;
};

static_assert(sizeof(BankFloor) == 4 * sizeof(float), "a bank floor is one A32B32G32R32F texel");

struct TerrainShadowConstants
{
    float uvAtCamera[2];
    float uvPerYard;
    float sharpness;
};

constexpr TerrainShadowConstants kTerrainShadowsOff = {-10.0f, -10.0f, 0.0f, 0.0f};

struct TerrainShadowStats
{
    int groundCells;
    int emptyCells;
    int probes;
    double probeSeconds;
    int sweeps;
    double sweepSeconds;
    int resets;
    int shifts;
};

class TerrainShadowMap
{
public:
    static constexpr int kSide = 512;
    static constexpr float kCellYards = 8.0f;
    static constexpr float kExtentYards = kSide * kCellYards;
    static constexpr float kUncoveredGain = 2.0f;
    static constexpr float kOutwardLookupCells = 1.0f;

    static constexpr int kFloorBinCells = 4;
    static constexpr int kFloorSide = kSide / kFloorBinCells;
    static constexpr float kFloorBinYards = kFloorBinCells * kCellYards;
    static constexpr int kFloorNearBins = 1;
    static constexpr int kFloorBasinBins = 8;

    TerrainShadowMap();

    void Update(const float* camPos, int mapId, const float* toLight, const HeightSource& source,
                const LiquidSource& liquids = {nullptr, nullptr}, bool floors = false);
    const BankFloor* Floors() const { return m_floors.data(); }
    unsigned FloorVersion() const { return m_floorVersion; }
    bool FloorsKnown() const { return m_floorsKnown; }
    void Forget()
    {
        m_valid = false;
        m_floorsKnown = false;
    }
    BankFloor FloorAt(float x, float y) const;
    TerrainShadowConstants Constants(const float* camPos) const;
    float Light(float x, float y, float z) const;
    float SurfaceShadow(float x, float y, float z, float outwardX = 0.0f, float outwardY = 0.0f) const;
    const float* Texels() const { return m_texels.data(); }
    const float* GroundTexels() const { return m_groundTexels.data(); }
    unsigned Version() const { return m_version; }
    int CentreCellX() const { return m_originX + kSide / 2; }
    int CentreCellY() const { return m_originY + kSide / 2; }
    TerrainShadowStats TakeStats();

private:
    enum class Cell : uint8_t
    {
        Unknown,
        Ground,
        Empty,
    };
    enum class Pass
    {
        Fill,
        Refresh,
    };

    struct SweepGeometry
    {
        bool alongX;
        bool casts;
        int step;
        float drop;
        int offset;
        float fraction;
    };

    int Index(int i, int j) const { return j * kSide + i; }
    void Reset(int originX, int originY, int mapId);
    void Shift(int originX, int originY);
    void Probe(float cameraZ, const HeightSource& source);
    bool LightMoved(const float* toLight) const;
    SweepGeometry GeometryToLight() const;
    void ShadeCell(int cell, float upstream);
    const float* OccluderRow(int j) const;
    void SweepAlongX(const SweepGeometry& g);
    void SweepAlongY(const SweepGeometry& g);
    void Sweep();
    void ComputeFloors();
    bool Footprint(float x, float y, int& i0, int& j0, float& fx, float& fy) const;

    std::vector<float> m_height;
    std::vector<Cell> m_cells;
    std::vector<LiquidAt> m_liquid;
    std::vector<BankFloor> m_floors;
    std::vector<double> m_binSums;
    std::vector<int> m_binCounts;
    std::vector<float> m_binWater;
    std::vector<double> m_floorSums;
    std::vector<int> m_floorCounts;
    LiquidSource m_liquids = {nullptr, nullptr};
    bool m_floorsDue = true;
    bool m_floorsKnown = false;
    int m_framesSinceFloors = 0;
    unsigned m_floorVersion = 0;
    std::vector<float> m_occluder;
    std::vector<float> m_texels;
    std::vector<float> m_groundTexels;
    std::vector<uint32_t> m_order;
    HeightSource m_source = {nullptr, nullptr};
    bool m_valid = false;
    int m_mapId = 0;
    int m_originX = 0;
    int m_originY = 0;
    Pass m_pass = Pass::Fill;
    size_t m_cursor = 0;
    bool m_sweepDue = true;
    int m_framesSinceSweep = 0;
    float m_light[3] = {0.0f, 0.0f, 1.0f};
    unsigned m_version = 0;
    TerrainShadowStats m_stats = {};
};
