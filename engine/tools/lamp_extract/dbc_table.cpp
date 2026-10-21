#include "dbc_table.h"

#include "byte_view.h"

namespace
{

constexpr size_t kHeaderSize = 20;

}

bool DbcTable::Load(std::vector<uint8_t> bytes)
{
    m_bytes = std::move(bytes);
    ByteView view(m_bytes);
    if (view.Size() < kHeaderSize || std::memcmp(m_bytes.data(), "WDBC", 4) != 0)
        return false;
    m_records = view.Get<uint32_t>(4);
    m_fields = view.Get<uint32_t>(8);
    m_recordSize = view.Get<uint32_t>(12);
    m_stringSize = view.Get<uint32_t>(16);
    size_t expected = kHeaderSize + static_cast<size_t>(m_records) * m_recordSize + m_stringSize;
    return expected <= view.Size() && m_fields * 4 <= m_recordSize;
}

uint32_t DbcTable::U32(uint32_t row, uint32_t field) const
{
    ByteView view(m_bytes);
    return view.Get<uint32_t>(kHeaderSize + static_cast<size_t>(row) * m_recordSize + field * 4u);
}

std::string DbcTable::String(uint32_t row, uint32_t field) const
{
    uint32_t offset = U32(row, field);
    if (offset >= m_stringSize)
        return std::string();
    ByteView view(m_bytes);
    return view.CString(kHeaderSize + static_cast<size_t>(m_records) * m_recordSize + offset);
}
