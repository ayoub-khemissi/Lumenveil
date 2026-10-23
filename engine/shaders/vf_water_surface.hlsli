float4 cWater : register(c9);
float4 cToLight : register(c10);
float4 cGlitterColour : register(c11);
float4 cSkyHorizon : register(c12);
float4 cSkyZenith : register(c13);
float4 cTerrain : register(c14);
float4 cWaveOrigin : register(c15);

sampler2D sDepthBefore : register(s0);
sampler2D sDepthAfter : register(s1);
sampler2D sSceneBefore : register(s2);

static const float kMinGrazing = 0.05;
static const float kMinFogShare = 1e-3;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
static const float kPi = 3.14159265;

float Reflectivity()
{
    return cWater.x;
}

float Clarity()
{
    return cWater.y;
}

float MaxReflectionYards()
{
    return cWater.z;
}

float FresnelAtNormalIncidence()
{
    return cWater.w;
}

float FocalPixels()
{
    return cGlitterColour.w;
}

float Smoothness()
{
    return cWaveOrigin.x;
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Fresnel(float cosine)
{
    return FresnelAtNormalIncidence() + (1 - FresnelAtNormalIncidence()) * Pow5(1 - saturate(cosine));
}

float3 SceneAt(sampler2D scene, float2 pixel)
{
    return tex2Dlod(scene, float4((pixel - ViewportOrigin()) / ViewportSize(), 0, 0)).rgb;
}

float ViewZBefore(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepthBefore, pixel));
}

float ViewZAfter(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepthAfter, pixel));
}

float3 WorldToViewDirection(float3 worldDirection)
{
    return mul((float3x3)cViewToWorld, worldDirection);
}

float StockFogShare(float yards)
{
    return saturate((yards - cSkyHorizon.w) * cSkyZenith.w);
}

float ExtraFog(float seenYards, float pathYards)
{
    return saturate(1 - (1 - StockFogShare(pathYards)) / max(1 - StockFogShare(seenYards), kMinFogShare));
}

struct WaterSurface
{
    float3 ray;
    float rayLength;
    float surfaceZ;
    float3 surface;
    float3 toSurfaceWorld;
    float3 relative;
    float2 slope;
    float roughness;
};

WaterSurface ReadGeometry(float2 pixel)
{
    WaterSurface s;
    s.ray = ViewRayAtUnitDepth(pixel);
    s.rayLength = length(s.ray);
    s.surfaceZ = ViewZAfter(pixel);
    s.surface = s.ray * s.surfaceZ;
    float3 rayWorld = ViewToWorldDirection(s.ray);
    s.toSurfaceWorld = rayWorld / s.rayLength;
    s.relative = rayWorld * s.surfaceZ;
    s.slope = 0;
    s.roughness = 0;
    return s;
}

WaterSurface ReadSurface(sampler2D slopes, float2 pixel)
{
    WaterSurface s = ReadGeometry(pixel);
    float4 slope = tex2Dlod(slopes, float4(pixel * DepthTexelSize(), 0, 0));
    s.slope = slope.xy;
    s.roughness = slope.z;
    return s;
}

float3 SurfaceNormal(WaterSurface s)
{
    return normalize(float3(-s.slope, 1));
}
