#pragma once

#include "relief_data.h"

#include <cstddef>
#include <cstdint>

struct ClientCode
{
    uintptr_t address;
    size_t size;
    uint64_t hash;
};

inline bool CodeMatches(const void* bytes, const ClientCode& code)
{
    return relieffile::Fnv1a(bytes, code.size) == code.hash;
}

inline bool ClientCodeMatches(const ClientCode& code)
{
    return CodeMatches(reinterpret_cast<const void*>(code.address), code);
}
