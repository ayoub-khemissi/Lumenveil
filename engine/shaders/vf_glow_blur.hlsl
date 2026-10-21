float4 cGlowBlur : register(c9);
float4 cGlowTarget : register(c10);

sampler2D sSource : register(s0);

static const int kTapPairs = 4;
static const float kCentreWeight = 0.2042;
static const float kPairWeights[kTapPairs] = { 0.1802, 0.1238, 0.0663, 0.0276 };

float2 TapStepUv()
{
    return cGlowBlur.xy;
}

float2 TargetTexelSize()
{
    return cGlowTarget.zw;
}

float4 main(float2 texel : VPOS) : COLOR0
{
    float2 uv = (texel + 0.5) * TargetTexelSize();
    float3 sum = tex2Dlod(sSource, float4(uv, 0, 0)).rgb * kCentreWeight;
    [unroll] for (int k = 0; k < kTapPairs; k++)
    {
        float2 offset = TapStepUv() * (k + 1);
        sum += (tex2Dlod(sSource, float4(uv + offset, 0, 0)).rgb + tex2Dlod(sSource, float4(uv - offset, 0, 0)).rgb) *
               kPairWeights[k];
    }
    return float4(sum, 1);
}
