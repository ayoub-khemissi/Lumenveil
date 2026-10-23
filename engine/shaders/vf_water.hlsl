#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"

sampler2D sSceneAfter : register(s3);
sampler2D sSlopes : register(s5);
sampler2D sFoam : register(s9);
sampler2D sFloors : register(s10);

float4 cWaterExtras : register(c32);
float4 cFoamUv : register(c33);
float4 cRiverShallow : register(c35);
float4 cRiverDeep : register(c36);
float4 cOceanShallow : register(c37);
float4 cOceanDeep : register(c38);
float4 cBands : register(c39);

static const float kRefractYards = 0.6;
static const float kRefractDepthYards = 4;
static const float kUnderwaterMargin = 0.01;
static const float kMaxThickness = 100;
static const float3 kAbsorption = float3(0.30, 0.08, 0.10);
static const float kTurbidity = 0.12;
static const float kDeepYards = 8;
static const float kDeepDarkening = 0.3;
static const float3 kDeepTeal = float3(0.05, 0.24, 0.28);
static const float kFoamDepthYards = 0.8;
static const float kFoamPatternGain = 1.3;
static const float kShoreFadeYards = 0.6;
static const float kFoamNearYards = 60;
static const float kFoamFarYards = 150;
static const float kFoamLift = 1.8;
static const float kFoamFloor = 0.02;
static const float kFoamCeiling = 0.85;
static const float3 kFoamTint = float3(0.92, 0.96, 1.0);
static const float kRiverDeepYards = 3;
static const float kOceanDeepYards = 8;
static const float kMinWetShare = 0.01;

float SeaShare(float2 relative)
{
    float share = cBands.y;
    [branch] if (cBands.x > 0)
    {
        float4 floors = tex2Dlod(sFloors, float4(cTerrain.xy + relative * cTerrain.z, 0, 0));
        float wet = floors.z + floors.w;
        share = wet > kMinWetShare ? floors.w / wet : share;
    }
    return share;
}

float3 BandWater(WaterSurface s, float3 seen, float verticalDepth)
{
    float sea = SeaShare(s.relative.xy);
    float depth = saturate(verticalDepth / lerp(kRiverDeepYards, kOceanDeepYards, sea));
    float4 band = lerp(lerp(cRiverShallow, cOceanShallow, sea), lerp(cRiverDeep, cOceanDeep, sea), depth);
    float3 colour = lerp(band.rgb, cSkyHorizon.rgb, cBands.z * StockFogShare(s.surfaceZ * s.rayLength));
    return lerp(seen, colour, band.a);
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 pixel = vpos + 0.5;
    WaterSurface s = ReadSurface(sSlopes, pixel);

    float thickness = min(max(ViewZBefore(pixel) - s.surfaceZ, 0) * s.rayLength, kMaxThickness);
    float3 bend = WorldToViewDirection(float3(s.slope, 0));
    float2 refracted = pixel + float2(bend.x, -bend.y) * (FocalPixels() / s.surfaceZ * kRefractYards *
                                                          min(thickness, kRefractDepthYards));
    refracted = clamp(refracted, ViewportOrigin() + 0.5, ViewportLastPixelCentre());
    refracted = ViewZBefore(refracted) > ViewZAfter(refracted) + kUnderwaterMargin ? refracted : pixel;
    float3 seen = SceneAt(sSceneBefore, refracted) * exp(-thickness * kAbsorption);
    float3 clientWater = SceneAt(sSceneAfter, pixel);
    float depthShare = 1 - exp(-thickness / kDeepYards);
    float3 teal = kDeepTeal * (dot(clientWater, kLuminanceWeights) / dot(kDeepTeal, kLuminanceWeights)) *
                  (1 - kDeepDarkening * depthShare);
    float3 deep = lerp(clientWater, teal, Clarity() * depthShare);
    float verticalDepth = thickness * abs(s.toSurfaceWorld.z);
    [branch] if (Smoothness() > 0)
        deep = lerp(deep, BandWater(s, seen, verticalDepth), Smoothness());
    float shore = Smoothness() * (1 - smoothstep(0, kShoreFadeYards, verticalDepth));
    float3 body = lerp(deep, seen, max(Clarity() * exp(-thickness * kTurbidity), shore));
    float2 foamUv = cFoamUv.xy + s.relative.xy * cFoamUv.z;
    float2 foamDx = ddx(foamUv);
    float2 foamDy = ddy(foamUv);
    [branch] if (cWaterExtras.w > 0 && !BeyondFarClip(SampleDepth(sDepthBefore, pixel)))
    {
        float depth = verticalDepth;
        float pattern = tex2Dgrad(sFoam, foamUv, foamDx, foamDy).r;
        float nearby = 1 - smoothstep(kFoamNearYards, kFoamFarYards, s.surfaceZ * s.rayLength);
        float foam = saturate(pattern * kFoamPatternGain - depth / kFoamDepthYards) *
                     saturate(1 - depth / (2 * kFoamDepthYards)) * nearby * cWaterExtras.w;
        float white = clamp(dot(clientWater, kLuminanceWeights) * kFoamLift + kFoamFloor, kFoamFloor, kFoamCeiling);
        body = lerp(body, kFoamTint * white, saturate(foam));
    }
    return float4(body, 1);
}
