#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr int kReliefMaxLayers = 4;
constexpr int kReliefFirstHeightSampler = 9;
constexpr int kReliefConstantBase = 200;
constexpr int kReliefConstantCount = 9;
constexpr int kReliefModelConstantCount = 8;
constexpr int kReliefLiteralBase = 180;
constexpr int kReliefLiteralLimit = 200;
constexpr int kReliefLightTowardVsRegister = 24;
constexpr int kReliefModelViewVsRegister = 0;
constexpr int kReliefChunkOriginVsRegister = 23;

struct TerrainPixelLayout
{
    int layers = 0;
    int alphaSampler = -1;
    int colourInput = -1;
    int highestInput = -1;
    int highestTemp = -1;
    bool viewPositionDeclared = false;
};

struct ModelPixelLayout
{
    int colourInput = -1;
    int viewInput = -1;
    int normalInput = -1;
    int viewIndex = -1;
    int diffuseInput = -1;
    int diffuseIndex = -1;
    int highestTemp = -1;
};

enum class ModelLightForm
{
    None,
    Ratio,
    Additive,
};

struct ModelVertexLayout
{
    bool viewAndNormal = false;
    int viewIndex = -1;
    unsigned uvFromTexcoord = 0;
    bool vertexColour = false;
    bool normalInput = false;
    bool skinned = false;
    ModelLightForm light = ModelLightForm::None;
    int lightRegister = -1;
    int directRegister = -1;
    int ambientRegister = -1;
    int multiplierRegister = -1;
    int emissiveRegister = -1;
};

struct ShaderCode
{
    const uint32_t* words = nullptr;
    size_t count = 0;
};

size_t ShaderWordCount(const uint32_t* code, size_t limit);
bool ReadTerrainPixelLayout(ShaderCode code, TerrainPixelLayout& layout, std::string* why);
bool VertexShaderWritesViewPosition(ShaderCode code);
constexpr unsigned kSpliceRelief = 1u;
constexpr unsigned kSpliceMaterial = 2u;

bool SpliceTerrainShader(ShaderCode client, ShaderCode fragment, unsigned mode, std::vector<uint32_t>& out,
                         std::string* why);

bool ReadModelPixelLayout(ShaderCode code, ModelPixelLayout& layout, std::string* why);
bool ReadModelVertexLayout(ShaderCode code, ModelVertexLayout& layout);
bool SpliceModelShader(ShaderCode client, ShaderCode fragment, std::vector<uint32_t>& out, std::string* why,
                       unsigned mode = kSpliceRelief);

inline bool SpliceTerrainRelief(ShaderCode client, ShaderCode fragment, std::vector<uint32_t>& out, std::string* why)
{
    return SpliceTerrainShader(client, fragment, kSpliceRelief, out, why);
}
