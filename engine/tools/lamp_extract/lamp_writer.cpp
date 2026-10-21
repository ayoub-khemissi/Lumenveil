#include "lamp_writer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

namespace
{

uint32_t KeyOf(const float* position)
{
    return lampfile::CellKey(lampfile::CellCoordinate(position[0]), lampfile::CellCoordinate(position[1]));
}

class NameTable
{
public:
    uint32_t Add(const std::string& name)
    {
        auto found = m_offsets.find(name);
        if (found != m_offsets.end())
            return found->second;
        uint32_t offset = static_cast<uint32_t>(m_bytes.size());
        m_bytes.insert(m_bytes.end(), name.begin(), name.end());
        m_bytes.push_back('\0');
        m_offsets.emplace(name, offset);
        return offset;
    }

    const std::vector<char>& Bytes() const { return m_bytes; }

private:
    std::unordered_map<std::string, uint32_t> m_offsets;
    std::vector<char> m_bytes;
};

template <typename T>
void Append(std::vector<char>& out, const T& value)
{
    const char* bytes = reinterpret_cast<const char*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

}

bool WriteLampFile(const std::filesystem::path& path, std::vector<MapLights>& maps)
{
    NameTable names;
    std::vector<char> body;
    std::vector<lampfile::MapEntry> entries;
    size_t directorySize = sizeof(lampfile::Header) + maps.size() * sizeof(lampfile::MapEntry);

    for (MapLights& map : maps)
    {
        std::stable_sort(map.lamps.begin(), map.lamps.end(), [](const WorldLamp& a, const WorldLamp& b) {
            return KeyOf(a.record.position) < KeyOf(b.record.position);
        });
        std::stable_sort(map.windows.begin(), map.windows.end(), [](const WorldWindow& a, const WorldWindow& b) {
            return KeyOf(a.record.center) < KeyOf(b.record.center);
        });
        std::map<uint32_t, lampfile::Cell> cells;
        for (uint32_t i = 0; i < map.lamps.size(); ++i)
        {
            uint32_t key = KeyOf(map.lamps[i].record.position);
            lampfile::Cell& cell = cells.try_emplace(key, lampfile::Cell{key, i, 0, 0, 0}).first->second;
            if (cell.lampCount == 0)
                cell.firstLamp = i;
            ++cell.lampCount;
        }
        for (uint32_t i = 0; i < map.windows.size(); ++i)
        {
            uint32_t key = KeyOf(map.windows[i].record.center);
            lampfile::Cell& cell = cells.try_emplace(key, lampfile::Cell{key, 0, 0, i, 0}).first->second;
            if (cell.windowCount == 0)
                cell.firstWindow = i;
            ++cell.windowCount;
        }

        lampfile::MapEntry entry{};
        entry.mapId = map.mapId;
        entry.cellCount = static_cast<uint32_t>(cells.size());
        entry.cellOffset = static_cast<uint32_t>(directorySize + body.size());
        for (const auto& [key, cell] : cells)
            Append(body, cell);
        entry.lampCount = static_cast<uint32_t>(map.lamps.size());
        entry.lampOffset = static_cast<uint32_t>(directorySize + body.size());
        for (WorldLamp& lamp : map.lamps)
        {
            lamp.record.name = names.Add(lamp.source);
            Append(body, lamp.record);
        }
        entry.windowCount = static_cast<uint32_t>(map.windows.size());
        entry.windowOffset = static_cast<uint32_t>(directorySize + body.size());
        for (WorldWindow& window : map.windows)
        {
            window.record.name = names.Add(window.source);
            Append(body, window.record);
        }
        entries.push_back(entry);
    }

    lampfile::Header header{};
    std::memcpy(header.magic, lampfile::kMagic, sizeof(header.magic));
    header.version = lampfile::kVersion;
    header.mapCount = static_cast<uint32_t>(entries.size());
    header.nameTableOffset = static_cast<uint32_t>(directorySize + body.size());
    header.nameTableSize = static_cast<uint32_t>(names.Bytes().size());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const lampfile::MapEntry& entry : entries)
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    file.write(names.Bytes().data(), static_cast<std::streamsize>(names.Bytes().size()));
    return static_cast<bool>(file);
}
