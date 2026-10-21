#include "cloud_noise.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
constexpr uint32_t kSeed = 0x2545F491u;
constexpr uint32_t kOctaveSeedStep = 0x9E3779B9u;
constexpr float kOctaveGain = 0.5f;
constexpr float kWarpCells = 0.35f;
constexpr int kWarpCellsPerTile = 2;
constexpr float kTwoPi = 6.2831853f;

constexpr bool IsPowerOfTwo(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

static_assert(IsPowerOfTwo(kCloudNoiseSize) && IsPowerOfTwo(kCloudCellsPerTile) && IsPowerOfTwo(kWarpCellsPerTile),
              "lattice periods are powers of two, so the clouds tile without a seam");

uint32_t Mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Gradient(int x, int y, int period, uint32_t seed, float dx, float dy)
{
    const uint32_t h =
        Mix(seed ^ Mix(static_cast<uint32_t>(x & (period - 1)) ^ Mix(static_cast<uint32_t>(y & (period - 1)))));
    const float angle = static_cast<float>(h & 0xFFFFu) / 65536.0f * kTwoPi;
    return std::cos(angle) * dx + std::sin(angle) * dy;
}

float GradientNoise(float u, float v, int period, uint32_t seed)
{
    const float x = u * static_cast<float>(period);
    const float y = v * static_cast<float>(period);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float a = Gradient(x0, y0, period, seed, fx, fy);
    const float b = Gradient(x0 + 1, y0, period, seed, fx - 1.0f, fy);
    const float c = Gradient(x0, y0 + 1, period, seed, fx, fy - 1.0f);
    const float d = Gradient(x0 + 1, y0 + 1, period, seed, fx - 1.0f, fy - 1.0f);
    const float sx = Fade(fx);
    const float sy = Fade(fy);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

float Clouds(float u, float v)
{
    const float warpU = GradientNoise(u, v, kWarpCellsPerTile, kSeed ^ 0xA5A5A5A5u);
    const float warpV = GradientNoise(u, v, kWarpCellsPerTile, kSeed ^ 0x5A5A5A5Au);
    const float wu = u + warpU * kWarpCells / static_cast<float>(kCloudCellsPerTile);
    const float wv = v + warpV * kWarpCells / static_cast<float>(kCloudCellsPerTile);
    float sum = 0.0f;
    float amplitude = 1.0f;
    for (int octave = 0; octave < kCloudOctaves; ++octave)
    {
        sum += amplitude * GradientNoise(wu, wv, kCloudCellsPerTile << octave, kSeed + kOctaveSeedStep * octave);
        amplitude *= kOctaveGain;
    }
    return sum;
}

CloudNoiseLevel Halved(const CloudNoiseLevel& level)
{
    CloudNoiseLevel half;
    half.size = std::max(level.size / 2, 1);
    half.texels.resize(static_cast<size_t>(half.size) * half.size);
    for (int y = 0; y < half.size; ++y)
        for (int x = 0; x < half.size; ++x)
        {
            const int sx = std::min(2 * x, level.size - 1);
            const int sy = std::min(2 * y, level.size - 1);
            const int nx = std::min(sx + 1, level.size - 1);
            const int ny = std::min(sy + 1, level.size - 1);
            const unsigned sum = level.texels[static_cast<size_t>(sy) * level.size + sx] +
                                 level.texels[static_cast<size_t>(sy) * level.size + nx] +
                                 level.texels[static_cast<size_t>(ny) * level.size + sx] +
                                 level.texels[static_cast<size_t>(ny) * level.size + nx];
            half.texels[static_cast<size_t>(y) * half.size + x] = static_cast<uint8_t>((sum + 2) / 4);
        }
    return half;
}
}

std::vector<CloudNoiseLevel> BuildCloudNoise()
{
    const int n = kCloudNoiseSize;
    std::vector<float> value(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            value[static_cast<size_t>(y) * n + x] =
                Clouds((static_cast<float>(x) + 0.5f) / static_cast<float>(n), (static_cast<float>(y) + 0.5f) / static_cast<float>(n));
    std::vector<uint32_t> order(value.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return value[a] < value[b]; });
    CloudNoiseLevel top;
    top.size = n;
    top.texels.resize(value.size());
    const double last = static_cast<double>(order.size() - 1);
    for (size_t rank = 0; rank < order.size(); ++rank)
        top.texels[order[rank]] = static_cast<uint8_t>(std::lround(255.0 * static_cast<double>(rank) / last));
    std::vector<CloudNoiseLevel> levels;
    levels.push_back(std::move(top));
    while (levels.back().size > 1)
        levels.push_back(Halved(levels.back()));
    return levels;
}

float CloudNoiseShareAbove(const CloudNoiseLevel& level, float threshold)
{
    if (level.texels.empty())
        return 0.0f;
    size_t above = 0;
    for (uint8_t t : level.texels)
        above += static_cast<float>(t) / 255.0f > threshold ? 1 : 0;
    return static_cast<float>(above) / static_cast<float>(level.texels.size());
}
