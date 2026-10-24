#pragma once

#include "engine.h"
#include "lamp_catalog.h"
#include "object_lamps.h"

#include <cstdint>
#include <vector>

constexpr int kEffectGatherPixels = 8;
constexpr int kEffectReduceTexels = 4;
constexpr int kEffectCellPixels = kEffectGatherPixels * kEffectReduceTexels;
constexpr float kEffectPixelsPerCell = static_cast<float>(kEffectCellPixels * kEffectCellPixels);
constexpr int kEffectReadbackRing = 3;
constexpr unsigned kEffectFlushAfterFrames = 4;
constexpr int kMaxEffectLights = 12;
constexpr int kMaxEffectClusters = 16;
constexpr float kEffectThreshold = 0.05f;
constexpr float kEffectNearestYards = 0.5f;
constexpr float kEffectFarthestYards = 80.0f;
constexpr float kEffectDepthChange = 2.0e-7f;
constexpr float kMinEffectCellEnergy = 0.5f;
constexpr float kEffectBackgroundShare = 0.75f;
constexpr float kEffectStandOut = 4.0f;
constexpr float kEffectWeatherCellEnergy = 32.0f;
constexpr float kEffectWeatherSaturation = 0.35f;
constexpr float kEffectMinChannel = 0.4f;
constexpr float kEffectFieldShareOfCells = 0.03f;
constexpr float kEffectFieldShare = 0.9f;
constexpr float kEffectFieldStandOut = 2.0f;
constexpr float kEffectClusterYards = 2.5f;
constexpr float kEffectClusterDepthYards = 2.0f;
constexpr float kEffectClusterDepthShare = 0.25f;
constexpr float kEffectStandOffYards = 1.0f;
constexpr float kEffectMaxStandOffShare = 0.5f;
constexpr float kEffectIntensityPerLitArea = 8.0f;
constexpr float kMaxEffectIntensity = 2.5f;
constexpr float kMinEffectIntensity = 0.05f;
constexpr float kEffectReachPerRootIntensity = 12.0f;
constexpr float kMinEffectReach = 6.0f;
constexpr float kMaxEffectReach = 24.0f;
constexpr float kMinEffectNearYards = 0.5f;
constexpr float kMaxEffectNearYards = 3.0f;
constexpr float kEffectClearYards = 0.5f;
constexpr float kEffectShadowStartYards = 0.2f;
constexpr float kEffectLiftYards = 0.5f;
constexpr float kEffectLampFlameYards = 0.75f;
constexpr float kEffectLampFlamePerReach = 0.05f;
constexpr float kEffectLampDepthYards = 1.5f;
constexpr float kEffectLampDepthShare = 0.1f;
constexpr float kEffectMatchYards = 2.5f;
constexpr float kEffectMatchShare = 0.12f;
constexpr int kEffectMissesBeforeFade = 2;
constexpr double kEffectStaleSeconds = 0.3;
constexpr float kEffectSpawnIntensity = 0.1f;
constexpr float kEffectGoneIntensity = 0.005f;
constexpr float kEffectPositionSeconds = 0.12f;
constexpr float kEffectColourSeconds = 0.2f;
constexpr float kEffectRiseSeconds = 0.1f;
constexpr float kEffectFallSeconds = 0.3f;
constexpr double kMaxEffectStepSeconds = 0.25;
constexpr float kEffectHotFrom = 0.45f;
constexpr float kEffectHotFull = 0.85f;
constexpr float kEffectHotAreaGain = 4.0f;
constexpr float kEffectPopHot = 0.5f;
constexpr float kEffectFlashLifeSeconds = 0.25f;
constexpr float kEffectFlashFallSeconds = 0.08f;
constexpr float kEffectFlashSeconds = 0.1f;
constexpr float kEffectFlameColour[3] = {1.0f, 0.6f, 0.25f};
constexpr float kEffectWhiteTolerance = 1.05f;
constexpr float kEffectFlashWhiteSaturation = 0.3f;
constexpr float kEffectPlayerZoneHeightShare = 0.85f;
constexpr float kEffectPlayerZoneForwardYards = 0.9f;
constexpr float kEffectPlayerZoneYards = 0.9f;
constexpr float kEffectWeaponFlashGain = 1.5f;
constexpr uint32_t kEffectMuzzleFlashClass = 3;
constexpr float kEffectSpriteDepthShare = 0.85f;
constexpr float kEffectLampGlowYards = 3.0f;
constexpr float kEffectLampGlowBehindYards = 1.5f;
constexpr float kEffectBeamGlowYards = 25.0f;
constexpr float kEffectRingPerBoundingRadius = 3.0f;
constexpr float kMinEffectRingYards = 1.5f;
constexpr float kMaxEffectRingYards = 10.0f;
constexpr float kEffectRingMarginYards = 0.5f;
constexpr float kEffectRingBelowYards = 1.0f;
constexpr float kEffectRingAboveYards = 0.75f;
constexpr float kEffectRingSlope = 0.5f;
constexpr int kMaxEffectAnchors = 2 * kMaxUnitInputs;
constexpr float kEffectUnitHeightPerRadius = 4.0f;
constexpr float kMinEffectUnitHeight = 2.5f;
constexpr float kMaxEffectUnitHeight = 12.0f;
constexpr float kMaxEffectUnitRadius = 8.0f;
constexpr float kEffectUnitRadiusShare = 1.5f;
constexpr float kMinEffectUnitRadius = 0.8f;
constexpr float kEffectUnitMarginYards = 0.4f;
constexpr float kEffectMissileHeightShare = 0.6f;
constexpr float kEffectMissileRadius = 1.0f;
constexpr float kMinEffectMissileYards = 2.0f;
constexpr float kEffectAnchorClearYards = 0.5f;
constexpr float kEffectCarriedMarginYards = kEffectStandOffYards;
constexpr int kEffectVelocitySamples = 16;
constexpr double kEffectVelocityWindowSeconds = 0.25;
constexpr int kMinEffectVelocitySamples = 4;
constexpr double kMinEffectVelocitySpanSeconds = 0.05;
constexpr float kEffectMovingFrom = 4.0f;
constexpr float kEffectMovingFull = 8.0f;
constexpr float kMaxEffectSpeed = 60.0f;
constexpr double kMaxEffectLeadSeconds = 0.2;

struct EffectAnchor
{
    float from[3];
    float to[3];
    float radius;
    uint64_t unit;
};

struct EffectFrame
{
    float cameraRelativeView[16];
    float glProjection[16];
    float camPos[3];
    float viewportWidth;
    float viewportHeight;
    double seconds;
    int mapId;
    float weatherIntensity;
    bool falling;
    bool playerKnown;
    float playerZone[3];
    bool handGlows;
    bool targetKnown;
    float targetFeet[3];
    float targetRingYards;
    int anchorCount;
    EffectAnchor anchors[kMaxEffectAnchors];
};

struct EffectMeasurement
{
    EffectFrame frame;
    int columns;
    int rows;
    std::vector<float> texels;
};

struct EffectCluster
{
    float position[3];
    float colour[3];
    float intensity;
    float nearYards;
    float surfaceReach;
    float viewDepth;
    float litArea;
    float hot;
    bool atWeapon;
    bool handGlow;
    bool atUnit;
    bool sprite;
    uint64_t unit;
    float unitOffset[3];
};

struct EffectClusterStats
{
    int cells;
    float background;
    float least;
    int colourless;
    int fieldCells;
    float field;
    int suppressedCells;
    int lampGlowCells;
    int ringCells;
    int clusters;
    int droppedClusters;
    int fallingDropped;
    int sprites;
    int atWeapon;
    int atUnits;
    int carried;
};

struct EffectFlashStats
{
    unsigned flashes;
    unsigned outlasted;
    unsigned weaponFlashes;
    unsigned bullets;
    float shortestSeconds;
    float longestSeconds;
};

struct EffectLamp
{
    uint32_t id;
    float position[3];
    float colour[3];
    float intensity;
    float nearYards;
    float surfaceReach;
};

float EffectLuminance(const float* rgb);
float EffectIntensity(float litArea);
float EffectSurfaceReach(float intensity);
void EffectFlashColour(const float* measured, float* out);
void EffectLightColour(const float* measured, float* out);
void EffectPlayerZone(const float* feet, float facing, float pivotHeight, float* centre);
bool EffectHandGlows(uint32_t playerClass);
void EffectFramePlayer(const FrameInputs& in, float pivotHeight, EffectFrame& f);
float EffectTargetRingYards(float boundingRadius);
int EffectUnitAnchors(const UnitInput* units, int count, EffectAnchor* out);
bool CarriedByUnit(const EffectFrame& f, const float* position, uint64_t& unit, float* offset);
std::vector<EffectCluster> ClusterEffects(const EffectMeasurement& m, const LampMap* lamps,
                                          EffectClusterStats* stats = nullptr,
                                          const std::vector<ObjectLamp>* objects = nullptr);

struct EffectVelocity
{
    float velocity[3];
    float moving;
};

EffectVelocity FitEffectVelocity(const double* seconds, const float (*positions)[3], int count);

struct EffectTrackerStats
{
    int carried;
    int moving;
};

class EffectTracker
{
public:
    void Update(const std::vector<EffectCluster>* measured, const float* camPos, double seconds, int mapId,
                double measuredSeconds = -1.0, const UnitInput* units = nullptr, int unitCount = 0);
    void Reset();
    const std::vector<EffectLamp>& Lights() const { return m_lights; }
    const EffectFlashStats& Flashes() const { return m_flashes; }
    void ResetFlashes() { m_flashes = {}; }
    EffectTrackerStats Stats() const { return m_stats; }

private:
    struct Tracked
    {
        EffectLamp now;
        uint64_t unit;
        bool carried;
        float feet[3];
        float offset[3];
        float targetOffset[3];
        int samples;
        double sampleSeconds[kEffectVelocitySamples];
        float samplePositions[kEffectVelocitySamples][3];
        EffectVelocity velocity;
        double measuredSeconds;
        float aim[3];
        float targetPosition[3];
        float targetColour[3];
        float targetIntensity;
        float targetNear;
        float targetReach;
        int misses;
        bool matched;
        float hot;
        float life;
        bool quick;
        bool flashing;
        bool bornFlash;
        bool outlasted;
        bool weapon;
        bool atWeapon;
        bool held;
        bool bullet;
    };

    void Carry(const UnitInput* units, int unitCount);
    void Match(const std::vector<EffectCluster>& measured, const float* camPos, double measuredSeconds);
    void Aim(double seconds);
    void Smooth(float step);

    std::vector<Tracked> m_tracked;
    std::vector<EffectLamp> m_lights;
    EffectTrackerStats m_stats = {};
    const UnitInput* m_units = nullptr;
    int m_unitCount = 0;
    double m_seconds = 0.0;
    double m_lastMeasured = 0.0;
    bool m_clockValid = false;
    int m_mapId = -1;
    uint32_t m_nextId = 1;
    EffectFlashStats m_flashes = {};
};

struct EffectReport
{
    bool available;
    int lights;
    EffectLamp lamps[kMaxEffectLights];
    int drawn;
    unsigned gathers;
    unsigned measurements;
    unsigned ringFull;
    unsigned lastLatencyFrames;
    unsigned maxLatencyFrames;
    double lastCpuMicroseconds;
    double maxCpuMicroseconds;
    double totalCpuMicroseconds;
    unsigned cpuFrames;
    EffectClusterStats lastClusters;
    int columns;
    int rows;
};
