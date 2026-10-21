#include "vf_common.hlsli"

float4 cLampBlur : register(c9);

sampler2D sDepth : register(s0);
sampler2D sLight : register(s1);

static const int kTapPairs = 3;
static const float kPairWeights[kTapPairs] = { 0.8007, 0.4111, 0.1353 };
static const float kDepthToleranceYards = 0.1;
static const float kDepthTolerancePerViewZ = 0.01;

float2 BlurStep()
{
    return cLampBlur.xy;
}

float ViewZAt(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepth, clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre())));
}

float4 LightAt(float2 targetTexel)
{
    return tex2Dlod(sLight, float4(targetTexel / ViewportSize(), 0, 0));
}

float4 main(float2 targetPixel : VPOS) : COLOR0
{
    float2 centreTexel = targetPixel + 0.5;
    float2 pixel = ViewportOrigin() + centreTexel;
    float4 centre = LightAt(centreTexel);
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (BeyondFarClip(depth))
        return centre;
    float z = LinearDepth(depth);
    float ahead = ViewZAt(pixel + BlurStep()) - z;
    float behind = z - ViewZAt(pixel - BlurStep());
    float slope = abs(ahead) < abs(behind) ? ahead : behind;
    float tolerance = kDepthToleranceYards + kDepthTolerancePerViewZ * z;
    float3 sum = centre.rgb;
    float weights = 1;
    [unroll] for (int k = 0; k < kTapPairs; k++)
    {
        [unroll] for (int side = -1; side <= 1; side += 2)
        {
            float offset = (k + 1) * side;
            float2 tap = BlurStep() * offset;
            float miss = (ViewZAt(pixel + tap) - z - slope * offset) / tolerance;
            float weight = kPairWeights[k] * saturate(1 - miss * miss);
            sum += LightAt(centreTexel + tap).rgb * weight;
            weights += weight;
        }
    }
    return float4(sum / weights, centre.a);
}
