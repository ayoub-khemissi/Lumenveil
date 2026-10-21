#include "vf_lamp_table.hlsli"
#include "vf_lamp_tiles.hlsli"

float4 cLampPass : register(c9);
float4 cLampTiles : register(c10);

sampler2D sDepth : register(s0);
sampler2D sFog : register(s1);
sampler2D sLampVisibility : register(s2);
sampler2D sLampTable : register(s3);
sampler2D sLampTiles : register(s4);

static const float kCoreRadius = 1.25;
static const float kFalloffShareOfReach = 0.2;
static const float kAnisotropy = 0.2;
static const float kInverseFourPi = 0.07957747;
static const float kEdgeFadeStart = 0.72;
static const float kEnergyScale = 0.9;
static const float kEnergyCap = 0.75;
static const float kMinTransmittance = 0.02;
static const float kMinRayYards = 1;
static const float kFarCoreYards = 0.4;
static const float kFarCoreSpread = 3;
static const float kFarCoreFrontMargin = 2;
static const float kFarCoreFrontMarginPerYard = 0.02;

float SlotCount()
{
    return cLampPass.x;
}

float FarCoreAnglePerTexel()
{
    return cLampPass.y;
}

float FarCoreAngle(float along)
{
    return max(FarCoreAnglePerTexel(), kFarCoreYards / along);
}

float LampPhase(float cosine)
{
    float denominator = 1 + kAnisotropy * kAnisotropy - 2 * kAnisotropy * cosine;
    return kInverseFourPi * (1 - kAnisotropy * kAnisotropy) / (denominator * sqrt(denominator));
}

float4 main(float2 lowResTexel : VPOS) : COLOR0
{
    float2 pixel = LowResTexelToFullPixel(lowResTexel);
    float viewZ = LinearDepth(SampleDepth(sDepth, pixel));
    float3 viewRay = ViewRayAtUnitDepth(pixel);
    float3 ray = ViewToWorldDirection(viewRay);
    float yardsPerViewZ = length(ray);
    ray /= yardsPerViewZ;
    float surface = viewZ * yardsPerViewZ;
    float fogOpacity = tex2Dlod(sFog, float4(LowResTexelToUv(lowResTexel), 0, 0)).a;
    float extinction = -log(max(1 - fogOpacity, kMinTransmittance)) / max(surface, kMinRayYards);

    float2 tile = LampTileOf(cLampTiles, pixel);
    float listed = LampTileListed(sLampTiles, cLampTiles, tile);
    bool every = listed > kLampTileCapacity;
    float count = every ? SlotCount() : listed;
    float3 energy = 0;
    float entry = 0;
    [loop] while (entry < count)
    {
        float slot = every ? entry : LampTileEntry(sLampTiles, cLampTiles, tile, entry + 1);
        float slotU = SlotU(slot);
        float4 sphere = LampTexel(sLampTable, slotU, kLampSphereRow);
        float along = dot(sphere.xyz, ray);
        float offsetSquared = max(dot(sphere.xyz, sphere.xyz) - along * along, 0);
        float reachSquared = sphere.w * sphere.w;
        [branch] if (offsetSquared < reachSquared)
        {
            float halfSpan = sqrt(reachSquared - offsetSquared);
            float start = max(0, along - halfSpan);
            float end = min(surface, along + halfSpan);
            [branch] if (end > start)
            {
                float falloff = max(sphere.w * kFalloffShareOfReach, 2 * kCoreRadius);
                float inner = sqrt(offsetSquared + kCoreRadius * kCoreRadius);
                float outer = sqrt(offsetSquared + falloff * falloff);
                float2 span = float2(start, end) - along;
                float2 innerAngles = atan(span / inner);
                float2 outerAngles = atan(span / outer);
                float integral = falloff / (falloff - kCoreRadius) *
                                 ((innerAngles.y - innerAngles.x) / inner - (outerAngles.y - outerAngles.x) / outer);
                float closest = clamp(along, start, end);
                float3 lampToSample = ray * closest - sphere.xyz;
                float3 lightPath = lampToSample / max(length(lampToSample), 1e-4);
                float edge = 1 - smoothstep(sphere.w * kEdgeFadeStart, sphere.w, sqrt(offsetSquared));
                float4 light = LampTexel(sLampTable, slotU, kLampLightRow);
                float visible = tex2Dlod(sLampVisibility, float4(slotU, 0.5, 0, 0)).r;
                energy += light.rgb * (integral * LampPhase(dot(lightPath, -ray)) * edge *
                                       lerp(light.w, 1, visible) * exp(-extinction * closest));
            }
        }
        float coreAngle = FarCoreAngle(max(along, kMinRayYards));
        float angleSquared = offsetSquared / max(along * along, kMinRayYards);
        [branch] if (along > kMinRayYards && angleSquared < kFarCoreSpread * kFarCoreSpread * coreAngle * coreAngle &&
                     along < surface + max(kFarCoreFrontMargin, kFarCoreFrontMarginPerYard * along))
        {
            float4 core = LampTexel(sLampTable, slotU, kLampCoreRow);
            float visible = tex2Dlod(sLampVisibility, float4(slotU, 0.5, 0, 0)).r;
            energy += core.rgb * (exp(-0.5 * angleSquared / (coreAngle * coreAngle)) * visible *
                                  exp(-extinction * along));
        }
        entry += 1;
    }
    return float4(1 - exp(-min(energy * kEnergyScale, kEnergyCap)), 1);
}
