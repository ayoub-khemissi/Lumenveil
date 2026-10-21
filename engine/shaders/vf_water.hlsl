#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"

sampler2D sSceneAfter : register(s3);
sampler2D sSlopes : register(s5);

static const float kRefractYards = 0.6;
static const float kRefractDepthYards = 4;
static const float kUnderwaterMargin = 0.01;
static const float kMaxThickness = 100;
static const float3 kAbsorption = float3(0.30, 0.08, 0.10);
static const float kTurbidity = 0.12;
static const float kDeepYards = 8;
static const float kDeepDarkening = 0.3;
static const float3 kDeepTeal = float3(0.05, 0.24, 0.28);

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
    return float4(lerp(deep, seen, Clarity() * exp(-thickness * kTurbidity)), 1);
}
