static const float kLampBodyYards = 1.5;
static const float kShadowStartYards = 0.2;
static const float kMaxShadowStartShare = 0.5;
static const float kShadowBias = 0.1;
static const float kShadowBiasPerViewZ = 0.003;
static const float kMinOccluderYards = 1;
static const float kOccluderThicknessInSteps = 1.5;
static const float kOccluderThicknessPerViewZ = 0.02;
static const float kMinSampleViewZ = 0.3;

float LampShadow(sampler2D depthTexture, float3 start, float3 toLamp, float distance, float jitter, int steps,
                 float clearYards, float startYards, float4 bodyBase, float4 bodyUp)
{
    float travel = saturate(1 - clearYards / max(distance, 1e-3));
    float3 path = toLamp * travel;
    float startShare = min(startYards / max(distance * travel, 1e-3), kMaxShadowStartShare);
    float thickness = max(kMinOccluderYards, distance * travel / steps * kOccluderThicknessInSteps);
    float3 lamp = start + toLamp;
    float lit = 1;
    [loop] for (int k = 0; k < steps && lit > 0; k++)
    {
        float3 samplePosition = start + path * lerp(startShare, 1, (k + jitter) / steps);
        [branch] if (samplePosition.z >= kMinSampleViewZ)
        {
            float2 samplePixel = ViewToPixel(samplePosition);
            [branch] if (!OutsideViewport(samplePixel))
            {
                float sceneDepth = SampleDepth(depthTexture, samplePixel);
                float sceneViewZ = LinearDepth(sceneDepth);
                float behind = samplePosition.z - sceneViewZ;
                float3 occluderPosition = samplePosition * (sceneViewZ / samplePosition.z);
                float3 fromLamp = occluderPosition - lamp;
                bool occluder = !BeyondFarClip(sceneDepth) &&
                                behind > kShadowBias + kShadowBiasPerViewZ * samplePosition.z &&
                                behind < max(thickness, kOccluderThicknessPerViewZ * samplePosition.z) &&
                                dot(fromLamp, fromLamp) > clearYards * clearYards &&
                                !InsideBody(occluderPosition, bodyBase, bodyUp);
                lit = occluder ? 0 : 1;
            }
        }
    }
    return lit;
}
