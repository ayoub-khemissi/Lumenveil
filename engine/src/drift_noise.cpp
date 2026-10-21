#include "drift_noise.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace
{
constexpr float kOctaveGain = 0.5f;
constexpr uint32_t kOctaveSeedStep = 0x632BE5ABu;
constexpr float kMean = 0.5f;
constexpr float kSpreadPerDeviation = 0.30f;
constexpr int kChannelShift[kDriftChannels] = {16, 8, 0, 24};
constexpr unsigned kMaxBuildThreads = 16;

enum class NoiseShape
{
    Smooth,
    Ridged,
};

struct ChannelRecipe
{
    uint32_t seed;
    int cellsPerTile;
    int octaves;
    NoiseShape shape;
};

constexpr ChannelRecipe kRecipes[kDriftChannels] = {
    {0x9E3779B9u, kDriftCellsPerTile, 3, NoiseShape::Smooth},
    {0x85EBCA6Bu, kDriftWispCellsPerTile, 3, NoiseShape::Ridged},
    {0xC2B2AE35u, kDriftCellsPerTile, 2, NoiseShape::Smooth},
    {0x27D4EB2Fu, kDriftCellsPerTile, 2, NoiseShape::Smooth},
};

constexpr float kGradients[12][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
    {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
};

uint32_t Mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

uint32_t LatticeHash(int x, int y, int z, uint32_t seed)
{
    return Mix(seed ^ Mix(static_cast<uint32_t>(x) ^ Mix(static_cast<uint32_t>(y) ^ Mix(static_cast<uint32_t>(z)))));
}

constexpr bool IsPowerOfTwo(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

static_assert(IsPowerOfTwo(kDriftCellsPerTile) && IsPowerOfTwo(kDriftWispCellsPerTile),
              "lattice periods are powers of two, so coordinates wrap by mask");

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

class GradientLattice
{
public:
    GradientLattice(int period, uint32_t seed)
        : m_period(period), m_mask(period - 1), m_gradient(static_cast<size_t>(period) * period * period)
    {
        size_t i = 0;
        for (int z = 0; z < period; ++z)
            for (int y = 0; y < period; ++y)
                for (int x = 0; x < period; ++x, ++i)
                    m_gradient[i] = static_cast<uint8_t>(LatticeHash(x, y, z, seed) % 12);
    }

    int Period() const { return m_period; }

    float Noise(float x, float y, float z) const
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int z0 = static_cast<int>(std::floor(z));
        const float fx = x - x0;
        const float fy = y - y0;
        const float fz = z - z0;
        float corners[8];
        for (int c = 0; c < 8; ++c)
        {
            const int cx = c & 1;
            const int cy = (c >> 1) & 1;
            const int cz = (c >> 2) & 1;
            corners[c] = Corner(x0 + cx, y0 + cy, z0 + cz, fx - cx, fy - cy, fz - cz);
        }
        const float u = Fade(fx);
        const float v = Fade(fy);
        const float w = Fade(fz);
        const float bottom = Lerp(Lerp(corners[0], corners[1], u), Lerp(corners[2], corners[3], u), v);
        const float top = Lerp(Lerp(corners[4], corners[5], u), Lerp(corners[6], corners[7], u), v);
        return Lerp(bottom, top, w);
    }

private:
    float Corner(int x, int y, int z, float dx, float dy, float dz) const
    {
        const size_t index = (static_cast<size_t>(z & m_mask) * m_period + (y & m_mask)) * m_period + (x & m_mask);
        const float* g = kGradients[m_gradient[index]];
        return g[0] * dx + g[1] * dy + g[2] * dz;
    }

    int m_period;
    int m_mask;
    std::vector<uint8_t> m_gradient;
};

float Ridge(float noise)
{
    const float crest = 1.0f - std::fabs(noise);
    return crest * crest;
}

float FractalNoise(float u, float v, float w, const std::vector<GradientLattice>& octaves, NoiseShape shape)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    for (const GradientLattice& lattice : octaves)
    {
        const float period = static_cast<float>(lattice.Period());
        const float noise = lattice.Noise(u * period, v * period, w * period);
        sum += amplitude * (shape == NoiseShape::Ridged ? Ridge(noise) : noise);
        total += amplitude;
        amplitude *= kOctaveGain;
    }
    return sum / total;
}

template <typename SliceWork>
void ForEachSlice(int slices, SliceWork work)
{
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, kMaxBuildThreads);
    std::atomic<int> next{0};
    auto drain = [&]() {
        for (int z = next++; z < slices; z = next++)
            work(z);
    };
    std::vector<std::thread> helpers;
    for (unsigned i = 1; i < threads; ++i)
        helpers.emplace_back(drain);
    drain();
    for (std::thread& helper : helpers)
        helper.join();
}

void Normalise(std::vector<float>& values)
{
    double sum = 0.0;
    for (float value : values)
        sum += value;
    const double mean = sum / values.size();
    double variance = 0.0;
    for (float value : values)
        variance += (value - mean) * (value - mean);
    const float deviation = static_cast<float>(std::sqrt(variance / values.size()));
    for (float& value : values)
        value = std::clamp(kMean + (value - static_cast<float>(mean)) / deviation * kSpreadPerDeviation, 0.0f, 1.0f);
}

std::vector<float> NormalisedChannel(int size, const ChannelRecipe& recipe)
{
    std::vector<GradientLattice> octaves;
    for (int octave = 0; octave < recipe.octaves; ++octave)
        octaves.emplace_back(recipe.cellsPerTile << octave,
                             recipe.seed + kOctaveSeedStep * static_cast<uint32_t>(octave));
    std::vector<float> values(static_cast<size_t>(size) * size * size);
    ForEachSlice(size, [&](int z) {
        size_t i = static_cast<size_t>(z) * size * size;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x, ++i)
                values[i] =
                    FractalNoise((x + 0.5f) / size, (y + 0.5f) / size, (z + 0.5f) / size, octaves, recipe.shape);
    });
    Normalise(values);
    return values;
}

uint32_t ChannelByte(float value, int channel)
{
    return static_cast<uint32_t>(std::lround(value * 255.0f)) << kChannelShift[channel];
}

DriftNoiseLevel HalfSize(const DriftNoiseLevel& level)
{
    const int size = level.size / 2;
    DriftNoiseLevel half = {size, std::vector<uint32_t>(static_cast<size_t>(size) * size * size)};
    auto at = [&level](int x, int y, int z) {
        return level.argb[(static_cast<size_t>(z) * level.size + y) * level.size + x];
    };
    size_t i = 0;
    for (int z = 0; z < size; ++z)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x, ++i)
            {
                uint32_t texel = 0;
                for (int channel = 0; channel < kDriftChannels; ++channel)
                {
                    float sum = 0.0f;
                    for (int c = 0; c < 8; ++c)
                        sum += DriftNoiseChannel(at(2 * x + (c & 1), 2 * y + ((c >> 1) & 1), 2 * z + ((c >> 2) & 1)),
                                                 channel);
                    texel |= ChannelByte(sum / 8.0f, channel);
                }
                half.argb[i] = texel;
            }
    return half;
}
}

std::vector<DriftNoiseLevel> BuildDriftNoise()
{
    const size_t texels = static_cast<size_t>(kDriftNoiseSize) * kDriftNoiseSize * kDriftNoiseSize;
    DriftNoiseLevel top = {kDriftNoiseSize, std::vector<uint32_t>(texels)};
    for (int channel = 0; channel < kDriftChannels; ++channel)
    {
        const std::vector<float> values = NormalisedChannel(kDriftNoiseSize, kRecipes[channel]);
        for (size_t i = 0; i < values.size(); ++i)
            top.argb[i] |= ChannelByte(values[i], channel);
    }
    std::vector<DriftNoiseLevel> levels;
    levels.push_back(std::move(top));
    while (levels.back().size > 1)
        levels.push_back(HalfSize(levels.back()));
    return levels;
}

float DriftNoiseChannel(uint32_t argb, int channel)
{
    return static_cast<float>((argb >> kChannelShift[channel]) & 0xFFu) / 255.0f;
}

float DriftNoiseMean(const DriftNoiseLevel& level, int channel)
{
    double sum = 0.0;
    for (uint32_t texel : level.argb)
        sum += DriftNoiseChannel(texel, channel);
    return static_cast<float>(sum / level.argb.size());
}
