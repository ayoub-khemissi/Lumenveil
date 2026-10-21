#pragma once

#include "lamp_selection.h"

#include <d3d9.h>

#include <cstdint>
#include <vector>

constexpr int kLampTilePixels = 64;
constexpr int kLampTileBlockWidth = 16;
constexpr int kLampTileBlockHeight = 8;
constexpr int kLampTileCapacity = kLampTileBlockWidth * kLampTileBlockHeight - 1;
constexpr uint8_t kLampTileOverflow = 255;
constexpr float kLampTileMarginPixels = 2.0f;
constexpr float kLampTileMarginShare = 0.01f;
constexpr float kLampTileNearDepth = 0.1f;
constexpr float kFarCoreSpreadForTiles = 3.0f;
constexpr float kFarCoreYardsForTiles = 0.4f;


static_assert(kLampSlots < kLampTileOverflow, "a slot index fits a byte below the overflow mark");

enum LampTileList
{
    kLampTileSurfaces,
    kLampTileHalos,
    kLampTileLists,
};

struct LampScreenRect
{
    float left;
    float top;
    float right;
    float bottom;
};

bool LampRectOnScreen(const float* viewCentre, float radius, const float* glProjection, float width, float height,
                      LampScreenRect& rect);
float LampHaloTileRadius(const LampTable& table, int slot, float coreAnglePerTexel);
float LampSurfaceTileRadius(const LampTable& table, int slot);

struct LampTileStats
{
    int tiles;
    int listed[kLampTileLists];
    int overflowing[kLampTileLists];
    int longest[kLampTileLists];
    int slots[kLampTileLists];
    int lit[kLampTileLists];
    int tableSlots;
    int tableSurfaceSlots;
    double buildMs;
    double writeMs;
};

class LampTiles
{
public:
    void Build(const LampTable& table, const float* glProjection, const D3DVIEWPORT9& viewport,
               float coreAnglePerTexel);
    int TilesX() const { return m_tilesX; }
    int TilesY() const { return m_tilesY; }
    UINT TextureWidth() const { return static_cast<UINT>(m_tilesX * kLampTileBlockWidth); }
    UINT TextureHeight() const { return static_cast<UINT>(m_tilesY * kLampTileBlockHeight * kLampTileLists); }
    void Write(unsigned char* bits, int pitch, int bytesPerTexel) const;
    void Empty();
    int Count(LampTileList list, int tileX, int tileY) const;
    int Entry(LampTileList list, int tileX, int tileY, int index) const;
    const std::vector<uint8_t>& Texels() const { return m_texels; }
    const LampTileStats& Stats() const { return m_stats; }
    void NoteWriteMs(double ms) { m_stats.writeMs = ms; }
    void NoteBuildMs(double ms) { m_stats.buildMs = ms; }

private:
    void Resize(const D3DVIEWPORT9& viewport);
    void Add(LampTileList list, int slot, const LampScreenRect& rect);
    size_t TexelOffset(int list, int tileX, int tileY, int index) const;

    int m_tilesX = 0;
    int m_tilesY = 0;
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::vector<uint16_t> m_counts[kLampTileLists];
    std::vector<uint8_t> m_texels;
    mutable std::vector<uint8_t> m_row;
    LampTileStats m_stats = {};
};
