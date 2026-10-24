#pragma once

#include "lamp_data.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LampMap
{
    int mapId;
    std::vector<lampfile::Cell> cells;
    std::vector<lampfile::Lamp> lamps;
    std::vector<lampfile::Window> windows;
    std::vector<uint8_t> beams;
};

struct ObjectModelLamps
{
    const lampfile::Lamp* lamps;
    const uint8_t* beams;
    uint32_t count;
};

class LampCatalog
{
public:
    bool Load(const std::string& path);
    bool Parse(const void* bytes, size_t size, const char* source);
    void Clear();
    bool Loaded() const { return !m_maps.empty(); }
    unsigned Version() const { return m_version; }
    const LampMap* Map(int mapId) const;
    const char* Name(uint32_t offset) const;
    size_t LampTotal() const;
    size_t WindowTotal() const;
    bool ObjectModel(uint32_t displayId, ObjectModelLamps& out) const;
    size_t ObjectModelTotal() const { return m_objectModels.size(); }
    size_t ObjectLampTotal() const { return m_objectLamps.size(); }

private:
    std::vector<LampMap> m_maps;
    std::vector<lampfile::ObjectModel> m_objectModels;
    std::vector<lampfile::Lamp> m_objectLamps;
    std::vector<uint8_t> m_objectBeams;
    std::vector<char> m_names;
    unsigned m_version = 0;
};

template <typename Visit>
void ForEachCellNear(const LampMap& map, float x, float y, float radius, Visit&& visit)
{
    const uint32_t x0 = lampfile::CellCoordinate(x - radius);
    const uint32_t x1 = lampfile::CellCoordinate(x + radius);
    const uint32_t y0 = lampfile::CellCoordinate(y - radius);
    const uint32_t y1 = lampfile::CellCoordinate(y + radius);
    for (uint32_t cellX = x0; cellX <= x1; ++cellX)
    {
        const uint32_t last = lampfile::CellKey(cellX, y1);
        auto cell = std::lower_bound(map.cells.begin(), map.cells.end(), lampfile::CellKey(cellX, y0),
                                     [](const lampfile::Cell& c, uint32_t key) { return c.key < key; });
        for (; cell != map.cells.end() && cell->key <= last; ++cell)
            visit(*cell);
    }
}

template <typename Visit>
void ForEachLampNear(const LampMap& map, float x, float y, float radius, Visit&& visit)
{
    ForEachCellNear(map, x, y, radius, [&](const lampfile::Cell& cell) {
        for (uint32_t i = cell.firstLamp; i < cell.firstLamp + cell.lampCount; ++i)
            visit(i, map.lamps[i]);
    });
}

template <typename Visit>
void ForEachWindowNear(const LampMap& map, float x, float y, float radius, Visit&& visit)
{
    ForEachCellNear(map, x, y, radius, [&](const lampfile::Cell& cell) {
        for (uint32_t i = cell.firstWindow; i < cell.firstWindow + cell.windowCount; ++i)
            visit(i, map.windows[i]);
    });
}

LampCatalog& GlobalLampCatalog();
