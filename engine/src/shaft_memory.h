#pragma once

#include <cmath>

constexpr unsigned kShaftMemorySize = 2048;
constexpr double kShaftMemoryTexelYards = 0.625;
constexpr float kShaftMemoryDepthOffset = 4096.0f;
constexpr float kShaftMemoryDepthSteps = 8.0f;
constexpr float kShaftMemorySubTexels = 4.0f;
constexpr float kShaftMemoryCodesPerDepthStep = 256.0f;
constexpr float kShaftMemoryCodesPerSlopeClass = 16.0f;
constexpr float kShaftMemorySlopeClassesPerOctave = 1.5f;
constexpr float kShaftMemorySlopeOctaveOffset = 4.0f;
constexpr float kShaftMemoryWindowMarginTexels = 2.0f;
constexpr unsigned kShaftMemoryRealignGrid = 512;
constexpr double kShaftMemoryMinHorizontal = 1e-3;

struct ShaftMemoryFrame
{
    double u[3];
    double v[3];
    double l[3];
    double anchor[3];
};

struct ShaftMemoryCamera
{
    long long cell[2];
    float index[2];
    float fraction[2];
    double depth;
};

struct ShaftMemoryReadback
{
    ShaftMemoryFrame frame;
    long long cell[2];
    int valid;
    unsigned forgets;
    unsigned realignments;
};

inline ShaftMemoryFrame MakeShaftMemoryFrame(const float* toLight, const float* anchor, const double* previousU)
{
    ShaftMemoryFrame frame = {};
    double l[3] = {toLight[0], toLight[1], toLight[2]};
    const double length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    for (double& c : l)
        c = length > 0.0 ? c / length : 0.0;
    if (length <= 0.0)
        l[2] = 1.0;
    double u[3] = {-l[1], l[0], 0.0};
    double horizontal = std::sqrt(u[0] * u[0] + u[1] * u[1]);
    if (horizontal < kShaftMemoryMinHorizontal)
    {
        const double along = previousU[0] * l[0] + previousU[1] * l[1] + previousU[2] * l[2];
        for (int k = 0; k < 3; ++k)
            u[k] = previousU[k] - along * l[k];
        horizontal = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (horizontal < kShaftMemoryMinHorizontal)
        {
            u[0] = 1.0 - l[0] * l[0];
            u[1] = -l[0] * l[1];
            u[2] = -l[0] * l[2];
            horizontal = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        }
    }
    for (int k = 0; k < 3; ++k)
    {
        frame.u[k] = u[k] / horizontal;
        frame.l[k] = l[k];
        frame.anchor[k] = anchor[k];
    }
    frame.v[0] = l[1] * frame.u[2] - l[2] * frame.u[1];
    frame.v[1] = l[2] * frame.u[0] - l[0] * frame.u[2];
    frame.v[2] = l[0] * frame.u[1] - l[1] * frame.u[0];
    return frame;
}

inline ShaftMemoryCamera ShaftMemoryCameraIn(const ShaftMemoryFrame& frame, const float* camPos)
{
    const double rel[3] = {camPos[0] - frame.anchor[0], camPos[1] - frame.anchor[1], camPos[2] - frame.anchor[2]};
    const double* axes[2] = {frame.u, frame.v};
    const double size = kShaftMemorySize;
    ShaftMemoryCamera camera = {};
    for (int k = 0; k < 2; ++k)
    {
        const double texels = (rel[0] * axes[k][0] + rel[1] * axes[k][1] + rel[2] * axes[k][2]) / kShaftMemoryTexelYards;
        const double cell = std::floor(texels);
        camera.cell[k] = static_cast<long long>(cell);
        camera.index[k] = static_cast<float>(cell - size * std::floor(cell / size));
        camera.fraction[k] = static_cast<float>(texels - cell);
    }
    camera.depth = rel[0] * frame.l[0] + rel[1] * frame.l[1] + rel[2] * frame.l[2];
    return camera;
}

inline float ShaftMemoryDepth(float code)
{
    return static_cast<float>(static_cast<long long>(code / kShaftMemoryCodesPerDepthStep)) /
               kShaftMemoryDepthSteps +
           0.5f / kShaftMemoryDepthSteps - kShaftMemoryDepthOffset;
}

inline void ShaftMemorySubTexel(float code, float& inTexelU, float& inTexelV)
{
    const long long sub = static_cast<long long>(code) % static_cast<long long>(kShaftMemoryCodesPerSlopeClass);
    const long long row = sub / static_cast<long long>(kShaftMemorySubTexels);
    inTexelU = (static_cast<float>(sub - row * static_cast<long long>(kShaftMemorySubTexels)) + 0.5f) /
               kShaftMemorySubTexels;
    inTexelV = (static_cast<float>(row) + 0.5f) / kShaftMemorySubTexels;
}

inline float ShaftMemorySlope(float code)
{
    const long long rest =
        static_cast<long long>(code) % static_cast<long long>(kShaftMemoryCodesPerDepthStep);
    const long long slopeClass = rest / static_cast<long long>(kShaftMemoryCodesPerSlopeClass);
    return std::exp2(static_cast<float>(slopeClass) / kShaftMemorySlopeClassesPerOctave - kShaftMemorySlopeOctaveOffset);
}

inline void ShaftMemoryPoint(const ShaftMemoryReadback& info, unsigned x, unsigned y, float code, double* world)
{
    const long long size = kShaftMemorySize;
    const long long texel[2] = {x, y};
    double across[2];
    float inTexel[2];
    ShaftMemorySubTexel(code, inTexel[0], inTexel[1]);
    for (int k = 0; k < 2; ++k)
    {
        long long fromCamera = (texel[k] - info.cell[k]) % size;
        fromCamera = fromCamera < 0 ? fromCamera + size : fromCamera;
        fromCamera = fromCamera >= size / 2 ? fromCamera - size : fromCamera;
        across[k] = (static_cast<double>(info.cell[k] + fromCamera) + inTexel[k]) * kShaftMemoryTexelYards;
    }
    const double along = ShaftMemoryDepth(code);
    for (int k = 0; k < 3; ++k)
        world[k] = info.frame.anchor[k] + across[0] * info.frame.u[k] + across[1] * info.frame.v[k] +
                   along * info.frame.l[k];
}
