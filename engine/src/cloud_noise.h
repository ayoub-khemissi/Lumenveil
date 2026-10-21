#pragma once

#include <cstdint>
#include <vector>

constexpr int kCloudNoiseSize = 256;
constexpr int kCloudCellsPerTile = 4;
constexpr int kCloudOctaves = 5;

struct CloudNoiseLevel
{
    int size;
    std::vector<uint8_t> texels;
};

std::vector<CloudNoiseLevel> BuildCloudNoise();
float CloudNoiseShareAbove(const CloudNoiseLevel& level, float threshold);
