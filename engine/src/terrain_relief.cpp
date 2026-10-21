#include "terrain_relief.h"

#include "call_watch.h"
#include "config.h"
#include "log.h"
#include "material_catalog.h"
#include "relief_catalog.h"
#include "shader_splice.h"
#include "terrain_material.h"
#include "wet_surfaces.h"

#include "ps_material_1.h"
#include "ps_material_2.h"
#include "ps_material_3.h"
#include "ps_material_4.h"
#include "ps_relief_1.h"
#include "ps_relief_2.h"
#include "ps_relief_3.h"
#include "ps_relief_4.h"
#include "ps_relief_material_1.h"
#include "ps_relief_material_2.h"
#include "ps_relief_material_3.h"
#include "ps_relief_material_4.h"
#include "ps_model_material.h"
#include "ps_relief_model.h"
#include "ps_relief_model_material.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr size_t kMaxShaderWords = 1u << 16;
constexpr unsigned long long kSummaryMilliseconds = 60000;
constexpr DWORD kAllChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
                               D3DCOLORWRITEENABLE_ALPHA;
constexpr DWORD kMaterialParamsTarget = 1;
constexpr DWORD kMaterialTagsTarget = 2;
constexpr DWORD kMaterialTargetsNeeded = 3;
constexpr int kModelVertexConstantsRead = 32;
constexpr int kModelLightToward = 0;
constexpr int kModelDirect = 1;
constexpr int kModelAmbient = 2;
constexpr int kModelMultiplier = 3;
constexpr int kModelEmissive = 4;
constexpr int kModelLayer = 5;
constexpr int kModelFade = 6;
constexpr int kModelSampling = 7;

int g_watchingDevices = 0;

template <size_t N>
ShaderCode CodeOf(const BYTE (&bytes)[N])
{
    return {reinterpret_cast<const uint32_t*>(bytes), N / sizeof(uint32_t)};
}

ShaderCode FragmentFor(int layers, unsigned mode)
{
    const bool relief = (mode & kSpliceRelief) != 0;
    const bool material = (mode & kSpliceMaterial) != 0;
    const ShaderCode both[kReliefMaxLayers] = {CodeOf(g_ps_relief_material_1), CodeOf(g_ps_relief_material_2),
                                               CodeOf(g_ps_relief_material_3), CodeOf(g_ps_relief_material_4)};
    const ShaderCode reliefOnly[kReliefMaxLayers] = {CodeOf(g_ps_relief_1), CodeOf(g_ps_relief_2),
                                                     CodeOf(g_ps_relief_3), CodeOf(g_ps_relief_4)};
    const ShaderCode materialOnly[kReliefMaxLayers] = {CodeOf(g_ps_material_1), CodeOf(g_ps_material_2),
                                                       CodeOf(g_ps_material_3), CodeOf(g_ps_material_4)};
    if (layers < 1 || layers > kReliefMaxLayers || (!relief && !material))
        return {};
    const int i = layers - 1;
    return relief && material ? both[i] : relief ? reliefOnly[i] : materialOnly[i];
}

const char* ModeName(unsigned mode)
{
    if (mode == (kSpliceRelief | kSpliceMaterial))
        return "relief and ground material";
    return mode == kSpliceRelief ? "relief" : "ground material";
}

void AddStats(TerrainReliefStats& to, const TerrainReliefStats& from)
{
    to.drawsWithRelief += from.drawsWithRelief;
    to.drawsWithoutViewPosition += from.drawsWithoutViewPosition;
    to.drawsBeyondReach += from.drawsBeyondReach;
    to.drawsWithMaterial += from.drawsWithMaterial;
    to.drawsMaterialBlended += from.drawsMaterialBlended;
    to.drawsMaterialOtherTarget += from.drawsMaterialOtherTarget;
    to.drawsWithModelRelief += from.drawsWithModelRelief;
    to.modelDrawsWithoutHeights += from.modelDrawsWithoutHeights;
    to.modelDrawsOtherVertexShader += from.modelDrawsOtherVertexShader;
    to.modelDrawsOfModels += from.modelDrawsOfModels;
    to.drawsWithRuntimeHeights += from.drawsWithRuntimeHeights;
    to.drawsWaitingForHeights += from.drawsWaitingForHeights;
    to.drawsWithModelMaterial += from.drawsWithModelMaterial;
}

bool BuildingVertexShader(const ModelVertexLayout& vs)
{
    return vs.vertexColour && vs.normalInput;
}
}

uint8_t ModelFallbackClass(const ModelVertexLayout& vs, bool building)
{
    if (building)
        return reliefclass::kWalls;
    return vs.skinned ? reliefclass::kCharacters : reliefclass::kProps;
}

bool ModelVertexShaderTakesRelief(const ModelVertexLayout& vs, bool models)
{
    if (BuildingVertexShader(vs))
        return true;
    return models && vs.normalInput && !vs.vertexColour && vs.light == ModelLightForm::Additive;
}

bool ModelMaterialOf(uint8_t reliefClass, MaterialParams& material)
{
    switch (reliefClass)
    {
    case reliefclass::kRoads:
    case reliefclass::kRock:
    case reliefclass::kWalls:
    case reliefclass::kRoofs:
        material = kStoneMaterial;
        return true;
    case reliefclass::kDirt:
        material = kSoilMaterial;
        return true;
    case reliefclass::kSand:
        material = kSandMaterial;
        return true;
    case reliefclass::kSnow:
        material = kSnowMaterial;
        return true;
    case reliefclass::kGrass:
    case reliefclass::kLeaves:
        material = kGrassMaterial;
        return true;
    case reliefclass::kWood:
    case reliefclass::kBark:
        material = kWoodMaterial;
        return true;
    case reliefclass::kMetal:
        material = kMetalMaterial;
        return true;
    case reliefclass::kCreatures:
    case reliefclass::kCharacters:
        material = kFabricMaterial;
        return true;
    default:
        material = kNoMaterial;
        return false;
    }
}

namespace
{

bool ModelPairFits(const ModelVertexLayout& vs, const ModelPixelLayout& ps)
{
    return vs.viewAndNormal && vs.viewIndex == ps.viewIndex && ps.diffuseIndex >= 0 && ps.diffuseIndex < 32 &&
           ((vs.uvFromTexcoord >> ps.diffuseIndex) & 1u) != 0;
}

bool WithinRead(int reg)
{
    return reg >= 0 && reg < kModelVertexConstantsRead;
}

bool WrapsOrMirrors(DWORD address)
{
    return address == D3DTADDRESS_WRAP || address == D3DTADDRESS_MIRROR;
}

const char* LightFormName(ModelLightForm form)
{
    switch (form)
    {
    case ModelLightForm::Ratio:
        return "vertex colour times light";
    case ModelLightForm::Additive:
        return "material times light";
    default:
        return "unlit";
    }
}

template <typename T>
void SafeRelease(T*& object)
{
    if (object)
        object->Release();
    object = nullptr;
}

bool ThirtyTwoBitColour(D3DFORMAT format)
{
    return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8;
}
}

bool TerrainMaterialsWanted(const Config& cfg)
{
    return cfg.terrainMaterials && WetSurfacesWanted(cfg) && GlobalMaterialCatalog().Loaded();
}

IDirect3DPixelShader9* TerrainRelief::Splice(IDirect3DDevice9* real, const uint32_t* words, size_t count, int layers,
                                             unsigned mode, unsigned long long hash)
{
    std::vector<uint32_t> spliced;
    std::string why;
    const bool material = (mode & kSpliceMaterial) != 0;
    unsigned& refused = material ? m_stats.materialShadersRefused : m_stats.shadersRefused;
    if (!SpliceTerrainShader({words, count}, FragmentFor(layers, mode), mode, spliced, &why))
    {
        ++refused;
        VF_LOG_DEBUG("relief: pixel shader %016llx with %d layers not patched for the %s: %s", hash, layers,
                     ModeName(mode), why.c_str());
        return nullptr;
    }
    IDirect3DPixelShader9* patched = nullptr;
    const HRESULT hr = real->CreatePixelShader(reinterpret_cast<const DWORD*>(spliced.data()), &patched);
    if (FAILED(hr) || !patched)
    {
        ++refused;
        VF_LOG_ERROR("relief: the device refused the terrain shader %016llx patched for the %s (0x%08lX)", hash,
                     ModeName(mode), hr);
        return nullptr;
    }
    if (material)
        ++m_stats.materialShadersPatched;
    else
        ++m_stats.shadersPatched;
    VF_LOG_INFO("relief: terrain pixel shader %016llx (%d layer%s) patched for the %s, %zu words", hash, layers,
                layers == 1 ? "" : "s", ModeName(mode), spliced.size());
    return patched;
}

IDirect3DPixelShader9* TerrainRelief::SpliceModel(IDirect3DDevice9* real, const uint32_t* words, size_t count,
                                                  unsigned long long hash, unsigned mode)
{
    const ShaderCode fragment = mode == kSpliceRelief     ? CodeOf(g_ps_relief_model)
                                : mode == kSpliceMaterial ? CodeOf(g_ps_model_material)
                                                          : CodeOf(g_ps_relief_model_material);
    std::vector<uint32_t> spliced;
    std::string why;
    if (!SpliceModelShader({words, count}, fragment, spliced, &why, mode))
    {
        ++m_stats.modelShadersRefused;
        VF_LOG_DEBUG("relief: model pixel shader %016llx not patched for the %s: %s", hash, ModeName(mode),
                     why.c_str());
        return nullptr;
    }
    IDirect3DPixelShader9* patched = nullptr;
    const HRESULT hr = real->CreatePixelShader(reinterpret_cast<const DWORD*>(spliced.data()), &patched);
    if (FAILED(hr) || !patched)
    {
        ++m_stats.modelShadersRefused;
        VF_LOG_ERROR("relief: the device refused the model shader %016llx patched for the %s (0x%08lX)", hash,
                     ModeName(mode), hr);
        return nullptr;
    }
    ++m_stats.modelShadersPatched;
    VF_LOG_INFO("relief: model pixel shader %016llx patched for the %s, %zu words", hash, ModeName(mode),
                spliced.size());
    return patched;
}

void TerrainRelief::ForgetPixelShader(IDirect3DPixelShader9* shader)
{
    auto old = m_pixel.find(shader);
    if (old != m_pixel.end())
    {
        SafeRelease(old->second.patched);
        SafeRelease(old->second.material);
        SafeRelease(old->second.both);
        m_pixel.erase(old);
    }
    auto model = m_model.find(shader);
    if (model != m_model.end())
    {
        SafeRelease(model->second.patched);
        SafeRelease(model->second.material);
        SafeRelease(model->second.both);
        m_model.erase(model);
    }
}

void TerrainRelief::PixelShaderCreated(IDirect3DDevice9* real, const DWORD* code, IDirect3DPixelShader9* shader)
{
    if (!shader || !code)
        return;
    ForgetPixelShader(shader);
    const auto* words = reinterpret_cast<const uint32_t*>(code);
    const size_t count = ShaderWordCount(words, kMaxShaderWords);
    if (!count)
        return;
    const unsigned long long hash = relieffile::Fnv1a(words, count * sizeof(uint32_t));
    TerrainPixelLayout layout;
    if (!ReadTerrainPixelLayout({words, count}, layout, nullptr))
    {
        ModelPixelLayout model;
        if (!ReadModelPixelLayout({words, count}, model, nullptr))
            return;
        IDirect3DPixelShader9* patched = SpliceModel(real, words, count, hash, kSpliceRelief);
        IDirect3DPixelShader9* material = SpliceModel(real, words, count, hash, kSpliceMaterial);
        IDirect3DPixelShader9* both = SpliceModel(real, words, count, hash, kSpliceRelief | kSpliceMaterial);
        if (patched || material || both)
            m_model[shader] = {model, hash, patched, material, both};
        return;
    }
    PixelEntry e = {layout.layers, hash, nullptr, nullptr, nullptr};
    e.patched = Splice(real, words, count, layout.layers, kSpliceRelief, hash);
    e.material = Splice(real, words, count, layout.layers, kSpliceMaterial, hash);
    e.both = Splice(real, words, count, layout.layers, kSpliceRelief | kSpliceMaterial, hash);
    if (e.patched || e.material || e.both)
        m_pixel[shader] = e;
}

void TerrainRelief::VertexShaderCreated(const DWORD* code, IDirect3DVertexShader9* shader)
{
    if (!shader || !code)
        return;
    m_viewPositionVs.erase(shader);
    m_modelVs.erase(shader);
    const auto* words = reinterpret_cast<const uint32_t*>(code);
    const size_t count = ShaderWordCount(words, kMaxShaderWords);
    if (count && VertexShaderWritesViewPosition({words, count}))
    {
        m_viewPositionVs.insert(shader);
        m_stats.viewPositionVertexShaders = static_cast<unsigned>(m_viewPositionVs.size());
    }
    ModelVertexLayout model;
    if (count && ReadModelVertexLayout({words, count}, model))
    {
        m_modelVs[shader] = model;
        m_stats.modelVertexShaders = static_cast<unsigned>(m_modelVs.size());
        VF_LOG_DEBUG("relief: model vertex shader with the view position in TEXCOORD%d, %s%s", model.viewIndex,
                     LightFormName(model.light), BuildingVertexShader(model) ? ", a building's" : "");
    }
}

void TerrainRelief::TextureCreated(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return;
    m_textures.erase(texture);
    m_runtime.Forget(texture);
}

bool TerrainRelief::HasPatchedTwin(IDirect3DPixelShader9* shader) const
{
    auto it = m_pixel.find(shader);
    return it != m_pixel.end() && it->second.patched;
}

bool TerrainRelief::ClientDrawsTerrain() const
{
    return m_pixel.count(m_clientPs) != 0 && m_viewPositionVs.count(m_clientVs) != 0;
}

TerrainRelief::KnownTexture TerrainRelief::Identify(IDirect3DBaseTexture9* texture)
{
    KnownTexture known = {nullptr, nullptr, m_frame};
    if (texture->GetType() != D3DRTYPE_TEXTURE)
        return known;
    auto* tex = static_cast<IDirect3DTexture9*>(texture);
    D3DSURFACE_DESC top = {};
    if (FAILED(tex->GetLevelDesc(0, &top)))
        return known;
    const uint32_t format = static_cast<uint32_t>(top.Format);
    if (!relieffile::RowBytes(format, 4))
        return known;
    const uint32_t level = relieffile::HashedLevel(top.Width, top.Height, tex->GetLevelCount());
    D3DSURFACE_DESC desc = {};
    if (FAILED(tex->GetLevelDesc(level, &desc)))
        return known;
    D3DLOCKED_RECT rect = {};
    if (FAILED(tex->LockRect(level, &rect, nullptr, D3DLOCK_READONLY)) || !rect.pBits)
        return known;
    const uint32_t rowBytes = relieffile::RowBytes(format, desc.Width);
    const uint32_t rows = relieffile::RowCount(format, desc.Height);
    uint64_t hash = relieffile::Fnv1a(nullptr, 0);
    for (uint32_t r = 0; r < rows; ++r)
        hash = relieffile::Fnv1a(static_cast<const uint8_t*>(rect.pBits) + static_cast<size_t>(r) * rect.Pitch,
                                 rowBytes, hash);
    tex->UnlockRect(level);
    known.entry = GlobalReliefCatalog().Find(hash, format, desc.Width, desc.Height);
    known.material = GlobalMaterialCatalog().Find(hash, format, desc.Width, desc.Height);
    return known;
}

TerrainRelief::KnownTexture TerrainRelief::Lookup(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return {nullptr, nullptr, m_frame};
    auto it = m_textures.find(texture);
    if (it != m_textures.end() && m_frame - it->second.checkedFrame < kReliefRecheckFrames)
        return it->second;
    const KnownTexture known = Identify(texture);
    if (it == m_textures.end())
    {
        if (known.entry && relieffile::HasHeights(*known.entry))
            ++m_stats.texturesWithHeights;
        else if (known.entry)
            ++m_stats.texturesTypeOnly;
        else
            ++m_stats.texturesWithout;
        if (known.material)
            ++m_stats.texturesWithMaterial;
    }
    else if (known.entry != it->second.entry || known.material != it->second.material)
        ++m_stats.texturesChanged;
    m_textures[texture] = known;
    return known;
}

IDirect3DTexture9* TerrainRelief::HeightTexture(IDirect3DDevice9* real, unsigned heightIndex)
{
    auto it = m_heights.find(heightIndex);
    if (it != m_heights.end())
        return it->second;
    const ReliefCatalog& catalog = GlobalReliefCatalog();
    std::vector<uint8_t> bytes;
    IDirect3DTexture9* texture = nullptr;
    const UINT size = catalog.HeightSize();
    const UINT levels = catalog.LevelCount();
    if (catalog.ReadHeights(heightIndex, bytes) &&
        SUCCEEDED(real->CreateTexture(size, size, levels, 0, D3DFMT_L8, D3DPOOL_MANAGED, &texture, nullptr)))
    {
        size_t offset = 0;
        for (UINT k = 0; k < levels; ++k)
        {
            const UINT side = std::max(size >> k, 1u);
            D3DLOCKED_RECT rect = {};
            if (SUCCEEDED(texture->LockRect(k, &rect, nullptr, 0)))
            {
                for (UINT row = 0; row < side; ++row)
                    std::memcpy(static_cast<uint8_t*>(rect.pBits) + static_cast<size_t>(row) * rect.Pitch,
                                bytes.data() + offset + static_cast<size_t>(row) * side, side);
                texture->UnlockRect(k);
            }
            offset += static_cast<size_t>(side) * side;
        }
        ++m_stats.heightMapsLoaded;
    }
    m_heights[heightIndex] = texture;
    return texture;
}

IDirect3DTexture9* TerrainRelief::FlatTexture(IDirect3DDevice9* real)
{
    if (m_flat)
        return m_flat;
    if (FAILED(real->CreateTexture(1, 1, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &m_flat, nullptr)))
        return m_flat = nullptr;
    D3DLOCKED_RECT rect = {};
    if (SUCCEEDED(m_flat->LockRect(0, &rect, nullptr, 0)))
    {
        *static_cast<uint8_t*>(rect.pBits) = 255;
        m_flat->UnlockRect(0);
    }
    return m_flat;
}

void TerrainRelief::SetSamplerStates(IDirect3DDevice9* real)
{
    for (DWORD k = 0; k < kReliefMaxLayers; ++k)
    {
        const DWORD s = kReliefFirstHeightSampler + k;
        real->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        real->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        real->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        real->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        real->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        real->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
        real->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
        real->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    m_samplersSet = true;
}

bool TerrainRelief::MaterialCapsMet(IDirect3DDevice9* real)
{
    if (m_materialCaps >= 0)
        return m_materialCaps != 0;
    D3DCAPS9 caps = {};
    const bool read = SUCCEEDED(real->GetDeviceCaps(&caps));
    const bool met = read && caps.NumSimultaneousRTs >= kMaterialTargetsNeeded &&
                     (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS) != 0;
    m_materialCaps = met ? 1 : 0;
    if (!met)
        VF_LOG_ERROR("ground materials: the device can't write %lu render targets with their own write masks (%lu "
                     "targets, misc caps 0x%08lX); the wet pass guesses the ground from its colour",
                     kMaterialTargetsNeeded, caps.NumSimultaneousRTs, caps.PrimitiveMiscCaps);
    return met;
}

bool TerrainRelief::EnsureMaterialTargets(IDirect3DDevice9* real)
{
    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(real->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
        return false;
    D3DSURFACE_DESC desc = {};
    backBuffer->GetDesc(&desc);
    backBuffer->Release();
    if (m_materialParams && m_materialWidth == desc.Width && m_materialHeight == desc.Height)
        return true;
    if (m_materialFailed)
        return false;
    LeaveMaterialTargets(real);
    ReleaseMaterialTargets();
    const bool made =
        SUCCEEDED(real->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                      D3DPOOL_DEFAULT, &m_materialParams, nullptr)) &&
        SUCCEEDED(real->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                      D3DPOOL_DEFAULT, &m_materialTags, nullptr)) &&
        SUCCEEDED(m_materialParams->GetSurfaceLevel(0, &m_materialParamsSurface)) &&
        SUCCEEDED(m_materialTags->GetSurfaceLevel(0, &m_materialTagsSurface));
    if (!made)
    {
        ReleaseMaterialTargets();
        m_materialFailed = true;
        VF_LOG_ERROR("ground materials: the two %ux%u targets could not be created; the wet pass guesses the "
                     "ground from its colour",
                     desc.Width, desc.Height);
        return false;
    }
    m_materialWidth = desc.Width;
    m_materialHeight = desc.Height;
    real->ColorFill(m_materialParamsSurface, nullptr, 0);
    real->ColorFill(m_materialTagsSurface, nullptr, 0);
    m_materialWritten = false;
    VF_LOG_INFO("ground materials: two %ux%u A8R8G8B8 targets written by the terrain shaders beside the picture "
                "(the layers' wetting from %zu textures: %zu grass, %zu soil, %zu stone, %zu sand, %zu snow, %zu ice, "
                "%zu wood, %zu hot)",
                desc.Width, desc.Height, GlobalMaterialCatalog().EntryCount(),
                GlobalMaterialCatalog().CountOf(materialfile::kGrass),
                GlobalMaterialCatalog().CountOf(materialfile::kSoil),
                GlobalMaterialCatalog().CountOf(materialfile::kStone),
                GlobalMaterialCatalog().CountOf(materialfile::kSand),
                GlobalMaterialCatalog().CountOf(materialfile::kSnow),
                GlobalMaterialCatalog().CountOf(materialfile::kIce),
                GlobalMaterialCatalog().CountOf(materialfile::kWood),
                GlobalMaterialCatalog().CountOf(materialfile::kHot));
    return true;
}

void TerrainRelief::ReleaseMaterialTargets()
{
    SafeRelease(m_materialParamsSurface);
    SafeRelease(m_materialTagsSurface);
    SafeRelease(m_materialParams);
    SafeRelease(m_materialTags);
    m_materialWidth = m_materialHeight = 0;
    m_materialWritten = false;
}

bool TerrainRelief::MaterialTargetFits(IDirect3DDevice9* real)
{
    if (!m_materialParamsSurface || !m_materialTagsSurface)
        return false;
    DWORD blend = FALSE;
    real->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    if (blend)
    {
        ++m_minute.drawsMaterialBlended;
        return false;
    }
    if (m_materialBound)
        return true;
    IDirect3DSurface9* target = nullptr;
    if (FAILED(real->GetRenderTarget(0, &target)) || !target)
        return false;
    D3DSURFACE_DESC desc = {};
    target->GetDesc(&desc);
    target->Release();
    if (desc.Width != m_materialWidth || desc.Height != m_materialHeight || !ThirtyTwoBitColour(desc.Format) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        ++m_minute.drawsMaterialOtherTarget;
        return false;
    }
    return true;
}

void TerrainRelief::BindMaterialTargets(IDirect3DDevice9* real)
{
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    real->GetViewport(&viewport);
    real->GetScissorRect(&scissor);
    real->GetRenderState(D3DRS_COLORWRITEENABLE1, &m_savedWriteMasks[0]);
    real->GetRenderState(D3DRS_COLORWRITEENABLE2, &m_savedWriteMasks[1]);
    real->SetRenderTarget(kMaterialParamsTarget, m_materialParamsSurface);
    real->SetRenderTarget(kMaterialTagsTarget, m_materialTagsSurface);
    real->SetRenderState(D3DRS_COLORWRITEENABLE1, kAllChannels);
    real->SetRenderState(D3DRS_COLORWRITEENABLE2, kAllChannels);
    real->SetViewport(&viewport);
    real->SetScissorRect(&scissor);
    m_materialBound = true;
    m_materialWritten = true;
}

void TerrainRelief::LeaveMaterialTargets(IDirect3DDevice9* real)
{
    if (!m_materialBound)
        return;
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    real->GetViewport(&viewport);
    real->GetScissorRect(&scissor);
    real->SetRenderTarget(kMaterialTagsTarget, nullptr);
    real->SetRenderTarget(kMaterialParamsTarget, nullptr);
    real->SetRenderState(D3DRS_COLORWRITEENABLE1, m_savedWriteMasks[0]);
    real->SetRenderState(D3DRS_COLORWRITEENABLE2, m_savedWriteMasks[1]);
    real->SetViewport(&viewport);
    real->SetScissorRect(&scissor);
    m_materialBound = false;
}

TerrainMaterialTargets TerrainRelief::MaterialTargets() const
{
    return {m_materialParams, m_materialTags, m_materialsOn && m_materialWritten};
}

bool TerrainRelief::ReadMaterialTargets(IDirect3DDevice9* real, uint8_t* params, uint8_t* tags, size_t bytes,
                                        unsigned* width, unsigned* height) const
{
    if (width)
        *width = m_materialWidth;
    if (height)
        *height = m_materialHeight;
    const size_t need = static_cast<size_t>(m_materialWidth) * m_materialHeight * 4;
    if (!m_materialParamsSurface || !m_materialTagsSurface || !need || bytes < need)
        return false;
    IDirect3DSurface9* copy = nullptr;
    if (FAILED(real->CreateOffscreenPlainSurface(m_materialWidth, m_materialHeight, D3DFMT_A8R8G8B8,
                                                 D3DPOOL_SYSTEMMEM, &copy, nullptr)))
        return false;
    bool read = true;
    IDirect3DSurface9* sources[2] = {m_materialParamsSurface, m_materialTagsSurface};
    uint8_t* outs[2] = {params, tags};
    for (int k = 0; k < 2; ++k)
    {
        D3DLOCKED_RECT rect = {};
        if (!outs[k] || FAILED(real->GetRenderTargetData(sources[k], copy)) ||
            FAILED(copy->LockRect(&rect, nullptr, D3DLOCK_READONLY)))
        {
            read = !outs[k] && read;
            continue;
        }
        for (UINT row = 0; row < m_materialHeight; ++row)
            std::memcpy(outs[k] + static_cast<size_t>(row) * m_materialWidth * 4,
                        static_cast<const uint8_t*>(rect.pBits) + static_cast<size_t>(row) * rect.Pitch,
                        static_cast<size_t>(m_materialWidth) * 4);
        copy->UnlockRect();
    }
    copy->Release();
    return read && m_materialWritten;
}

void TerrainRelief::SetMaterialConstants(IDirect3DDevice9* real, const KnownTexture* layers, int count)
{
    float constants[kMaterialConstantCount][4] = {};
    for (int k = 0; k < count && k < kReliefMaxLayers; ++k)
    {
        const uint8_t kind = layers[k].material ? layers[k].material->kind : materialfile::kUnknown;
        const MaterialParams p = MaterialParamsOf(kind);
        constants[k][0] = p.darkening;
        constants[k][1] = p.pores;
        constants[k][2] = p.film;
        constants[k][3] = p.puddle;
        constants[kMaterialLayerGlintRegister - kMaterialFirstLayerRegister][k] = p.glint;
        constants[kMaterialLayerKnownRegister - kMaterialFirstLayerRegister][k] =
            kind != materialfile::kUnknown && kind < materialfile::kKindCount ? 1.0f : 0.0f;
    }
    real->SetPixelShaderConstantF(kMaterialFirstLayerRegister, constants[0], kMaterialConstantCount);
}

void TerrainRelief::BindPixelShader(IDirect3DDevice9* real, IDirect3DPixelShader9* use)
{
    if (m_realPs == use)
        return;
    real->SetPixelShader(use);
    m_realPs = use;
}

void TerrainRelief::SetModelMaterialConstants(IDirect3DDevice9* real, const MaterialParams& material)
{
    float constants[kMaterialConstantCount][4] = {};
    constants[0][0] = material.darkening;
    constants[0][1] = material.pores;
    constants[0][2] = material.film;
    constants[0][3] = material.puddle;
    constants[kMaterialLayerGlintRegister - kMaterialFirstLayerRegister][0] = material.glint;
    constants[kMaterialLayerKnownRegister - kMaterialFirstLayerRegister][0] = 1.0f;
    real->SetPixelShaderConstantF(kMaterialFirstLayerRegister, constants[0], kMaterialConstantCount);
}

IDirect3DPixelShader9* TerrainRelief::ModelDraw(IDirect3DDevice9* real, const Config& cfg)
{
    auto pixel = (m_reliefOn || m_materialsOn) && m_clientPs ? m_model.find(m_clientPs) : m_model.end();
    if (pixel == m_model.end())
    {
        LeaveMaterialTargets(real);
        return m_clientPs;
    }
    auto vertex = m_modelVs.find(m_clientVs);
    if (vertex == m_modelVs.end() || !ModelVertexShaderTakesRelief(vertex->second, cfg.reliefModels) ||
        !ModelPairFits(vertex->second, pixel->second.layout))
    {
        ++m_minute.modelDrawsOtherVertexShader;
        LeaveMaterialTargets(real);
        return m_clientPs;
    }
    const ModelVertexLayout& vs = vertex->second;
    IDirect3DBaseTexture9* diffuse = nullptr;
    real->GetTexture(0, &diffuse);
    const KnownTexture known = Lookup(diffuse);
    const relieffile::Entry* entry = known.entry;
    const bool building = BuildingVertexShader(vs);
    if (!building)
        ++m_minute.modelDrawsOfModels;
    const uint8_t type = entry ? entry->reliefClass : ModelFallbackClass(vs, building);
    float depth = 0.0f;
    IDirect3DTexture9* height = nullptr;
    float heightSize = static_cast<float>(GlobalReliefCatalog().HeightSize());
    if (m_reliefOn && pixel->second.patched)
    {
        depth = entry ? ReliefLayerDepth(cfg, entry->flatness, entry->reliefClass) : 0.0f;
        height = entry && depth > 0.0f && relieffile::HasHeights(*entry) ? HeightTexture(real, entry->heightIndex)
                                                                         : nullptr;
        if (!height && cfg.reliefModels && diffuse && !(entry && relieffile::HasHeights(*entry)) &&
            ReliefClassShare(cfg, type) > 0.0f)
        {
            const RuntimeHeights::Ready* made = m_runtime.Find(real, diffuse, m_frame);
            if (made)
            {
                height = made->texture;
                depth = ReliefLayerDepth(cfg, made->depthFactor, type);
                heightSize = static_cast<float>(made->size);
                ++m_minute.drawsWithRuntimeHeights;
            }
            else
                ++m_minute.drawsWaitingForHeights;
        }
        if (!height || depth <= 0.0f)
            ++m_minute.modelDrawsWithoutHeights;
    }
    if (diffuse)
        diffuse->Release();
    const bool relief = height && depth > 0.0f;
    MaterialParams material = kNoMaterial;
    IDirect3DPixelShader9* withMaterial = relief ? pixel->second.both : pixel->second.material;
    const bool writesMaterial =
        m_materialsOn && withMaterial && ModelMaterialOf(type, material) && MaterialTargetFits(real);
    if (writesMaterial)
    {
        if (!m_materialBound)
            BindMaterialTargets(real);
        SetModelMaterialConstants(real, material);
        ++m_minute.drawsWithModelMaterial;
    }
    else
        LeaveMaterialTargets(real);
    if (!relief)
        return writesMaterial ? withMaterial : m_clientPs;
    if (!m_samplersSet)
        SetSamplerStates(real);
    real->SetTexture(kReliefFirstHeightSampler, height);

    float vertexConstants[kModelVertexConstantsRead][4] = {};
    real->GetVertexShaderConstantF(0, vertexConstants[0], kModelVertexConstantsRead);
    ModelLightForm form = vs.light;
    if (!WithinRead(vs.lightRegister) || !WithinRead(vs.directRegister) || !WithinRead(vs.ambientRegister) ||
        (form == ModelLightForm::Ratio && !WithinRead(vs.emissiveRegister)) ||
        (form == ModelLightForm::Additive && !WithinRead(vs.multiplierRegister)))
        form = ModelLightForm::None;
    float c[kReliefModelConstantCount][4] = {};
    c[kModelLightToward][2] = 1.0f;
    c[kModelMultiplier][0] = c[kModelMultiplier][1] = c[kModelMultiplier][2] = 1.0f;
    if (form != ModelLightForm::None)
    {
        const float* light = vertexConstants[vs.lightRegister];
        const float length = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
        if (length > 1e-6f)
            for (int k = 0; k < 3; ++k)
                c[kModelLightToward][k] = -light[k] / length;
        else
            form = ModelLightForm::None;
    }
    if (form != ModelLightForm::None)
    {
        std::memcpy(c[kModelDirect], vertexConstants[vs.directRegister], 3 * sizeof(float));
        std::memcpy(c[kModelAmbient], vertexConstants[vs.ambientRegister], 3 * sizeof(float));
        if (form == ModelLightForm::Ratio)
            std::memcpy(c[kModelEmissive], vertexConstants[vs.emissiveRegister], 3 * sizeof(float));
        else
            std::memcpy(c[kModelMultiplier], vertexConstants[vs.multiplierRegister], 3 * sizeof(float));
        c[kModelDirect][3] = form == ModelLightForm::Ratio ? 1.0f : 0.0f;
        c[kModelAmbient][3] = form == ModelLightForm::Additive ? 1.0f : 0.0f;
    }
    DWORD addressU = D3DTADDRESS_WRAP;
    DWORD addressV = D3DTADDRESS_WRAP;
    real->GetSamplerState(0, D3DSAMP_ADDRESSU, &addressU);
    real->GetSamplerState(0, D3DSAMP_ADDRESSV, &addressV);
    const bool clamped = !WrapsOrMirrors(addressU) || !WrapsOrMirrors(addressV);
    if (clamped)
    {
        real->SetSamplerState(kReliefFirstHeightSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        real->SetSamplerState(kReliefFirstHeightSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        m_samplersSet = false;
    }
    c[kModelLayer][0] = depth;
    c[kModelLayer][1] = clamped ? 1.0f : 0.0f;
    c[kModelLayer][2] = kReliefModelEdgeTexels / std::max(heightSize, 1.0f);
    c[kModelFade][0] = cfg.reliefDistance;
    c[kModelFade][1] = 1.0f / std::max(cfg.reliefDistance * kReliefFadeShareOfDistance, 0.5f);
    c[kModelFade][2] = cfg.reliefLight;
    c[kModelFade][3] = form == ModelLightForm::None ? 0.0f : cfg.reliefShadows;
    c[kModelSampling][0] = heightSize;
    c[kModelSampling][1] = kReliefMinDepthPerLength;
    c[kModelSampling][2] = kReliefMaxTexelsPerPixel;
    real->SetPixelShaderConstantF(kReliefConstantBase, c[0], kReliefModelConstantCount);
    std::memcpy(m_lastDraw.model, c, sizeof c);
    m_lastDraw.modelLight = static_cast<int>(form);
    ++m_lastDraw.modelDraws;
    ++m_minute.drawsWithModelRelief;
    return writesMaterial ? withMaterial : pixel->second.patched;
}

void TerrainRelief::BeforeDraw(IDirect3DDevice9* real, const Config& cfg)
{
    auto it = m_clientPs ? m_pixel.find(m_clientPs) : m_pixel.end();
    const bool terrain = it != m_pixel.end();
    if (!terrain)
    {
        BindPixelShader(real, ModelDraw(real, cfg));
        return;
    }
    const bool viewPosition = terrain && m_viewPositionVs.count(m_clientVs) != 0;
    if (terrain && !viewPosition)
        ++m_minute.drawsWithoutViewPosition;
    float modelView[4][4] = {};
    bool relief = false;
    if (viewPosition && m_reliefOn && it->second.patched)
    {
        float origin[4] = {};
        real->GetVertexShaderConstantF(kReliefModelViewVsRegister, modelView[0], 4);
        real->GetVertexShaderConstantF(kReliefChunkOriginVsRegister, origin, 1);
        relief = ChunkWithinReach(modelView, origin, cfg.reliefDistance);
        if (!relief)
            ++m_minute.drawsBeyondReach;
    }
    bool material = viewPosition && m_materialsOn && MaterialTargetFits(real);
    if (material && (relief ? !it->second.both : !it->second.material))
        material = false;
    if (!material)
        LeaveMaterialTargets(real);
    IDirect3DPixelShader9* use = m_clientPs;
    if (relief)
        use = material ? it->second.both : it->second.patched;
    else if (material)
        use = it->second.material;
    BindPixelShader(real, use);
    if (!relief && !material)
        return;
    if (material && !m_materialBound)
        BindMaterialTargets(real);

    const PixelEntry& e = it->second;
    KnownTexture layers[kReliefMaxLayers] = {};
    for (int k = 0; k < e.layers && k < kReliefMaxLayers; ++k)
    {
        IDirect3DBaseTexture9* layer = nullptr;
        real->GetTexture(static_cast<DWORD>(k), &layer);
        layers[k] = Lookup(layer);
        if (layer)
            layer->Release();
    }
    if (material)
    {
        SetMaterialConstants(real, layers, e.layers);
        ++m_minute.drawsWithMaterial;
    }
    if (!relief)
        return;
    if (!m_samplersSet)
        SetSamplerStates(real);
    ReliefDrawInputs d = m_lastDraw;
    d.layers = e.layers;
    std::memcpy(d.modelView, modelView, sizeof d.modelView);
    real->GetVertexShaderConstantF(kReliefLightTowardVsRegister, d.light[0], 3);
    for (int k = 0; k < e.layers && k < kReliefMaxLayers; ++k)
    {
        const relieffile::Entry* entry = layers[k].entry;
        const float depth = entry ? ReliefLayerDepth(cfg, entry->flatness, entry->reliefClass) : 0.0f;
        IDirect3DTexture9* height =
            entry && depth > 0.0f && relieffile::HasHeights(*entry) ? HeightTexture(real, entry->heightIndex) : nullptr;
        d.layerDepths[k] = height ? depth : 0.0f;
        real->SetTexture(kReliefFirstHeightSampler + k, height ? height : FlatTexture(real));
    }
    const float fadeLength = std::max(cfg.reliefDistance * kReliefFadeShareOfDistance, 0.5f);
    float constants[kReliefConstantCount][4] = {};
    std::memcpy(constants[0], d.modelView, sizeof d.modelView);
    std::memcpy(constants[3], d.light, sizeof d.light);
    std::memcpy(constants[6], d.layerDepths, sizeof d.layerDepths);
    constants[7][0] = cfg.reliefDistance;
    constants[7][1] = 1.0f / fadeLength;
    constants[7][2] = cfg.reliefLight;
    constants[7][3] = cfg.reliefShadows;
    constants[8][0] = static_cast<float>(GlobalReliefCatalog().HeightSize());
    constants[8][1] = kReliefMinDepthPerLength;
    constants[8][2] = kReliefMaxTexelsPerPixel;
    real->SetPixelShaderConstantF(kReliefConstantBase, constants[0], kReliefConstantCount);
    m_lastDraw = d;
    ++m_minute.drawsWithRelief;
}

void TerrainRelief::Enable(IDirect3DDevice9* real)
{
    m_watching = true;
    if (g_watchingDevices++ == 0)
        g_callWatch |= kWatchRelief;
    IDirect3DPixelShader9* ps = nullptr;
    if (SUCCEEDED(real->GetPixelShader(&ps)) && ps)
        ps->Release();
    m_clientPs = m_realPs = ps;
    IDirect3DVertexShader9* vs = nullptr;
    if (SUCCEEDED(real->GetVertexShader(&vs)) && vs)
        vs->Release();
    m_clientVs = vs;
    m_samplersSet = false;
    VF_LOG_INFO("relief: watching the terrain and building draws (relief %s, ground materials %s; %u terrain pixel "
                "shaders patched for the relief, %u refused; %u for the ground materials, %u refused; %u vertex "
                "shaders with the view position; %u model pixel shaders patched for the relief, %u refused; %u model "
                "vertex shaders with the view position and normal)",
                m_reliefOn ? "on" : "off", m_materialsOn ? "on" : "off", m_stats.shadersPatched,
                m_stats.shadersRefused, m_stats.materialShadersPatched, m_stats.materialShadersRefused,
                m_stats.viewPositionVertexShaders, m_stats.modelShadersPatched, m_stats.modelShadersRefused,
                m_stats.modelVertexShaders);
}

void TerrainRelief::Disable(IDirect3DDevice9* real)
{
    m_watching = false;
    if (--g_watchingDevices == 0)
        g_callWatch &= ~kWatchRelief;
    LeaveMaterialTargets(real);
    if (m_realPs != m_clientPs)
        real->SetPixelShader(m_clientPs);
    m_realPs = m_clientPs;
    VF_LOG_INFO("relief: off (relief and ground materials)");
}

void TerrainRelief::LogSummary()
{
    VF_LOG_INFO("relief: %u draws with relief in the last minute, %u farther than the relief reaches, %u terrain "
                "draws without the view position; %u textures with heights, %u known by type alone, %u without, %u "
                "changed since; %u height maps loaded",
                m_minute.drawsWithRelief, m_minute.drawsBeyondReach, m_minute.drawsWithoutViewPosition,
                m_stats.texturesWithHeights, m_stats.texturesTypeOnly, m_stats.texturesWithout, m_stats.texturesChanged,
                m_stats.heightMapsLoaded);
    VF_LOG_INFO("relief: %u building and model draws with relief in the last minute (%u of models, %u with heights "
                "made in the game, %u waiting for theirs), %u whose texture has no heights, %u with another kind of "
                "vertex shader",
                m_minute.drawsWithModelRelief, m_minute.modelDrawsOfModels, m_minute.drawsWithRuntimeHeights,
                m_minute.drawsWaitingForHeights, m_minute.modelDrawsWithoutHeights,
                m_minute.modelDrawsOtherVertexShader);
    const RuntimeHeightStats& r = m_runtime.Stats();
    if (r.requested || r.refused)
        VF_LOG_INFO("relief: heights made in the game: %u asked, %u made (%u cut out), %u failed, %u refused, %u "
                    "evicted, %zu kept; %.1f ms each on their own thread",
                    r.requested, r.made, r.cutouts, r.failed, r.refused, r.evicted, m_runtime.Cached(),
                    r.made ? r.makeMilliseconds / r.made : 0.0);
    if (m_materialsOn)
        VF_LOG_INFO("ground materials: %u terrain draws and %u building and model draws wrote them in the last "
                    "minute, %u left out while blending, %u into another target; %u textures with a material",
                    m_minute.drawsWithMaterial, m_minute.drawsWithModelMaterial, m_minute.drawsMaterialBlended,
                    m_minute.drawsMaterialOtherTarget, m_stats.texturesWithMaterial);
    AddStats(m_stats, m_minute);
    m_minute = {};
}

TerrainReliefStats TerrainRelief::Totals() const
{
    TerrainReliefStats totals = m_stats;
    AddStats(totals, m_minute);
    return totals;
}

void TerrainRelief::ForgetTextures()
{
    for (auto& [index, texture] : m_heights)
        if (texture)
            texture->Release();
    m_heights.clear();
    m_textures.clear();
}

void TerrainRelief::Frame(IDirect3DDevice9* real, const Config& cfg, bool fogActive)
{
    ++m_frame;
    LeaveMaterialTargets(real);
    if (GlobalReliefCatalog().Generation() != m_catalogGeneration ||
        GlobalMaterialCatalog().Generation() != m_materialGeneration)
    {
        ForgetTextures();
        m_catalogGeneration = GlobalReliefCatalog().Generation();
        m_materialGeneration = GlobalMaterialCatalog().Generation();
    }
    if (m_materialWritten && m_materialParamsSurface && m_materialTagsSurface)
    {
        real->ColorFill(m_materialParamsSurface, nullptr, 0);
        real->ColorFill(m_materialTagsSurface, nullptr, 0);
    }
    m_materialWritten = false;
    const bool reliefOn = fogActive && cfg.relief && GlobalReliefCatalog().Loaded();
    const bool materialsOn =
        fogActive && TerrainMaterialsWanted(cfg) && MaterialCapsMet(real) && EnsureMaterialTargets(real);
    if (m_watching && (reliefOn != m_reliefOn || materialsOn != m_materialsOn))
        VF_LOG_INFO("relief: relief %s, ground materials %s", reliefOn ? "on" : "off", materialsOn ? "on" : "off");
    m_reliefOn = reliefOn;
    m_materialsOn = materialsOn;
    const bool wanted = (reliefOn || materialsOn) && (!m_pixel.empty() || !m_model.empty());
    if (wanted && !m_watching)
        Enable(real);
    else if (!wanted && m_watching)
        Disable(real);
    m_samplersSet = false;
    if (reliefOn && cfg.reliefModels)
        m_runtime.Frame(real, m_frame);
    const unsigned long long now = GetTickCount64();
    if (!m_watching)
        return;
    if (!m_lastSummary)
        m_lastSummary = now;
    if (now - m_lastSummary >= kSummaryMilliseconds)
    {
        LogSummary();
        m_lastSummary = now;
    }
}

void TerrainRelief::ReleaseDefaultPool()
{
    m_materialBound = false;
    ReleaseMaterialTargets();
    m_materialFailed = false;
}

void TerrainRelief::ReleaseAll()
{
    for (auto& [shader, e] : m_pixel)
    {
        SafeRelease(e.patched);
        SafeRelease(e.material);
        SafeRelease(e.both);
    }
    m_pixel.clear();
    for (auto& [shader, e] : m_model)
        SafeRelease(e.patched);
    m_model.clear();
    m_modelVs.clear();
    ForgetTextures();
    m_runtime.ReleaseAll();
    if (m_flat)
        m_flat->Release();
    m_flat = nullptr;
    m_materialBound = false;
    ReleaseMaterialTargets();
    m_viewPositionVs.clear();
    if (m_watching && --g_watchingDevices == 0)
        g_callWatch &= ~kWatchRelief;
    m_watching = false;
    m_clientPs = m_realPs = nullptr;
    m_clientVs = nullptr;
}

void PressReliefKey()
{
    ConfigStore& store = GlobalConfig();
    const bool after = !store.Get().relief;
    const bool saved = store.SaveRelief(after);
    VF_LOG_INFO("relief: %s pressed, %s%s", HotkeyName(store.Get().reliefKey).c_str(), after ? "on" : "off",
                saved ? ", saved to the ini" : "");
}
