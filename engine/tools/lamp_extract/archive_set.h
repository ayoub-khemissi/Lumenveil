#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct mpq_archive;

class ArchiveSet
{
public:
    ArchiveSet() = default;
    ArchiveSet(const ArchiveSet&) = delete;
    ArchiveSet& operator=(const ArchiveSet&) = delete;
    ~ArchiveSet();

    bool Open(const std::filesystem::path& dataDir, const std::string& locale, bool customPatches = true);
    std::optional<std::vector<uint8_t>> Read(const std::string& name) const;
    std::vector<std::string> Holders(const std::string& name) const;
    std::vector<std::string> ListedNames() const;
    const std::vector<std::string>& OpenedNames() const { return m_names; }
    uint64_t FilesRead() const { return m_filesRead; }
    uint64_t BytesRead() const { return m_bytesRead; }

private:
    std::vector<mpq_archive*> m_archives;
    std::vector<std::string> m_names;
    mutable uint64_t m_filesRead = 0;
    mutable uint64_t m_bytesRead = 0;
};

std::string NormalizedGamePath(std::string name);
