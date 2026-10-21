#include "png_reader.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

uint32_t BigEndian32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t ChannelsOf(uint8_t colourType)
{
    switch (colourType)
    {
    case 0:
        return 1;
    case 2:
        return 3;
    case 4:
        return 2;
    case 6:
        return 4;
    default:
        return 0;
    }
}

uint8_t Paeth(uint8_t a, uint8_t b, uint8_t c)
{
    const int p = int(a) + int(b) - int(c);
    const int pa = std::abs(p - int(a));
    const int pb = std::abs(p - int(b));
    const int pc = std::abs(p - int(c));
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

bool Unfilter(std::vector<uint8_t>& raw, uint32_t rows, size_t stride, size_t pixelBytes)
{
    std::vector<uint8_t> previous(stride, 0);
    for (uint32_t y = 0; y < rows; ++y)
    {
        uint8_t* row = raw.data() + y * (stride + 1);
        const uint8_t filter = row[0];
        uint8_t* line = row + 1;
        for (size_t i = 0; i < stride; ++i)
        {
            const uint8_t left = i >= pixelBytes ? line[i - pixelBytes] : 0;
            const uint8_t up = previous[i];
            const uint8_t upLeft = i >= pixelBytes ? previous[i - pixelBytes] : 0;
            switch (filter)
            {
            case 0:
                break;
            case 1:
                line[i] = uint8_t(line[i] + left);
                break;
            case 2:
                line[i] = uint8_t(line[i] + up);
                break;
            case 3:
                line[i] = uint8_t(line[i] + ((int(left) + int(up)) >> 1));
                break;
            case 4:
                line[i] = uint8_t(line[i] + Paeth(left, up, upLeft));
                break;
            default:
                return false;
            }
        }
        std::memcpy(previous.data(), line, stride);
    }
    return true;
}

void Explain(std::string* why, const char* text)
{
    if (why)
        *why = text;
}
}

bool ReadPng(const std::string& path, PngImage& out, std::string* why)
{
    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0)
    {
        Explain(why, "not a PNG file");
        return false;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t depth = 0;
    uint8_t colourType = 0;
    std::vector<uint8_t> compressed;
    size_t at = 8;
    while (at + 12 <= bytes.size())
    {
        const uint32_t length = BigEndian32(&bytes[at]);
        const char* type = reinterpret_cast<const char*>(&bytes[at + 4]);
        const uint8_t* data = &bytes[at + 8];
        if (at + 12 + length > bytes.size())
            break;
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13)
        {
            width = BigEndian32(data);
            height = BigEndian32(data + 4);
            depth = data[8];
            colourType = data[9];
            if (data[12] != 0)
            {
                Explain(why, "interlaced PNGs are not read");
                return false;
            }
        }
        else if (std::memcmp(type, "IDAT", 4) == 0)
            compressed.insert(compressed.end(), data, data + length);
        else if (std::memcmp(type, "IEND", 4) == 0)
            break;
        at += 12 + length;
    }
    const uint32_t channels = ChannelsOf(colourType);
    if (!width || !height || !channels || (depth != 8 && depth != 16))
    {
        Explain(why, "only 8- and 16-bit grey, grey-alpha, RGB and RGBA PNGs are read");
        return false;
    }
    const size_t pixelBytes = channels * (depth / 8);
    const size_t stride = pixelBytes * width;
    std::vector<uint8_t> raw((stride + 1) * height);
    uLongf rawSize = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &rawSize, compressed.data(), static_cast<uLong>(compressed.size())) != Z_OK ||
        rawSize != raw.size() || !Unfilter(raw, height, stride, pixelBytes))
    {
        Explain(why, "the image data does not inflate");
        return false;
    }
    out.width = width;
    out.height = height;
    out.channels = channels;
    out.values.resize(static_cast<size_t>(width) * height * channels);
    const float scale = depth == 16 ? 1.0f / 65535.0f : 1.0f / 255.0f;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* line = raw.data() + y * (stride + 1) + 1;
        for (size_t i = 0; i < static_cast<size_t>(width) * channels; ++i)
        {
            const uint32_t v = depth == 16 ? (uint32_t(line[2 * i]) << 8) | line[2 * i + 1] : line[i];
            out.values[static_cast<size_t>(y) * width * channels + i] = v * scale;
        }
    }
    return true;
}
