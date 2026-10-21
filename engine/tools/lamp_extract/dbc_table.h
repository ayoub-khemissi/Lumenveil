#pragma once

#include <cstdint>
#include <string>
#include <vector>

class DbcTable
{
public:
    bool Load(std::vector<uint8_t> bytes);
    uint32_t Rows() const { return m_records; }
    uint32_t Fields() const { return m_fields; }
    uint32_t U32(uint32_t row, uint32_t field) const;
    std::string String(uint32_t row, uint32_t field) const;

private:
    std::vector<uint8_t> m_bytes;
    uint32_t m_records = 0;
    uint32_t m_fields = 0;
    uint32_t m_recordSize = 0;
    uint32_t m_stringSize = 0;
};
