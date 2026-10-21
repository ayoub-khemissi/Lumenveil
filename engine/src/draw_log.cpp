#include "draw_log.h"

#include "config.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

unsigned g_callWatch = 0;

namespace
{
constexpr int kLoggedStreams = 4;
constexpr int kLoggedStages = 16;
constexpr size_t kReservedRows = 16384;
constexpr unsigned long long kFnvOffset = 14695981039346656037ull;
constexpr unsigned long long kFnvPrime = 1099511628211ull;
constexpr const char* kCsvName = "CoAVolFog-draws.csv";
constexpr const char* kSideFolderName = "CoAVolFog-draws";
constexpr UINT kValuesBeforeTheFrame = 0xFFFFFFFFu;
constexpr int kLoggedFloatDigits = 9;
constexpr double kMicrosecondsPerSecond = 1.0e6;
constexpr double kMillisecondsPerSecond = 1.0e3;
constexpr DWORD kShaderTypeMask = 0xFFFF0000u;
constexpr DWORD kPixelShaderToken = 0xFFFF0000u;
constexpr DWORD kVertexShaderToken = 0xFFFE0000u;
constexpr unsigned kTextureTransformBit = 3;
constexpr unsigned kFirstVertexSampler = D3DVERTEXTEXTURESAMPLER0;
constexpr DWORD kVertexSamplerCount = 4;
constexpr int kPhaseCount = static_cast<int>(DrawPhase::Count);

const char* const kPhaseNames[] = {"before-world", "world",   "opaque-m2",      "after-opaque-m2", "liquid",
                                   "after-liquid", "fog",     "screen-effects", "interface"};
static_assert(sizeof(kPhaseNames) / sizeof(kPhaseNames[0]) == kPhaseCount, "one name per draw phase");

const char* const kCallNames[] = {"DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP",
                                  "DrawIndexedPrimitiveUP", "Clear"};

struct ConstantRange
{
    UINT start;
    UINT count;
};

struct ConstantValue
{
    UINT row;
    unsigned short reg;
    bool pixel;
    float v[4];
};

struct Stream
{
    unsigned buffer;
    UINT offset;
    UINT stride;
};

struct Raster
{
    DWORD zEnable = D3DZB_TRUE;
    DWORD zWrite = TRUE;
    DWORD zFunc = D3DCMP_LESSEQUAL;
    DWORD blend = FALSE;
    DWORD srcBlend = D3DBLEND_ONE;
    DWORD destBlend = D3DBLEND_ZERO;
    DWORD alphaTest = FALSE;
    DWORD alphaRef = 0;
    DWORD cull = D3DCULL_CCW;
    DWORD colorWrite = 0xF;
    DWORD stencil = FALSE;
};

struct DrawState
{
    unsigned vs = 0;
    unsigned ps = 0;
    DWORD fvf = 0;
    unsigned declaration = 0;
    Stream streams[kLoggedStreams] = {};
    unsigned indices = 0;
    Raster raster;
    unsigned target = 0;
    unsigned depthTarget = 0;
    D3DVIEWPORT9 viewport = {};
    unsigned textures[kLoggedStages] = {};
    unsigned vertexTextures[kVertexSamplerCount] = {};
};

struct Row
{
    bool mark;
    DrawPhase phase;
    DrawCall call;
    DrawState state;
    size_t vsRangesBegin;
    size_t vsRangesCount;
    size_t psRangesBegin;
    size_t psRangesCount;
    unsigned transforms;
    unsigned stateCalls;
    long long gap;
    long long duration;
};

struct Resource
{
    char kind;
    const void* pointer;
    std::string description;
    unsigned long long hash;
};

struct Bytecode
{
    unsigned long long hash;
    bool pixel;
    std::vector<DWORD> code;
};

struct Capture
{
    std::string directory;
    bool armed = false;
    unsigned seenGeneration = 0;
    bool worldSeen = false;
    DrawPhase phase = DrawPhase::BeforeWorld;
    DrawState state;
    std::vector<ConstantRange> ranges;
    std::vector<ConstantRange> pendingVs;
    std::vector<ConstantRange> pendingPs;
    ConstantRanges vsValueRanges = {};
    ConstantRanges psValueRanges = {};
    std::vector<ConstantValue> values;
    unsigned pendingTransforms = 0;
    unsigned pendingStateCalls = 0;
    std::vector<Row> rows;
    std::unordered_map<const void*, unsigned> ids;
    std::vector<Resource> resources;
    std::vector<Bytecode> bytecode;
    std::unordered_set<std::string> writtenShaders;
    long long start = 0;
    long long lastReturn = 0;
    long long ownTicks = 0;
    long long phaseStart = 0;
    long long phaseTicks[kPhaseCount] = {};
    DrawLogStats stats = {};
    std::string summary;
};

Capture g_log;

long long Now()
{
    ++g_log.stats.timerReads;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double TicksPerSecond()
{
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return frequency;
}

double Microseconds(long long ticks)
{
    return static_cast<double>(ticks) * kMicrosecondsPerSecond / TicksPerSecond();
}

double Milliseconds(long long ticks)
{
    return static_cast<double>(ticks) * kMillisecondsPerSecond / TicksPerSecond();
}

void Queried()
{
    ++g_log.stats.d3dQueries;
}

template <typename T> void ReleaseQueried(T* object)
{
    if (object)
        object->Release();
}

std::string Hex(unsigned long long value, int digits)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%0*llx", digits, value);
    return buf;
}

std::string PointerText(const void* p)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(p)));
    return buf;
}

std::string FormatName(D3DFORMAT format)
{
    struct Named
    {
        D3DFORMAT format;
        const char* name;
    };
    static const Named kNames[] = {
        {D3DFMT_UNKNOWN, "UNKNOWN"},
        {D3DFMT_A8R8G8B8, "A8R8G8B8"},
        {D3DFMT_X8R8G8B8, "X8R8G8B8"},
        {D3DFMT_R5G6B5, "R5G6B5"},
        {D3DFMT_A1R5G5B5, "A1R5G5B5"},
        {D3DFMT_A4R4G4B4, "A4R4G4B4"},
        {D3DFMT_A8, "A8"},
        {D3DFMT_A2B10G10R10, "A2B10G10R10"},
        {D3DFMT_A8B8G8R8, "A8B8G8R8"},
        {D3DFMT_G16R16, "G16R16"},
        {D3DFMT_A2R10G10B10, "A2R10G10B10"},
        {D3DFMT_A16B16G16R16, "A16B16G16R16"},
        {D3DFMT_L8, "L8"},
        {D3DFMT_A8L8, "A8L8"},
        {D3DFMT_V8U8, "V8U8"},
        {D3DFMT_D16, "D16"},
        {D3DFMT_D24S8, "D24S8"},
        {D3DFMT_D24X8, "D24X8"},
        {D3DFMT_D32, "D32"},
        {D3DFMT_D32F_LOCKABLE, "D32F_LOCKABLE"},
        {D3DFMT_INDEX16, "INDEX16"},
        {D3DFMT_INDEX32, "INDEX32"},
        {D3DFMT_R16F, "R16F"},
        {D3DFMT_G16R16F, "G16R16F"},
        {D3DFMT_A16B16G16R16F, "A16B16G16R16F"},
        {D3DFMT_R32F, "R32F"},
        {D3DFMT_G32R32F, "G32R32F"},
        {D3DFMT_A32B32G32R32F, "A32B32G32R32F"},
    };
    for (const Named& n : kNames)
        if (n.format == format)
            return n.name;
    const unsigned v = static_cast<unsigned>(format);
    const char fourcc[5] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                            static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF), 0};
    if (std::all_of(fourcc, fourcc + 4, [](char c) { return c >= '0' && c <= 'z'; }))
        return fourcc;
    return std::to_string(v);
}

std::string UsageText(DWORD usage, D3DPOOL pool)
{
    std::string text;
    if (usage & D3DUSAGE_RENDERTARGET)
        text += " rendertarget";
    if (usage & D3DUSAGE_DEPTHSTENCIL)
        text += " depthstencil";
    if (usage & D3DUSAGE_DYNAMIC)
        text += " dynamic";
    if (usage & D3DUSAGE_WRITEONLY)
        text += " writeonly";
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        text += " autogenmipmap";
    static const char* const kPools[] = {"default", "managed", "systemmem", "scratch"};
    text += " ";
    text += static_cast<unsigned>(pool) < 4 ? kPools[pool] : std::to_string(pool).c_str();
    return text;
}

std::string SizeText(UINT w, UINT h)
{
    return std::to_string(w) + "x" + std::to_string(h);
}

unsigned Known(const void* p)
{
    const auto it = g_log.ids.find(p);
    return it == g_log.ids.end() ? 0 : it->second;
}

unsigned Register(const void* p, char kind, const std::string& description, unsigned long long hash)
{
    g_log.resources.push_back({kind, p, description, hash});
    const unsigned id = static_cast<unsigned>(g_log.resources.size());
    g_log.ids[p] = id;
    return id;
}

class OwnTime
{
public:
    OwnTime() : m_start(Now()) {}
    ~OwnTime() { g_log.ownTicks += Now() - m_start; }

private:
    long long m_start;
};

unsigned TextureId(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return 0;
    if (const unsigned id = Known(texture))
        return id;
    OwnTime own;
    std::string d = "texture";
    D3DSURFACE_DESC desc = {};
    Queried();
    switch (texture->GetType())
    {
    case D3DRTYPE_TEXTURE:
        Queried();
        static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &desc);
        d = "texture " + SizeText(desc.Width, desc.Height);
        break;
    case D3DRTYPE_CUBETEXTURE:
        Queried();
        static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0, &desc);
        d = "cube " + SizeText(desc.Width, desc.Height);
        break;
    case D3DRTYPE_VOLUMETEXTURE:
    {
        D3DVOLUME_DESC volume = {};
        Queried();
        static_cast<IDirect3DVolumeTexture9*>(texture)->GetLevelDesc(0, &volume);
        desc.Format = volume.Format;
        desc.Usage = volume.Usage;
        desc.Pool = volume.Pool;
        d = "volume " + SizeText(volume.Width, volume.Height) + "x" + std::to_string(volume.Depth);
        break;
    }
    default:
        break;
    }
    Queried();
    d += " " + FormatName(desc.Format) + UsageText(desc.Usage, desc.Pool) + " levels " +
         std::to_string(texture->GetLevelCount());
    return Register(texture, 't', d, 0);
}

unsigned SurfaceId(IDirect3DSurface9* surface)
{
    if (!surface)
        return 0;
    if (const unsigned id = Known(surface))
        return id;
    OwnTime own;
    D3DSURFACE_DESC desc = {};
    Queried();
    surface->GetDesc(&desc);
    std::string d = "surface " + SizeText(desc.Width, desc.Height) + " " + FormatName(desc.Format) +
                    UsageText(desc.Usage, desc.Pool);
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE)
        d += " msaa " + std::to_string(desc.MultiSampleType);
    IDirect3DTexture9* container = nullptr;
    Queried();
    if (SUCCEEDED(surface->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&container))) &&
        container)
    {
        d += " of t" + std::to_string(TextureId(container));
        container->Release();
    }
    return Register(surface, 's', d, 0);
}

unsigned VertexBufferId(IDirect3DVertexBuffer9* buffer)
{
    if (!buffer)
        return 0;
    if (const unsigned id = Known(buffer))
        return id;
    OwnTime own;
    D3DVERTEXBUFFER_DESC desc = {};
    Queried();
    buffer->GetDesc(&desc);
    return Register(buffer, 'b',
                    "vertex buffer " + std::to_string(desc.Size) + " bytes fvf 0x" + Hex(desc.FVF, 0) +
                        UsageText(desc.Usage, desc.Pool),
                    0);
}

unsigned IndexBufferId(IDirect3DIndexBuffer9* buffer)
{
    if (!buffer)
        return 0;
    if (const unsigned id = Known(buffer))
        return id;
    OwnTime own;
    D3DINDEXBUFFER_DESC desc = {};
    Queried();
    buffer->GetDesc(&desc);
    return Register(buffer, 'i',
                    "index buffer " + std::to_string(desc.Size) + " bytes " + FormatName(desc.Format) +
                        UsageText(desc.Usage, desc.Pool),
                    0);
}

const char* DeclarationTypeName(BYTE type)
{
    static const char* const kTypes[] = {"float1",   "float2",   "float3",    "float4",    "color",   "ubyte4",
                                         "short2",   "short4",   "ubyte4n",   "short2n",   "short4n", "ushort2n",
                                         "ushort4n", "udec3",    "dec3n",     "float16x2", "float16x4"};
    return type < sizeof(kTypes) / sizeof(kTypes[0]) ? kTypes[type] : "unused";
}

const char* DeclarationUsageName(BYTE usage)
{
    static const char* const kUsages[] = {"position", "blendweight", "blendindices", "normal",   "psize",
                                          "texcoord", "tangent",     "binormal",     "tessfactor", "positiont",
                                          "color",    "fog",         "depth",        "sample"};
    return usage < sizeof(kUsages) / sizeof(kUsages[0]) ? kUsages[usage] : "other";
}

unsigned DeclarationId(IDirect3DVertexDeclaration9* declaration)
{
    if (!declaration)
        return 0;
    if (const unsigned id = Known(declaration))
        return id;
    OwnTime own;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1] = {};
    UINT count = MAXD3DDECLLENGTH + 1;
    Queried();
    std::string d = "declaration";
    if (SUCCEEDED(declaration->GetDeclaration(elements, &count)))
        for (UINT i = 0; i < count && elements[i].Stream != 0xFF; ++i)
            d += " s" + std::to_string(elements[i].Stream) + "+" + std::to_string(elements[i].Offset) + ":" +
                 DeclarationTypeName(elements[i].Type) + ":" + DeclarationUsageName(elements[i].Usage) +
                 std::to_string(elements[i].UsageIndex);
    return Register(declaration, 'd', d, 0);
}

unsigned long long Fnv1aOf(const void* data, size_t size)
{
    unsigned long long h = kFnvOffset;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ bytes[i]) * kFnvPrime;
    return h;
}

std::string ShaderVersionText(DWORD token)
{
    const char* prefix = (token & kShaderTypeMask) == kPixelShaderToken ? "ps" : "vs";
    return std::string(prefix) + "_" + std::to_string(D3DSHADER_VERSION_MAJOR(token)) + "_" +
           std::to_string(D3DSHADER_VERSION_MINOR(token));
}

template <typename Shader> unsigned ShaderId(Shader* shader, bool pixel)
{
    if (!shader)
        return 0;
    if (const unsigned id = Known(shader))
        return id;
    OwnTime own;
    UINT size = 0;
    Queried();
    std::vector<DWORD> code;
    if (SUCCEEDED(shader->GetFunction(nullptr, &size)) && size >= sizeof(DWORD))
    {
        code.resize(size / sizeof(DWORD));
        Queried();
        if (FAILED(shader->GetFunction(code.data(), &size)))
            code.clear();
    }
    const unsigned long long hash = Fnv1aOf(code.data(), code.size() * sizeof(DWORD));
    std::string d = code.empty() ? std::string(pixel ? "pixel shader" : "vertex shader")
                                 : ShaderVersionText(code[0]) + " " + std::to_string(code.size() * sizeof(DWORD)) +
                                       " bytes";
    const bool known = std::any_of(g_log.bytecode.begin(), g_log.bytecode.end(),
                                   [&](const Bytecode& b) { return b.hash == hash && b.pixel == pixel; });
    if (!known && !code.empty())
        g_log.bytecode.push_back({hash, pixel, std::move(code)});
    return Register(shader, pixel ? 'p' : 'v', d, hash);
}

void TrackRenderState(Raster& r, D3DRENDERSTATETYPE state, DWORD value)
{
    switch (state)
    {
    case D3DRS_ZENABLE:
        r.zEnable = value;
        break;
    case D3DRS_ZWRITEENABLE:
        r.zWrite = value;
        break;
    case D3DRS_ZFUNC:
        r.zFunc = value;
        break;
    case D3DRS_ALPHABLENDENABLE:
        r.blend = value;
        break;
    case D3DRS_SRCBLEND:
        r.srcBlend = value;
        break;
    case D3DRS_DESTBLEND:
        r.destBlend = value;
        break;
    case D3DRS_ALPHATESTENABLE:
        r.alphaTest = value;
        break;
    case D3DRS_ALPHAREF:
        r.alphaRef = value;
        break;
    case D3DRS_CULLMODE:
        r.cull = value;
        break;
    case D3DRS_COLORWRITEENABLE:
        r.colorWrite = value;
        break;
    case D3DRS_STENCILENABLE:
        r.stencil = value;
        break;
    default:
        break;
    }
}

const D3DRENDERSTATETYPE kTrackedStates[] = {D3DRS_ZENABLE,         D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
                                              D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,     D3DRS_DESTBLEND,
                                              D3DRS_ALPHATESTENABLE,  D3DRS_ALPHAREF,     D3DRS_CULLMODE,
                                              D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE};

void ReadDeviceState(IDirect3DDevice9* dev)
{
    DrawState& s = g_log.state;
    s = DrawState();
    IDirect3DVertexShader9* vs = nullptr;
    Queried();
    dev->GetVertexShader(&vs);
    s.vs = ShaderId(vs, false);
    ReleaseQueried(vs);
    IDirect3DPixelShader9* ps = nullptr;
    Queried();
    dev->GetPixelShader(&ps);
    s.ps = ShaderId(ps, true);
    ReleaseQueried(ps);
    Queried();
    dev->GetFVF(&s.fvf);
    IDirect3DVertexDeclaration9* declaration = nullptr;
    Queried();
    dev->GetVertexDeclaration(&declaration);
    s.declaration = DeclarationId(declaration);
    ReleaseQueried(declaration);
    for (int i = 0; i < kLoggedStreams; ++i)
    {
        IDirect3DVertexBuffer9* buffer = nullptr;
        UINT offset = 0;
        UINT stride = 0;
        Queried();
        dev->GetStreamSource(i, &buffer, &offset, &stride);
        s.streams[i] = {VertexBufferId(buffer), offset, stride};
        ReleaseQueried(buffer);
    }
    IDirect3DIndexBuffer9* indices = nullptr;
    Queried();
    dev->GetIndices(&indices);
    s.indices = IndexBufferId(indices);
    ReleaseQueried(indices);
    for (D3DRENDERSTATETYPE state : kTrackedStates)
    {
        DWORD value = 0;
        Queried();
        if (SUCCEEDED(dev->GetRenderState(state, &value)))
            TrackRenderState(s.raster, state, value);
    }
    IDirect3DSurface9* target = nullptr;
    Queried();
    dev->GetRenderTarget(0, &target);
    s.target = SurfaceId(target);
    ReleaseQueried(target);
    IDirect3DSurface9* depth = nullptr;
    Queried();
    dev->GetDepthStencilSurface(&depth);
    s.depthTarget = SurfaceId(depth);
    ReleaseQueried(depth);
    for (int i = 0; i < kLoggedStages; ++i)
    {
        IDirect3DBaseTexture9* texture = nullptr;
        Queried();
        dev->GetTexture(i, &texture);
        s.textures[i] = TextureId(texture);
        ReleaseQueried(texture);
    }
    for (DWORD i = 0; i < kVertexSamplerCount; ++i)
    {
        IDirect3DBaseTexture9* texture = nullptr;
        Queried();
        dev->GetTexture(kFirstVertexSampler + i, &texture);
        s.vertexTextures[i] = TextureId(texture);
        ReleaseQueried(texture);
    }
    Queried();
    dev->GetViewport(&s.viewport);
}

void ClearCapture()
{
    g_log.rows.clear();
    g_log.rows.reserve(kReservedRows);
    g_log.ranges.clear();
    g_log.pendingVs.clear();
    g_log.pendingPs.clear();
    g_log.values.clear();
    g_log.pendingTransforms = 0;
    g_log.pendingStateCalls = 0;
    g_log.ids.clear();
    g_log.resources.clear();
    g_log.bytecode.clear();
    g_log.worldSeen = false;
    g_log.phase = DrawPhase::BeforeWorld;
    g_log.ownTicks = 0;
    std::fill(std::begin(g_log.phaseTicks), std::end(g_log.phaseTicks), 0);
}

void RecordConstantValues(bool pixel, UINT row, UINT start, const float* data, UINT count)
{
    const ConstantRanges& wanted = pixel ? g_log.psValueRanges : g_log.vsValueRanges;
    if (!count)
        return;
    for (int r = 0; r < wanted.count; ++r)
    {
        const UINT first = std::max<UINT>(start, wanted.first[r]);
        const UINT last = std::min<UINT>(start + count - 1, wanted.last[r]);
        for (UINT reg = first; reg <= last; ++reg)
        {
            ConstantValue v = {row, static_cast<unsigned short>(reg), pixel, {}};
            std::memcpy(v.v, data + (reg - start) * 4, sizeof(v.v));
            g_log.values.push_back(v);
        }
    }
}

void ReadDeviceConstants(IDirect3DDevice9* dev, bool pixel)
{
    const ConstantRanges& wanted = pixel ? g_log.psValueRanges : g_log.vsValueRanges;
    float values[4 * kVertexShaderConstantRegisters];
    for (int r = 0; r < wanted.count; ++r)
    {
        const UINT first = wanted.first[r];
        const UINT count = wanted.last[r] - first + 1u;
        Queried();
        const HRESULT hr = pixel ? dev->GetPixelShaderConstantF(first, values, count)
                                 : dev->GetVertexShaderConstantF(first, values, count);
        if (SUCCEEDED(hr))
            RecordConstantValues(pixel, kValuesBeforeTheFrame, first, values, count);
    }
}

void StartCapture(IDirect3DDevice9* real)
{
    ClearCapture();
    const Config& cfg = GlobalConfig().Get();
    g_log.vsValueRanges = cfg.drawLogVertexConstants;
    g_log.psValueRanges = cfg.drawLogPixelConstants;
    ReadDeviceState(real);
    ReadDeviceConstants(real, false);
    ReadDeviceConstants(real, true);
    g_log.start = Now();
    g_log.lastReturn = g_log.start;
    g_log.phaseStart = g_log.start;
    g_log.ownTicks = 0;
    g_callWatch |= kWatchDrawLog;
}

std::string RangesText(size_t begin, size_t count)
{
    if (!count)
        return std::string();
    std::vector<ConstantRange> r(g_log.ranges.begin() + begin, g_log.ranges.begin() + begin + count);
    std::sort(r.begin(), r.end(), [](const ConstantRange& a, const ConstantRange& b) { return a.start < b.start; });
    std::string text;
    UINT lo = r[0].start;
    UINT hi = r[0].start + r[0].count;
    auto flush = [&] {
        if (!text.empty())
            text += " ";
        text += "c" + std::to_string(lo);
        if (hi - lo > 1)
            text += "-" + std::to_string(hi - 1);
    };
    for (size_t i = 1; i < r.size(); ++i)
    {
        if (r[i].start <= hi)
        {
            hi = std::max(hi, r[i].start + r[i].count);
            continue;
        }
        flush();
        lo = r[i].start;
        hi = r[i].start + r[i].count;
    }
    flush();
    return text;
}

std::string TransformsText(unsigned mask)
{
    std::string text;
    auto add = [&](const std::string& name) {
        if (!text.empty())
            text += " ";
        text += name;
    };
    if (mask & 1u)
        add("world");
    if (mask & 2u)
        add("view");
    if (mask & 4u)
        add("projection");
    for (unsigned t = 0; t < 8; ++t)
        if (mask & (1u << (kTextureTransformBit + t)))
            add("texture" + std::to_string(t));
    return text;
}

const Resource* ResourceOf(unsigned id)
{
    return id && id <= g_log.resources.size() ? &g_log.resources[id - 1] : nullptr;
}

std::string ShaderPointer(unsigned id)
{
    const Resource* r = ResourceOf(id);
    return r ? PointerText(r->pointer) : "fixed-function";
}

std::string ShaderHash(unsigned id)
{
    const Resource* r = ResourceOf(id);
    return r ? Hex(r->hash, 16) : std::string();
}

std::string VertexFormatText(const DrawState& s)
{
    if (s.declaration)
        return "d" + std::to_string(s.declaration);
    return s.fvf ? "fvf 0x" + Hex(s.fvf, 0) : std::string();
}

std::string StreamsText(const Row& row)
{
    std::string text;
    const bool up = row.call.kind == DrawCallKind::PrimitiveUp || row.call.kind == DrawCallKind::IndexedPrimitiveUp;
    if (up)
        return "up/" + std::to_string(row.call.upStride);
    for (int i = 0; i < kLoggedStreams; ++i)
    {
        const Stream& st = row.state.streams[i];
        if (!st.buffer)
            continue;
        if (!text.empty())
            text += " ";
        text += std::to_string(i) + ":b" + std::to_string(st.buffer) + "+" + std::to_string(st.offset) + "/" +
                std::to_string(st.stride);
    }
    return text;
}

std::string TexturesText(const DrawState& s)
{
    std::string text;
    for (int i = 0; i < kLoggedStages; ++i)
        if (s.textures[i])
            text += (text.empty() ? "" : " ") + std::to_string(i) + ":t" + std::to_string(s.textures[i]);
    for (DWORD i = 0; i < kVertexSamplerCount; ++i)
        if (s.vertexTextures[i])
            text += (text.empty() ? "v" : " v") + std::to_string(i) + ":t" + std::to_string(s.vertexTextures[i]);
    return text;
}

std::string BlendText(const Raster& r)
{
    return r.blend ? std::to_string(r.srcBlend) + "/" + std::to_string(r.destBlend) : "off";
}

std::string AlphaTestText(const Raster& r)
{
    return r.alphaTest ? "ref " + std::to_string(r.alphaRef) : "off";
}

std::string IdText(char kind, unsigned id)
{
    return id ? std::string(1, kind) + std::to_string(id) : std::string();
}

std::string ViewportText(const D3DVIEWPORT9& v)
{
    return std::to_string(v.X) + " " + std::to_string(v.Y) + " " + std::to_string(v.Width) + " " +
           std::to_string(v.Height);
}

const char* kCsvHeader = "index,phase,call,primitive,primitives,vertices,start,base_vertex,vs,vs_hash,ps,ps_hash,"
                         "vs_constants,ps_constants,transforms,vertex_format,streams,indices,z,zwrite,zfunc,blend,"
                         "alpha_test,cull,color_write,stencil,target,depth_target,viewport,textures,state_calls,"
                         "gap_us,draw_us\n";

std::string RowText(size_t index, const Row& row)
{
    char times[64];
    std::snprintf(times, sizeof(times), "%.2f,%.2f", Microseconds(row.gap), Microseconds(row.duration));
    const std::string phase = kPhaseNames[static_cast<int>(row.phase)];
    if (row.mark)
        return std::to_string(index) + "," + phase + ",phase,,,,,,,,,,,,,,,,,,,,,,,,,,,,," + times + "\n";
    const DrawState& s = row.state;
    const Raster& r = s.raster;
    const bool clear = row.call.kind == DrawCallKind::Clear;
    std::string t = std::to_string(index) + "," + phase + "," + kCallNames[static_cast<int>(row.call.kind)] + "," +
                    (clear ? "flags 0x" + Hex(row.call.start, 0) : std::to_string(row.call.type)) + "," +
                    std::to_string(row.call.primitives) + "," +
                    std::to_string(row.call.vertices) + "," + std::to_string(row.call.start) + "," +
                    std::to_string(row.call.baseVertex) + ",";
    t += ShaderPointer(s.vs) + "," + ShaderHash(s.vs) + "," + ShaderPointer(s.ps) + "," + ShaderHash(s.ps) + ",";
    t += RangesText(row.vsRangesBegin, row.vsRangesCount) + "," + RangesText(row.psRangesBegin, row.psRangesCount) +
         "," + TransformsText(row.transforms) + "," + VertexFormatText(s) + "," + StreamsText(row) + "," +
         IdText('i', s.indices) + ",";
    t += std::to_string(r.zEnable) + "," + std::to_string(r.zWrite) + "," + std::to_string(r.zFunc) + "," +
         BlendText(r) + "," + AlphaTestText(r) + "," + std::to_string(r.cull) + "," + "0x" + Hex(r.colorWrite, 0) +
         "," + std::to_string(r.stencil) + ",";
    t += IdText('s', s.target) + "," + IdText('s', s.depthTarget) + "," + ViewportText(s.viewport) + "," +
         TexturesText(s) + "," + std::to_string(row.stateCalls) + "," + times + "\n";
    return t;
}

bool WriteText(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

std::string FloatText(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*g", kLoggedFloatDigits, v);
    return buf;
}

std::string ConstantValuesText()
{
    std::string text = "row,stage,register,x,y,z,w\n";
    text.reserve(g_log.values.size() * 64);
    for (const ConstantValue& v : g_log.values)
    {
        text += v.row == kValuesBeforeTheFrame ? std::string("start") : std::to_string(v.row);
        text += v.pixel ? ",ps,c" : ",vs,c";
        text += std::to_string(v.reg);
        for (float f : v.v)
            text += "," + FloatText(f);
        text += "\n";
    }
    return text;
}

bool WriteShaders(const std::string& folder)
{
    bool ok = true;
    for (const Bytecode& b : g_log.bytecode)
    {
        const std::string path = folder + "\\" + (b.pixel ? "ps_" : "vs_") + Hex(b.hash, 16) + ".bin";
        if (g_log.writtenShaders.count(path) && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            continue;
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
        {
            ok = false;
            continue;
        }
        const size_t bytes = b.code.size() * sizeof(DWORD);
        const bool written = std::fwrite(b.code.data(), 1, bytes, f) == bytes;
        ok = std::fclose(f) == 0 && written && ok;
        if (written)
        {
            g_log.writtenShaders.insert(path);
            ++g_log.stats.filesWritten;
        }
    }
    return ok;
}

struct PhaseTotals
{
    unsigned draws = 0;
    unsigned long long primitives = 0;
    long long drawTicks = 0;
    long long gapTicks = 0;
};

void Summarise(long long frameTicks, const std::string& csvPath)
{
    PhaseTotals totals[kPhaseCount];
    PhaseTotals all;
    for (const Row& row : g_log.rows)
    {
        if (row.mark || row.call.kind == DrawCallKind::Clear)
            continue;
        PhaseTotals& t = totals[static_cast<int>(row.phase)];
        ++t.draws;
        t.primitives += row.call.primitives;
        t.drawTicks += row.duration;
        t.gapTicks += row.gap;
        ++all.draws;
        all.primitives += row.call.primitives;
        all.drawTicks += row.duration;
    }
    char line[512];
    std::snprintf(line, sizeof(line),
                  "draw log: one frame written to %s: %u draws, %llu primitives, %u resources, %u shaders, %u "
                  "constant values; %.2f ms from Present to Present, %.2f ms inside the game's draw calls",
                  csvPath.c_str(), all.draws, all.primitives, static_cast<unsigned>(g_log.resources.size()),
                  static_cast<unsigned>(g_log.bytecode.size()), static_cast<unsigned>(g_log.values.size()),
                  Milliseconds(frameTicks),
                  Milliseconds(all.drawTicks));
    g_log.summary = line;
    VF_LOG_INFO("%s", line);
    for (int p = 0; p < kPhaseCount; ++p)
    {
        const PhaseTotals& t = totals[p];
        std::snprintf(line, sizeof(line),
                      "draw log:   %s: %u draws, %llu primitives, %.2f ms in draw calls, %.2f ms between them, "
                      "%.2f ms in the phase",
                      kPhaseNames[p], t.draws, t.primitives, Milliseconds(t.drawTicks), Milliseconds(t.gapTicks),
                      Milliseconds(g_log.phaseTicks[p]));
        g_log.summary += "\n";
        g_log.summary += line;
        VF_LOG_INFO("%s", line);
    }
}

void WriteCapture(long long frameTicks)
{
    const std::string csvPath = DrawLogCsvPath();
    const std::string folder = DrawLogSideFolder();
    std::string csv = kCsvHeader;
    csv.reserve(g_log.rows.size() * 256);
    for (size_t i = 0; i < g_log.rows.size(); ++i)
        csv += RowText(i, g_log.rows[i]);
    bool ok = WriteText(csvPath, csv);
    if (ok)
        ++g_log.stats.filesWritten;
    CreateDirectoryA(folder.c_str(), nullptr);
    std::string resources = "id,kind,pointer,hash,description\n";
    for (size_t i = 0; i < g_log.resources.size(); ++i)
    {
        const Resource& r = g_log.resources[i];
        resources += std::string(1, r.kind) + std::to_string(i + 1) + "," + std::string(1, r.kind) + "," +
                     PointerText(r.pointer) + "," + (r.hash ? Hex(r.hash, 16) : std::string()) + "," +
                     r.description + "\n";
    }
    const bool resourcesOk = WriteText(folder + "\\resources.csv", resources);
    if (resourcesOk)
        ++g_log.stats.filesWritten;
    const bool constantsOk = WriteText(folder + "\\constants.csv", ConstantValuesText());
    if (constantsOk)
        ++g_log.stats.filesWritten;
    ok = WriteShaders(folder) && resourcesOk && constantsOk && ok;
    if (!ok)
        VF_LOG_ERROR("draw log: could not write every file to %s and %s", csvPath.c_str(), folder.c_str());
    Summarise(frameTicks, csvPath);
}

void CloseRow(Row& row, long long begin, long long end)
{
    row.phase = g_log.phase;
    row.state = g_log.state;
    row.vsRangesBegin = g_log.ranges.size();
    row.vsRangesCount = g_log.pendingVs.size();
    g_log.ranges.insert(g_log.ranges.end(), g_log.pendingVs.begin(), g_log.pendingVs.end());
    row.psRangesBegin = g_log.ranges.size();
    row.psRangesCount = g_log.pendingPs.size();
    g_log.ranges.insert(g_log.ranges.end(), g_log.pendingPs.begin(), g_log.pendingPs.end());
    row.transforms = g_log.pendingTransforms;
    row.stateCalls = g_log.pendingStateCalls;
    row.gap = std::max(begin - g_log.lastReturn - g_log.ownTicks, 0ll);
    row.duration = end - begin;
    g_log.pendingVs.clear();
    g_log.pendingPs.clear();
    g_log.pendingTransforms = 0;
    g_log.pendingStateCalls = 0;
    g_log.ownTicks = 0;
}
}

void DrawLogSetDirectory(const std::string& directory)
{
    g_log.directory = directory;
    if (!g_log.directory.empty() && g_log.directory.back() != '\\' && g_log.directory.back() != '/')
        g_log.directory += "\\";
}

std::string DrawLogCsvPath()
{
    return g_log.directory + kCsvName;
}

std::string DrawLogSideFolder()
{
    return g_log.directory + kSideFolderName;
}

void DrawLogArm()
{
    if (!g_log.armed)
        VF_LOG_INFO("draw log: the next frame that draws the world is captured to %s", DrawLogCsvPath().c_str());
    g_log.armed = true;
}

void DrawLogCancel()
{
    if (!(g_callWatch & kWatchDrawLog))
        return;
    g_callWatch &= ~kWatchDrawLog;
    ClearCapture();
    VF_LOG_INFO("draw log: capture cancelled by a device reset; it is taken on the next frame");
}

DrawLogStats DrawLogStatistics()
{
    DrawLogStats s = g_log.stats;
    s.rows = static_cast<unsigned>(g_log.rows.size());
    s.draws = static_cast<unsigned>(std::count_if(g_log.rows.begin(), g_log.rows.end(), [](const Row& r) {
        return !r.mark && r.call.kind != DrawCallKind::Clear;
    }));
    s.constantValues = static_cast<unsigned>(g_log.values.size());
    return s;
}

void DrawLogResetStatistics()
{
    g_log.stats = {};
}

const std::string& DrawLogLastSummary()
{
    return g_log.summary;
}

void DrawLogMark(DrawPhase phase)
{
    if (!(g_callWatch & kWatchDrawLog))
        return;
    const long long now = Now();
    g_log.phaseTicks[static_cast<int>(g_log.phase)] += now - g_log.phaseStart;
    g_log.phaseStart = now;
    g_log.phase = phase;
    if (phase == DrawPhase::World)
        g_log.worldSeen = true;
    Row row = {};
    row.mark = true;
    row.phase = phase;
    row.gap = std::max(now - g_log.lastReturn - g_log.ownTicks, 0ll);
    g_log.ownTicks = 0;
    g_log.rows.push_back(row);
    g_log.lastReturn = Now();
}

void DrawLogBeforePresent()
{
    if (!(g_callWatch & kWatchDrawLog))
        return;
    g_callWatch &= ~kWatchDrawLog;
    const long long end = Now();
    g_log.phaseTicks[static_cast<int>(g_log.phase)] += end - g_log.phaseStart;
    if (!g_log.worldSeen)
    {
        ++g_log.stats.framesDiscarded;
        return;
    }
    g_log.armed = false;
    ++g_log.stats.captures;
    WriteCapture(end - g_log.start);
}

void DrawLogAfterPresent(IDirect3DDevice9* real)
{
    const Config& cfg = GlobalConfig().Get();
    if (cfg.drawLog == kDrawLogOnce && GlobalConfig().Generation() != g_log.seenGeneration)
    {
        g_log.seenGeneration = GlobalConfig().Generation();
        DrawLogArm();
    }
    if (!g_log.armed)
        return;
    ++g_log.stats.framesWatched;
    StartCapture(real);
}

void DrawLogVertexShader(IDirect3DVertexShader9* shader)
{
    g_log.state.vs = ShaderId(shader, false);
    ++g_log.pendingStateCalls;
}

void DrawLogPixelShader(IDirect3DPixelShader9* shader)
{
    g_log.state.ps = ShaderId(shader, true);
    ++g_log.pendingStateCalls;
}

void DrawLogConstants(bool pixel, UINT start, const float* data, UINT count)
{
    (pixel ? g_log.pendingPs : g_log.pendingVs).push_back({start, count});
    ++g_log.pendingStateCalls;
    if (data)
        RecordConstantValues(pixel, static_cast<UINT>(g_log.rows.size()), start, data, count);
}

void DrawLogTransform(D3DTRANSFORMSTATETYPE state)
{
    if (state == D3DTS_WORLD)
        g_log.pendingTransforms |= 1u;
    else if (state == D3DTS_VIEW)
        g_log.pendingTransforms |= 2u;
    else if (state == D3DTS_PROJECTION)
        g_log.pendingTransforms |= 4u;
    else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7)
        g_log.pendingTransforms |= 1u << (kTextureTransformBit + (state - D3DTS_TEXTURE0));
    ++g_log.pendingStateCalls;
}

void DrawLogFvf(DWORD fvf)
{
    g_log.state.fvf = fvf;
    g_log.state.declaration = 0;
    ++g_log.pendingStateCalls;
}

void DrawLogDeclaration(IDirect3DVertexDeclaration9* declaration)
{
    g_log.state.declaration = DeclarationId(declaration);
    g_log.state.fvf = 0;
    ++g_log.pendingStateCalls;
}

void DrawLogStream(UINT index, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride)
{
    if (index < kLoggedStreams)
        g_log.state.streams[index] = {VertexBufferId(buffer), offset, stride};
    ++g_log.pendingStateCalls;
}

void DrawLogIndices(IDirect3DIndexBuffer9* buffer)
{
    g_log.state.indices = IndexBufferId(buffer);
    ++g_log.pendingStateCalls;
}

void DrawLogRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    TrackRenderState(g_log.state.raster, state, value);
    ++g_log.pendingStateCalls;
}

void DrawLogRenderTarget(DWORD index, IDirect3DSurface9* surface)
{
    if (index == 0)
        g_log.state.target = SurfaceId(surface);
    ++g_log.pendingStateCalls;
}

void DrawLogDepthStencil(IDirect3DSurface9* surface)
{
    g_log.state.depthTarget = SurfaceId(surface);
    ++g_log.pendingStateCalls;
}

void DrawLogTexture(DWORD stage, IDirect3DBaseTexture9* texture)
{
    if (stage < kLoggedStages)
        g_log.state.textures[stage] = TextureId(texture);
    else if (stage >= kFirstVertexSampler && stage < kFirstVertexSampler + kVertexSamplerCount)
        g_log.state.vertexTextures[stage - kFirstVertexSampler] = TextureId(texture);
    ++g_log.pendingStateCalls;
}

void DrawLogViewport(const D3DVIEWPORT9* viewport)
{
    if (viewport)
        g_log.state.viewport = *viewport;
    ++g_log.pendingStateCalls;
}

void DrawLogStateCall()
{
    ++g_log.pendingStateCalls;
}

long long DrawLogDrawBegins()
{
    return Now();
}

void DrawLogDrawEnds(const DrawCall& call, long long begin)
{
    const long long end = Now();
    Row row = {};
    row.call = call;
    CloseRow(row, begin, end);
    g_log.rows.push_back(row);
    g_log.lastReturn = Now();
}

unsigned long long BytecodeHash(const void* data, size_t size)
{
    return Fnv1aOf(data, size);
}
