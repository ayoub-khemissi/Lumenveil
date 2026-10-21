#include "water_ripples.h"

#include <algorithm>
#include <cmath>

namespace
{
double SmoothStep(double from, double to, double x)
{
    const double t = std::clamp((x - from) / (to - from), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Reach(const double* dx, const double* dy)
{
    const double longest = std::sqrt(std::max(dx[0] * dx[0] + dx[1] * dx[1], dy[0] * dy[0] + dy[1] * dy[1]));
    return kWaterRippleBehindFront + 2.0 * longest;
}

double Spread()
{
    return kWaterRippleRing.frontYardsPerSecond * kWaterRippleRing.lifeSeconds + kWaterRippleBehindFront +
           kWaterRippleReachMargin;
}
}

double WaterRippleAmplitude(double speed, bool swimming)
{
    const double walking = kWaterRippleStillAmplitude +
                           (1.0 - kWaterRippleStillAmplitude) * std::clamp(speed / kWaterRippleFullSpeed, 0.0, 1.0);
    return swimming ? std::max(kWaterRippleSwimAmplitude, walking) : walking;
}

double WaterRippleWetness(double immersion)
{
    return SmoothStep(0.0, kWaterRippleWetYards, immersion) *
           std::clamp((kWaterRippleDeepestYards + kWaterRippleFadeYards - immersion) / kWaterRippleFadeYards, 0.0,
                      1.0);
}

void WaterRipples::Forget()
{
    m_count = 0;
    m_next = 0;
    m_speed = 0.0;
    m_have = false;
}

bool WaterRipples::Update(bool known, const float* feet, bool swimming, double seconds)
{
    if (!known)
    {
        Forget();
        return false;
    }
    const double here[3] = {feet[0], feet[1], feet[2]};
    if (m_have)
    {
        const double dt = seconds - m_lastSeconds;
        const double mx = here[0] - m_last[0];
        const double my = here[1] - m_last[1];
        const double mz = here[2] - m_last[2];
        if (dt < 0.0 || std::sqrt(mx * mx + my * my + mz * mz) > kWaterRippleForgetYards)
            Forget();
        else if (dt > 0.0)
            m_speed += (std::sqrt(mx * mx + my * my) / dt - m_speed) * (1.0 - std::exp(-dt / kWaterRippleSpeedSeconds));
    }
    for (int k = 0; k < 3; ++k)
        m_last[k] = here[k];
    m_lastSeconds = seconds;
    m_have = true;
    if (m_count > 0)
    {
        const WaterRippleSource& last = Source(0);
        const double ox = here[0] - last.x;
        const double oy = here[1] - last.y;
        const bool moved = ox * ox + oy * oy >= kWaterRippleSpacingYards * kWaterRippleSpacingYards;
        const double period = moved      ? kWaterRippleStepSeconds
                              : swimming ? kWaterRippleSwimSeconds
                                         : kWaterRippleStillSeconds;
        if (seconds - m_lastEmit < period)
            return false;
    }
    m_sources[m_next] = {here[0], here[1], here[2], seconds, WaterRippleAmplitude(m_speed, swimming)};
    m_next = (m_next + 1) % kWaterRippleSources;
    m_count = std::min(m_count + 1, kWaterRippleSources);
    m_lastEmit = seconds;
    return true;
}

const WaterRippleSource& WaterRipples::Source(int newest) const
{
    return m_sources[(m_next - 1 - newest + 2 * kWaterRippleSources) % kWaterRippleSources];
}

WaterRippleConstants WaterRipples::Constants(const float* camPos, double seconds, float strength) const
{
    WaterRippleConstants c = {};
    for (int i = 0; i < kWaterRippleSources; ++i)
        c.table[0][i][3] = static_cast<float>(kWaterRippleUnusedAge);
    double low[2] = {1e30, 1e30};
    double high[2] = {-1e30, -1e30};
    int live = 0;
    for (int i = 0; i < m_count; ++i)
    {
        const WaterRippleSource& s = Source(i);
        const double age = seconds - s.born;
        if (age < 0.0 || age >= kWaterRippleRing.lifeSeconds)
            continue;
        c.table[0][i][0] = static_cast<float>(s.x - camPos[0]);
        c.table[0][i][1] = static_cast<float>(s.y - camPos[1]);
        c.table[0][i][2] = static_cast<float>(s.z - camPos[2]);
        c.table[0][i][3] = static_cast<float>(age);
        c.table[1][i][0] = static_cast<float>(s.amplitude);
        low[0] = std::min(low[0], s.x);
        low[1] = std::min(low[1], s.y);
        high[0] = std::max(high[0], s.x);
        high[1] = std::max(high[1], s.y);
        ++live;
    }
    if (!live || !(strength > 0.0f))
        return c;
    const double halfX = 0.5 * (high[0] - low[0]);
    const double halfY = 0.5 * (high[1] - low[1]);
    c.ripples[0] = strength;
    c.reach[0] = static_cast<float>(0.5 * (low[0] + high[0]) - camPos[0]);
    c.reach[1] = static_cast<float>(0.5 * (low[1] + high[1]) - camPos[1]);
    c.reach[2] = static_cast<float>(std::sqrt(halfX * halfX + halfY * halfY) + Spread());
    return c;
}

void WaterRipples::SlopeAt(double seconds, float strength, double x, double y, double waterZ, const double* dx,
                           const double* dy, double out[3]) const
{
    out[0] = out[1] = out[2] = 0.0;
    if (!(strength > 0.0f))
        return;
    const double reach = Reach(dx, dy);
    for (int i = 0; i < m_count; ++i)
    {
        const WaterRippleSource& s = Source(i);
        const double age = seconds - s.born;
        const double tx = x - s.x;
        const double ty = y - s.y;
        const double wet = WaterRippleWetness(waterZ - s.z);
        if (age < 0.0 || age >= kWaterRippleRing.lifeSeconds || !(wet > 0.0) ||
            std::fabs(std::sqrt(tx * tx + ty * ty) - age * kWaterRippleRing.frontYardsPerSecond) >= reach)
            continue;
        AddWaterRing(kWaterRippleRing, tx, ty, age, strength * wet * s.amplitude, dx, dy, out);
    }
}
