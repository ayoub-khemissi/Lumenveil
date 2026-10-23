#include "vf_lamp_table.hlsli"
#include "vf_lamp_shadow.hlsli"
#include "vf_lamp_tiles.hlsli"
#include "vf_surface_normal.hlsli"
#include "vf_ground_map.hlsli"

float4 cLampSurfaces : register(c9);
float4 cSunInView : register(c10);
float4 cCarrierBase : register(c11);
float4 cCarrierUp : register(c12);
float4 cLampTiles : register(c13);
float4 cTerrainMap : register(c14);
float4 cTerrainReach : register(c15);

sampler2D sLampTable : register(s1);
sampler2D sOccluderDepth : register(s2);
sampler2D sLampTiles : register(s3);
sampler2D sTerrain : register(s4);

static const int kShadowSteps = 16;
static const float kNearYards = 0.5;
static const float kFalloffPerYard = 0.7;
static const float kFalloffPerSquareYard = 0.03;
static const float kWrap = 0.15;
static const float kMinShadowedLight = 0.002;
static const float kThinFacing = 0.6;
static const float kWindowWrap = 0.1;
static const float kThinFacingCode = 1;
static const float kFacingCodeScale = 0.5;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
static const float kReceiverClearanceYards = 0.2;
static const float kReceiverClearancePerYard = 0.15;

float SlotCount()
{
    return cLampSurfaces.x;
}

float SunFacingCode(float4 normal, float3 litNormal)
{
    return normal.w > 0 ? kThinFacingCode : saturate(dot(litNormal, cSunInView.xyz)) * kFacingCodeScale;
}

float4 main(float2 targetPixel : VPOS) : COLOR0
{
    float2 pixel = ViewportOrigin() + targetPixel + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (BeyondFarClip(depth))
        return float4(0, 0, 0, 1);
    float3 surface = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    float4 normal = SurfaceNormal(pixel, surface);
    float jitter = InterleavedGradientNoise(pixel);
    MapGround mapGround = GroundOnTheMap(sTerrain, cTerrainMap, cTerrainReach, ViewToWorldDirection(surface));
    float onMap = normal.w > 0 ? 0 : OnTheMap(mapGround, ViewToWorldDirection(normal.xyz));
    float3 litNormal =
        normalize(lerp(LitNormal(normal), mul((float3x3)cViewToWorld, mapGround.normal), onMap));

    float2 tile = LampTileOf(cLampTiles, pixel);
    float listed = LampTileListed(sLampTiles, cLampTiles, tile);
    bool every = listed > kLampTileCapacity;
    float count = every ? SlotCount() : listed;
    float3 pointLight = 0;
    float entry = 0;
    [loop] while (entry < count)
    {
        float slot = every ? entry : LampTileEntry(sLampTiles, cLampTiles, tile, entry + 1);
        float slotU = SlotU(slot);
        float4 lamp = LampTexel(sLampTable, slotU, kLampViewRow);
        float3 toLamp = lamp.xyz - surface;
        float distanceSquared = dot(toLamp, toLamp);
        float reachSquared = lamp.w * lamp.w;
        [branch] if (distanceSquared < reachSquared)
        {
            float4 light = LampTexel(sLampTable, slotU, kLampSurfaceRow);
            float4 emit = LampTexel(sLampTable, slotU, kLampEmitRow);
            float distance = sqrt(distanceSquared);
            float3 direction = toLamp / max(distance, 1e-4);
            bool carried = emit.w < 0;
            bool lantern = carried && emit.y > 0;
            float nearYards = emit.w != 0 ? abs(emit.w) : kNearYards;
            float spread = distanceSquared + nearYards * nearYards;
            float falloff = 1 / (kFalloffPerYard * sqrt(spread) + kFalloffPerSquareYard * spread);
            float lambert = saturate((dot(litNormal, direction) + kWrap) / (1 + kWrap));
            float facing = normal.w > 0 ? max(lambert, kThinFacing) : lambert;
            float outward = emit.w > 0 ? saturate((kWindowWrap - dot(emit.xyz, direction)) / (1 + kWindowWrap)) : 1;
            float window = saturate(1 - distanceSquared * distanceSquared / (reachSquared * reachSquared));
            float3 lit = light.rgb * (facing * outward * window * window * falloff);
            bool shadowed = light.w > 0 && (normal.w == 0 || lantern);
            [branch] if (shadowed && dot(lit, kLuminanceWeights) > kMinShadowedLight)
                lit *= lerp(1, LampShadow(sOccluderDepth, surface, toLamp, distance, jitter, kShadowSteps,
                                          carried ? emit.x : kLampBodyYards, carried ? emit.z : kShadowStartYards,
                                          float4(cCarrierBase.xyz, lantern ? cCarrierBase.w : 0), cCarrierUp,
                                          normal.w > 0 ? float3(0, 0, 0) : normal.xyz,
                                          normal.w > 0 ? float2(-1, 0)
                                                       : float2(kReceiverClearanceYards, kReceiverClearancePerYard)),
                            light.w);
            pointLight += lit;
        }
        entry += 1;
    }
    return float4(pointLight, SunFacingCode(normal, litNormal));
}
