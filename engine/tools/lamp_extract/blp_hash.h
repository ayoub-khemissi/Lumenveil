#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct BlpLevel
{
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t hash = 0;
};

struct BlpPixels
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

bool HashBlp(const std::vector<uint8_t>& b, BlpLevel& out, std::string& why);
bool DecodeBlp(const std::vector<uint8_t>& b, uint32_t largestSide, BlpPixels& out, std::string& why);
const char* BlpFormatName(uint32_t format);
