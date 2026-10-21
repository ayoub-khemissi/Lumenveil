#include "vf_common.hlsli"

float4 cGlowMask : register(c9);
float4 cGlowSource : register(c10);

sampler2D sDepth : register(s0);
sampler2D sHalfResScene : register(s1);

static const float3 kLumaWeights = float3(0.299, 0.587, 0.114);
static const float2 kBoxTapOffsets[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };

float LuminanceThreshold()
{
    return cGlowMask.x;
}

float FullPixelsPerGlowTexel()
{
    return cGlowMask.y;
}

float2 GlowTexelSize()
{
    return cGlowMask.zw;
}

float2 HalfResSourceTexelSize()
{
    return cGlowSource.xy;
}

float3 BrightPart(float3 scene)
{
    float brightness = saturate((dot(scene, kLumaWeights) - LuminanceThreshold()) / (1 - LuminanceThreshold()));
    return pow(scene, 2.2) * brightness;
}

float3 BrightBoxAverage(float2 glowTexel)
{
    float2 centreUv = (glowTexel + 0.5) * GlowTexelSize();
    float3 sum = 0;
    [unroll] for (int k = 0; k < 4; k++)
        sum += BrightPart(
            tex2Dlod(sHalfResScene, float4(centreUv + kBoxTapOffsets[k] * HalfResSourceTexelSize(), 0, 0)).rgb);
    return sum * 0.25;
}

float4 main(float2 glowTexel : VPOS) : COLOR0
{
    float2 pixel = min(ViewportOrigin() + (glowTexel + 0.5) * FullPixelsPerGlowTexel(), ViewportLastPixelCentre());
    float notSky = IsSky(SampleDepth(sDepth, pixel)) ? 0 : 1;
    return float4(BrightBoxAverage(glowTexel) * notSky, 1);
}
