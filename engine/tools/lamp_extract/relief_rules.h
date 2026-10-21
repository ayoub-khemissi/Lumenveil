#pragma once

#include <cstdint>
#include <string>

std::string ReliefNameWithoutLookAlikes(const std::string& gamePath);
uint8_t ReliefClassOfName(const std::string& gamePath, std::string* why = nullptr);
uint32_t ReliefMedianFor(bool groundSource, uint8_t reliefClass, uint32_t groundMedian, uint32_t buildingMedian);
