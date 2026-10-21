#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PngImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<float> values;

    float At(uint32_t x, uint32_t y, uint32_t channel) const
    {
        return values[(static_cast<size_t>(y) * width + x) * channels + channel];
    }
};

bool ReadPng(const std::string& path, PngImage& out, std::string* why);
