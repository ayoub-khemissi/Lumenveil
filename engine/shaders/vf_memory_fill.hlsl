#include "vf_memory.hlsli"

float4 cFill : register(c9);

sampler2D sRealigned : register(s0);

static const float kMemoryCentreSubTexel = 10;
static const float kFillMinNeighbours = 2.5;

float TexelUv()
{
    return cFill.x;
}

float CodeAt(float2 vpos)
{
    return tex2Dlod(sRealigned, float4((vpos + 0.5) * TexelUv(), 0, 0)).r;
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float code = CodeAt(vpos);
    float4 around = float4(CodeAt(vpos + float2(1, 0)), CodeAt(vpos - float2(1, 0)), CodeAt(vpos + float2(0, 1)),
                           CodeAt(vpos - float2(0, 1)));
    float4 known = around >= kMemoryCodesPerDepthStep;
    float count = dot(known, 1);
    float4 steps = floor(around / kMemoryCodesPerDepthStep);
    float4 classes = floor((around - steps * kMemoryCodesPerDepthStep) / kMemoryCodesPerSlopeClass);
    float4 low = known ? steps : 1e9;
    float4 high = known ? steps : -1e9;
    float lowest = min(min(low.x, low.y), min(low.z, low.w));
    float highest = max(max(high.x, high.y), max(high.z, high.w));
    float middle = dot(known ? steps : 0, 1) - lowest - highest;
    middle = count > 3.5 ? floor(middle * 0.5) : middle;
    float4 apart = known ? abs(steps - middle) : 1e9;
    float nearest = min(min(apart.x, apart.y), min(apart.z, apart.w));
    float4 nearClasses = apart <= nearest + 0.5 ? classes : 0;
    float slopeClass = max(max(nearClasses.x, nearClasses.y), max(nearClasses.z, nearClasses.w));
    float filled = middle * kMemoryCodesPerDepthStep + slopeClass * kMemoryCodesPerSlopeClass + kMemoryCentreSubTexel;
    return MemoryCodeKnown(code) || count < kFillMinNeighbours ? code : filled;
}
