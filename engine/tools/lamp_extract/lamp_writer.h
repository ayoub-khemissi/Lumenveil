#pragma once

#include <filesystem>
#include <vector>

#include "world_scan.h"

bool WriteLampFile(const std::filesystem::path& path, std::vector<MapLights>& maps);
