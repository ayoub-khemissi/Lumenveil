#include "vf_lamp_table.hlsli"

float4 cLampVisibility : register(c9);

sampler2D sDepth : register(s0);
sampler2D sPreviousVisibility : register(s1);
sampler2D sLampTable : register(s2);

static const int kLampTaps = 5;
static const float2 kTapDirections[kLampTaps] = { float2(0, 0), float2(1, 0), float2(-1, 0), float2(0, 1),
                                                  float2(0, -1) };

float SmoothingRate()
{
    return cLampVisibility.x;
}

float4 main(float2 slotTexel : VPOS) : COLOR0
{
    float slotU = SlotU(slotTexel.x);
    float4 screen = LampTexel(sLampTable, slotU, kLampScreenRow);
    float4 test = LampTexel(sLampTable, slotU, kLampTestRow);
    float visible = 1;
    [branch] if (test.y > 0.5)
    {
        float unblocked = 0;
        [unroll] for (int t = 0; t < kLampTaps; t++)
        {
            float2 pixel = clamp(screen.xy + kTapDirections[t] * screen.w, ViewportOrigin() + 0.5,
                                 ViewportLastPixelCentre());
            unblocked += LinearDepth(SampleDepth(sDepth, pixel)) > screen.z - test.z ? 1 : 0;
        }
        visible = unblocked / kLampTaps;
    }
    float previous = tex2Dlod(sPreviousVisibility, float4(slotU, 0.5, 0, 0)).r;
    float rate = test.x > 0.5 ? SmoothingRate() : 1;
    return float4(lerp(previous, visible, rate).xxx, 1);
}
