#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "geometry.h"
#include "world_scan.h"

class ArchiveSet;
class ModelLightCatalogue;
class WmoCatalogue;

struct CensusPlace
{
    std::string name;
    uint32_t map = 0;
    Vec3 position;
};

struct CensusInputs
{
    const ArchiveSet& archives;
    const ModelLightCatalogue& models;
    const WmoCatalogue& wmos;
    const WorldScanner& scanner;
    const std::vector<MapLights>& maps;
    const std::vector<CensusPlace>& places;
    double seconds;
};

void WriteCensus(std::ostream& out, const CensusInputs& inputs);
