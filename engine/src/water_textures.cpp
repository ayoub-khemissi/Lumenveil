#include "water_textures.h"

#include "log.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
constexpr uint8_t kBlpEncodingDxt = 2;
constexpr uint8_t kBlpEncodingArgb = 3;
constexpr uint8_t kBlpAlphaDxt3 = 1;
constexpr uint8_t kBlpAlphaDxt5 = 7;
constexpr size_t kBlpHeaderBytes = 148;
constexpr size_t kBlpMipOffsets = 20;
constexpr size_t kBlpMipSizes = 84;
constexpr int kBlpMipSlots = 16;
constexpr UINT kBlpLargestSide = 4096;

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t at)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

size_t LevelBytes(D3DFORMAT format, UINT width, UINT height)
{
    if (format == D3DFMT_A8R8G8B8)
        return static_cast<size_t>(width) * height * 4;
    const size_t blocks = static_cast<size_t>(std::max(1u, (width + 3) / 4)) * std::max(1u, (height + 3) / 4);
    return blocks * (format == D3DFMT_DXT1 ? 8 : 16);
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

const char* FormatName(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8:
        return "A8R8G8B8";
    case D3DFMT_DXT1:
        return "DXT1";
    case D3DFMT_DXT3:
        return "DXT3";
    case D3DFMT_DXT5:
        return "DXT5";
    default:
        return "?";
    }
}

bool LoadImage(const std::string& path, const char* role, BlpImage& image)
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes))
    {
        VF_LOG_INFO("water: no %s (Lumenveil-Setup.cmd copies it from the game's archives when they hold it); no %s",
                    path.c_str(), role);
        return false;
    }
    if (!ParseBlp(bytes, image))
    {
        VF_LOG_ERROR("water: %s is not a BLP2 texture this mod reads; no %s", path.c_str(), role);
        return false;
    }
    VF_LOG_INFO("water: %s %ux%u %s, %zu levels, from %s", role, image.width, image.height, FormatName(image.format),
                image.levels.size(), path.c_str());
    return true;
}
}

bool ParseBlp(const std::vector<uint8_t>& bytes, BlpImage& out)
{
    out = BlpImage{};
    if (bytes.size() < kBlpHeaderBytes || std::memcmp(bytes.data(), "BLP2", 4) != 0)
        return false;
    const uint8_t encoding = bytes[8];
    const uint8_t alphaType = bytes[10];
    const bool mips = bytes[11] != 0;
    UINT width = ReadU32(bytes, 12);
    UINT height = ReadU32(bytes, 16);
    if (width == 0 || height == 0 || width > kBlpLargestSide || height > kBlpLargestSide)
        return false;
    if (encoding == kBlpEncodingArgb)
        out.format = D3DFMT_A8R8G8B8;
    else if (encoding == kBlpEncodingDxt)
        out.format =
            alphaType == kBlpAlphaDxt5 ? D3DFMT_DXT5 : (alphaType == kBlpAlphaDxt3 ? D3DFMT_DXT3 : D3DFMT_DXT1);
    else
        return false;
    out.width = width;
    out.height = height;
    for (int level = 0; level < kBlpMipSlots; ++level)
    {
        const size_t offset = ReadU32(bytes, kBlpMipOffsets + 4 * level);
        const size_t size = ReadU32(bytes, kBlpMipSizes + 4 * level);
        const size_t wanted = LevelBytes(out.format, width, height);
        if (offset < kBlpHeaderBytes || size < wanted || offset + wanted > bytes.size())
            break;
        out.levels.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                bytes.begin() + static_cast<std::ptrdiff_t>(offset + wanted));
        if (!mips || (width == 1 && height == 1))
            break;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return !out.levels.empty();
}

void WaterTextureFiles::Load(const std::string& directory)
{
    m_bump = BlpImage{};
    m_foam = BlpImage{};
    m_bumpBias[0] = m_bumpBias[1] = 0.0f;
    if (LoadImage(directory + "waterbump.blp", "bump", m_bump) && m_bump.format != D3DFMT_A8R8G8B8)
    {
        VF_LOG_ERROR("water: the bump must be an uncompressed normal map; no bump");
        m_bump = BlpImage{};
    }
    LoadImage(directory + "waterfoam.blp", "foam", m_foam);
    if (!m_bump.levels.empty())
    {
        const std::vector<uint8_t>& texels = m_bump.levels[0];
        const size_t count = texels.size() / 4;
        double sum[2] = {};
        for (size_t i = 0; i < count; ++i)
        {
            sum[0] += texels[4 * i + 2] / 127.5 - 1.0;
            sum[1] += texels[4 * i + 1] / 127.5 - 1.0;
        }
        m_bumpBias[0] = static_cast<float>(sum[0] / static_cast<double>(count));
        m_bumpBias[1] = static_cast<float>(sum[1] / static_cast<double>(count));
    }
}

WaterTextureFiles& GlobalWaterTextureFiles()
{
    static WaterTextureFiles files;
    return files;
}
