#include "lamp_catalog.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
constexpr float kMaxLampReach = 64.0f;
constexpr float kMaxLampIntensity = 16.0f;

bool Finite(const float* values, int count)
{
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(values[i]))
            return false;
    return true;
}

bool FitsIn(uint64_t offset, uint64_t count, uint64_t recordSize, uint64_t fileSize)
{
    return offset <= fileSize && count <= (fileSize - offset) / recordSize;
}

template <typename T>
std::vector<T> Records(const unsigned char* bytes, uint32_t offset, uint32_t count)
{
    std::vector<T> out(count);
    if (count)
        std::memcpy(out.data(), bytes + offset, static_cast<size_t>(count) * sizeof(T));
    return out;
}

bool BeamLampName(const char* name)
{
    static const char* const kBeamModels[] = {"SPOTLIGHT", "SEARCHLIGHT", "LIGHTHOUSE"};
    std::string upper(name);
    for (char& c : upper)
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    for (const char* key : kBeamModels)
        if (upper.find(key) != std::string::npos)
            return true;
    return false;
}

bool ValidLamp(const lampfile::Lamp& lamp, uint32_t nameTableSize)
{
    return Finite(lamp.position, 3) && Finite(lamp.color, 3) && std::isfinite(lamp.intensity) &&
           std::isfinite(lamp.reach) && lamp.reach > 0.0f && lamp.reach <= kMaxLampReach && lamp.intensity >= 0.0f &&
           lamp.intensity <= kMaxLampIntensity && lamp.color[0] >= 0.0f && lamp.color[1] >= 0.0f &&
           lamp.color[2] >= 0.0f && lamp.kind <= static_cast<uint8_t>(lampfile::LampKind::Fixture) &&
           lamp.name < nameTableSize;
}

bool ValidWindow(const lampfile::Window& window, uint32_t nameTableSize)
{
    return Finite(window.center, 3) && Finite(window.normal, 3) && Finite(window.halfWidth, 3) &&
           std::isfinite(window.halfHeight) && Finite(window.color, 3) && std::isfinite(window.area) &&
           window.name < nameTableSize;
}

bool ValidCells(const LampMap& map)
{
    uint32_t previousKey = 0;
    for (size_t c = 0; c < map.cells.size(); ++c)
    {
        const lampfile::Cell& cell = map.cells[c];
        if ((c > 0 && cell.key <= previousKey) || cell.key / 1024u >= lampfile::kCellsPerSide ||
            cell.key % 1024u >= lampfile::kCellsPerSide || cell.lampCount > map.lamps.size() ||
            cell.firstLamp > map.lamps.size() - cell.lampCount || cell.windowCount > map.windows.size() ||
            cell.firstWindow > map.windows.size() - cell.windowCount)
            return false;
        for (uint32_t i = cell.firstLamp; i < cell.firstLamp + cell.lampCount; ++i)
        {
            const float* p = map.lamps[i].position;
            if (lampfile::CellKey(lampfile::CellCoordinate(p[0]), lampfile::CellCoordinate(p[1])) != cell.key)
                return false;
        }
        previousKey = cell.key;
    }
    return true;
}
}

bool LampCatalog::Load(const std::string& path)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        Clear();
        VF_LOG_INFO("no lamp data at %s; no lamp halos", path.c_str());
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char buffer[65536];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
        bytes.insert(bytes.end(), buffer, buffer + n);
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    if (readError)
    {
        Clear();
        VF_LOG_ERROR("lamp data %s could not be read; no lamp halos", path.c_str());
        return false;
    }
    return Parse(bytes.data(), bytes.size(), path.c_str());
}

bool LampCatalog::Parse(const void* data, size_t size, const char* source)
{
    Clear();
    const auto* bytes = static_cast<const unsigned char*>(data);
    lampfile::Header header = {};
    bool ok = bytes && size >= sizeof(header);
    if (ok)
    {
        std::memcpy(&header, bytes, sizeof(header));
        ok = std::memcmp(header.magic, lampfile::kMagic, sizeof(header.magic)) == 0 &&
             (header.version == lampfile::kVersion || header.version == lampfile::kVersionOne) &&
             FitsIn(sizeof(header), header.mapCount, sizeof(lampfile::MapEntry), size) &&
             FitsIn(header.nameTableOffset, header.nameTableSize, 1, size);
    }
    std::vector<LampMap> maps;
    for (uint32_t m = 0; ok && m < header.mapCount; ++m)
    {
        lampfile::MapEntry entry = {};
        std::memcpy(&entry, bytes + sizeof(header) + m * sizeof(entry), sizeof(entry));
        ok = FitsIn(entry.cellOffset, entry.cellCount, sizeof(lampfile::Cell), size) &&
             FitsIn(entry.lampOffset, entry.lampCount, sizeof(lampfile::Lamp), size) &&
             FitsIn(entry.windowOffset, entry.windowCount, sizeof(lampfile::Window), size);
        for (const LampMap& seen : maps)
            ok = ok && seen.mapId != static_cast<int>(entry.mapId);
        if (!ok)
            break;
        LampMap map;
        map.mapId = static_cast<int>(entry.mapId);
        map.cells = Records<lampfile::Cell>(bytes, entry.cellOffset, entry.cellCount);
        map.lamps = Records<lampfile::Lamp>(bytes, entry.lampOffset, entry.lampCount);
        map.windows = Records<lampfile::Window>(bytes, entry.windowOffset, entry.windowCount);
        for (const lampfile::Lamp& lamp : map.lamps)
            ok = ok && ValidLamp(lamp, header.nameTableSize);
        for (const lampfile::Window& window : map.windows)
            ok = ok && ValidWindow(window, header.nameTableSize);
        ok = ok && ValidCells(map);
        maps.push_back(std::move(map));
    }
    const uint32_t objectModelCount = header.version == lampfile::kVersionOne ? 0 : header.objectModelCount;
    std::vector<lampfile::ObjectModel> objectModels;
    std::vector<lampfile::Lamp> objectLamps;
    if (ok && objectModelCount > 0)
    {
        ok = FitsIn(header.objectModelOffset, objectModelCount, sizeof(lampfile::ObjectModel), size);
        if (ok)
            objectModels = Records<lampfile::ObjectModel>(bytes, header.objectModelOffset, objectModelCount);
        uint64_t lampTotal = 0;
        for (size_t i = 0; ok && i < objectModels.size(); ++i)
        {
            const lampfile::ObjectModel& model = objectModels[i];
            ok = model.lampCount > 0 && model.firstLamp == lampTotal &&
                 (i == 0 || model.displayId > objectModels[i - 1].displayId);
            lampTotal += model.lampCount;
        }
        ok = ok && FitsIn(header.objectLampOffset, lampTotal, sizeof(lampfile::Lamp), size);
        if (ok)
            objectLamps = Records<lampfile::Lamp>(bytes, header.objectLampOffset, static_cast<uint32_t>(lampTotal));
        for (const lampfile::Lamp& lamp : objectLamps)
            ok = ok && ValidLamp(lamp, header.nameTableSize);
    }
    ok = ok && (header.nameTableSize == 0 || bytes[header.nameTableOffset + header.nameTableSize - 1] == '\0');
    if (!ok)
    {
        VF_LOG_ERROR("lamp data %s is invalid; no lamp halos", source);
        return false;
    }
    m_maps = std::move(maps);
    m_objectModels = std::move(objectModels);
    m_objectLamps = std::move(objectLamps);
    m_names.assign(bytes + header.nameTableOffset, bytes + header.nameTableOffset + header.nameTableSize);
    size_t beams = 0;
    for (LampMap& map : m_maps)
    {
        map.beams.assign(map.lamps.size(), 0);
        for (size_t i = 0; i < map.lamps.size(); ++i)
            if (BeamLampName(Name(map.lamps[i].name)))
            {
                map.beams[i] = 1;
                ++beams;
            }
    }
    m_objectBeams.assign(m_objectLamps.size(), 0);
    for (size_t i = 0; i < m_objectLamps.size(); ++i)
        m_objectBeams[i] = BeamLampName(Name(m_objectLamps[i].name)) ? 1 : 0;
    VF_LOG_INFO("lamp data: %u maps, %zu lamps, %zu windows from %s", header.mapCount, LampTotal(), WindowTotal(),
                source);
    if (header.version == lampfile::kVersionOne)
        VF_LOG_INFO("lamp data: made by an older Lumenveil-Setup.cmd, without the game objects' lamps; run it again "
                    "so the campfires, braziers and candles the server places light the world too");
    else
        VF_LOG_INFO("lamp data: %zu game object models with %zu lamps (campfires, braziers and candles the server "
                    "places, lit where the client shows them)",
                    m_objectModels.size(), m_objectLamps.size());
    VF_LOG_INFO("lamp data: %zu spotlights, searchlights and lighthouses, whose beams give no spell light", beams);
    return true;
}

void LampCatalog::Clear()
{
    m_maps.clear();
    m_objectModels.clear();
    m_objectLamps.clear();
    m_objectBeams.clear();
    m_names.clear();
    ++m_version;
}

const LampMap* LampCatalog::Map(int mapId) const
{
    for (const LampMap& map : m_maps)
        if (map.mapId == mapId)
            return &map;
    return nullptr;
}

bool LampCatalog::ObjectModel(uint32_t displayId, ObjectModelLamps& out) const
{
    const auto found = std::lower_bound(
        m_objectModels.begin(), m_objectModels.end(), displayId,
        [](const lampfile::ObjectModel& model, uint32_t id) { return model.displayId < id; });
    if (found == m_objectModels.end() || found->displayId != displayId)
        return false;
    out = {m_objectLamps.data() + found->firstLamp, m_objectBeams.data() + found->firstLamp, found->lampCount};
    return true;
}

const char* LampCatalog::Name(uint32_t offset) const
{
    return offset < m_names.size() ? m_names.data() + offset : "";
}

size_t LampCatalog::LampTotal() const
{
    size_t total = 0;
    for (const LampMap& map : m_maps)
        total += map.lamps.size();
    return total;
}

size_t LampCatalog::WindowTotal() const
{
    size_t total = 0;
    for (const LampMap& map : m_maps)
        total += map.windows.size();
    return total;
}

LampCatalog& GlobalLampCatalog()
{
    static LampCatalog catalog;
    return catalog;
}
