#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class ByteView
{
public:
    ByteView() = default;
    ByteView(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    explicit ByteView(const std::vector<uint8_t>& bytes) : m_data(bytes.data()), m_size(bytes.size()) {}

    size_t Size() const { return m_size; }
    bool Empty() const { return m_size == 0; }

    bool Contains(size_t offset, size_t length) const
    {
        return offset <= m_size && length <= m_size - offset;
    }

    template <typename T>
    bool Read(size_t offset, T& out) const
    {
        if (!Contains(offset, sizeof(T)))
            return false;
        std::memcpy(&out, m_data + offset, sizeof(T));
        return true;
    }

    template <typename T>
    T Get(size_t offset, T fallback = T{}) const
    {
        T value{};
        return Read(offset, value) ? value : fallback;
    }

    ByteView Slice(size_t offset, size_t length) const
    {
        return Contains(offset, length) ? ByteView(m_data + offset, length) : ByteView();
    }

    ByteView From(size_t offset) const
    {
        return offset <= m_size ? ByteView(m_data + offset, m_size - offset) : ByteView();
    }

    std::string CString(size_t offset, size_t maxLength = 1024) const
    {
        std::string text;
        for (size_t i = offset; i < m_size && text.size() < maxLength; ++i)
        {
            char c = static_cast<char>(m_data[i]);
            if (c == '\0')
                break;
            text.push_back(c);
        }
        return text;
    }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
};

constexpr uint32_t ChunkId(const char (&name)[5])
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(name[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(name[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(name[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(name[3]));
}

struct Chunk
{
    uint32_t id;
    ByteView body;
};

inline std::vector<Chunk> ChunksOf(ByteView file)
{
    std::vector<Chunk> chunks;
    size_t offset = 0;
    while (file.Contains(offset, 8))
    {
        uint32_t id = file.Get<uint32_t>(offset);
        uint32_t size = file.Get<uint32_t>(offset + 4);
        if (!file.Contains(offset + 8, size))
            break;
        chunks.push_back({id, file.Slice(offset + 8, size)});
        offset += 8 + static_cast<size_t>(size);
    }
    return chunks;
}

inline ByteView FindChunk(const std::vector<Chunk>& chunks, uint32_t id)
{
    for (const Chunk& chunk : chunks)
        if (chunk.id == id)
            return chunk.body;
    return ByteView();
}
