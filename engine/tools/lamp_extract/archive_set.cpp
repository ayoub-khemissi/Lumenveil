#include "archive_set.h"

#include <algorithm>
#include <cctype>

extern "C"
{
#include <libmpq/mpq.h>
}

namespace fs = std::filesystem;

namespace
{

std::vector<fs::path> ArchiveOrder(const fs::path& data, const std::string& locale, bool customPatches)
{
    const std::string custom = customPatches ? "ZYXWVUTSRQPONMLKJIHGFEDCBA987654" : "";
    const fs::path localeDir = data / locale;
    std::vector<fs::path> order;
    for (char c : custom)
        order.push_back(data / ("patch-" + std::string(1, c) + ".MPQ"));
    for (char c : custom)
        order.push_back(localeDir / ("patch-" + locale + "-" + std::string(1, c) + ".MPQ"));
    order.push_back(localeDir / ("patch-" + locale + "-3.MPQ"));
    order.push_back(localeDir / ("patch-" + locale + "-2.MPQ"));
    order.push_back(localeDir / ("patch-" + locale + ".MPQ"));
    order.push_back(data / "patch-3.MPQ");
    order.push_back(data / "patch-2.MPQ");
    order.push_back(data / "patch.MPQ");
    order.push_back(localeDir / ("lichking-locale-" + locale + ".MPQ"));
    order.push_back(localeDir / ("expansion-locale-" + locale + ".MPQ"));
    order.push_back(localeDir / ("locale-" + locale + ".MPQ"));
    order.push_back(data / "lichking.MPQ");
    order.push_back(data / "expansion.MPQ");
    order.push_back(data / "common-2.MPQ");
    order.push_back(data / "common.MPQ");
    return order;
}

}

std::string NormalizedGamePath(std::string name)
{
    for (char& c : name)
    {
        if (c == '/')
            c = '\\';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name;
}

ArchiveSet::~ArchiveSet()
{
    for (mpq_archive* archive : m_archives)
        libmpq__archive_close(archive);
}

bool ArchiveSet::Open(const fs::path& dataDir, const std::string& locale, bool customPatches)
{
    for (const fs::path& path : ArchiveOrder(dataDir, locale, customPatches))
    {
        std::error_code error;
        if (!fs::is_regular_file(path, error))
            continue;
        mpq_archive* archive = nullptr;
        if (libmpq__archive_open(&archive, path.string().c_str(), -1) != 0)
            continue;
        m_archives.push_back(archive);
        m_names.push_back(path.string());
    }
    return !m_archives.empty();
}

std::optional<std::vector<uint8_t>> ArchiveSet::Read(const std::string& name) const
{
    const std::string key = NormalizedGamePath(name);
    for (mpq_archive* archive : m_archives)
    {
        uint32_t number = 0;
        if (libmpq__file_number(archive, key.c_str(), &number) != 0)
            continue;
        libmpq__off_t size = 0;
        if (libmpq__file_unpacked_size(archive, number, &size) != 0 || size <= 1)
            return std::nullopt;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        libmpq__off_t transferred = 0;
        if (libmpq__file_read(archive, number, bytes.data(), size, &transferred) != 0 || transferred != size)
            return std::nullopt;
        ++m_filesRead;
        m_bytesRead += static_cast<uint64_t>(size);
        return bytes;
    }
    return std::nullopt;
}

std::vector<std::string> ArchiveSet::ListedNames() const
{
    std::vector<std::string> names;
    for (mpq_archive* archive : m_archives)
    {
        uint32_t number = 0;
        if (libmpq__file_number(archive, "(listfile)", &number) != 0)
            continue;
        libmpq__off_t size = 0;
        if (libmpq__file_unpacked_size(archive, number, &size) != 0 || size <= 0)
            continue;
        std::string text(static_cast<size_t>(size), '\0');
        libmpq__off_t transferred = 0;
        if (libmpq__file_read(archive, number, reinterpret_cast<uint8_t*>(text.data()), size, &transferred) != 0 ||
            transferred != size)
            continue;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find_first_of("\r\n;", start);
            if (end == std::string::npos)
                end = text.size();
            if (end > start)
                names.push_back(NormalizedGamePath(text.substr(start, end - start)));
            start = end + 1;
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::vector<std::string> ArchiveSet::Holders(const std::string& name) const
{
    const std::string key = NormalizedGamePath(name);
    std::vector<std::string> holders;
    for (size_t i = 0; i < m_archives.size(); ++i)
    {
        uint32_t number = 0;
        if (libmpq__file_number(m_archives[i], key.c_str(), &number) == 0)
            holders.push_back(fs::path(m_names[i]).filename().string());
    }
    return holders;
}
