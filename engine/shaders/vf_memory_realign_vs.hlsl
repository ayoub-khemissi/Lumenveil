#include "vf_memory.hlsli"

float4 cRealign : register(c9);
float4 cNewCamera : register(c10);
float4 cNewU : register(c11);
float4 cNewV : register(c12);
float4 cNewL : register(c13);
float4 cOldCamera : register(c14);
float4 cOldU : register(c15);
float4 cOldV : register(c16);
float4 cOldL : register(c17);

sampler2D sOld : register(s0);

static const float4 kCulledPoint = float4(2, 2, 2, 1);

struct RealignPoint
{
    float4 position : POSITION;
    float size : PSIZE;
    float code : TEXCOORD0;
};

float2 TileOrigin()
{
    return cRealign.xy;
}

float HalfWindowTexels()
{
    return cRealign.z;
}

float RealignSeed()
{
    return cRealign.w;
}

float3 PlaceInCell(float2 texel)
{
    return frac(float3(dot(texel, float2(0.7548777, 0.5698403)), dot(texel, float2(0.5698403, 0.2548777)),
                       dot(texel, float2(0.3141593, 0.8191725))) +
                RealignSeed() * float3(0.6180340, 0.3819660, 0.7548777));
}

RealignPoint main(float2 grid : POSITION)
{
    MemoryFrame before = LoadMemoryFrame(cOldCamera, cOldU, cOldV, cOldL);
    MemoryFrame after = LoadMemoryFrame(cNewCamera, cNewU, cNewV, cNewL);
    float2 texel = TileOrigin() + grid;
    float code = tex2Dlod(sOld, float4((texel + 0.5) / before.size, 0, 0)).r;
    float3 offset = MemoryPointWithin(before, MemoryTexelFromCamera(before, texel), code, PlaceInCell(texel));
    MemoryPlacement placement = PlaceInMemory(after, offset, DecodeMemory(code).slopeClass, HalfWindowTexels());
    RealignPoint o;
    o.position = MemoryCodeKnown(code) && placement.inside ? MemoryTexelPosition(placement.texel, after.size)
                                                           : kCulledPoint;
    o.size = 1;
    o.code = placement.code;
    return o;
}
