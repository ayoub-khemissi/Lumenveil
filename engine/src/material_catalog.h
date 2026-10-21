#pragma once

#include "material_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MaterialCatalog
{
public:
    bool Load(const std::string& path);
    bool Parse(const void* bytes, size_t size, const char* source);
    void Clear();
    bool Loaded() const { return !m_entries.empty(); }
    size_t EntryCount() const { return m_entries.size(); }
    size_t CountOf(uint8_t kind) const;
    uint32_t Generation() const { return m_generation; }
    const materialfile::Entry* Find(uint64_t hash, uint32_t format, uint32_t width, uint32_t height) const;

private:
    bool Take(const materialfile::Header& h, std::vector<materialfile::Entry>&& entries);

    std::vector<materialfile::Entry> m_entries;
    uint32_t m_generation = 0;
};

MaterialCatalog& GlobalMaterialCatalog();
