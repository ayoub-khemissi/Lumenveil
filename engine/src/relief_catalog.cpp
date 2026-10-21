#include "relief_catalog.h"

#include "log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{
bool HeaderValid(const relieffile::Header& h)
{
    return std::memcmp(h.magic, relieffile::kMagic, 4) == 0 && relieffile::EntryBytes(h.version) != 0 &&
           h.heightSize > 0 && h.heightSize <= 2048 && h.levelCount > 0 && h.levelCount <= 12 &&
           h.heightBytes == relieffile::LevelBytes(h.heightSize, h.levelCount);
}

uint64_t HeightsOffset(const relieffile::Header& h)
{
    return sizeof h + static_cast<uint64_t>(h.entryCount) * relieffile::EntryBytes(h.version);
}

uint64_t FileBytesNeeded(const relieffile::Header& h)
{
    return HeightsOffset(h) + static_cast<uint64_t>(h.heightCount) * h.heightBytes;
}

bool HeightIndexValid(uint32_t heightIndex, uint32_t heightCount)
{
    return heightIndex < heightCount || heightIndex == relieffile::kNoHeights;
}

bool ReadEntries(const relieffile::Header& h, const uint8_t* bytes, std::vector<relieffile::Entry>& out)
{
    out.resize(h.entryCount);
    if (h.version == relieffile::kVersion)
    {
        std::memcpy(out.data(), bytes, out.size() * sizeof(relieffile::Entry));
        for (const relieffile::Entry& e : out)
            if (!HeightIndexValid(e.heightIndex, h.heightCount) || !(e.flatness >= 0.0f && e.flatness <= 1.0f) ||
                !reliefclass::Valid(e.reliefClass))
                return false;
        return true;
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        relieffile::EntryVersionOne old = {};
        std::memcpy(&old, bytes + i * sizeof old, sizeof old);
        if (old.heightIndex >= h.heightCount || !(old.depth >= 0.0f && old.depth <= relieffile::kMaxVersionOneDepth))
            return false;
        out[i] = relieffile::FromVersionOne(old);
    }
    return true;
}

bool EntriesInOrder(const std::vector<relieffile::Entry>& entries)
{
    for (size_t i = 1; i < entries.size(); ++i)
        if (!relieffile::KeyLess(entries[i - 1], entries[i]))
            return false;
    return true;
}

size_t CountWithHeights(const std::vector<relieffile::Entry>& entries)
{
    size_t n = 0;
    for (const relieffile::Entry& e : entries)
        n += relieffile::HasHeights(e) ? 1 : 0;
    return n;
}
}

bool ReliefCatalog::Load(const std::string& path)
{
    Clear();
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        VF_LOG_INFO("no relief data at %s; no ground relief", path.c_str());
        return false;
    }
    relieffile::Header h = {};
    f.read(reinterpret_cast<char*>(&h), sizeof h);
    if (!f || !HeaderValid(h))
    {
        VF_LOG_ERROR("relief data %s has an unknown header; no ground relief", path.c_str());
        return false;
    }
    f.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(f.tellg());
    std::vector<uint8_t> raw;
    bool entriesRead = f && size >= FileBytesNeeded(h);
    if (entriesRead)
    {
        raw.resize(static_cast<size_t>(h.entryCount) * relieffile::EntryBytes(h.version));
        f.seekg(static_cast<std::streamoff>(sizeof h));
        f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        entriesRead = static_cast<bool>(f);
    }
    std::vector<relieffile::Entry> entries;
    if (!entriesRead || !ReadEntries(h, raw.data(), entries) || !EntriesInOrder(entries))
    {
        VF_LOG_ERROR("relief data %s is truncated or out of order; no ground relief", path.c_str());
        return false;
    }
    m_header = h;
    m_entries = std::move(entries);
    m_path = path;
    m_heightsOffset = HeightsOffset(h);
    VF_LOG_INFO("relief data: %zu textures (%zu with heights, %zu by type alone), %u height maps of %ux%u (%u "
                "levels), version %u, from %s",
                m_entries.size(), CountWithHeights(m_entries), m_entries.size() - CountWithHeights(m_entries),
                h.heightCount, h.heightSize, h.heightSize, h.levelCount, h.version, path.c_str());
    return true;
}

bool ReliefCatalog::Parse(const void* bytes, size_t size, const char* source)
{
    Clear();
    relieffile::Header h = {};
    if (!bytes || size < sizeof h)
        return false;
    std::memcpy(&h, bytes, sizeof h);
    if (!HeaderValid(h) || size < FileBytesNeeded(h))
        return false;
    std::vector<relieffile::Entry> entries;
    if (!ReadEntries(h, static_cast<const uint8_t*>(bytes) + sizeof h, entries) || !EntriesInOrder(entries))
        return false;
    m_header = h;
    m_entries = std::move(entries);
    m_inMemory.assign(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + size);
    m_heightsOffset = HeightsOffset(h);
    VF_LOG_INFO("relief data: %zu textures (%zu with heights), %u height maps, version %u, from %s", m_entries.size(),
                CountWithHeights(m_entries), h.heightCount, h.version, source);
    return true;
}

void ReliefCatalog::Clear()
{
    m_header = {};
    m_entries.clear();
    m_path.clear();
    m_inMemory.clear();
    m_heightsOffset = 0;
    ++m_generation;
}

const relieffile::Entry* ReliefCatalog::Find(uint64_t hash, uint32_t format, uint32_t width, uint32_t height) const
{
    relieffile::Entry key = {};
    key.hash = hash;
    key.format = format;
    key.width = static_cast<uint16_t>(width);
    key.height = static_cast<uint16_t>(height);
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), key, relieffile::KeyLess);
    if (it == m_entries.end() || relieffile::KeyLess(key, *it))
        return nullptr;
    return &*it;
}

bool ReliefCatalog::ReadHeights(uint32_t heightIndex, std::vector<uint8_t>& out) const
{
    if (heightIndex >= m_header.heightCount)
        return false;
    const uint64_t offset = m_heightsOffset + static_cast<uint64_t>(heightIndex) * m_header.heightBytes;
    out.resize(m_header.heightBytes);
    if (!m_inMemory.empty())
    {
        std::memcpy(out.data(), m_inMemory.data() + static_cast<size_t>(offset), out.size());
        return true;
    }
    std::ifstream f(m_path, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(offset));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

ReliefCatalog& GlobalReliefCatalog()
{
    static ReliefCatalog catalog;
    return catalog;
}
