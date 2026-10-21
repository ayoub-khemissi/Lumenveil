#include "material_catalog.h"

#include "log.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace
{
bool HeaderValid(const materialfile::Header& h)
{
    return std::memcmp(h.magic, materialfile::kMagic, 4) == 0 && h.version == materialfile::kVersion &&
           h.entryCount > 0 && h.entryCount <= (1u << 20);
}

bool EntriesValid(const std::vector<materialfile::Entry>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].kind == materialfile::kUnknown || entries[i].kind >= materialfile::kKindCount)
            return false;
        if (i > 0 && !materialfile::KeyLess(entries[i - 1], entries[i]))
            return false;
    }
    return true;
}
}

bool MaterialCatalog::Take(const materialfile::Header& h, std::vector<materialfile::Entry>&& entries)
{
    if (!HeaderValid(h) || entries.size() != h.entryCount || !EntriesValid(entries))
        return false;
    m_entries = std::move(entries);
    return true;
}

bool MaterialCatalog::Load(const std::string& path)
{
    Clear();
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        VF_LOG_INFO("no material data at %s; wet surfaces judge the ground by its colour", path.c_str());
        return false;
    }
    materialfile::Header h = {};
    f.read(reinterpret_cast<char*>(&h), sizeof h);
    if (!f || !HeaderValid(h))
    {
        VF_LOG_ERROR("material data %s has an unknown header; wet surfaces judge the ground by its colour",
                     path.c_str());
        return false;
    }
    std::vector<materialfile::Entry> entries(h.entryCount);
    f.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(entries.size() * sizeof entries[0]));
    if (!f || !Take(h, std::move(entries)))
    {
        VF_LOG_ERROR("material data %s is truncated or out of order; wet surfaces judge the ground by its colour",
                     path.c_str());
        Clear();
        return false;
    }
    VF_LOG_INFO("material data: %zu textures (grass %zu, soil %zu, stone %zu, sand %zu, snow %zu, ice %zu, wood %zu, "
                "hot %zu) from %s",
                m_entries.size(), CountOf(materialfile::kGrass), CountOf(materialfile::kSoil),
                CountOf(materialfile::kStone), CountOf(materialfile::kSand), CountOf(materialfile::kSnow),
                CountOf(materialfile::kIce), CountOf(materialfile::kWood), CountOf(materialfile::kHot), path.c_str());
    return true;
}

bool MaterialCatalog::Parse(const void* bytes, size_t size, const char* source)
{
    Clear();
    materialfile::Header h = {};
    if (!bytes || size < sizeof h)
        return false;
    std::memcpy(&h, bytes, sizeof h);
    if (!HeaderValid(h) || size != sizeof h + static_cast<size_t>(h.entryCount) * sizeof(materialfile::Entry))
        return false;
    std::vector<materialfile::Entry> entries(h.entryCount);
    std::memcpy(entries.data(), static_cast<const uint8_t*>(bytes) + sizeof h, entries.size() * sizeof entries[0]);
    if (!Take(h, std::move(entries)))
    {
        Clear();
        return false;
    }
    VF_LOG_INFO("material data: %zu textures from %s", m_entries.size(), source);
    return true;
}

void MaterialCatalog::Clear()
{
    m_entries.clear();
    ++m_generation;
}

size_t MaterialCatalog::CountOf(uint8_t kind) const
{
    return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(),
                                             [kind](const materialfile::Entry& e) { return e.kind == kind; }));
}

const materialfile::Entry* MaterialCatalog::Find(uint64_t hash, uint32_t format, uint32_t width, uint32_t height) const
{
    materialfile::Entry key = {};
    key.hash = hash;
    key.format = format;
    key.width = static_cast<uint16_t>(width);
    key.height = static_cast<uint16_t>(height);
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), key, materialfile::KeyLess);
    if (it == m_entries.end() || materialfile::KeyLess(key, *it))
        return nullptr;
    return &*it;
}

MaterialCatalog& GlobalMaterialCatalog()
{
    static MaterialCatalog catalog;
    return catalog;
}
