#include "corpse_lights.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kTwoPi = 6.283185307179586;

float Phase(uint32_t id, uint32_t salt)
{
    uint32_t h = (id ^ salt) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFu) / 65536.0f;
}
}

float CorpseTwinkle(uint32_t id, double seconds)
{
    const double a = 0.5 + 0.5 * std::sin(kTwoPi * (kCorpseTwinkleHz[0] * seconds + Phase(id, 0x9E3779B9u)));
    const double b = 0.5 + 0.5 * std::sin(kTwoPi * (kCorpseTwinkleHz[1] * seconds + Phase(id, 0x85EBCA6Bu)));
    return 1.0f - kCorpseTwinkle * static_cast<float>(a * b);
}

void CorpseLights::Reset()
{
    m_lit.clear();
    m_lamps.clear();
    m_clockValid = false;
}

const std::vector<EffectLamp>& CorpseLights::Update(const FrameInputs& in, float strength)
{
    if (in.mapId != m_mapId)
    {
        Reset();
        m_mapId = in.mapId;
    }
    const double step = m_clockValid ? std::clamp(in.seconds - m_seconds, 0.0, kMaxCorpseStepSeconds) : 0.0;
    m_seconds = in.seconds;
    m_clockValid = true;

    struct Near
    {
        int index;
        float distanceSquared;
    };
    std::vector<Near> nearby;
    const int count = std::clamp(in.corpseCount, 0, kMaxCorpseInputs);
    m_lootable = count;
    for (int k = 0; k < count; ++k)
    {
        const float d[3] = {in.corpsePos[k][0] - in.camPos[0], in.corpsePos[k][1] - in.camPos[1],
                            in.corpsePos[k][2] - in.camPos[2]};
        const float squared = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (squared <= kCorpseLightYards * kCorpseLightYards)
            nearby.push_back({k, squared});
    }
    std::sort(nearby.begin(), nearby.end(), [&](const Near& a, const Near& b) {
        return a.distanceSquared != b.distanceSquared ? a.distanceSquared < b.distanceSquared
                                                      : in.corpseIds[a.index] < in.corpseIds[b.index];
    });
    if (nearby.size() > static_cast<size_t>(kMaxCorpseLights))
        nearby.resize(kMaxCorpseLights);

    for (Lit& lit : m_lit)
        lit.wanted = false;
    for (const Near& n : nearby)
    {
        const uint32_t id = kCorpseIdBase | (in.corpseIds[n.index] & kCorpseIdMask);
        auto found = std::find_if(m_lit.begin(), m_lit.end(), [&](const Lit& lit) { return lit.id == id; });
        if (found == m_lit.end())
        {
            m_lit.push_back({id, {}, 0.0f, false});
            found = m_lit.end() - 1;
        }
        found->wanted = true;
        std::memcpy(found->position, in.corpsePos[n.index], sizeof(found->position));
        found->position[2] += kCorpseLiftYards;
    }
    const float fade = static_cast<float>(step) / kCorpseFadeSeconds;
    for (Lit& lit : m_lit)
        lit.fade = lit.wanted ? std::min(lit.fade + fade, 1.0f) : std::max(lit.fade - fade, 0.0f);
    m_lit.erase(std::remove_if(m_lit.begin(), m_lit.end(), [](const Lit& lit) { return !lit.wanted && lit.fade <= 0.0f; }),
                m_lit.end());

    m_lamps.clear();
    if (!(strength > 0.0f))
        return m_lamps;
    for (const Lit& lit : m_lit)
    {
        if (!(lit.fade > 0.0f))
            continue;
        EffectLamp lamp = {};
        lamp.id = lit.id;
        std::memcpy(lamp.position, lit.position, sizeof(lamp.position));
        std::memcpy(lamp.colour, kCorpseColour, sizeof(lamp.colour));
        lamp.intensity = kCorpseIntensity * strength * lit.fade * CorpseTwinkle(lit.id, in.seconds);
        lamp.nearYards = kCorpseNearYards;
        lamp.surfaceReach = kCorpseReach;
        m_lamps.push_back(lamp);
    }
    return m_lamps;
}
