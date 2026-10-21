#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"
#include "vf_water_rings.hlsli"

float4 cWaveLookup[7] : register(c16);
float4 cWaveTurns : register(c23);

sampler2D sWaves0 : register(s5);
sampler2D sWaves1 : register(s6);
sampler2D sWaves2 : register(s7);

static const float kMinFootprintGrazing = 0.02;

float UnresolvedRoughness()
{
    return cWaveOrigin.w;
}

float2 LookupStep(float4 lookup, float2 step)
{
    return float2(dot(lookup.zw, step), dot(float2(-lookup.w, lookup.z), step));
}

float4 LayerMoments(sampler2D waves, float4 lookup, float2 position, float2 dx, float2 dy)
{
    return tex2Dgrad(waves, lookup.xy + LookupStep(lookup, position), LookupStep(lookup, dx), LookupStep(lookup, dy));
}

float2 Turn(float2 slope, float2 turn)
{
    return float2(turn.x * slope.x - turn.y * slope.y, turn.y * slope.x + turn.x * slope.y);
}

float2 LayerVariance(float4 moments)
{
    return max(moments.zw - moments.xy * moments.xy, 0);
}

float2 FootprintStep(float3 rayWorld, float3 stepWorld, float surfaceZ)
{
    float limit = kMinFootprintGrazing * length(rayWorld);
    float rise = rayWorld.z < 0 ? min(rayWorld.z, -limit) : max(rayWorld.z, limit);
    return surfaceZ * (stepWorld.xy - rayWorld.xy * (stepWorld.z / rise));
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 pixel = vpos + 0.5;
    WaterSurface s = ReadGeometry(pixel);
    float3 rayWorld = ViewToWorldDirection(s.ray);
    float2 perPixel = 2 / (ViewportSize() * ViewToNdcScale());
    float2 dx = FootprintStep(rayWorld, ViewToWorldDirection(float3(perPixel.x, 0, 0)), s.surfaceZ);
    float2 dy = FootprintStep(rayWorld, ViewToWorldDirection(float3(0, -perPixel.y, 0)), s.surfaceZ);
    float2 position = s.relative.xy;
    float4 m0 = LayerMoments(sWaves0, cWaveLookup[0], position, dx, dy);
    float4 m1 = LayerMoments(sWaves1, cWaveLookup[1], position, dx, dy);
    float4 m2 = LayerMoments(sWaves1, cWaveLookup[2], position, dx, dy);
    float4 m3 = LayerMoments(sWaves1, cWaveLookup[3], position, dx, dy);
    float4 m4 = LayerMoments(sWaves2, cWaveLookup[4], position, dx, dy);
    float4 m5 = LayerMoments(sWaves2, cWaveLookup[5], position, dx, dy);
    float4 m6 = LayerMoments(sWaves2, cWaveLookup[6], position, dx, dy);
    float2 slope = m0.xy + m1.xy + m4.xy + Turn(m2.xy + m5.xy, cWaveTurns.xy) + Turn(m3.xy + m6.xy, cWaveTurns.zw);
    float2 variance = LayerVariance(m0) + LayerVariance(m1) + LayerVariance(m2) + LayerVariance(m3) +
                      LayerVariance(m4) + LayerVariance(m5) + LayerVariance(m6);
    float roughness = UnresolvedRoughness() + variance.x + variance.y;
    float3 rings = WaterRings(s.relative, dx, dy);
    slope += rings.xy;
    roughness += rings.z;
    return float4(slope, roughness, 1);
}
