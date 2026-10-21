#pragma once

#include <d3d9.h>

#include <string>
#include <unordered_map>

constexpr int kClientShadowMaxMaps = 8;
constexpr int kClientShadowPairs = 4;
constexpr int kClientShadowRows = 3 * kClientShadowPairs;
constexpr DWORD kClientShadowFirstStage = 4;
constexpr DWORD kClientShadowStages = 5;
constexpr UINT kCasterProjectionRegister = 2;
constexpr UINT kCasterCameraToLightRegister = 14;
constexpr UINT kModelReceiverRegister = 224;
constexpr UINT kTerrainReceiverRegister = 37;
constexpr DWORD kModelReceiverFirstStage = 4;
constexpr DWORD kTerrainReceiverFirstStage = 5;

enum class ClientShaderRole : int
{
    None = 0,
    DirectCaster = 1,
    PaletteCaster = 2,
    CasterPixel = 3,
};

constexpr unsigned kReadsModelReceiverRows = 1u;
constexpr unsigned kReadsTerrainReceiverRows = 2u;

struct ClientShadowMap
{
    IDirect3DTexture9* texture;
    UINT size;
    D3DFORMAT format;
    unsigned casterDraws;
    unsigned paletteDraws;
    unsigned directDraws;
    bool hasPalette;
    bool hasDirect;
    unsigned long long paletteVs;
    float projection[4][4];
    float cameraToLight[3][4];
    float lightViewProjection[4][4];
    float directProjection[4][4];
};

struct ClientShadowReceiver
{
    unsigned draws;
    unsigned long long vs;
    float rows[kClientShadowRows][4];
    int stageMaps[kClientShadowPairs];
};

struct ClientShadowFrame
{
    int mapCount;
    ClientShadowMap maps[kClientShadowMaxMaps];
    ClientShadowReceiver models[kClientShadowMaxMaps];
    ClientShadowReceiver terrain[kClientShadowMaxMaps];
    unsigned casterDrawsElsewhere;
    unsigned receiverDrawsWithoutMap;
};

struct ClientShadowStats
{
    unsigned framesWatched;
    unsigned framesWithMaps;
    unsigned shadersClassified;
    unsigned deviceQueries;
    unsigned heldTextures;
    unsigned foundLogged;
    unsigned noneLogged;
    unsigned debugPasses;
};

struct ClientShadowPairs
{
    int count;
    const char* source;
    float constants[kClientShadowPairs][4][4];
    IDirect3DTexture9* textures[kClientShadowPairs];
    int maps[kClientShadowPairs];
};

class ClientShadowTracker
{
public:
    ClientShadowTracker() = default;
    ~ClientShadowTracker();
    ClientShadowTracker(const ClientShadowTracker&) = delete;
    ClientShadowTracker& operator=(const ClientShadowTracker&) = delete;

    void Present(IDirect3DDevice9* real, bool wanted);
    void ReleaseAll();
    bool Enabled() const { return m_enabled; }
    const ClientShadowFrame& Frame() const { return m_frame; }

    void ShaderCreated(const void* shader);
    void VertexShader(IDirect3DVertexShader9* shader)
    {
        if (shader != m_vs)
        {
            m_vs = shader;
            m_vsStale = true;
        }
    }
    void PixelShader(IDirect3DPixelShader9* shader)
    {
        if (shader != m_ps)
        {
            m_ps = shader;
            m_psStale = true;
        }
    }
    void DepthStencil(IDirect3DSurface9* surface)
    {
        m_depth = surface;
        m_depthResolved = false;
    }
    void Texture(DWORD stage, IDirect3DBaseTexture9* texture)
    {
        if (stage - kClientShadowFirstStage < kClientShadowStages)
            m_stages[stage - kClientShadowFirstStage] = texture;
    }
    void Draw(IDirect3DDevice9* real);

private:
    struct Shader
    {
        unsigned long long hash;
        ClientShaderRole role;
        unsigned reads;
    };

    Shader Classify(IUnknown* shader, bool pixel);
    void Enable(IDirect3DDevice9* real);
    void Disable();
    void EndFrame();
    void ReleaseFrame();
    int MapIndex(const IDirect3DBaseTexture9* texture) const;
    void ResolveDepth();
    void CasterDraw(IDirect3DDevice9* real);
    void ReceiverDraw(IDirect3DDevice9* real, bool model);
    int BoundMap(IDirect3DDevice9* real, DWORD stage);
    bool Rejected(const IDirect3DBaseTexture9* texture) const;

    bool m_enabled = false;
    unsigned m_rolesGeneration = 0;
    std::unordered_map<const void*, Shader> m_shaders;
    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_ps = nullptr;
    bool m_vsStale = true;
    bool m_psStale = true;
    Shader m_vsInfo = {};
    Shader m_psInfo = {};
    IDirect3DSurface9* m_depth = nullptr;
    bool m_depthResolved = false;
    int m_depthMap = -1;
    IDirect3DBaseTexture9* m_stages[kClientShadowStages] = {};
    const IDirect3DBaseTexture9* m_rejected[kClientShadowMaxMaps] = {};
    int m_rejectedCount = 0;
    ClientShadowFrame m_frame = {};
};

ClientShaderRole KnownClientShaderRole(unsigned long long hash);
void SetExtraClientShaderRole(unsigned long long hash, ClientShaderRole role);
unsigned ClientShaderConstantReads(const DWORD* code, size_t tokens);

void MultiplyCasterMatrices(const float projection[4][4], const float cameraToLight[3][4], float out[4][4]);
float ClientShadowLightAngleDegrees(const float lightViewProjection[4][4], const float* toLightInView);

ClientShadowPairs ClientShadowDebugPairs(const ClientShadowFrame* frame, int debugView);
void LogClientShadowFrame(const ClientShadowFrame& frame, const float* toLightInView);
void NoteClientShadowDebugPass();

ClientShadowStats ClientShadowStatistics();
void ResetClientShadowStatistics();
const ClientShadowFrame& LastClientShadowFrame();
const std::string& LastClientShadowLogLine();
