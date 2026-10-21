#pragma once

#include "runtime_heights.h"
#include "shader_splice.h"
#include "terrain_material.h"

#include <d3d9.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

struct Config;

namespace relieffile
{
struct Entry;
}

namespace materialfile
{
struct Entry;
}

constexpr float kReliefMinDepthPerLength = 0.15f;
constexpr float kReliefMaxTexelsPerPixel = 48.0f;
constexpr float kReliefFadeShareOfDistance = 0.25f;
constexpr float kReliefModelEdgeTexels = 0.5f;
constexpr unsigned kReliefRecheckFrames = 120;
constexpr float kReliefChunkReach = 48.0f;

inline bool ChunkWithinReach(const float modelView[4][4], const float chunkOrigin[4], float reliefDistance)
{
    const float* t = modelView[3];
    const float cameraX = -(modelView[0][0] * t[0] + modelView[0][1] * t[1] + modelView[0][2] * t[2]);
    const float cameraY = -(modelView[1][0] * t[0] + modelView[1][1] * t[1] + modelView[1][2] * t[2]);
    const float dx = chunkOrigin[0] - cameraX;
    const float dy = chunkOrigin[1] - cameraY;
    return std::sqrt(dx * dx + dy * dy) - kReliefChunkReach < reliefDistance;
}

struct TerrainReliefStats
{
    unsigned shadersPatched;
    unsigned shadersRefused;
    unsigned viewPositionVertexShaders;
    unsigned texturesWithHeights;
    unsigned texturesTypeOnly;
    unsigned texturesWithout;
    unsigned texturesChanged;
    unsigned heightMapsLoaded;
    unsigned drawsWithRelief;
    unsigned drawsWithoutViewPosition;
    unsigned drawsBeyondReach;
    unsigned materialShadersPatched;
    unsigned materialShadersRefused;
    unsigned texturesWithMaterial;
    unsigned drawsWithMaterial;
    unsigned drawsMaterialBlended;
    unsigned drawsMaterialOtherTarget;
    unsigned modelShadersPatched;
    unsigned modelShadersRefused;
    unsigned modelVertexShaders;
    unsigned drawsWithModelRelief;
    unsigned modelDrawsWithoutHeights;
    unsigned modelDrawsOtherVertexShader;
    unsigned modelDrawsOfModels;
    unsigned drawsWithRuntimeHeights;
    unsigned drawsWaitingForHeights;
    unsigned drawsWithModelMaterial;
};

struct TerrainMaterialTargets
{
    IDirect3DTexture9* params;
    IDirect3DTexture9* tags;
    bool written;
};

bool TerrainMaterialsWanted(const Config& cfg);

struct ReliefDrawInputs
{
    float modelView[3][4];
    float light[3][4];
    float layerDepths[4];
    int layers;
    float model[kReliefModelConstantCount][4];
    int modelLight;
    unsigned modelDraws;
};

class TerrainRelief
{
public:
    void PixelShaderCreated(IDirect3DDevice9* real, const DWORD* code, IDirect3DPixelShader9* shader);
    void VertexShaderCreated(const DWORD* code, IDirect3DVertexShader9* shader);
    void TextureCreated(IDirect3DBaseTexture9* texture);
    void ClientPixelShader(IDirect3DPixelShader9* shader)
    {
        m_clientPs = shader;
        m_realPs = shader;
    }
    void ClientVertexShader(IDirect3DVertexShader9* shader) { m_clientVs = shader; }
    bool Substituted() const { return m_realPs != m_clientPs; }
    IDirect3DPixelShader9* ClientPixelShaderBound() const { return m_clientPs; }
    void BeforeDraw(IDirect3DDevice9* real, const Config& cfg);
    void Frame(IDirect3DDevice9* real, const Config& cfg, bool fogActive);
    void ReleaseAll();
    void ReleaseDefaultPool();
    bool MaterialBound() const { return m_materialBound; }
    bool ClientDrawsTerrain() const;
    void LeaveMaterialTargets(IDirect3DDevice9* real);
    TerrainMaterialTargets MaterialTargets() const;
    bool ReadMaterialTargets(IDirect3DDevice9* real, uint8_t* params, uint8_t* tags, size_t bytes, unsigned* width,
                             unsigned* height) const;
    bool MaterialsOn() const { return m_materialsOn; }
    bool Watching() const { return m_watching; }
    bool HasPatchedTwin(IDirect3DPixelShader9* shader) const;
    const TerrainReliefStats& Stats() const { return m_stats; }
    TerrainReliefStats Totals() const;
    const ReliefDrawInputs& LastDraw() const { return m_lastDraw; }
    RuntimeHeights& Runtime() { return m_runtime; }

private:
    struct PixelEntry
    {
        int layers;
        unsigned long long hash;
        IDirect3DPixelShader9* patched;
        IDirect3DPixelShader9* material;
        IDirect3DPixelShader9* both;
    };
    struct KnownTexture
    {
        const relieffile::Entry* entry;
        const materialfile::Entry* material;
        unsigned checkedFrame;
    };

    struct ModelEntry
    {
        ModelPixelLayout layout;
        unsigned long long hash;
        IDirect3DPixelShader9* patched;
        IDirect3DPixelShader9* material;
        IDirect3DPixelShader9* both;
    };

    IDirect3DPixelShader9* Splice(IDirect3DDevice9* real, const uint32_t* words, size_t count, int layers,
                                  unsigned mode, unsigned long long hash);
    IDirect3DPixelShader9* SpliceModel(IDirect3DDevice9* real, const uint32_t* words, size_t count,
                                       unsigned long long hash, unsigned mode);
    void SetModelMaterialConstants(IDirect3DDevice9* real, const MaterialParams& material);
    void ForgetPixelShader(IDirect3DPixelShader9* shader);
    IDirect3DPixelShader9* ModelDraw(IDirect3DDevice9* real, const Config& cfg);
    void BindPixelShader(IDirect3DDevice9* real, IDirect3DPixelShader9* use);
    KnownTexture Identify(IDirect3DBaseTexture9* texture);
    KnownTexture Lookup(IDirect3DBaseTexture9* texture);
    IDirect3DTexture9* HeightTexture(IDirect3DDevice9* real, unsigned heightIndex);
    IDirect3DTexture9* FlatTexture(IDirect3DDevice9* real);
    void SetSamplerStates(IDirect3DDevice9* real);
    void Enable(IDirect3DDevice9* real);
    void Disable(IDirect3DDevice9* real);
    void LogSummary();
    void ForgetTextures();
    bool MaterialCapsMet(IDirect3DDevice9* real);
    bool EnsureMaterialTargets(IDirect3DDevice9* real);
    void ReleaseMaterialTargets();
    bool MaterialTargetFits(IDirect3DDevice9* real);
    void BindMaterialTargets(IDirect3DDevice9* real);
    void SetMaterialConstants(IDirect3DDevice9* real, const KnownTexture* layers, int count);

    std::unordered_map<IDirect3DPixelShader9*, PixelEntry> m_pixel;
    std::unordered_set<IDirect3DVertexShader9*> m_viewPositionVs;
    std::unordered_map<IDirect3DPixelShader9*, ModelEntry> m_model;
    std::unordered_map<IDirect3DVertexShader9*, ModelVertexLayout> m_modelVs;
    std::unordered_map<IDirect3DBaseTexture9*, KnownTexture> m_textures;
    unsigned m_frame = 0;
    std::unordered_map<unsigned, IDirect3DTexture9*> m_heights;
    IDirect3DTexture9* m_flat = nullptr;
    IDirect3DPixelShader9* m_clientPs = nullptr;
    IDirect3DPixelShader9* m_realPs = nullptr;
    IDirect3DVertexShader9* m_clientVs = nullptr;
    bool m_watching = false;
    bool m_samplersSet = false;
    bool m_reliefOn = false;
    bool m_materialsOn = false;
    bool m_materialBound = false;
    bool m_materialWritten = false;
    bool m_materialFailed = false;
    int m_materialCaps = -1;
    DWORD m_savedWriteMasks[2] = {};
    IDirect3DTexture9* m_materialParams = nullptr;
    IDirect3DTexture9* m_materialTags = nullptr;
    IDirect3DSurface9* m_materialParamsSurface = nullptr;
    IDirect3DSurface9* m_materialTagsSurface = nullptr;
    UINT m_materialWidth = 0;
    UINT m_materialHeight = 0;
    unsigned long long m_lastSummary = 0;
    unsigned m_catalogGeneration = 0;
    unsigned m_materialGeneration = 0;
    TerrainReliefStats m_stats = {};
    TerrainReliefStats m_minute = {};
    ReliefDrawInputs m_lastDraw = {};
    RuntimeHeights m_runtime;
};

uint8_t ModelFallbackClass(const ModelVertexLayout& vs, bool building);
bool ModelVertexShaderTakesRelief(const ModelVertexLayout& vs, bool models);
bool ModelMaterialOf(uint8_t reliefClass, MaterialParams& material);

void PressReliefKey();
