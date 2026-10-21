#include "vf_common.hlsli"

float4 cEffectGather : register(c9);
float4 cEffectScene : register(c10);

sampler2D sSceneNow : register(s0);
sampler2D sSceneBefore : register(s1);
sampler2D sDepthBefore : register(s2);
sampler2D sDepthNow : register(s3);

static const int kCellPixels = 8;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

struct EffectSums
{
    float4 light : COLOR0;
    float4 place : COLOR1;
};

float Threshold()
{
    return cEffectGather.x;
}

float NearestYards()
{
    return cEffectGather.y;
}

float FarthestYards()
{
    return cEffectGather.z;
}

float DepthChange()
{
    return cEffectGather.w;
}

float2 SceneTexelSize()
{
    return cEffectScene.xy;
}

float SumScale()
{
    return cEffectScene.z;
}

EffectSums main(float2 texel : VPOS)
{
    float4 light = 0;
    float4 place = 0;
    float2 corner = texel * kCellPixels;
    [loop] for (int y = 0; y < kCellPixels; y++)
    {
        [loop] for (int x = 0; x < kCellPixels; x++)
        {
            float2 local = corner + float2(x, y) + 0.5;
            float2 uv = local * SceneTexelSize();
            float3 now = tex2Dlod(sSceneNow, float4(uv, 0, 0)).rgb;
            float3 before = tex2Dlod(sSceneBefore, float4(uv, 0, 0)).rgb;
            float3 added = max(now - before, 0);
            float gained = dot(added, kLuminanceWeights);
            float energy = all(uv < 1) ? max(gained - Threshold(), 0) : 0;
            [branch] if (energy > 0)
            {
                float2 pixel = ViewportOrigin() + local;
                float depthBefore = SampleDepth(sDepthBefore, pixel);
                float depthNow = SampleDepth(sDepthNow, pixel);
                float viewZ = LinearDepth(depthBefore);
                bool counted = abs(depthNow - depthBefore) <= DepthChange() && !BeyondFarClip(depthBefore) &&
                               viewZ <= FarthestYards();
                [branch] if (counted)
                {
                    float inverseDepth = 1 / max(viewZ, NearestYards());
                    light += float4(added * (energy / gained), energy * inverseDepth);
                    place += float4(energy * uv, 1, 0);
                    place.w = max(place.w, inverseDepth);
                }
            }
        }
    }
    EffectSums sums;
    sums.light = light * SumScale();
    sums.place = float4(place.xyz * SumScale(), place.w);
    return sums;
}
