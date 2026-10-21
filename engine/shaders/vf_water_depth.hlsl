#include "vf_common.hlsli"

sampler2D sDepth : register(s0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    return SampleDepth(sDepth, vpos + 0.5).xxxx;
}
