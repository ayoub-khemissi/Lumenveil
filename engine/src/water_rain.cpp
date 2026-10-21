#include "water_rain.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kSeed = 0x2A1D0C5Bu;
constexpr int kEnergyRadiusSteps = 4000;
constexpr int kEnergyAgeSteps = 1000;
constexpr double kByte = 255.0;

uint32_t Hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

const std::vector<uint32_t>& Table()
{
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(static_cast<size_t>(kWaterRainTableTexels) * kWaterRainTableTexels);
        BuildWaterRainTable(t.data());
        return t;
    }();
    return table;
}

int Wrapped(double index)
{
    const double n = static_cast<double>(kWaterRainTableTexels);
    return static_cast<int>(index - n * std::floor(index / n));
}

double SmoothStep(double from, double to, double x)
{
    const double t = std::clamp((x - from) / (to - from), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Amplitude(float tableValue)
{
    return kWaterRainMinAmplitude + (1.0 - kWaterRainMinAmplitude) * tableValue;
}
}

void BuildWaterRainTable(uint32_t* texels)
{
    const int n = kWaterRainTableTexels * kWaterRainTableTexels;
    for (int i = 0; i < n; ++i)
    {
        uint32_t texel = 0;
        for (int channel = 0; channel < 4; ++channel)
        {
            const uint32_t byte = Hash(Hash(kSeed + static_cast<uint32_t>(i)) + static_cast<uint32_t>(channel)) >> 24;
            const int shift = channel == kWaterRainX            ? 16
                              : channel == kWaterRainY          ? 8
                              : channel == kWaterRainActive     ? 0
                                                                : 24;
            texel |= byte << shift;
        }
        texels[i] = texel;
    }
}

float WaterRainTableValue(int u, int v, WaterRainChannel channel)
{
    const uint32_t texel = Table()[static_cast<size_t>(Wrapped(v)) * kWaterRainTableTexels + Wrapped(u)];
    const int shift = channel == kWaterRainX        ? 16
                      : channel == kWaterRainY      ? 8
                      : channel == kWaterRainActive ? 0
                                                    : 24;
    return static_cast<float>(((texel >> shift) & 0xFFu) / kByte);
}

float WaterRainActiveShare(bool raining, float intensity)
{
    if (!raining)
        return 0.0f;
    return static_cast<float>(kWaterRainActivePerIntensity * std::clamp(intensity, 0.0f, 1.0f));
}

float WaterRainStrength(float waterRain)
{
    return std::clamp(waterRain, 0.0f, kWaterRainMostStrength);
}

double WaterRainLifeEnvelope(double age)
{
    return WaterRingEnvelope(kWaterRainRing, age);
}

double WaterRainRingSlope(double radius, double age, double amplitude)
{
    return WaterRingSlope(kWaterRainRing, radius, age, amplitude);
}

double WaterRainRingEnergy(double age)
{
    const double step = kWaterRainCellYards / kEnergyRadiusSteps;
    double sum = 0.0;
    for (int i = 0; i < kEnergyRadiusSteps; ++i)
    {
        const double r = (i + 0.5) * step;
        const double s = WaterRainRingSlope(r, age, 1.0);
        sum += s * s * 2.0 * kPi * r * step;
    }
    return sum;
}

double WaterRainMeanRingEnergy()
{
    static const double mean = [] {
        double sum = 0.0;
        for (int i = 0; i < kEnergyAgeSteps; ++i)
            sum += WaterRainRingEnergy((i + 0.5) / kEnergyAgeSteps);
        return sum / kEnergyAgeSteps;
    }();
    return mean;
}

double WaterRainMeanSquareAmplitude()
{
    static const double mean = [] {
        double sum = 0.0;
        for (int v = 0; v < kWaterRainTableTexels; ++v)
            for (int u = 0; u < kWaterRainTableTexels; ++u)
            {
                const double a = Amplitude(WaterRainTableValue(u, v, kWaterRainAmplitudeOrPhase));
                sum += a * a;
            }
        return sum / (static_cast<double>(kWaterRainTableTexels) * kWaterRainTableTexels);
    }();
    return mean;
}

float WaterRainVariance(float activeShare)
{
    return static_cast<float>(activeShare * kWaterRainSlots / (kWaterRainCellYards * kWaterRainCellYards) *
                              WaterRainMeanSquareAmplitude() * WaterRainMeanRingEnergy());
}

float WaterRainCycles(double seconds)
{
    const double cycles = seconds / kWaterRainLifeSeconds;
    return static_cast<float>(cycles - kWaterRainCycleWrap * std::floor(cycles / kWaterRainCycleWrap));
}

WaterRainConstants BuildWaterRainConstants(const float* camPos, double seconds, float activeShare, float strength)
{
    WaterRainConstants c = {};
    const bool on = activeShare > 0.0f && strength > 0.0f;
    c.rain[0] = on ? activeShare : 0.0f;
    c.rain[1] = WaterRainCycles(seconds);
    c.rain[2] = on ? WaterRainVariance(activeShare) * strength * strength : 0.0f;
    c.rain[3] = on ? strength : 0.0f;
    for (int axis = 0; axis < 2; ++axis)
    {
        const double cells = camPos[axis] / kWaterRainCellYards;
        const double whole = std::floor(cells);
        c.cell[axis] = static_cast<float>(cells - whole);
        c.cell[2 + axis] = static_cast<float>(Wrapped(whole));
    }
    return c;
}

double WaterRainPooledShare(const double* dx, const double* dy)
{
    const double longest = std::sqrt(std::max(dx[0] * dx[0] + dx[1] * dx[1], dy[0] * dy[0] + dy[1] * dy[1]));
    return SmoothStep(kWaterRainResolvedFootprint, kWaterRainPooledFootprint, kWaterRingFootprintShare * longest);
}

void WaterRainSlopeAt(const WaterRainConstants& c, double x, double y, const double* dx, const double* dy,
                      double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    if (!(c.rain[3] > 0.0f))
        return;
    const double pooled = WaterRainPooledShare(dx, dy);
    double sum[3] = {};
    if (pooled < 1.0)
    {
        const double qx = x / kWaterRainCellYards;
        const double qy = y / kWaterRainCellYards;
        const double homeX = std::floor(qx);
        const double homeY = std::floor(qy);
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i)
                for (int k = 0; k < kWaterRainSlots; ++k)
                {
                    const double cellX = homeX + i;
                    const double cellY = homeY + j;
                    const int slotX = Wrapped(cellX) + k * kWaterRainSlotStep[0];
                    const int slotY = Wrapped(cellY) + k * kWaterRainSlotStep[1];
                    const float cycles =
                        c.rain[1] + WaterRainTableValue(slotX, slotY + kWaterRainPhaseRow, kWaterRainAmplitudeOrPhase);
                    const float n = std::floor(cycles);
                    const float age = cycles - n;
                    const int dropX = slotX + static_cast<int>(n) * kWaterRainCycleStep[0];
                    const int dropY = slotY + static_cast<int>(n) * kWaterRainCycleStep[1];
                    const double tx =
                        (qx - cellX - WaterRainTableValue(dropX, dropY, kWaterRainX)) * kWaterRainCellYards;
                    const double ty =
                        (qy - cellY - WaterRainTableValue(dropX, dropY, kWaterRainY)) * kWaterRainCellYards;
                    const double radius = std::sqrt(tx * tx + ty * ty);
                    const double amplitude =
                        c.rain[3] * Amplitude(WaterRainTableValue(dropX, dropY, kWaterRainAmplitudeOrPhase));
                    if (WaterRainTableValue(dropX, dropY, kWaterRainActive) < c.rain[0] &&
                        radius < kWaterRainCellYards)
                        AddWaterRing(kWaterRainRing, tx, ty, age, amplitude, dx, dy, sum);
                }
    }
    const double keep = 1.0 - pooled;
    out[0] = keep * sum[0];
    out[1] = keep * sum[1];
    out[2] = keep * sum[2] + pooled * c.rain[2] + pooled * keep * (sum[0] * sum[0] + sum[1] * sum[1]);
}
