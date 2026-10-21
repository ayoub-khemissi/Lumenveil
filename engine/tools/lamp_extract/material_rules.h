#pragma once

#include <cstdint>
#include <string>

std::string MaterialBaseName(const std::string& gamePath);
std::string MaterialPlaceStrippedName(const std::string& gamePath);
uint8_t MaterialKindOfName(const std::string& gamePath, std::string* word = nullptr);
