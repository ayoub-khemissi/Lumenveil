#pragma once

#include "belt_lantern.h"
#include "config.h"
#include "effect_lights.h"
#include "engine.h"
#include "lamp_catalog.h"

#include <cstdint>
#include <vector>

constexpr float kLampSearchYards = 800.0f;
constexpr float kLampFadeStartYards = 650.0f;
constexpr float kWindowSearchYards = 250.0f;
constexpr float kWindowFadeStartYards = 200.0f;
constexpr int kFarHaloSlots = 64;
constexpr float kFarHaloMinYards = 80.0f;
constexpr float kLampFadeSeconds = 0.4f;
constexpr float kIndoorLampHaze = 0.55f;
constexpr float kHiddenOutdoorLampShare = 0.4f;
constexpr float kHiddenIndoorLampShare = 0.1f;
constexpr float kIndoorLampLight = 0.35f;
constexpr float kLampVisibilitySeconds = 0.1f;
constexpr float kMinLampReach = 1.0f;
constexpr float kMaxLampReach = 48.0f;
constexpr float kSurfaceReachPerLampReach = 2.0f;
constexpr float kPointFalloffPerYard = 0.7f;
constexpr float kPointFalloffPerSquareYard = 0.03f;
constexpr float kMinRankYards = 1.0f;
constexpr int kWindowSlots = 32;
constexpr float kWindowOutsetYards = 0.3f;
constexpr float kWindowLightPerSquareYard = 0.5f;
constexpr float kMinWindowLight = 0.3f;
constexpr float kMaxWindowLight = 2.0f;
constexpr float kWindowReachYards = 6.0f;
constexpr float kWindowReachPerYard = 2.0f;
constexpr float kMaxWindowReach = 14.0f;
constexpr float kMinWindowNearYards = 1.0f;
constexpr float kMinWindowColour = 0.05f;
constexpr float kWindowHaloShare = 0.3f;
constexpr float kFarCoreFromYards = 60.0f;
constexpr float kFarCoreFullYards = 150.0f;
constexpr float kFarCoreGain = 0.4f;
constexpr float kWindowHaloReachYards = 2.5f;
constexpr float kWindowHaloReachPerYard = 1.5f;
constexpr float kMaxWindowHaloReach = 6.0f;
constexpr int kEffectSlots = kMaxEffectLights;
constexpr int kLanternSlots = 1;
constexpr int kSurfaceSlots = kMaxLampCount + kWindowSlots + kEffectSlots + kLanternSlots;
constexpr int kLampSlots = kSurfaceSlots + kFarHaloSlots;

float CarriedShadowStrength(const Config& cfg);
float PointLightFalloff(float yards);
float LampSurfaceReach(float reach);
float LampDistanceFade(float yards);
float WindowDistanceFade(float yards);
float FarHaloRank(float intensity, float reach, float distanceSquared);
float FarCoreShare(float yards);
float FarCoreAnglePerTexel(const FrameInputs& in, int lowHeight);
float WindowIntensity(const lampfile::Window& window);
float WindowReach(const lampfile::Window& window);
float WindowHaloReach(const lampfile::Window& window);
float WindowNearYards(const lampfile::Window& window);
bool WindowGivesLight(const lampfile::Window& window);

struct LampFlickerValue
{
    float intensity;
    float blue;
};

bool LampFlickers(const lampfile::Lamp& lamp);
LampFlickerValue LampFlicker(const lampfile::Lamp& lamp, double seconds, float gain);
LampFlickerValue LanternFlicker(double seconds, float gain);

enum LampTableRow
{
    kLampSphereRow,
    kLampLightRow,
    kLampScreenRow,
    kLampTestRow,
    kLampSurfaceRow,
    kLampViewRow,
    kLampEmitRow,
    kLampCoreRow,
    kLampTableRows,
};

struct LampTable
{
    float texels[kLampTableRows][kLampSlots][4];
    int slots;
    int surfaceSlots;
};

struct LampStats
{
    int nearby;
    int inView;
    int drawn;
    int farDrawn;
    float nearestYards;
    uint32_t nearestName;
    int windowsNearby;
    int windowsInView;
    int windowsDrawn;
    bool lanternDrawn;
    int effectsDrawn;
    int cores;
};

class LampSelection
{
public:
    LampSelection();

    const LampTable& Update(const LampCatalog& catalog, const FrameInputs& in, const Config& cfg, float haze,
                            float light, float windowLight = 0.0f, float lanternGlow = 1.0f,
                            const LanternLamp* lantern = nullptr, float lanternLight = 0.0f,
                            const std::vector<EffectLamp>* effects = nullptr, float effectLight = 0.0f,
                            float farCore = 0.0f);
    void ForgetVisibility();
    float VisibilityBlend() const { return m_visibilityBlend; }
    const LampStats& Stats() const { return m_stats; }
    float SlotFade(uint32_t lamp) const;
    float WindowSlotFade(uint32_t window) const { return SlotFade(window | kWindowBit); }
    float FarSlotFade(uint32_t lamp) const { return SlotFade(lamp | kHaloOnlyBit); }
    float LanternSlotFade() const { return SlotFade(kLanternSlotId); }
    int EffectSlot(uint32_t effect) const;

private:
    static constexpr uint32_t kNoLamp = 0xFFFFFFFFu;
    static constexpr uint32_t kWindowBit = 0x80000000u;
    static constexpr uint32_t kHaloOnlyBit = 0x40000000u;
    static constexpr uint32_t kLanternSlotId = 0x20000000u;
    static constexpr uint32_t kEffectBit = 0x10000000u;
    static constexpr uint32_t kSlotKindMask = 0xF0000000u;
    static constexpr uint32_t kEffectIdMask = 0x0FFFFFFFu;

    struct Slot
    {
        uint32_t lamp = kNoLamp;
        float fade = 0.0f;
        bool fresh = true;
    };

    struct Candidate
    {
        float importance;
        float distanceSquared;
        uint32_t lamp;
    };

    void Reset();
    bool Wanted(uint32_t lamp, size_t count) const;
    void WriteSlot(int slot, const lampfile::Lamp& lamp, const FrameInputs& in, const Config& cfg, float haze,
                   float light, float lanternGlow, bool haloOnly);
    void WriteScreenRows(int slot, const float* view, const FrameInputs& in);
    void WriteLanternSlot(int slot, const LanternLamp& lantern, const FrameInputs& in, const Config& cfg, float haze,
                          float lanternLight);
    void WriteWindowSlot(int slot, const lampfile::Window& window, const FrameInputs& in, const Config& cfg,
                         float windowLight, float haze);
    void WriteEffectSlot(int slot, const EffectLamp* effect, const FrameInputs& in, const Config& cfg,
                         float effectLight);
    size_t PickLamps(const LampMap& map, const FrameInputs& in, size_t wanted, size_t wantedFar, bool lanterns);
    size_t PickWindows(const LampMap& map, const FrameInputs& in, size_t wanted);

    Slot m_slots[kLampSlots];
    std::vector<Candidate> m_candidates;
    std::vector<Candidate> m_windowCandidates;
    LampTable m_table = {};
    LampStats m_stats = {};
    unsigned m_catalogVersion = 0;
    int m_mapId = -1;
    double m_seconds = 0.0;
    bool m_clockValid = false;
    float m_visibilityBlend = 1.0f;
    float m_farCore = 0.0f;
};
