#include "vf_common.hlsli"
#include "vf_memory.hlsli"

float4 cScatter : register(c9);
float4 cMemoryCamera : register(c10);
float4 cMemoryU : register(c11);
float4 cMemoryV : register(c12);
float4 cMemoryL : register(c13);
float4 cBodyBase : register(c14);
float4 cBodyUp : register(c15);

sampler2D sDepth : register(s0);

static const float4 kCulledPoint = float4(2, 2, 2, 1);

struct ScatterPoint
{
    float4 position : POSITION;
    float size : PSIZE;
    float code : TEXCOORD0;
};

float ScatterStride()
{
    return cScatter.x;
}

float2 ScatterOffset()
{
    return cScatter.yz;
}

float HalfWindowTexels()
{
    return cScatter.w;
}

float3 ViewPositionAt(float2 pixel)
{
    float2 clamped = clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre());
    return ViewRayAtUnitDepth(clamped) * LinearDepth(SampleDepth(sDepth, clamped));
}

float3 SmallerStep(float3 centre, float3 ahead, float3 behind)
{
    float3 forward = ahead - centre;
    float3 backward = centre - behind;
    return abs(forward.z) < abs(backward.z) ? forward : backward;
}

ScatterPoint main(float2 grid : POSITION)
{
    MemoryFrame frame = LoadMemoryFrame(cMemoryCamera, cMemoryU, cMemoryV, cMemoryL);
    float2 pixel = ViewportOrigin() + grid * ScatterStride() + ScatterOffset() + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    float3 view = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    float3 acrossX = SmallerStep(view, ViewPositionAt(pixel + float2(1, 0)), ViewPositionAt(pixel - float2(1, 0)));
    float3 acrossY = SmallerStep(view, ViewPositionAt(pixel + float2(0, 1)), ViewPositionAt(pixel - float2(0, 1)));
    float3 viewNormal = normalize(cross(acrossX, acrossY));
    viewNormal = dot(viewNormal, view) > 0 ? -viewNormal : viewNormal;
    float3 normal = ViewToWorldDirection(viewNormal);
    float slopeClass = dot(normal, frame.l) < 0 ? 0 : MemorySlopeClass(SlopeAcrossLight(normal, frame.l));
    MemoryPlacement placement = PlaceInMemory(frame, ViewToWorldDirection(view), slopeClass, HalfWindowTexels());
    bool kept = all(pixel < ViewportOrigin() + ViewportSize()) && !BeyondFarClip(depth) && !IsSky(depth) &&
                placement.inside && !InsideBody(view, cBodyBase, cBodyUp);
    ScatterPoint o;
    o.position = kept ? MemoryTexelPosition(placement.texel, frame.size) : kCulledPoint;
    o.size = 1;
    o.code = placement.code;
    return o;
}
