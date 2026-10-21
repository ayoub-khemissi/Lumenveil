#pragma once

#include "material_data.h"

#include <cstdint>

struct MaterialParams
{
    float darkening;
    float pores;
    float film;
    float puddle;
    float glint;
};

constexpr MaterialParams kNoMaterial = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
constexpr MaterialParams kGrassMaterial = {0.3f, 0.0f, 0.0f, 0.0f, 1.0f};
constexpr MaterialParams kSoilMaterial = {1.0f, 0.45f, 0.6f, 1.0f, 0.0f};
constexpr MaterialParams kStoneMaterial = {1.0f, 0.05f, 1.0f, 1.0f, 0.0f};
constexpr MaterialParams kSandMaterial = {1.0f, 0.38f, 0.3f, 0.1f, 0.0f};
constexpr MaterialParams kSnowMaterial = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
constexpr MaterialParams kHotMaterial = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
constexpr MaterialParams kIceMaterial = {0.0f, 0.0f, 1.0f, 0.5f, 0.0f};
constexpr MaterialParams kWoodMaterial = {1.0f, 0.6f, 1.0f, 0.2f, 0.0f};
constexpr MaterialParams kMetalMaterial = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
constexpr MaterialParams kFabricMaterial = {1.0f, 0.7f, 0.25f, 0.0f, 0.0f};

inline MaterialParams MaterialParamsOf(uint8_t kind)
{
    switch (kind)
    {
    case materialfile::kGrass:
        return kGrassMaterial;
    case materialfile::kSoil:
        return kSoilMaterial;
    case materialfile::kStone:
        return kStoneMaterial;
    case materialfile::kSand:
        return kSandMaterial;
    case materialfile::kSnow:
        return kSnowMaterial;
    case materialfile::kHot:
        return kHotMaterial;
    case materialfile::kIce:
        return kIceMaterial;
    case materialfile::kWood:
        return kWoodMaterial;
    default:
        return kNoMaterial;
    }
}

constexpr float kMaterialTagNearYards = 0.25f;
constexpr float kMaterialTagFarYards = 4096.0f;
constexpr float kMaterialTagShare = 0.01f;
constexpr float kMaterialTagYards = 0.05f;
constexpr float kGlintCellYards = 0.03f;
constexpr float kGlintCellShare = 0.02f;
constexpr float kWetTransmission = 0.5145f;
constexpr float kWaterInternalReflectance = 0.475f;
constexpr int kMaterialFirstLayerRegister = 209;
constexpr int kMaterialLayerGlintRegister = 213;
constexpr int kMaterialLayerKnownRegister = 214;
constexpr int kMaterialConstantCount = 6;
