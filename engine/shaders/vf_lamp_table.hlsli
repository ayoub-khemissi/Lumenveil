#include "vf_common.hlsli"

static const float kLampSlots = 237;
static const float kLampTableRows = 8;
static const float kLampSphereRow = 0;
static const float kLampLightRow = 1;
static const float kLampScreenRow = 2;
static const float kLampTestRow = 3;
static const float kLampSurfaceRow = 4;
static const float kLampViewRow = 5;
static const float kLampEmitRow = 6;
static const float kLampCoreRow = 7;

float SlotU(float slot)
{
    return (slot + 0.5) / kLampSlots;
}

float4 LampTexel(sampler2D table, float slotU, float row)
{
    return tex2Dlod(table, float4(slotU, (row + 0.5) / kLampTableRows, 0, 0));
}
