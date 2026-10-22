#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"
#include "vf_lamp_shadow.hlsli"

float4 cWaterLamps : register(c30);

sampler2D sSlopes : register(s5);
sampler2D sWaterLamps : register(s7);

static const float kWaterLampRows = 3;
static const float kPositionRow = 0;
static const float kEnergyRow = 1;
static const float kNormalRow = 2;
static const float kSourceSpread = 0.25;
static const float kMinRoughness = 1e-5;
static const float kMinHalfwayCosine = 0.01;
static const float kWindowWrap = 0.1;
static const float kFalloffPerYard = 0.7;
static const float kFalloffPerSquareYard = 0.03;
static const float kMinOccludedLight = 0.002;
static const int kOcclusionSteps = 8;
static const float kOcclusionPlace = 0.5;
static const float4 kNoBody = float4(0, 0, 0, 0);

float LampCount()
{
    return cWaterLamps.x;
}

float Gain()
{
    return cWaterLamps.y;
}

float SlotWidth()
{
    return cWaterLamps.z;
}

float4 WaterLampTexel(float slot, float row)
{
    return tex2Dlod(sWaterLamps, float4((slot + 0.5) * SlotWidth(), (row + 0.5) / kWaterLampRows, 0, 0));
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 pixel = vpos + 0.5;
    WaterSurface s = ReadSurface(sSlopes, pixel);
    float3 normal = SurfaceNormal(s);
    float3 toCamera = -s.toSurfaceWorld;
    float viewCosine = max(dot(normal, toCamera), kMinGrazing);
    float roughness = max(s.roughness, kMinRoughness);

    float3 glint = 0;
    float slot = 0;
    [loop] while (slot < LampCount())
    {
        float4 lamp = WaterLampTexel(slot, kPositionRow);
        float4 energy = WaterLampTexel(slot, kEnergyRow);
        float4 pane = WaterLampTexel(slot, kNormalRow);
        float3 toLamp = lamp.xyz - s.relative;
        float distanceSquared = max(dot(toLamp, toLamp), 1e-4);
        float distance = sqrt(distanceSquared);
        float3 light = toLamp / distance;
        float3 halfway = normalize(light + toCamera);
        float cosine = max(dot(normal, halfway), kMinHalfwayCosine);
        float c2 = cosine * cosine;
        float spread = roughness + kSourceSpread * lamp.w * lamp.w / distanceSquared;
        float lobe = exp((c2 - 1) / (c2 * spread)) / (kPi * spread * c2 * c2);
        float softened = distanceSquared + pane.w * pane.w;
        float falloff = 1 / (kFalloffPerYard * sqrt(softened) + kFalloffPerSquareYard * softened);
        float side = energy.w > 0 ? saturate((kWindowWrap - dot(pane.xyz, light)) / (1 + kWindowWrap)) : 1;
        float3 lit = energy.rgb * (lobe * Fresnel(dot(toCamera, halfway)) / (4 * viewCosine) *
                                   saturate(dot(normal, light)) * falloff * side);
        [branch] if (Gain() * dot(lit, kLuminanceWeights) > kMinOccludedLight)
            lit *= LampShadow(sDepthBefore, s.surface, WorldToViewDirection(toLamp), distance, kOcclusionPlace,
                              kOcclusionSteps, kLampBodyYards, kShadowStartYards, kNoBody, kNoBody, float3(0, 0, 0),
                              float2(-1, 0));
        glint += lit;
        slot += 1;
    }
    return float4(1 - exp(-Gain() * glint), 0);
}
