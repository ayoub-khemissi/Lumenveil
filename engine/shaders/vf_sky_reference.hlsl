#include "vf_common.hlsli"

#ifndef PASS
#define PASS 0
#endif

static const int kSkyBlockTaps = 4;
static const int kSkyRows = 32;
static const int kSkyColumns = 64;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

float4 cSkyPass : register(c9);
float4 cSkyTarget : register(c10);
float4 cSkyLandBalance : register(c11);
float4 cSkyFallback : register(c12);
float4 cSkyLight : register(c13);

sampler2D sFirst : register(s0);
sampler2D sSecond : register(s1);

float3 GammaToLinear(float3 colour)
{
    return pow(max(colour, 0), 2.2);
}

float3 LinearToGamma(float3 colour)
{
    return pow(max(colour, 0), 1 / 2.2);
}

#if PASS == 0

float ElevationSpread()
{
    return cSkyPass.x;
}

float AwayFromLightFullCos()
{
    return cSkyPass.y;
}

float AwayFromLightNoneCos()
{
    return cSkyPass.z;
}

float AwayFromLight(float3 direction)
{
    return 1 - smoothstep(AwayFromLightFullCos(), AwayFromLightNoneCos(), dot(direction, cSkyLight.xyz));
}

float4 main(float2 texel : VPOS) : COLOR0
{
    float2 blockSize = ViewportSize() / float2(kSkyColumns, kSkyRows);
    float3 light = 0;
    float sky = 0;
    [loop] for (int y = 0; y < kSkyBlockTaps; y++)
    {
        [loop] for (int x = 0; x < kSkyBlockTaps; x++)
        {
            float2 local = (texel + (float2(x, y) + 0.5) / kSkyBlockTaps) * blockSize;
            float2 pixel = ViewportOrigin() + local;
            [branch] if (IsSky(SampleDepth(sFirst, pixel)))
            {
                light += GammaToLinear(tex2Dlod(sSecond, float4(local / ViewportSize(), 0, 0)).rgb);
                sky += 1;
            }
        }
    }
    float2 centre = ViewportOrigin() + (texel + 0.5) * blockSize;
    float3 direction = ViewToWorldDirection(normalize(ViewRayAtUnitDepth(centre)));
    float weight = AwayFromLight(direction) * exp(-abs(direction.z) / ElevationSpread()) / (kSkyBlockTaps * kSkyBlockTaps);
    return float4(light, sky) * weight;
}

#elif PASS == 1

float4 main(float2 texel : VPOS) : COLOR0
{
    float4 sum = 0;
    float u = (texel.x + 0.5) / kSkyColumns;
    [loop] for (int row = 0; row < kSkyRows; row++)
        sum += tex2Dlod(sFirst, float4(u, (row + 0.5) / kSkyRows, 0, 0));
    return sum;
}

#else

float ColumnSpread()
{
    return cSkyPass.x;
}

float MinimumWeight()
{
    return cSkyPass.y;
}

float NewShare()
{
    return cSkyPass.z;
}

bool PreviousKept()
{
    return cSkyPass.w > 0.5;
}

float Level()
{
    return cSkyTarget.x;
}

float LandSaturation()
{
    return cSkyTarget.y;
}

float Exposure()
{
    return cSkyTarget.z;
}

float FullWeight()
{
    return cSkyTarget.w;
}

float3 HazeBeforeTheLandGrade(float3 skyLight)
{
    float3 shown = Level() * LinearToGamma(skyLight);
    float luminance = dot(shown, kLuminanceWeights);
    float3 unsaturated = LandSaturation() > 0.01 ? luminance + (shown - luminance) / LandSaturation() : luminance;
    return GammaToLinear(unsaturated / cSkyLandBalance.rgb) / Exposure();
}

float4 main(float2 texel : VPOS) : COLOR0
{
    float4 near = 0;
    float4 all = 0;
    [loop] for (int column = 0; column < kSkyColumns; column++)
    {
        float4 sums = tex2Dlod(sFirst, float4((column + 0.5) / kSkyColumns, 0.5, 0, 0));
        near += sums * exp(-abs(column - texel.x) / ColumnSpread());
        all += sums;
    }
    float3 previous = PreviousKept() ? tex2Dlod(sSecond, float4((texel.x + 0.5) / kSkyColumns, 0.5, 0, 0)).rgb : 1;
    float3 ratio = previous;
    [branch] if (all.w > MinimumWeight())
    {
        float3 skyLight = lerp(all.rgb / all.w, near.rgb / max(near.w, 1e-6), saturate(near.w / FullWeight()));
        float3 measured = min(HazeBeforeTheLandGrade(skyLight) / SkyRatioBase(cSkyFallback.rgb), kSkyRatioMax);
        ratio = PreviousKept() ? lerp(previous, measured, NewShare()) : measured;
    }
    return float4(ratio, 1);
}

#endif
