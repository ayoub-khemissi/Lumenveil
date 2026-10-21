#include "water_waves.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;
constexpr double kCoxMunkCalm = 0.003;
constexpr double kCoxMunkPerMetrePerSecond = 0.00512;
constexpr int kSpectrumSteps = 256;
constexpr double kSpectrumFloorPerPeak = 0.1;
constexpr int kDirectionTries = 8;
constexpr int kSpreadBisections = 48;
constexpr uint32_t kSeed = 0x57A7E125u;

uint32_t Hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

double Variate(int layer, int wave, int stream)
{
    const uint32_t h = Hash(Hash(Hash(kSeed + static_cast<uint32_t>(layer)) + static_cast<uint32_t>(wave)) +
                            static_cast<uint32_t>(stream));
    return (static_cast<double>(h) + 0.5) / 4294967296.0;
}

double SpreadShare(double angle)
{
    return (1.5 * (angle + kPi) + 2.0 * std::sin(angle) + 0.25 * std::sin(2.0 * angle)) / (3.0 * kPi);
}

double SpreadAngle(double share)
{
    double low = -kPi;
    double high = kPi;
    for (int i = 0; i < kSpreadBisections; ++i)
    {
        const double middle = 0.5 * (low + high);
        (SpreadShare(middle) < share ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

struct FixedWave
{
    double position;
    double spread[kDirectionTries];
    double phase;
};

struct FixedWaves
{
    FixedWave waves[kWaterWaveLayers][kWaterWavesPerLayer];

    FixedWaves()
    {
        for (int layer = 0; layer < kWaterWaveLayers; ++layer)
        {
            int order[kWaterWavesPerLayer];
            for (int j = 0; j < kWaterWavesPerLayer; ++j)
                order[j] = j;
            for (int j = kWaterWavesPerLayer - 1; j > 0; --j)
                std::swap(order[j], order[static_cast<int>(Variate(layer, j, 2 + kDirectionTries) * (j + 1))]);
            for (int j = 0; j < kWaterWavesPerLayer; ++j)
            {
                FixedWave& w = waves[layer][j];
                w.position = (j + Variate(layer, j, 0)) / kWaterWavesPerLayer;
                w.spread[0] = SpreadAngle((order[j] + Variate(layer, j, 1)) / kWaterWavesPerLayer);
                for (int t = 1; t < kDirectionTries; ++t)
                    w.spread[t] = SpreadAngle(Variate(layer, j, 1 + t));
                w.phase = Variate(layer, j, 1 + kDirectionTries);
            }
        }
    }
};

const FixedWaves& Fixed()
{
    static const FixedWaves waves;
    return waves;
}

double PeakWavenumber(float windYardsPerSecond)
{
    const double u = std::max(windYardsPerSecond, kWaterMinWind);
    return kWaterGravity / (u * u);
}

double SpectrumIntegral(double peak, double lnFrom, double lnTo)
{
    if (lnTo <= lnFrom)
        return 0.0;
    const double step = (lnTo - lnFrom) / kSpectrumSteps;
    double sum = 0.0;
    for (int i = 0; i < kSpectrumSteps; ++i)
    {
        const double x = peak / std::exp(lnFrom + (i + 0.5) * step);
        sum += std::exp(-x * x);
    }
    return sum * step;
}

double SpectrumTotal(double peak)
{
    const double lnCap = std::log(kTau / kWaterCapillaryYards);
    return SpectrumIntegral(peak, std::min(std::log(peak * kSpectrumFloorPerPeak), lnCap), lnCap);
}

double Fraction(double x)
{
    return x - std::floor(x);
}
}

float WaterMeanSquareSlope(float windYardsPerSecond)
{
    const double metresPerSecond = std::max(windYardsPerSecond, 0.0f) / kWaterYardsPerMetre;
    return static_cast<float>(kCoxMunkCalm + kCoxMunkPerMetrePerSecond * metresPerSecond);
}

float WaterPeakYards(float windYardsPerSecond)
{
    return static_cast<float>(kTau / PeakWavenumber(windYardsPerSecond));
}

double WaterSpectrumShare(float windYardsPerSecond, double longestYards, double shortestYards)
{
    const double peak = PeakWavenumber(windYardsPerSecond);
    const double total = SpectrumTotal(peak);
    if (total <= 0.0)
        return 0.0;
    return SpectrumIntegral(peak, std::log(kTau / longestYards), std::log(kTau / shortestYards)) / total;
}

double WaterAngularFrequency(const WaterWave& wave, int layer)
{
    const double cycles = std::sqrt(static_cast<double>(wave.cyclesX) * wave.cyclesX +
                                    static_cast<double>(wave.cyclesY) * wave.cyclesY);
    return std::sqrt(kWaterGravity * kTau * cycles / kWaterWaveLayerSpecs[layer].tileYards);
}

WaterWaveField BuildWaterWaveField(float windYardsPerSecond, float headingDegrees, float wavesScale, double seconds)
{
    WaterWaveField field = {};
    const float scale = std::max(wavesScale, 0.0f);
    const double peak = PeakWavenumber(windYardsPerSecond);
    const double total = SpectrumTotal(peak);
    field.windSpeed = std::max(windYardsPerSecond, 0.0f);
    field.headingDegrees = headingDegrees;
    field.peakYards = WaterPeakYards(windYardsPerSecond);
    field.meanSquareSlope = WaterMeanSquareSlope(windYardsPerSecond) * scale * scale;
    const double heading = -headingDegrees * kPi / 180.0;
    double resolved = 0.0;
    for (int layer = 0; layer < kWaterWaveLayers; ++layer)
    {
        const WaterWaveLayerSpec& spec = kWaterWaveLayerSpecs[layer];
        const int lookups = WaterWaveLookupsOf(layer);
        double meanTurn = 0.0;
        for (int i = 0; i < kWaterWaveLookups; ++i)
            meanTurn += kWaterWaveLookupSpecs[i].layer == layer ? WaterLookupTurnRadians(i) / lookups : 0.0;
        const double lnLow = std::log(kTau / spec.longestYards);
        const double width = std::log(kTau / spec.shortestYards) - lnLow;
        for (int j = 0; j < kWaterWavesPerLayer; ++j)
        {
            const FixedWave& fixed = Fixed().waves[layer][j];
            const double k = std::exp(lnLow + width * fixed.position);
            const double x = peak / k;
            const double variance =
                total > 0.0 ? field.meanSquareSlope * std::exp(-x * x) * width / kWaterWavesPerLayer / total : 0.0;
            const double cyclesPerTile = k * spec.tileYards / kTau;
            double m = 0.0;
            double n = 0.0;
            for (int t = 0; t < kDirectionTries; ++t)
            {
                const double angle = heading - meanTurn + fixed.spread[t];
                m = std::round(cyclesPerTile * std::cos(angle));
                n = std::round(cyclesPerTile * std::sin(angle));
                bool taken = m == 0.0 && n == 0.0;
                for (int i = 0; i < j && !taken; ++i)
                    taken = field.waves[layer][i].cyclesX == m && field.waves[layer][i].cyclesY == n;
                if (!taken)
                    break;
            }
            WaterWave& w = field.waves[layer][j];
            w.cyclesX = static_cast<float>(m);
            w.cyclesY = static_cast<float>(n);
            w.slope = static_cast<float>(std::sqrt(2.0 * variance / lookups));
            w.phase = static_cast<float>(Fraction(fixed.phase + WaterAngularFrequency(w, layer) * seconds / kTau));
            resolved += 0.5 * lookups * static_cast<double>(w.slope) * w.slope;
        }
    }
    field.resolvedMeanSquareSlope = static_cast<float>(resolved);
    field.unresolvedMeanSquareSlope = std::max(field.meanSquareSlope - field.resolvedMeanSquareSlope, 0.0f);
    return field;
}

float WaterBaseRoughness(const WaterWaveField& field)
{
    return kWaterGlitterSharpness + kWaterUnresolvedGlitterShare * field.unresolvedMeanSquareSlope;
}

int WaterWaveLookupsOf(int layer)
{
    int lookups = 0;
    for (const WaterWaveLookup& l : kWaterWaveLookupSpecs)
        lookups += l.layer == layer ? 1 : 0;
    return lookups;
}

double WaterLookupTurnRadians(int lookup)
{
    return kWaterWaveLookupSpecs[lookup].turnDegrees * kPi / 180.0;
}

int WaterWaveLevelTexels(int level)
{
    return std::max(kWaterWaveTexels >> level, 1);
}

float WaterWaveFade(float cycles, int level)
{
    const float wavelengthTexels = WaterWaveLevelTexels(level) / std::max(cycles, 1e-3f);
    return std::clamp((wavelengthTexels - kWaterWaveFadeFromTexels) / kWaterWaveFadeWidthTexels, 0.0f, 1.0f);
}

void WaterWaveTexel(const WaterWaveField& field, int layer, int level, double u, double v, float out[4])
{
    double slope[2] = {};
    double lost[2] = {};
    for (const WaterWave& w : field.waves[layer])
    {
        const double cycles = std::sqrt(static_cast<double>(w.cyclesX) * w.cyclesX +
                                        static_cast<double>(w.cyclesY) * w.cyclesY);
        const double fade = WaterWaveFade(static_cast<float>(cycles), level);
        const double direction[2] = {w.cyclesX / cycles, w.cyclesY / cycles};
        const double c = std::cos(kTau * Fraction(w.cyclesX * u + w.cyclesY * v - w.phase));
        for (int i = 0; i < 2; ++i)
        {
            slope[i] += direction[i] * w.slope * fade * c;
            lost[i] += direction[i] * direction[i] * 0.5 * w.slope * w.slope * (1.0 - fade * fade);
        }
    }
    out[0] = static_cast<float>(slope[0]);
    out[1] = static_cast<float>(slope[1]);
    out[2] = static_cast<float>(slope[0] * slope[0] + lost[0]);
    out[3] = static_cast<float>(slope[1] * slope[1] + lost[1]);
}

void WaterLookupUv(int lookup, double x, double y, double* u, double* v)
{
    const double turn = WaterLookupTurnRadians(lookup);
    const double perTile = 1.0 / kWaterWaveLayerSpecs[kWaterWaveLookupSpecs[lookup].layer].tileYards;
    *u = (std::cos(turn) * x + std::sin(turn) * y) * perTile;
    *v = (-std::sin(turn) * x + std::cos(turn) * y) * perTile;
}

void WaterSlopeAt(const WaterWaveField& field, double x, double y, int level, double out[2])
{
    out[0] = out[1] = 0.0;
    for (int lookup = 0; lookup < kWaterWaveLookups; ++lookup)
    {
        double u = 0.0;
        double v = 0.0;
        WaterLookupUv(lookup, x, y, &u, &v);
        float texel[4];
        WaterWaveTexel(field, kWaterWaveLookupSpecs[lookup].layer, level, u, v, texel);
        const double turn = WaterLookupTurnRadians(lookup);
        out[0] += std::cos(turn) * texel[0] - std::sin(turn) * texel[1];
        out[1] += std::sin(turn) * texel[0] + std::cos(turn) * texel[1];
    }
}
