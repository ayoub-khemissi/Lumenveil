#pragma once

#include <d3d9.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct Config;

constexpr unsigned kSkyRecheckFrames = 120;
constexpr unsigned kSkyDomeMemoryFrames = 30;
constexpr UINT kMaxSkyMoonVertices = 64;
constexpr UINT kMaxSkyMoonIndices = 96;

enum class SkyKind : int
{
    None = 0,
    SkyboxMoon = 1,
    GameMoon = 2,
    MoonHidingDome = 3,
    GameSun = 4,
};

constexpr int kSkyKindCount = 5;

struct SkyTexture
{
    uint64_t hash;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    SkyKind kind;
    const char* name;
};

struct SkyFilterStats
{
    unsigned texturesSeen;
    unsigned moonTextures;
    unsigned texturesChanged;
    unsigned drawsHidden;
    unsigned moonsRedrawn;
    unsigned moonsMissed;
    unsigned drawnOverTheSky;
    unsigned overTheSkyMissed;
};

enum class SkyCallKind
{
    Primitive,
    IndexedPrimitive,
    PrimitiveUp,
    IndexedPrimitiveUp,
};

struct SkyCall
{
    SkyCallKind kind;
    D3DPRIMITIVETYPE type;
    UINT count;
    INT base;
    UINT minIndex;
    UINT vertices;
    UINT start;
    const void* data;
    const void* indices;
    D3DFORMAT indexFormat;
    UINT stride;
};

const SkyTexture* FindSkyTexture(uint64_t hash, uint32_t format, uint32_t width, uint32_t height);
bool SkyTextureSize(uint32_t format, uint32_t width, uint32_t height);
void SetTestSkyTexture(SkyKind kind, const SkyTexture* texture);

class SkyFilter
{
public:
    void Texture(DWORD stage, IDirect3DBaseTexture9* texture)
    {
        if (stage != 0)
            return;
        m_bound = texture;
        m_resolved = false;
    }
    void TextureCreated(IDirect3DBaseTexture9* texture);
    bool Draw(IDirect3DDevice9* real, const SkyCall& call);
    void RedrawMoon(IDirect3DDevice9* real);
    void Frame(IDirect3DDevice9* real, const Config& cfg, bool fogActive);
    void Composed(bool shown)
    {
        m_composed = true;
        m_composedShown = shown;
    }
    IDirect3DTexture9* DrawnOverTheSky() const { return m_overDrawn ? m_over : nullptr; }
    bool TakingOver() const { return m_takeOver; }
    void ReleaseDeviceObjects();
    void ReleaseAll();
    bool Watching() const { return m_watching; }
    SkyFilterStats Totals() const;

private:
    struct KnownTexture
    {
        const SkyTexture* sky;
        unsigned checkedFrame;
    };

    const SkyTexture* Identify(IDirect3DBaseTexture9* texture);
    const SkyTexture* Lookup(IDirect3DBaseTexture9* texture);
    SkyKind BoundKind();
    bool CopyMoon(IDirect3DDevice9* real, const SkyCall& call);
    bool CopyIndexed(IDirect3DDevice9* real, const SkyCall& call);
    bool CopyListed(IDirect3DDevice9* real, const SkyCall& call);
    bool EnsureOverTarget(IDirect3DDevice9* real, UINT width, UINT height);
    void ReleaseOverTarget();
    bool DrawOverTheSky(IDirect3DDevice9* real, const SkyCall& call);
    void Enable(IDirect3DDevice9* real);
    void Disable();
    void LogSummary();

    std::unordered_map<IDirect3DBaseTexture9*, KnownTexture> m_textures;
    IDirect3DBaseTexture9* m_bound = nullptr;
    bool m_resolved = false;
    SkyKind m_boundKind = SkyKind::None;
    bool m_watching = false;
    bool m_hideSkyboxMoons = false;
    bool m_takeOver = false;
    bool m_composed = false;
    bool m_composedShown = false;
    IDirect3DTexture9* m_over = nullptr;
    IDirect3DSurface9* m_overSurface = nullptr;
    UINT m_overWidth = 0;
    UINT m_overHeight = 0;
    bool m_overFailed = false;
    bool m_overDrawn = false;
    unsigned m_frame = 0;
    unsigned m_domeFrame = 0;
    bool m_domeSeen = false;
    bool m_moonCopied = false;
    bool m_domeAfterMoon = false;
    D3DPRIMITIVETYPE m_moonType = D3DPT_TRIANGLELIST;
    UINT m_moonPrimitives = 0;
    UINT m_moonStride = 0;
    std::vector<uint8_t> m_moonVertices;
    std::vector<uint16_t> m_moonIndices;
    IDirect3DStateBlock9* m_moonState = nullptr;
    IDirect3DStateBlock9* m_savedState = nullptr;
    unsigned long long m_lastSummary = 0;
    SkyFilterStats m_stats = {};
    SkyFilterStats m_minute = {};
};
