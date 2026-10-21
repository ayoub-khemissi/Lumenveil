#include "sky_filter.h"

#include "call_watch.h"
#include "config.h"
#include "log.h"
#include "relief_data.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <iterator>

namespace
{
constexpr unsigned long long kSummaryMilliseconds = 60000;

constexpr SkyTexture kSkyTextures[] = {
    {0x7bdcaf47a9264375ull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "orgri_moon01"},
    {0x43ca92d470882cd9ull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "orgri_moonflare01"},
    {0xc9d3ffecb02bab8bull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "arathi_moon"},
    {0x1386c54564d4c1e6ull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "stormwind_moon"},
    {0x0ec17d83bdbe235full, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "lazur_moon01"},
    {0x6d744d294f1835d7ull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "neksys_moon"},
    {0x337547a5050c7440ull, relieffile::kFormatDxt5, 64, 64, SkyKind::SkyboxMoon, "Shadowmoon__blue_moon01"},
    {0xcf59ab69908a1b00ull, relieffile::kFormatDxt1, 64, 64, SkyKind::SkyboxMoon, "frostwinddesert_moon_overlay01"},
    {0x046a6e9eaa645fbaull, relieffile::kFormatDxt3, 64, 64, SkyKind::GameMoon, "the game's moon"},
    {0xe017f2296ac0a92cull, relieffile::kFormatDxt3, 64, 64, SkyKind::GameSun, "the game's sun"},
    {0xd72f796d495cdb0bull, relieffile::kFormatDxt5, 64, 64, SkyKind::MoonHidingDome, "valleyeternal_sky01_night"},
};

SkyTexture g_testTextures[kSkyKindCount] = {};
std::atomic<bool> g_hasTest[kSkyKindCount];
int g_watchingDevices = 0;

const char* const kTestNames[kSkyKindCount] = {"", "the harness's skybox moon", "the harness's game moon",
                                               "the harness's dome", "the harness's game sun"};

const D3DRENDERSTATETYPE kOverTheSkyStates[] = {
    D3DRS_ZENABLE,          D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,     D3DRS_DESTBLEND,
};

constexpr size_t kOverTheSkyStateCount = std::size(kOverTheSkyStates);

DWORD SavedState(const DWORD* saved, D3DRENDERSTATETYPE state)
{
    for (size_t i = 0; i < kOverTheSkyStateCount; ++i)
        if (kOverTheSkyStates[i] == state)
            return saved[i];
    return 0;
}

HRESULT Reissue(IDirect3DDevice9* real, const SkyCall& c)
{
    switch (c.kind)
    {
    case SkyCallKind::Primitive:
        return real->DrawPrimitive(c.type, c.start, c.count);
    case SkyCallKind::IndexedPrimitive:
        return real->DrawIndexedPrimitive(c.type, c.base, c.minIndex, c.vertices, c.start, c.count);
    case SkyCallKind::PrimitiveUp:
        return real->DrawPrimitiveUP(c.type, c.count, c.data, c.stride);
    case SkyCallKind::IndexedPrimitiveUp:
        return real->DrawIndexedPrimitiveUP(c.type, c.minIndex, c.vertices, c.count, c.indices, c.indexFormat, c.data,
                                            c.stride);
    }
    return E_FAIL;
}

bool Matches(const SkyTexture& t, uint64_t hash, uint32_t format, uint32_t width, uint32_t height)
{
    return t.hash == hash && t.format == format && t.width == width && t.height == height;
}

bool SameSize(const SkyTexture& t, uint32_t format, uint32_t width, uint32_t height)
{
    return t.format == format && t.width == width && t.height == height;
}

unsigned CountOf(SkyKind kind)
{
    unsigned n = 0;
    for (const SkyTexture& t : kSkyTextures)
        n += t.kind == kind ? 1u : 0u;
    return n;
}

UINT VertexCountOf(D3DPRIMITIVETYPE type, UINT primitives)
{
    switch (type)
    {
    case D3DPT_POINTLIST:
        return primitives;
    case D3DPT_LINELIST:
        return 2 * primitives;
    case D3DPT_LINESTRIP:
        return primitives + 1;
    case D3DPT_TRIANGLELIST:
        return 3 * primitives;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return primitives + 2;
    default:
        return 0;
    }
}

bool OnlyStreamZero(IDirect3DDevice9* real)
{
    IDirect3DVertexDeclaration9* decl = nullptr;
    if (FAILED(real->GetVertexDeclaration(&decl)) || !decl)
        return false;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
    UINT count = MAXD3DDECLLENGTH + 1;
    const bool read = SUCCEEDED(decl->GetDeclaration(elements, &count));
    decl->Release();
    if (!read)
        return false;
    for (UINT i = 0; i < count && elements[i].Stream != 0xFF; ++i)
        if (elements[i].Stream != 0)
            return false;
    return true;
}

DWORD ReadFlags(DWORD usage)
{
    return (usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE : D3DLOCK_READONLY;
}

bool ReadVertices(IDirect3DVertexBuffer9* vb, UINT offset, UINT bytes, std::vector<uint8_t>& out)
{
    D3DVERTEXBUFFER_DESC desc = {};
    if (FAILED(vb->GetDesc(&desc)) || offset + bytes > desc.Size)
        return false;
    void* p = nullptr;
    if (FAILED(vb->Lock(offset, bytes, &p, ReadFlags(desc.Usage))) || !p)
        return false;
    out.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + bytes);
    vb->Unlock();
    return true;
}

bool ReadIndices(IDirect3DIndexBuffer9* ib, UINT start, UINT count, std::vector<uint32_t>& out)
{
    D3DINDEXBUFFER_DESC desc = {};
    if (FAILED(ib->GetDesc(&desc)))
        return false;
    const UINT size = desc.Format == D3DFMT_INDEX32 ? 4u : 2u;
    if ((start + count) * size > desc.Size)
        return false;
    void* p = nullptr;
    if (FAILED(ib->Lock(start * size, count * size, &p, ReadFlags(desc.Usage))) || !p)
        return false;
    out.resize(count);
    for (UINT i = 0; i < count; ++i)
        out[i] = size == 4 ? static_cast<const uint32_t*>(p)[i] : static_cast<const uint16_t*>(p)[i];
    ib->Unlock();
    return true;
}
}

const SkyTexture* FindSkyTexture(uint64_t hash, uint32_t format, uint32_t width, uint32_t height)
{
    for (const SkyTexture& t : kSkyTextures)
        if (Matches(t, hash, format, width, height))
            return &t;
    for (int k = 1; k < kSkyKindCount; ++k)
        if (g_hasTest[k] && Matches(g_testTextures[k], hash, format, width, height))
            return &g_testTextures[k];
    return nullptr;
}

bool SkyTextureSize(uint32_t format, uint32_t width, uint32_t height)
{
    for (const SkyTexture& t : kSkyTextures)
        if (SameSize(t, format, width, height))
            return true;
    for (int k = 1; k < kSkyKindCount; ++k)
        if (g_hasTest[k] && SameSize(g_testTextures[k], format, width, height))
            return true;
    return false;
}

void SetTestSkyTexture(SkyKind kind, const SkyTexture* texture)
{
    const int k = static_cast<int>(kind);
    if (k <= 0 || k >= kSkyKindCount)
        return;
    g_hasTest[k] = false;
    if (!texture)
        return;
    g_testTextures[k] = *texture;
    g_testTextures[k].kind = kind;
    g_testTextures[k].name = kTestNames[k];
    g_hasTest[k] = true;
}

void SkyFilter::TextureCreated(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return;
    m_textures.erase(texture);
    if (texture == m_bound)
        m_resolved = false;
}

const SkyTexture* SkyFilter::Identify(IDirect3DBaseTexture9* texture)
{
    if (texture->GetType() != D3DRTYPE_TEXTURE)
        return nullptr;
    auto* tex = static_cast<IDirect3DTexture9*>(texture);
    D3DSURFACE_DESC top = {};
    if (FAILED(tex->GetLevelDesc(0, &top)))
        return nullptr;
    const uint32_t format = static_cast<uint32_t>(top.Format);
    if (!relieffile::RowBytes(format, 4))
        return nullptr;
    const uint32_t level = relieffile::HashedLevel(top.Width, top.Height, tex->GetLevelCount());
    D3DSURFACE_DESC desc = {};
    if (FAILED(tex->GetLevelDesc(level, &desc)) || !SkyTextureSize(format, desc.Width, desc.Height))
        return nullptr;
    D3DLOCKED_RECT rect = {};
    if (FAILED(tex->LockRect(level, &rect, nullptr, D3DLOCK_READONLY)) || !rect.pBits)
        return nullptr;
    const uint32_t rowBytes = relieffile::RowBytes(format, desc.Width);
    const uint32_t rows = relieffile::RowCount(format, desc.Height);
    uint64_t hash = relieffile::Fnv1a(nullptr, 0);
    for (uint32_t r = 0; r < rows; ++r)
        hash = relieffile::Fnv1a(static_cast<const uint8_t*>(rect.pBits) + static_cast<size_t>(r) * rect.Pitch,
                                 rowBytes, hash);
    tex->UnlockRect(level);
    return FindSkyTexture(hash, format, desc.Width, desc.Height);
}

const SkyTexture* SkyFilter::Lookup(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return nullptr;
    auto it = m_textures.find(texture);
    if (it != m_textures.end() && m_frame - it->second.checkedFrame < kSkyRecheckFrames)
        return it->second.sky;
    const SkyTexture* sky = Identify(texture);
    const bool fresh = it == m_textures.end();
    if (fresh)
        ++m_stats.texturesSeen;
    else if (sky != it->second.sky)
        ++m_stats.texturesChanged;
    if (fresh && sky)
        ++m_stats.moonTextures;
    if (sky && (fresh || sky != it->second.sky))
    {
        const char* refilled = fresh ? "" : " in a refilled texture";
        switch (sky->kind)
        {
        case SkyKind::SkyboxMoon:
            VF_LOG_INFO("sky: %s from the skybox recognised%s; its draws are left out", sky->name, refilled);
            break;
        case SkyKind::GameMoon:
        case SkyKind::GameSun:
            VF_LOG_INFO("sky: %s recognised%s", sky->name, refilled);
            break;
        case SkyKind::MoonHidingDome:
            VF_LOG_INFO("sky: %s recognised%s; it hides the game's moon, which is drawn again over it", sky->name,
                        refilled);
            break;
        default:
            break;
        }
    }
    m_textures[texture] = {sky, m_frame};
    return sky;
}

SkyKind SkyFilter::BoundKind()
{
    if (!m_resolved)
    {
        const SkyTexture* sky = Lookup(m_bound);
        m_boundKind = sky ? sky->kind : SkyKind::None;
        m_resolved = true;
    }
    return m_boundKind;
}

bool SkyFilter::CopyIndexed(IDirect3DDevice9* real, const SkyCall& call)
{
    const UINT indexCount = VertexCountOf(call.type, call.count);
    if (!indexCount || indexCount > kMaxSkyMoonIndices || !call.vertices || call.vertices > kMaxSkyMoonVertices)
        return false;
    std::vector<uint32_t> indices;
    if (call.kind == SkyCallKind::IndexedPrimitiveUp)
    {
        if (!call.data || !call.indices || !call.stride)
            return false;
        indices.resize(indexCount);
        for (UINT i = 0; i < indexCount; ++i)
            indices[i] = call.indexFormat == D3DFMT_INDEX32 ? static_cast<const uint32_t*>(call.indices)[i]
                                                            : static_cast<const uint16_t*>(call.indices)[i];
        m_moonStride = call.stride;
        const auto* first = static_cast<const uint8_t*>(call.data) + static_cast<size_t>(call.minIndex) * call.stride;
        m_moonVertices.assign(first, first + static_cast<size_t>(call.vertices) * call.stride);
    }
    else
    {
        IDirect3DVertexBuffer9* vb = nullptr;
        UINT offset = 0;
        UINT stride = 0;
        IDirect3DIndexBuffer9* ib = nullptr;
        const bool bound = SUCCEEDED(real->GetStreamSource(0, &vb, &offset, &stride)) && vb && stride &&
                           SUCCEEDED(real->GetIndices(&ib)) && ib;
        const INT first = call.base + static_cast<INT>(call.minIndex);
        const bool read = bound && first >= 0 && ReadIndices(ib, call.start, indexCount, indices) &&
                          ReadVertices(vb, offset + static_cast<UINT>(first) * stride, call.vertices * stride,
                                       m_moonVertices);
        if (vb)
            vb->Release();
        if (ib)
            ib->Release();
        if (!read)
            return false;
        m_moonStride = stride;
    }
    m_moonIndices.resize(indexCount);
    for (UINT i = 0; i < indexCount; ++i)
    {
        if (indices[i] < call.minIndex || indices[i] - call.minIndex >= call.vertices)
            return false;
        m_moonIndices[i] = static_cast<uint16_t>(indices[i] - call.minIndex);
    }
    return true;
}

bool SkyFilter::CopyListed(IDirect3DDevice9* real, const SkyCall& call)
{
    const UINT vertexCount = VertexCountOf(call.type, call.count);
    if (!vertexCount || vertexCount > kMaxSkyMoonVertices)
        return false;
    m_moonIndices.clear();
    if (call.kind == SkyCallKind::PrimitiveUp)
    {
        if (!call.data || !call.stride)
            return false;
        m_moonStride = call.stride;
        const auto* first = static_cast<const uint8_t*>(call.data);
        m_moonVertices.assign(first, first + static_cast<size_t>(vertexCount) * call.stride);
        return true;
    }
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT offset = 0;
    UINT stride = 0;
    const bool read = SUCCEEDED(real->GetStreamSource(0, &vb, &offset, &stride)) && vb && stride &&
                      ReadVertices(vb, offset + call.start * stride, vertexCount * stride, m_moonVertices);
    if (vb)
        vb->Release();
    m_moonStride = stride;
    return read;
}

bool SkyFilter::CopyMoon(IDirect3DDevice9* real, const SkyCall& call)
{
    if (!OnlyStreamZero(real))
        return false;
    const bool indexed = call.kind == SkyCallKind::IndexedPrimitive || call.kind == SkyCallKind::IndexedPrimitiveUp;
    if (!(indexed ? CopyIndexed(real, call) : CopyListed(real, call)))
        return false;
    m_moonType = call.type;
    m_moonPrimitives = call.count;
    if (m_moonState)
        return SUCCEEDED(m_moonState->Capture());
    return SUCCEEDED(real->CreateStateBlock(D3DSBT_ALL, &m_moonState)) && m_moonState;
}

bool SkyFilter::EnsureOverTarget(IDirect3DDevice9* real, UINT width, UINT height)
{
    if (m_over && m_overWidth == width && m_overHeight == height)
        return true;
    if (m_overFailed && m_overWidth == width && m_overHeight == height)
        return false;
    ReleaseOverTarget();
    m_overWidth = width;
    m_overHeight = height;
    const bool made = SUCCEEDED(real->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                                    D3DPOOL_DEFAULT, &m_over, nullptr)) &&
                      m_over && SUCCEEDED(m_over->GetSurfaceLevel(0, &m_overSurface)) && m_overSurface;
    if (!made)
    {
        ReleaseOverTarget();
        m_overFailed = true;
        VF_LOG_ERROR("sky: no %ux%u target for the game's sun and moon; the client draws them", width, height);
        return false;
    }
    VF_LOG_INFO("sky: the game's sun and moon are drawn into a %ux%u target of their own and laid over the sky "
                "after the skybox and the haze",
                width, height);
    return true;
}

void SkyFilter::ReleaseOverTarget()
{
    if (m_overSurface)
    {
        m_overSurface->Release();
        m_overSurface = nullptr;
    }
    if (m_over)
    {
        m_over->Release();
        m_over = nullptr;
    }
    m_overDrawn = false;
    m_overFailed = false;
}

bool SkyFilter::DrawOverTheSky(IDirect3DDevice9* real, const SkyCall& call)
{
    IDirect3DSurface9* client = nullptr;
    if (FAILED(real->GetRenderTarget(0, &client)) || !client)
        return false;
    D3DSURFACE_DESC desc = {};
    if (FAILED(client->GetDesc(&desc)) || !EnsureOverTarget(real, desc.Width, desc.Height))
    {
        client->Release();
        ++m_minute.overTheSkyMissed;
        return false;
    }
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    real->GetViewport(&viewport);
    real->GetScissorRect(&scissor);
    DWORD saved[kOverTheSkyStateCount] = {};
    for (size_t i = 0; i < kOverTheSkyStateCount; ++i)
        real->GetRenderState(kOverTheSkyStates[i], &saved[i]);
    real->SetRenderTarget(0, m_overSurface);
    if (!m_overDrawn)
        real->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    real->SetViewport(&viewport);
    real->SetScissorRect(&scissor);
    real->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    real->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    real->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    real->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                                     D3DCOLORWRITEENABLE_BLUE);
    const bool blended = SavedState(saved, D3DRS_ALPHABLENDENABLE) != FALSE;
    if (!blended)
    {
        real->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        real->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        real->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
    }
    const HRESULT hr = Reissue(real, call);
    real->SetRenderTarget(0, client);
    client->Release();
    real->SetViewport(&viewport);
    real->SetScissorRect(&scissor);
    for (size_t i = 0; i < kOverTheSkyStateCount; ++i)
        real->SetRenderState(kOverTheSkyStates[i], saved[i]);
    if (FAILED(hr))
    {
        ++m_minute.overTheSkyMissed;
        return false;
    }
    m_overDrawn = true;
    ++m_minute.drawnOverTheSky;
    return true;
}

bool SkyFilter::Draw(IDirect3DDevice9* real, const SkyCall& call)
{
    switch (BoundKind())
    {
    case SkyKind::SkyboxMoon:
        if (!m_hideSkyboxMoons)
            return true;
        ++m_minute.drawsHidden;
        return false;
    case SkyKind::GameSun:
        return !(m_takeOver && DrawOverTheSky(real, call));
    case SkyKind::GameMoon:
        if (m_takeOver && DrawOverTheSky(real, call))
            return false;
        if (m_hideSkyboxMoons && m_domeSeen && m_frame - m_domeFrame <= kSkyDomeMemoryFrames && !m_moonCopied)
            m_moonCopied = CopyMoon(real, call);
        return true;
    case SkyKind::MoonHidingDome:
        m_domeSeen = true;
        m_domeFrame = m_frame;
        m_domeAfterMoon = m_domeAfterMoon || m_moonCopied;
        return true;
    default:
        return true;
    }
}

void SkyFilter::RedrawMoon(IDirect3DDevice9* real)
{
    const bool wanted = m_watching && m_moonCopied && m_domeAfterMoon && m_moonState;
    m_moonCopied = false;
    m_domeAfterMoon = false;
    if (!wanted)
        return;
    const bool saved = m_savedState ? SUCCEEDED(m_savedState->Capture())
                                    : SUCCEEDED(real->CreateStateBlock(D3DSBT_ALL, &m_savedState)) && m_savedState;
    if (!saved)
    {
        ++m_minute.moonsMissed;
        return;
    }
    m_moonState->Apply();
    const UINT vertexCount = m_moonStride ? static_cast<UINT>(m_moonVertices.size() / m_moonStride) : 0;
    const HRESULT hr =
        m_moonIndices.empty()
            ? real->DrawPrimitiveUP(m_moonType, m_moonPrimitives, m_moonVertices.data(), m_moonStride)
            : real->DrawIndexedPrimitiveUP(m_moonType, 0, vertexCount, m_moonPrimitives, m_moonIndices.data(),
                                           D3DFMT_INDEX16, m_moonVertices.data(), m_moonStride);
    m_savedState->Apply();
    if (SUCCEEDED(hr))
        ++m_minute.moonsRedrawn;
    else
        ++m_minute.moonsMissed;
}

void SkyFilter::Enable(IDirect3DDevice9* real)
{
    m_watching = true;
    if (g_watchingDevices++ == 0)
        g_callWatch |= kWatchSky;
    IDirect3DBaseTexture9* bound = nullptr;
    if (SUCCEEDED(real->GetTexture(0, &bound)) && bound)
        bound->Release();
    m_bound = bound;
    m_resolved = false;
}

void SkyFilter::Disable()
{
    m_watching = false;
    if (--g_watchingDevices == 0)
        g_callWatch &= ~kWatchSky;
    m_bound = nullptr;
    m_resolved = false;
    m_moonCopied = false;
    m_domeAfterMoon = false;
    m_domeSeen = false;
}

void SkyFilter::LogSummary()
{
    VF_LOG_INFO("sky: in the last minute %u skybox moon draws left out, the game's moon drawn again over the skybox "
                "%u times (%u missed), the game's sun or moon drawn over the sky %u times (%u missed); %u of %u "
                "textures seen are on the sky list, %u changed since",
                m_minute.drawsHidden, m_minute.moonsRedrawn, m_minute.moonsMissed, m_minute.drawnOverTheSky,
                m_minute.overTheSkyMissed, m_stats.moonTextures, m_stats.texturesSeen, m_stats.texturesChanged);
    m_stats.drawsHidden += m_minute.drawsHidden;
    m_stats.moonsRedrawn += m_minute.moonsRedrawn;
    m_stats.moonsMissed += m_minute.moonsMissed;
    m_stats.drawnOverTheSky += m_minute.drawnOverTheSky;
    m_stats.overTheSkyMissed += m_minute.overTheSkyMissed;
    m_minute = {};
}

SkyFilterStats SkyFilter::Totals() const
{
    SkyFilterStats totals = m_stats;
    totals.drawsHidden += m_minute.drawsHidden;
    totals.moonsRedrawn += m_minute.moonsRedrawn;
    totals.moonsMissed += m_minute.moonsMissed;
    totals.drawnOverTheSky += m_minute.drawnOverTheSky;
    totals.overTheSkyMissed += m_minute.overTheSkyMissed;
    return totals;
}

void SkyFilter::Frame(IDirect3DDevice9* real, const Config& cfg, bool fogActive)
{
    ++m_frame;
    m_resolved = false;
    if (m_moonCopied && m_domeAfterMoon)
        ++m_minute.moonsMissed;
    m_moonCopied = false;
    m_domeAfterMoon = false;
    m_overDrawn = false;
    const bool takeOver = fogActive && m_composed && m_composedShown;
    m_composed = false;
    m_composedShown = false;
    const bool hide = fogActive && !cfg.skyboxMoons;
    if (hide != m_hideSkyboxMoons)
    {
        if (hide)
            VF_LOG_INFO("sky: the skyboxes' own moons are left out (%u known textures), and the game's moon is drawn "
                        "again over a skybox that hides it (%u known)",
                        CountOf(SkyKind::SkyboxMoon), CountOf(SkyKind::MoonHidingDome));
        else
            VF_LOG_INFO("sky: the skyboxes' own moons are drawn");
    }
    if (takeOver != m_takeOver)
    {
        if (takeOver)
            VF_LOG_DEBUG("sky: the game's sun and moon are drawn over the sky, after the skybox and the haze");
        else
            VF_LOG_DEBUG("sky: the game's sun and moon are left to the client");
    }
    m_hideSkyboxMoons = hide;
    m_takeOver = takeOver;
    const bool wanted = hide || takeOver;
    if (wanted && !m_watching)
        Enable(real);
    else if (!wanted && m_watching)
        Disable();
    if (!m_watching)
        return;
    const unsigned long long now = GetTickCount64();
    if (!m_lastSummary)
        m_lastSummary = now;
    if (now - m_lastSummary >= kSummaryMilliseconds)
    {
        LogSummary();
        m_lastSummary = now;
    }
}

void SkyFilter::ReleaseDeviceObjects()
{
    if (m_moonState)
    {
        m_moonState->Release();
        m_moonState = nullptr;
    }
    if (m_savedState)
    {
        m_savedState->Release();
        m_savedState = nullptr;
    }
    ReleaseOverTarget();
    m_moonCopied = false;
    m_domeAfterMoon = false;
    m_takeOver = false;
    m_composed = false;
    m_composedShown = false;
}

void SkyFilter::ReleaseAll()
{
    ReleaseDeviceObjects();
    m_textures.clear();
    if (m_watching && --g_watchingDevices == 0)
        g_callWatch &= ~kWatchSky;
    m_watching = false;
    m_hideSkyboxMoons = false;
    m_bound = nullptr;
    m_resolved = false;
    m_domeSeen = false;
}
