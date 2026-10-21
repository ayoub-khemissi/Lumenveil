#include "vf_common.hlsli"

float4 cEdgeBlock : register(c9);
float4 cBodyBase : register(c10);
float4 cBodyUp : register(c11);

sampler2D sDepth : register(s0);

static const int kTapsPerSide = 8;

float2 BlockSize()
{
    return cEdgeBlock.xy;
}

float4 main(float2 edgeTexel : VPOS) : COLOR0
{
    float2 blockStart = ViewportOrigin() + edgeTexel * BlockSize();
    float2 tapStep = BlockSize() / kTapsPerSide;
    float4 nearer = 0;
    [loop] for (int row = 0; row < kTapsPerSide; row++)
    {
        [loop] for (int column = 0; column < kTapsPerSide; column++)
        {
            float2 pixel = min(blockStart + (float2(column, row) + 0.5) * tapStep, ViewportLastPixelCentre());
            float depth = SampleDepth(sDepth, pixel);
            float viewZ = LinearDepth(depth);
            float4 inFront = viewZ < EdgeOccluderViewZs() ? 1 : 0;
            bool body = InsideBody(ViewRayAtUnitDepth(pixel) * viewZ, cBodyBase, cBodyUp);
            nearer += BeyondFarClip(depth) || body ? 0 : inFront;
        }
    }
    return nearer / (kTapsPerSide * kTapsPerSide);
}
