#pragma once

#include "relief_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ReliefCatalog
{
public:
    bool Load(const std::string& path);
    bool Parse(const void* bytes, size_t size, const char* source);
    void Clear();
    bool Loaded() const { return !m_entries.empty(); }
    size_t EntryCount() const { return m_entries.size(); }
    uint32_t HeightCount() const { return m_header.heightCount; }
    uint32_t HeightSize() const { return m_header.heightSize; }
    uint32_t LevelCount() const { return m_header.levelCount; }
    uint32_t Version() const { return m_header.version; }
    uint32_t Generation() const { return m_generation; }
    const relieffile::Entry* Find(uint64_t hash, uint32_t format, uint32_t width, uint32_t height) const;
    bool ReadHeights(uint32_t heightIndex, std::vector<uint8_t>& out) const;

private:
    relieffile::Header m_header = {};
    std::vector<relieffile::Entry> m_entries;
    std::string m_path;
    std::vector<uint8_t> m_inMemory;
    uint64_t m_heightsOffset = 0;
    uint32_t m_generation = 0;
};

ReliefCatalog& GlobalReliefCatalog();
