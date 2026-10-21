#include "vf_common.hlsli"
#include "vf_memory.hlsli"

float4 cMemoryMove : register(c9);
float4 cMemoryCamera : register(c10);
float4 cMemoryU : register(c11);
float4 cMemoryV : register(c12);
float4 cMemoryL : register(c13);

sampler2D sPrevious : register(s0);
sampler2D sDepth : register(s1);

static const float kNearestViewZ = 0.3;
static const float kKeepMarginYards = 1;
static const float kKeepMarginPerViewZ = 0.02;
static const float kPlacementYards = 0.1;
static const float kMinTapPixels = 1.5;

float2 WindowShift()
{
    return cMemoryMove.xy;
}

bool Forget()
{
    return cMemoryMove.z > 0;
}

float FocalPixels()
{
    return cMemoryMove.w;
}

float3 WorldToViewDirection(float3 worldDirection)
{
    return mul((float3x3)cViewToWorld, worldDirection);
}

float SceneViewZ(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepth, clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre())));
}

float NearestSceneViewZ(float2 pixel, float radius)
{
    return min(min(SceneViewZ(pixel), min(SceneViewZ(pixel + float2(radius, 0)), SceneViewZ(pixel - float2(radius, 0)))),
               min(SceneViewZ(pixel + float2(0, radius)), SceneViewZ(pixel - float2(0, radius))));
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    MemoryFrame frame = LoadMemoryFrame(cMemoryCamera, cMemoryU, cMemoryV, cMemoryL);
    float code = tex2Dlod(sPrevious, float4((vpos + 0.5) / frame.size, 0, 0)).r;
    float2 fromCamera = MemoryTexelFromCamera(frame, vpos);
    float2 before = fromCamera + WindowShift();
    bool known = MemoryCodeKnown(code) && !Forget() && all(before >= -0.5 * frame.size) &&
                 all(before < 0.5 * frame.size);
    float3 view = WorldToViewDirection(MemoryPointFromCamera(frame, fromCamera, code));
    float2 pixel = ViewToPixel(view);
    bool seen = known && view.z > kNearestViewZ && !OutsideViewport(pixel);
    float kept = known ? code : 0;
    [branch] if (seen)
    {
        float radius = max(kMinTapPixels, kPlacementYards * FocalPixels() / view.z);
        float margin = max(kKeepMarginYards, kKeepMarginPerViewZ * view.z);
        kept = NearestSceneViewZ(pixel, radius) > view.z + margin ? 0 : code;
    }
    return kept;
}
