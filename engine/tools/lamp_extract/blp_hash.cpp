#include "blp_hash.h"

#include "relief_data.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr size_t kBlpHeaderBytes = 1172;

uint32_t U32(const std::vector<uint8_t>& b, size_t at)
{
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

uint16_t U16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void Rgb565(uint16_t c, int rgb[3])
{
    rgb[0] = ((c >> 11) & 31) * 255 / 31;
    rgb[1] = ((c >> 5) & 63) * 255 / 63;
    rgb[2] = (c & 31) * 255 / 31;
}

void ColourBlock(const uint8_t* block, bool fourColours, uint8_t texels[16][4])
{
    const uint16_t c0 = U16(block);
    const uint16_t c1 = U16(block + 2);
    int palette[4][4] = {};
    Rgb565(c0, palette[0]);
    Rgb565(c1, palette[1]);
    palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 255;
    const bool interpolate = fourColours || c0 > c1;
    for (int k = 0; k < 3; ++k)
    {
        palette[2][k] = interpolate ? (2 * palette[0][k] + palette[1][k]) / 3 : (palette[0][k] + palette[1][k]) / 2;
        palette[3][k] = interpolate ? (palette[0][k] + 2 * palette[1][k]) / 3 : 0;
    }
    if (!interpolate)
        palette[3][3] = 0;
    const uint32_t bits = static_cast<uint32_t>(block[4]) | (block[5] << 8) | (block[6] << 16) |
                          (static_cast<uint32_t>(block[7]) << 24);
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 4; ++k)
            texels[i][k] = static_cast<uint8_t>(palette[(bits >> (2 * i)) & 3][k]);
}

void ExplicitAlpha(const uint8_t* block, uint8_t texels[16][4])
{
    for (int i = 0; i < 16; ++i)
        texels[i][3] = static_cast<uint8_t>(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
}

void InterpolatedAlpha(const uint8_t* block, uint8_t texels[16][4])
{
    int a[8] = {block[0], block[1]};
    for (int k = 2; k < 8; ++k)
        a[k] = a[0] > a[1] ? ((8 - k) * a[0] + (k - 1) * a[1]) / 7
                           : k < 6 ? ((6 - k) * a[0] + (k - 1) * a[1]) / 5 : (k == 6 ? 0 : 255);
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k)
        bits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i)
        texels[i][3] = static_cast<uint8_t>(a[(bits >> (3 * i)) & 7]);
}
}

bool HashBlp(const std::vector<uint8_t>& b, BlpLevel& out, std::string& why)
{
    if (b.size() < kBlpHeaderBytes || std::memcmp(b.data(), "BLP2", 4) != 0)
    {
        why = "not a BLP2 file";
        return false;
    }
    const uint32_t compression = b[8];
    const uint32_t alphaDepth = b[9];
    const uint32_t alphaType = b[10];
    const uint32_t width = U32(b, 12);
    const uint32_t height = U32(b, 16);
    if (compression == 2)
    {
        if (alphaType == 0 || alphaDepth <= 1)
            out.format = relieffile::kFormatDxt1;
        else
            out.format = alphaType == 1 ? relieffile::kFormatDxt3 : relieffile::kFormatDxt5;
    }
    else if (compression == 3)
        out.format = relieffile::kFormatArgb;
    else
    {
        why = compression == 1 ? "palettized" : "unknown compression";
        return false;
    }
    uint32_t levels = 0;
    while (levels < 16 && U32(b, 20 + 4 * levels) && U32(b, 84 + 4 * levels))
        ++levels;
    if (!b[11])
        levels = std::min(levels, 1u);
    if (!levels || !width || !height)
    {
        why = "no mip levels";
        return false;
    }
    const uint32_t level = relieffile::HashedLevel(width, height, levels);
    out.width = std::max(width >> level, 1u);
    out.height = std::max(height >> level, 1u);
    const size_t bytes = static_cast<size_t>(relieffile::RowBytes(out.format, out.width)) *
                         relieffile::RowCount(out.format, out.height);
    const size_t offset = U32(b, 20 + 4 * level);
    if (U32(b, 84 + 4 * level) < bytes || offset + bytes > b.size())
    {
        why = "mip level too short";
        return false;
    }
    out.hash = relieffile::Fnv1a(b.data() + offset, bytes);
    return true;
}

bool DecodeBlp(const std::vector<uint8_t>& b, uint32_t largestSide, BlpPixels& out, std::string& why)
{
    BlpLevel top;
    if (!HashBlp(b, top, why))
        return false;
    const uint32_t width = U32(b, 12);
    const uint32_t height = U32(b, 16);
    uint32_t levels = 0;
    while (levels < 16 && U32(b, 20 + 4 * levels) && U32(b, 84 + 4 * levels))
        ++levels;
    if (!b[11])
        levels = std::min(levels, 1u);
    uint32_t level = 0;
    while (level + 1 < levels && std::max(width >> level, height >> level) > largestSide)
        ++level;
    out.width = std::max(width >> level, 1u);
    out.height = std::max(height >> level, 1u);
    const size_t bytes = static_cast<size_t>(relieffile::RowBytes(top.format, out.width)) *
                         relieffile::RowCount(top.format, out.height);
    const size_t offset = U32(b, 20 + 4 * level);
    if (U32(b, 84 + 4 * level) < bytes || offset + bytes > b.size())
    {
        why = "mip level too short";
        return false;
    }
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    const uint8_t* p = b.data() + offset;
    if (top.format == relieffile::kFormatArgb)
    {
        for (size_t i = 0; i < static_cast<size_t>(out.width) * out.height; ++i)
        {
            out.rgba[i * 4 + 0] = p[i * 4 + 2];
            out.rgba[i * 4 + 1] = p[i * 4 + 1];
            out.rgba[i * 4 + 2] = p[i * 4 + 0];
            out.rgba[i * 4 + 3] = b[9] ? p[i * 4 + 3] : 255;
        }
        return true;
    }
    const uint32_t blockBytes = relieffile::BlockBytes(top.format);
    const uint32_t blocksX = (out.width + 3) / 4;
    const uint32_t blocksY = (out.height + 3) / 4;
    for (uint32_t by = 0; by < blocksY; ++by)
        for (uint32_t bx = 0; bx < blocksX; ++bx)
        {
            const uint8_t* block = p + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
            uint8_t texels[16][4];
            if (top.format == relieffile::kFormatDxt1)
                ColourBlock(block, false, texels);
            else
            {
                ColourBlock(block + 8, true, texels);
                if (top.format == relieffile::kFormatDxt3)
                    ExplicitAlpha(block, texels);
                else
                    InterpolatedAlpha(block, texels);
            }
            for (uint32_t i = 0; i < 16; ++i)
            {
                const uint32_t x = bx * 4 + i % 4;
                const uint32_t y = by * 4 + i / 4;
                if (x < out.width && y < out.height)
                    std::memcpy(&out.rgba[(static_cast<size_t>(y) * out.width + x) * 4], texels[i], 4);
            }
        }
    return true;
}

const char* BlpFormatName(uint32_t format)
{
    switch (format)
    {
    case relieffile::kFormatDxt1:
        return "DXT1";
    case relieffile::kFormatDxt3:
        return "DXT3";
    case relieffile::kFormatDxt5:
        return "DXT5";
    default:
        return "ARGB";
    }
}
