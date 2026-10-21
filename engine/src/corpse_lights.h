#pragma once

#include "effect_lights.h"
#include "engine.h"

#include <cstdint>
#include <vector>

constexpr int kMaxCorpseLights = 4;
constexpr float kCorpseLightYards = 40.0f;
constexpr float kCorpseColour[3] = {1.0f, 0.85f, 0.5f};
constexpr float kCorpseIntensity = 0.35f;
constexpr float kCorpseLiftYards = 0.4f;
constexpr float kCorpseNearYards = 0.3f;
constexpr float kCorpseReach = 6.0f;
constexpr float kCorpseFadeSeconds = 0.3f;
constexpr float kCorpseTwinkle = 0.25f;
constexpr float kCorpseTwinkleHz[2] = {2.7f, 4.3f};
constexpr double kMaxCorpseStepSeconds = 0.25;
constexpr uint32_t kCorpseIdBase = 0x08000000u;
constexpr uint32_t kCorpseIdMask = 0x00FFFFFFu;

float CorpseTwinkle(uint32_t id, double seconds);

class CorpseLights
{
public:
    const std::vector<EffectLamp>& Update(const FrameInputs& in, float strength);
    void Reset();
    int Lootable() const { return m_lootable; }
    const std::vector<EffectLamp>& Lamps() const { return m_lamps; }

private:
    struct Lit
    {
        uint32_t id;
        float position[3];
        float fade;
        bool wanted;
    };

    std::vector<Lit> m_lit;
    std::vector<EffectLamp> m_lamps;
    double m_seconds = 0.0;
    bool m_clockValid = false;
    int m_mapId = -1;
    int m_lootable = 0;
};
