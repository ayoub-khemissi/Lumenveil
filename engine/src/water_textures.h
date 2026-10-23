#pragma once

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

struct BlpImage
{
    UINT width = 0;
    UINT height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    std::vector<std::vector<uint8_t>> levels;
};

bool ParseBlp(const std::vector<uint8_t>& bytes, BlpImage& out);

class WaterTextureFiles
{
public:
    void Load(const std::string& directory);
    const BlpImage* Bump() const { return m_bump.levels.empty() ? nullptr : &m_bump; }
    const BlpImage* Foam() const { return m_foam.levels.empty() ? nullptr : &m_foam; }
    const float* BumpBias() const { return m_bumpBias; }

private:
    BlpImage m_bump;
    BlpImage m_foam;
    float m_bumpBias[2] = {};
};

WaterTextureFiles& GlobalWaterTextureFiles();
