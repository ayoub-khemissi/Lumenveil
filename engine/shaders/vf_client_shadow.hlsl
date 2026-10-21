#include "vf_common.hlsli"

float4 cPairs[16] : register(c9);
float4 cClientShadowView : register(c25);

sampler2D sDepth : register(s0);
sampler2D sMap0 : register(s1);
sampler2D sMap1 : register(s2);
sampler2D sMap2 : register(s3);
sampler2D sMap3 : register(s4);

static const int kPairs = 4;
static const float3 kLitTint[kPairs] = {float3(1, 1, 1), float3(0.8, 1, 0.8), float3(0.8, 0.8, 1), float3(1, 1, 0.7)};
static const float3 kShadowed = float3(0.25, 0.35, 1);
static const float3 kOutsideEveryMap = float3(1, 0.35, 0.35);
static const float3 kNoMaps = float3(1, 0.4, 1);
static const float kMapEdge = 1;
static const float kLitAbove = 0.5;

float PairCount()
{
    return cClientShadowView.x;
}

float3 MapCoordinates(int pair, float4 view)
{
    return float3(dot(cPairs[pair * 4], view), dot(cPairs[pair * 4 + 1], view), dot(cPairs[pair * 4 + 2], view));
}

float4 MapLookup(int pair, float3 coordinates)
{
    float4 settings = cPairs[pair * 4 + 3];
    return float4(0.5 * coordinates.x + 0.5, 0.5 + settings.x * 0.5 * coordinates.y, coordinates.z - settings.y, 0);
}

float CompareWithMap(int pair, float4 lookup)
{
    if (pair == 0)
        return tex2Dlod(sMap0, lookup).r;
    if (pair == 1)
        return tex2Dlod(sMap1, lookup).r;
    if (pair == 2)
        return tex2Dlod(sMap2, lookup).r;
    return tex2Dlod(sMap3, lookup).r;
}

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 pixel = pixelIndex + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (IsSky(depth))
        return float4(1, 1, 1, 1);
    [branch] if (PairCount() < 0.5)
        return float4(kNoMaps, 1);
    float4 view = float4(ViewRayAtUnitDepth(pixel) * LinearDepth(depth), 1);
    [unroll] for (int pair = 0; pair < kPairs; ++pair)
    {
        float3 coordinates = MapCoordinates(pair, view);
        [branch] if (pair < PairCount() && all(abs(coordinates.xy) < kMapEdge))
            return float4(CompareWithMap(pair, MapLookup(pair, coordinates)) > kLitAbove ? kLitTint[pair] : kShadowed,
                          1);
    }
    return float4(kOutsideEveryMap, 1);
}
