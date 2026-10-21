#include "client_shadows.h"

#include "call_watch.h"
#include "config.h"
#include "draw_log.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
struct KnownShader
{
    unsigned long long hash;
    ClientShaderRole role;
};

const KnownShader kClientShadowShaders[] = {
    {0xea99ea307a197a84ull, ClientShaderRole::DirectCaster},
    {0x202b452e580a7742ull, ClientShaderRole::PaletteCaster},
    {0x8535271f53b044b4ull, ClientShaderRole::PaletteCaster},
    {0x78641e03a615f46aull, ClientShaderRole::CasterPixel},
    {0x4f20b99325ed9c47ull, ClientShaderRole::CasterPixel},
    {0x86734e450fc6656eull, ClientShaderRole::CasterPixel},
};

constexpr UINT kCasterProjectionRows = 4;
constexpr UINT kCameraToLightRows = 3;
constexpr unsigned kModelRowsFirst = kModelReceiverRegister;
constexpr unsigned kModelRowsLast = kModelReceiverRegister + kClientShadowRows - 1;
constexpr unsigned kTerrainRowsFirst = kTerrainReceiverRegister;
constexpr unsigned kTerrainRowsLast = kTerrainReceiverRegister + kClientShadowRows - 1;
constexpr DWORD kOpcodeMask = 0x0000FFFFu;
constexpr DWORD kCommentOpcode = 0xFFFEu;
constexpr DWORD kEndOpcode = 0xFFFFu;
constexpr DWORD kCommentSizeMask = 0x7FFF0000u;
constexpr unsigned kCommentSizeShift = 16;
constexpr DWORD kInstructionLengthMask = 0x0F000000u;
constexpr unsigned kInstructionLengthShift = 24;
constexpr DWORD kRegisterTypeMask = 0x70000000u;
constexpr unsigned kRegisterTypeShift = 28;
constexpr DWORD kRegisterTypeMask2 = 0x00001800u;
constexpr unsigned kRegisterTypeShift2 = 8;
constexpr DWORD kRegisterNumberMask = 0x000007FFu;
constexpr DWORD kConstRegisterType = 2;
constexpr DWORD kDclOpcode = 31;
constexpr DWORD kDefbOpcode = 47;
constexpr DWORD kDefiOpcode = 48;
constexpr DWORD kDefOpcode = 81;
constexpr unsigned kFirstLengthCodedMajor = 2;
constexpr float kCasterViewDepthBias = 2.0e-4f;
constexpr float kReceiverYSign = 1.0f;
constexpr float kCasterYSign = -1.0f;
constexpr float kOrthographicTolerance = 1.0e-4f;
constexpr double kDegreesPerRadian = 57.29577951308232;

std::vector<KnownShader> g_extraRoles;
unsigned g_rolesGeneration = 1;
int g_enabledTrackers = 0;
ClientShadowStats g_stats = {};
ClientShadowFrame g_lastFrame = {};
std::string g_lastLogLine;

void Query()
{
    ++g_stats.deviceQueries;
}

bool IsCaster(ClientShaderRole role)
{
    return role == ClientShaderRole::DirectCaster || role == ClientShaderRole::PaletteCaster;
}

float RowLength(const float* row)
{
    return std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
}

std::string Vector4Text(const float* v)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%.6g %.6g %.6g %.6g)", v[0], v[1], v[2], v[3]);
    return buf;
}

std::string HashText(unsigned long long hash)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", hash);
    return buf;
}

std::string FormatText(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_D16:
        return "D16";
    case D3DFMT_D24X8:
        return "D24X8";
    case D3DFMT_D24S8:
        return "D24S8";
    case D3DFMT_D32:
        return "D32";
    case D3DFMT_D24FS8:
        return "D24FS8";
    case D3DFMT_D32F_LOCKABLE:
        return "D32F_LOCKABLE";
    default:
        break;
    }
    const unsigned v = static_cast<unsigned>(format);
    char fourcc[5] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                      static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF), 0};
    if (std::all_of(fourcc, fourcc + 4, [](char c) { return c >= '0' && c <= 'z'; }))
        return fourcc;
    return std::to_string(v);
}

bool Orthographic(const float m[4][4])
{
    return std::fabs(m[3][0]) < kOrthographicTolerance && std::fabs(m[3][1]) < kOrthographicTolerance &&
           std::fabs(m[3][2]) < kOrthographicTolerance && std::fabs(m[3][3] - 1.0f) < kOrthographicTolerance;
}

std::string MapText(const ClientShadowFrame& frame, int index, const float* toLightInView)
{
    const ClientShadowMap& m = frame.maps[index];
    char buf[512];
    if (!m.casterDraws)
    {
        std::snprintf(buf, sizeof(buf), "m%d %ux%u %s: not drawn in this frame, bound for receivers", index, m.size,
                      m.size, FormatText(m.format).c_str());
        return buf;
    }
    int used = std::snprintf(buf, sizeof(buf), "m%d %ux%u %s: %u casters (%u palette, %u direct", index, m.size, m.size,
                             FormatText(m.format).c_str(), m.casterDraws, m.paletteDraws, m.directDraws);
    if (m.hasPalette)
        used += std::snprintf(buf + used, sizeof(buf) - used, ", vs %s", HashText(m.paletteVs).c_str());
    used += std::snprintf(buf + used, sizeof(buf) - used, ")");
    if (!m.hasPalette)
    {
        std::snprintf(buf + used, sizeof(buf) - used, ", no camera-to-light matrix");
        return buf;
    }
    const float(&l)[4][4] = m.lightViewProjection;
    const float width = 2.0f / std::max(RowLength(l[0]), 1e-12f);
    const float height = 2.0f / std::max(RowLength(l[1]), 1e-12f);
    const float depth = 1.0f / std::max(RowLength(l[2]), 1e-12f);
    std::snprintf(buf + used, sizeof(buf) - used,
                  ", %s %.0fx%.0f yd, %.0f yd deep, camera at (%.2f %.2f %.2f), light %.1f deg from the fog's",
                  Orthographic(l) ? "orthographic" : "not orthographic", width, height, depth, l[0][3], l[1][3],
                  l[2][3], ClientShadowLightAngleDegrees(l, toLightInView));
    return buf;
}

void CompareWithCasters(const ClientShadowFrame& frame, const ClientShadowReceiver& r, float& same, float& flipped,
                        int& compared)
{
    for (int k = 0; k < kClientShadowPairs; ++k)
    {
        const int map = r.stageMaps[k];
        if (map < 0 || !frame.maps[map].hasPalette)
            continue;
        ++compared;
        const float(&l)[4][4] = frame.maps[map].lightViewProjection;
        for (int row = 0; row < 3; ++row)
            for (int c = 0; c < 4; ++c)
            {
                const float got = r.rows[3 * k + row][c];
                same = std::max(same, std::fabs(got - l[row][c]));
                flipped = std::max(flipped, std::fabs(got - (row == 1 ? -l[row][c] : l[row][c])));
            }
    }
}

std::string ReceiverText(const ClientShadowFrame& frame, const char* kind, DWORD firstStage, int map,
                         const ClientShadowReceiver& r)
{
    std::string maps;
    for (int k = 0; k < kClientShadowPairs; ++k)
        maps += (k ? " " : "") + (r.stageMaps[k] >= 0 ? "m" + std::to_string(r.stageMaps[k]) : std::string("-"));
    float same = 0.0f;
    float flipped = 0.0f;
    int compared = 0;
    CompareWithCasters(frame, r, same, flipped, compared);
    char buf[384];
    int used = std::snprintf(buf, sizeof(buf), "%s vs %s with m%d on s%lu (s%lu-s%lu: %s), %u draws", kind,
                             HashText(r.vs).c_str(), map, firstStage, firstStage,
                             firstStage + kClientShadowPairs - 1, maps.c_str(), r.draws);
    if (!compared)
        std::snprintf(buf + used, sizeof(buf) - used, ", no caster matrix to compare");
    else
        std::snprintf(buf + used, sizeof(buf) - used, ", rows match the casters' to %.2g %s", std::min(same, flipped),
                      flipped < same ? "with y flipped" : "with the same y");
    return buf;
}

void LogMatrixRows(const char* what, int index, const float* rows, int count)
{
    if (!LogEnabled(LogLevel::Debug))
        return;
    std::string text;
    for (int r = 0; r < count; ++r)
        text += " " + Vector4Text(rows + 4 * r);
    VF_LOG_DEBUG("client shadow maps:   %s %d:%s", what, index, text.c_str());
}
}

ClientShaderRole KnownClientShaderRole(unsigned long long hash)
{
    for (const KnownShader& s : g_extraRoles)
        if (s.hash == hash)
            return s.role;
    for (const KnownShader& s : kClientShadowShaders)
        if (s.hash == hash)
            return s.role;
    return ClientShaderRole::None;
}

void SetExtraClientShaderRole(unsigned long long hash, ClientShaderRole role)
{
    g_extraRoles.erase(std::remove_if(g_extraRoles.begin(), g_extraRoles.end(),
                                      [hash](const KnownShader& s) { return s.hash == hash; }),
                       g_extraRoles.end());
    if (role != ClientShaderRole::None)
        g_extraRoles.push_back({hash, role});
    ++g_rolesGeneration;
}

unsigned ClientShaderConstantReads(const DWORD* code, size_t tokens)
{
    unsigned reads = 0;
    if (!code || tokens < 1 || D3DSHADER_VERSION_MAJOR(code[0]) < kFirstLengthCodedMajor)
        return 0;
    size_t i = 1;
    while (i < tokens)
    {
        const DWORD token = code[i];
        const DWORD opcode = token & kOpcodeMask;
        if (opcode == kEndOpcode)
            break;
        if (opcode == kCommentOpcode)
        {
            i += 1 + ((token & kCommentSizeMask) >> kCommentSizeShift);
            continue;
        }
        const size_t length = (token & kInstructionLengthMask) >> kInstructionLengthShift;
        const bool declaration =
            opcode == kDclOpcode || opcode == kDefOpcode || opcode == kDefiOpcode || opcode == kDefbOpcode;
        for (size_t p = i + 1; !declaration && p <= i + length && p < tokens; ++p)
        {
            const DWORD param = code[p];
            const DWORD type = ((param & kRegisterTypeMask) >> kRegisterTypeShift) |
                               ((param & kRegisterTypeMask2) >> kRegisterTypeShift2);
            if (type != kConstRegisterType)
                continue;
            const DWORD reg = param & kRegisterNumberMask;
            if (reg >= kModelRowsFirst && reg <= kModelRowsLast)
                reads |= kReadsModelReceiverRows;
            if (reg >= kTerrainRowsFirst && reg <= kTerrainRowsLast)
                reads |= kReadsTerrainReceiverRows;
        }
        i += 1 + length;
    }
    return reads;
}

void MultiplyCasterMatrices(const float projection[4][4], const float cameraToLight[3][4], float out[4][4])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
        {
            float sum = projection[r][3] * (c == 3 ? 1.0f : 0.0f);
            for (int k = 0; k < 3; ++k)
                sum += projection[r][k] * cameraToLight[k][c];
            out[r][c] = sum;
        }
}

float ClientShadowLightAngleDegrees(const float lightViewProjection[4][4], const float* toLightInView)
{
    const float* z = lightViewProjection[2];
    const float length = RowLength(z);
    const float toLightLength = std::sqrt(toLightInView[0] * toLightInView[0] + toLightInView[1] * toLightInView[1] +
                                          toLightInView[2] * toLightInView[2]);
    if (length <= 0.0f || toLightLength <= 0.0f)
        return 180.0f;
    const float cosine = -(z[0] * toLightInView[0] + z[1] * toLightInView[1] + z[2] * toLightInView[2]) /
                         (length * toLightLength);
    return static_cast<float>(std::acos(std::clamp(static_cast<double>(cosine), -1.0, 1.0)) * kDegreesPerRadian);
}

ClientShadowPairs ClientShadowDebugPairs(const ClientShadowFrame* frame, int debugView)
{
    ClientShadowPairs pairs = {};
    pairs.source = "no maps";
    for (int& map : pairs.maps)
        map = -1;
    if (!frame || frame->mapCount <= 0)
        return pairs;
    auto addPair = [&](const float* rows, IDirect3DTexture9* texture, int map, float ySign, float bias) {
        const int k = pairs.count++;
        std::memcpy(pairs.constants[k], rows, sizeof(float) * 12);
        pairs.constants[k][3][0] = ySign;
        pairs.constants[k][3][1] = bias;
        pairs.textures[k] = texture;
        pairs.maps[k] = map;
    };
    if (debugView == kDebugViewClientShadowCasters)
    {
        int order[kClientShadowMaxMaps];
        int n = 0;
        for (int i = 0; i < frame->mapCount; ++i)
            if (frame->maps[i].hasPalette)
                order[n++] = i;
        std::stable_sort(order, order + n, [frame](int a, int b) {
            return RowLength(frame->maps[a].lightViewProjection[0]) > RowLength(frame->maps[b].lightViewProjection[0]);
        });
        for (int i = 0; i < n && pairs.count < kClientShadowPairs; ++i)
            addPair(&frame->maps[order[i]].lightViewProjection[0][0], frame->maps[order[i]].texture, order[i],
                    kCasterYSign, kCasterViewDepthBias);
        pairs.source = "the casters' matrices";
        return pairs;
    }
    const ClientShadowReceiver* chosen = nullptr;
    for (int i = 0; i < frame->mapCount; ++i)
        if (frame->terrain[i].draws && (!chosen || frame->terrain[i].draws > chosen->draws))
            chosen = &frame->terrain[i];
    pairs.source = "terrain receivers' matrices";
    if (!chosen)
    {
        for (int i = 0; i < frame->mapCount; ++i)
            if (frame->models[i].draws && (!chosen || frame->models[i].draws > chosen->draws))
                chosen = &frame->models[i];
        pairs.source = "model receivers' matrices";
    }
    if (!chosen)
    {
        pairs.source = "no receivers";
        return pairs;
    }
    for (int k = 0; k < kClientShadowPairs; ++k)
        if (chosen->stageMaps[k] >= 0)
            addPair(&chosen->rows[3 * k][0], frame->maps[chosen->stageMaps[k]].texture, chosen->stageMaps[k],
                    kReceiverYSign, 0.0f);
    return pairs;
}

void LogClientShadowFrame(const ClientShadowFrame& frame, const float* toLightInView)
{
    std::string line;
    if (frame.mapCount <= 0)
    {
        line = "client shadow maps: none in this frame (" + std::to_string(frame.casterDrawsElsewhere) +
               " caster draws into other targets, " + std::to_string(frame.receiverDrawsWithoutMap) +
               " receiver draws without a map); at extShadowQuality 0 the client draws none";
        ++g_stats.noneLogged;
    }
    else
    {
        line = "client shadow maps: " + std::to_string(frame.mapCount) + " in this frame";
        for (int i = 0; i < frame.mapCount; ++i)
            line += "; " + MapText(frame, i, toLightInView);
        line += "; receivers:";
        bool any = false;
        for (int i = 0; i < frame.mapCount; ++i)
        {
            if (frame.terrain[i].draws)
            {
                line += (any ? "; " : " ") + ReceiverText(frame, "terrain", kTerrainReceiverFirstStage, i,
                                                           frame.terrain[i]);
                any = true;
            }
            if (frame.models[i].draws)
            {
                line +=
                    (any ? "; " : " ") + ReceiverText(frame, "models", kModelReceiverFirstStage, i, frame.models[i]);
                any = true;
            }
        }
        if (!any)
            line += " none";
        line += "; " + std::to_string(frame.casterDrawsElsewhere) + " caster draws into other targets, " +
                std::to_string(frame.receiverDrawsWithoutMap) + " receiver draws without a map";
        ++g_stats.foundLogged;
    }
    VF_LOG_INFO("%s", line.c_str());
    g_lastLogLine = line;
    for (int i = 0; i < frame.mapCount; ++i)
    {
        const ClientShadowMap& m = frame.maps[i];
        if (m.hasPalette)
        {
            LogMatrixRows("caster projection c2-5 of m", i, &m.projection[0][0], 4);
            LogMatrixRows("caster camera-to-light c14-16 of m", i, &m.cameraToLight[0][0], 3);
            LogMatrixRows("their product for m", i, &m.lightViewProjection[0][0], 4);
        }
        if (m.hasDirect)
            LogMatrixRows("direct caster c2-5 of m", i, &m.directProjection[0][0], 4);
        if (frame.terrain[i].draws)
            LogMatrixRows("terrain receiver c37-48 with m", i, &frame.terrain[i].rows[0][0], kClientShadowRows);
        if (frame.models[i].draws)
            LogMatrixRows("model receiver c224-235 with m", i, &frame.models[i].rows[0][0], kClientShadowRows);
    }
}

void NoteClientShadowDebugPass()
{
    ++g_stats.debugPasses;
}

ClientShadowStats ClientShadowStatistics()
{
    return g_stats;
}

void ResetClientShadowStatistics()
{
    const unsigned held = g_stats.heldTextures;
    g_stats = {};
    g_stats.heldTextures = held;
}

const ClientShadowFrame& LastClientShadowFrame()
{
    return g_lastFrame;
}

const std::string& LastClientShadowLogLine()
{
    return g_lastLogLine;
}

ClientShadowTracker::~ClientShadowTracker()
{
    if (m_enabled)
        Disable();
    ReleaseFrame();
}

void ClientShadowTracker::Present(IDirect3DDevice9* real, bool wanted)
{
    if (m_enabled)
        EndFrame();
    if (wanted && !m_enabled)
        Enable(real);
    else if (!wanted && m_enabled)
        Disable();
    if (m_enabled && m_rolesGeneration != g_rolesGeneration)
    {
        m_shaders.clear();
        m_vsStale = true;
        m_psStale = true;
        m_rolesGeneration = g_rolesGeneration;
    }
}

void ClientShadowTracker::ReleaseAll()
{
    ReleaseFrame();
    m_vs = nullptr;
    m_ps = nullptr;
    m_vsStale = true;
    m_psStale = true;
    m_depth = nullptr;
    m_depthResolved = false;
    std::fill(std::begin(m_stages), std::end(m_stages), nullptr);
}

void ClientShadowTracker::ShaderCreated(const void* shader)
{
    if (!m_enabled)
        return;
    m_shaders.erase(shader);
    if (shader == m_vs)
        m_vsStale = true;
    if (shader == m_ps)
        m_psStale = true;
}

void ClientShadowTracker::Enable(IDirect3DDevice9* real)
{
    m_enabled = true;
    if (g_enabledTrackers++ == 0)
        g_callWatch |= kWatchClientShadows;
    m_shaders.clear();
    m_rolesGeneration = g_rolesGeneration;
    ReleaseAll();
    IDirect3DVertexShader9* vs = nullptr;
    Query();
    if (SUCCEEDED(real->GetVertexShader(&vs)) && vs)
    {
        m_vs = vs;
        vs->Release();
    }
    IDirect3DPixelShader9* ps = nullptr;
    Query();
    if (SUCCEEDED(real->GetPixelShader(&ps)) && ps)
    {
        m_ps = ps;
        ps->Release();
    }
    IDirect3DSurface9* depth = nullptr;
    Query();
    if (SUCCEEDED(real->GetDepthStencilSurface(&depth)) && depth)
    {
        m_depth = depth;
        depth->Release();
    }
    for (DWORD i = 0; i < kClientShadowStages; ++i)
    {
        IDirect3DBaseTexture9* texture = nullptr;
        Query();
        if (SUCCEEDED(real->GetTexture(kClientShadowFirstStage + i, &texture)) && texture)
        {
            m_stages[i] = texture;
            texture->Release();
        }
    }
    VF_LOG_INFO("client shadow maps: watching the client's shadow-map draws");
}

void ClientShadowTracker::Disable()
{
    m_enabled = false;
    if (--g_enabledTrackers == 0)
        g_callWatch &= ~kWatchClientShadows;
    ReleaseFrame();
    m_shaders.clear();
    VF_LOG_INFO("client shadow maps: no longer watched");
}

void ClientShadowTracker::EndFrame()
{
    ++g_stats.framesWatched;
    if (m_frame.mapCount > 0)
        ++g_stats.framesWithMaps;
    g_lastFrame = m_frame;
    ReleaseFrame();
}

void ClientShadowTracker::ReleaseFrame()
{
    for (int i = 0; i < m_frame.mapCount; ++i)
        if (m_frame.maps[i].texture)
        {
            m_frame.maps[i].texture->Release();
            --g_stats.heldTextures;
        }
    m_frame = {};
    m_depthResolved = false;
    m_depthMap = -1;
    m_rejectedCount = 0;
}

ClientShadowTracker::Shader ClientShadowTracker::Classify(IUnknown* shader, bool pixel)
{
    if (!shader)
        return {};
    const auto known = m_shaders.find(shader);
    if (known != m_shaders.end())
        return known->second;
    ++g_stats.shadersClassified;
    UINT size = 0;
    std::vector<DWORD> code;
    Query();
    const HRESULT sized = pixel ? static_cast<IDirect3DPixelShader9*>(shader)->GetFunction(nullptr, &size)
                                : static_cast<IDirect3DVertexShader9*>(shader)->GetFunction(nullptr, &size);
    if (SUCCEEDED(sized) && size >= sizeof(DWORD))
    {
        code.resize(size / sizeof(DWORD));
        Query();
        const HRESULT read = pixel ? static_cast<IDirect3DPixelShader9*>(shader)->GetFunction(code.data(), &size)
                                   : static_cast<IDirect3DVertexShader9*>(shader)->GetFunction(code.data(), &size);
        if (FAILED(read))
            code.clear();
    }
    Shader s = {BytecodeHash(code.data(), code.size() * sizeof(DWORD)), ClientShaderRole::None, 0};
    const ClientShaderRole role = KnownClientShaderRole(s.hash);
    if ((pixel && role == ClientShaderRole::CasterPixel) || (!pixel && IsCaster(role)))
        s.role = role;
    if (!pixel && s.role == ClientShaderRole::None)
        s.reads = ClientShaderConstantReads(code.data(), code.size());
    m_shaders[shader] = s;
    return s;
}

int ClientShadowTracker::MapIndex(const IDirect3DBaseTexture9* texture) const
{
    if (!texture)
        return -1;
    for (int i = 0; i < m_frame.mapCount; ++i)
        if (static_cast<const IDirect3DBaseTexture9*>(m_frame.maps[i].texture) == texture)
            return i;
    return -1;
}

void ClientShadowTracker::ResolveDepth()
{
    m_depthResolved = true;
    m_depthMap = -1;
    if (!m_depth)
        return;
    D3DSURFACE_DESC desc = {};
    Query();
    if (FAILED(m_depth->GetDesc(&desc)) || desc.Width != desc.Height || !(desc.Usage & D3DUSAGE_DEPTHSTENCIL))
        return;
    IDirect3DTexture9* texture = nullptr;
    Query();
    if (FAILED(m_depth->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))) || !texture)
        return;
    const int known = MapIndex(texture);
    if (known >= 0 || m_frame.mapCount == kClientShadowMaxMaps)
    {
        texture->Release();
        m_depthMap = known;
        return;
    }
    ClientShadowMap& m = m_frame.maps[m_frame.mapCount];
    m = {};
    m.texture = texture;
    m.size = desc.Width;
    m.format = desc.Format;
    ++g_stats.heldTextures;
    m_depthMap = m_frame.mapCount++;
}

void ClientShadowTracker::CasterDraw(IDirect3DDevice9* real)
{
    if (!m_depthResolved)
        ResolveDepth();
    if (m_depthMap < 0)
    {
        ++m_frame.casterDrawsElsewhere;
        return;
    }
    ClientShadowMap& m = m_frame.maps[m_depthMap];
    ++m.casterDraws;
    if (m_vsInfo.role == ClientShaderRole::PaletteCaster)
    {
        ++m.paletteDraws;
        if (m.hasPalette)
            return;
        Query();
        Query();
        if (SUCCEEDED(real->GetVertexShaderConstantF(kCasterProjectionRegister, &m.projection[0][0],
                                                     kCasterProjectionRows)) &&
            SUCCEEDED(real->GetVertexShaderConstantF(kCasterCameraToLightRegister, &m.cameraToLight[0][0],
                                                     kCameraToLightRows)))
        {
            MultiplyCasterMatrices(m.projection, m.cameraToLight, m.lightViewProjection);
            m.hasPalette = true;
            m.paletteVs = m_vsInfo.hash;
        }
    }
    else if (m_vsInfo.role == ClientShaderRole::DirectCaster)
    {
        ++m.directDraws;
        if (m.hasDirect)
            return;
        Query();
        m.hasDirect = SUCCEEDED(real->GetVertexShaderConstantF(kCasterProjectionRegister, &m.directProjection[0][0],
                                                               kCasterProjectionRows));
    }
}

bool ClientShadowTracker::Rejected(const IDirect3DBaseTexture9* texture) const
{
    return std::find(m_rejected, m_rejected + m_rejectedCount, texture) != m_rejected + m_rejectedCount;
}

int ClientShadowTracker::BoundMap(IDirect3DDevice9* real, DWORD stage)
{
    if (stage - kClientShadowFirstStage >= kClientShadowStages)
        return -1;
    const IDirect3DBaseTexture9* bound = m_stages[stage - kClientShadowFirstStage];
    const int known = MapIndex(bound);
    if (known >= 0 || !bound || Rejected(bound) || m_frame.mapCount == kClientShadowMaxMaps)
        return known;
    IDirect3DBaseTexture9* base = nullptr;
    IDirect3DTexture9* texture = nullptr;
    D3DSURFACE_DESC desc = {};
    Query();
    if (SUCCEEDED(real->GetTexture(stage, &base)) && base)
    {
        Query();
        if (base == bound && base->GetType() == D3DRTYPE_TEXTURE &&
            FAILED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))))
            texture = nullptr;
        base->Release();
    }
    Query();
    if (!texture || FAILED(texture->GetLevelDesc(0, &desc)) || desc.Width != desc.Height ||
        !(desc.Usage & D3DUSAGE_DEPTHSTENCIL))
    {
        if (texture)
            texture->Release();
        if (m_rejectedCount < kClientShadowMaxMaps)
            m_rejected[m_rejectedCount++] = bound;
        return -1;
    }
    ClientShadowMap& m = m_frame.maps[m_frame.mapCount];
    m = {};
    m.texture = texture;
    m.size = desc.Width;
    m.format = desc.Format;
    ++g_stats.heldTextures;
    return m_frame.mapCount++;
}

void ClientShadowTracker::ReceiverDraw(IDirect3DDevice9* real, bool model)
{
    const DWORD firstStage = model ? kModelReceiverFirstStage : kTerrainReceiverFirstStage;
    const int map = BoundMap(real, firstStage);
    if (map < 0)
    {
        ++m_frame.receiverDrawsWithoutMap;
        return;
    }
    ClientShadowReceiver& r = model ? m_frame.models[map] : m_frame.terrain[map];
    if (r.draws++)
        return;
    r.vs = m_vsInfo.hash;
    Query();
    if (FAILED(real->GetVertexShaderConstantF(model ? kModelReceiverRegister : kTerrainReceiverRegister, &r.rows[0][0],
                                              kClientShadowRows)))
        std::memset(r.rows, 0, sizeof(r.rows));
    for (int k = 0; k < kClientShadowPairs; ++k)
        r.stageMaps[k] = BoundMap(real, firstStage + k);
}

void ClientShadowTracker::Draw(IDirect3DDevice9* real)
{
    if (!m_enabled)
        return;
    if (m_vsStale)
    {
        m_vsInfo = Classify(m_vs, false);
        m_vsStale = false;
    }
    if (m_psStale)
    {
        m_psInfo = Classify(m_ps, true);
        m_psStale = false;
    }
    if (IsCaster(m_vsInfo.role) || m_psInfo.role == ClientShaderRole::CasterPixel)
    {
        CasterDraw(real);
        return;
    }
    if (m_vsInfo.reads & kReadsModelReceiverRows)
        ReceiverDraw(real, true);
    else if (m_vsInfo.reads & kReadsTerrainReceiverRows)
        ReceiverDraw(real, false);
}
