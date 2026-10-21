#include "lamp_tiles.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
bool AnyLight(const float* rgb)
{
    return rgb[0] != 0.0f || rgb[1] != 0.0f || rgb[2] != 0.0f;
}

void Extent(float centre, float radius, float nearZ, float farZ, float& low, float& high)
{
    const float up = centre + radius;
    const float down = centre - radius;
    high = up / (up > 0.0f ? nearZ : farZ);
    low = down / (down < 0.0f ? nearZ : farZ);
}

int TileOf(float pixels, int tiles)
{
    return std::clamp(static_cast<int>(std::floor(pixels / static_cast<float>(kLampTilePixels))), 0, tiles - 1);
}
}

bool LampRectOnScreen(const float* viewCentre, float radius, const float* glProjection, float width, float height,
                      LampScreenRect& rect)
{
    if (!(radius > 0.0f) || !(viewCentre[2] + radius > 0.0f))
        return false;
    const float nearZ = viewCentre[2] - radius;
    const float farZ = viewCentre[2] + radius;
    if (nearZ <= kLampTileNearDepth)
    {
        rect = {0.0f, 0.0f, width, height};
        return true;
    }
    float xLow = 0.0f;
    float xHigh = 0.0f;
    float yLow = 0.0f;
    float yHigh = 0.0f;
    Extent(viewCentre[0], radius, nearZ, farZ, xLow, xHigh);
    Extent(viewCentre[1], radius, nearZ, farZ, yLow, yHigh);
    const float ndcX[2] = {xLow * glProjection[0] + glProjection[8], xHigh * glProjection[0] + glProjection[8]};
    const float ndcY[2] = {yLow * glProjection[5] + glProjection[9], yHigh * glProjection[5] + glProjection[9]};
    float left = (std::min(ndcX[0], ndcX[1]) * 0.5f + 0.5f) * width;
    float right = (std::max(ndcX[0], ndcX[1]) * 0.5f + 0.5f) * width;
    float top = (0.5f - std::max(ndcY[0], ndcY[1]) * 0.5f) * height;
    float bottom = (0.5f - std::min(ndcY[0], ndcY[1]) * 0.5f) * height;
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) || !std::isfinite(bottom))
    {
        rect = {0.0f, 0.0f, width, height};
        return true;
    }
    const float margin = kLampTileMarginPixels + kLampTileMarginShare * std::max(right - left, bottom - top);
    left -= margin;
    right += margin;
    top -= margin;
    bottom += margin;
    if (right < 0.0f || bottom < 0.0f || left > width || top > height)
        return false;
    rect = {std::max(left, 0.0f), std::max(top, 0.0f), std::min(right, width), std::min(bottom, height)};
    return true;
}

float LampSurfaceTileRadius(const LampTable& table, int slot)
{
    const float reach = table.texels[kLampViewRow][slot][3];
    return AnyLight(table.texels[kLampSurfaceRow][slot]) && reach > 0.0f ? reach : 0.0f;
}

float LampHaloTileRadius(const LampTable& table, int slot, float coreAnglePerTexel)
{
    const float* sphere = table.texels[kLampSphereRow][slot];
    float radius = AnyLight(table.texels[kLampLightRow][slot]) ? std::max(sphere[3], 0.0f) : 0.0f;
    if (AnyLight(table.texels[kLampCoreRow][slot]))
    {
        const float yards = std::sqrt(sphere[0] * sphere[0] + sphere[1] * sphere[1] + sphere[2] * sphere[2]);
        radius = std::max(radius, kFarCoreSpreadForTiles *
                                      std::max(kFarCoreYardsForTiles, std::fabs(coreAnglePerTexel) * yards));
    }
    return radius;
}

void LampTiles::Resize(const D3DVIEWPORT9& viewport)
{
    const int tilesX = std::max(1, static_cast<int>((viewport.Width + kLampTilePixels - 1) / kLampTilePixels));
    const int tilesY = std::max(1, static_cast<int>((viewport.Height + kLampTilePixels - 1) / kLampTilePixels));
    m_width = static_cast<float>(viewport.Width);
    m_height = static_cast<float>(viewport.Height);
    if (tilesX == m_tilesX && tilesY == m_tilesY)
        return;
    m_tilesX = tilesX;
    m_tilesY = tilesY;
    for (std::vector<uint16_t>& counts : m_counts)
        counts.assign(static_cast<size_t>(tilesX * tilesY), 0);
    m_texels.assign(static_cast<size_t>(TextureWidth()) * TextureHeight(), 0);
}

size_t LampTiles::TexelOffset(int list, int tileX, int tileY, int index) const
{
    const size_t row = static_cast<size_t>((list * m_tilesY + tileY) * kLampTileBlockHeight + index / kLampTileBlockWidth);
    return row * TextureWidth() + static_cast<size_t>(tileX * kLampTileBlockWidth + index % kLampTileBlockWidth);
}

void LampTiles::Add(LampTileList list, int slot, const LampScreenRect& rect)
{
    const int x0 = TileOf(rect.left, m_tilesX);
    const int x1 = TileOf(rect.right, m_tilesX);
    const int y0 = TileOf(rect.top, m_tilesY);
    const int y1 = TileOf(rect.bottom, m_tilesY);
    std::vector<uint16_t>& counts = m_counts[list];
    const uint8_t value = static_cast<uint8_t>(slot);
    for (int y = y0; y <= y1; ++y)
    {
        uint16_t* count = &counts[static_cast<size_t>(y * m_tilesX + x0)];
        uint8_t* block = &m_texels[TexelOffset(list, x0, y, 0)];
        for (int x = x0; x <= x1; ++x, ++count, block += kLampTileBlockWidth)
        {
            const int index = *count + 1;
            if (index <= kLampTileCapacity)
                block[(index / kLampTileBlockWidth) * TextureWidth() + index % kLampTileBlockWidth] = value;
            if (*count < 0xFFFF)
                ++*count;
        }
    }
    ++m_stats.slots[list];
}

void LampTiles::Build(const LampTable& table, const float* glProjection, const D3DVIEWPORT9& viewport,
                      float coreAnglePerTexel)
{
    Resize(viewport);
    m_stats = {};
    m_stats.tiles = m_tilesX * m_tilesY;
    m_stats.tableSlots = table.slots;
    m_stats.tableSurfaceSlots = table.surfaceSlots;
    for (std::vector<uint16_t>& counts : m_counts)
        std::fill(counts.begin(), counts.end(), static_cast<uint16_t>(0));
    LampScreenRect rect = {};
    for (int slot = 0; slot < std::min(table.surfaceSlots, kLampSlots); ++slot)
    {
        const float radius = LampSurfaceTileRadius(table, slot);
        m_stats.lit[kLampTileSurfaces] += radius > 0.0f ? 1 : 0;
        if (radius > 0.0f &&
            LampRectOnScreen(table.texels[kLampViewRow][slot], radius, glProjection, m_width, m_height, rect))
            Add(kLampTileSurfaces, slot, rect);
    }
    for (int slot = 0; slot < std::min(table.slots, kLampSlots); ++slot)
    {
        const float radius = LampHaloTileRadius(table, slot, coreAnglePerTexel);
        m_stats.lit[kLampTileHalos] += radius > 0.0f ? 1 : 0;
        if (radius > 0.0f &&
            LampRectOnScreen(table.texels[kLampViewRow][slot], radius, glProjection, m_width, m_height, rect))
            Add(kLampTileHalos, slot, rect);
    }
    for (int list = 0; list < kLampTileLists; ++list)
        for (int tileY = 0; tileY < m_tilesY; ++tileY)
            for (int tileX = 0; tileX < m_tilesX; ++tileX)
            {
                const int count = m_counts[list][static_cast<size_t>(tileY * m_tilesX + tileX)];
                m_texels[TexelOffset(list, tileX, tileY, 0)] =
                    count > kLampTileCapacity ? kLampTileOverflow : static_cast<uint8_t>(count);
                m_stats.listed[list] += count;
                m_stats.overflowing[list] += count > kLampTileCapacity ? 1 : 0;
                m_stats.longest[list] = std::max(m_stats.longest[list], count);
            }
}

void LampTiles::Empty()
{
    for (std::vector<uint16_t>& counts : m_counts)
        std::fill(counts.begin(), counts.end(), static_cast<uint16_t>(0));
    std::fill(m_texels.begin(), m_texels.end(), static_cast<uint8_t>(0));
}

void LampTiles::Write(unsigned char* bits, int pitch, int bytesPerTexel) const
{
    const size_t width = TextureWidth();
    const UINT height = TextureHeight();
    if (bytesPerTexel == 1)
    {
        for (UINT row = 0; row < height; ++row)
            std::memcpy(bits + static_cast<size_t>(row) * pitch, &m_texels[row * width], width);
        return;
    }
    std::vector<uint8_t>& expanded = m_row;
    expanded.resize(width * static_cast<size_t>(bytesPerTexel));
    for (UINT row = 0; row < height; ++row)
    {
        const uint8_t* source = &m_texels[row * width];
        for (size_t i = 0; i < width; ++i)
            std::memset(&expanded[i * bytesPerTexel], source[i], static_cast<size_t>(bytesPerTexel));
        std::memcpy(bits + static_cast<size_t>(row) * pitch, expanded.data(), expanded.size());
    }
}

int LampTiles::Count(LampTileList list, int tileX, int tileY) const
{
    if (tileX < 0 || tileY < 0 || tileX >= m_tilesX || tileY >= m_tilesY)
        return 0;
    return m_counts[list][static_cast<size_t>(tileY * m_tilesX + tileX)];
}

int LampTiles::Entry(LampTileList list, int tileX, int tileY, int index) const
{
    if (tileX < 0 || tileY < 0 || tileX >= m_tilesX || tileY >= m_tilesY || index < 0 ||
        index >= std::min(Count(list, tileX, tileY), kLampTileCapacity))
        return -1;
    return m_texels[TexelOffset(list, tileX, tileY, index + 1)];
}
