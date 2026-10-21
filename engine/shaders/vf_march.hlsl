#include "vf_common.hlsli"
#include "vf_clouds.hlsli"

#ifndef STEPS
#define STEPS 24
#endif
#ifndef SHAFT_STEPS
#define SHAFT_STEPS 16
#endif
#ifndef SHAFT_MEMORY
#define SHAFT_MEMORY 0
#endif

static const int kShadowSteps = 6;
static const int kShaftSunSteps = 12;
static const int kFogLayers = 4;
static const int kRegistersPerLayer = 6;
static const float kShadowMinViewZ = 0.3;
static const float kShadowDepthBias = 0.3;
static const float kGoldenRatioFraction = 0.618034;
static const float kPlasticNumberFraction = 0.7548777;
static const float kShaftMinOccluderThickness = 2.0;
static const float kMinOpticalDepth = 1e-6;

float4 cLight : register(c9);
float4 cShadowMarch : register(c10);
float4 cMarch : register(c11);
float4 cLayers[kFogLayers * kRegistersPerLayer] : register(c12);
float4 cShafts : register(c36);
float4 cShaftAir : register(c37);
float4 cDriftCoarse : register(c38);
float4 cDriftFine : register(c39);
float4 cDriftScale : register(c40);
float4 cMist : register(c41);
float4 cDriftDetail : register(c42);
float4 cTerrainShadow : register(c43);
float4 cShaftFade : register(c44);
float4 cMemoryU : register(c45);
float4 cMemoryV : register(c46);
float4 cMemoryL : register(c47);
float4 cMemoryGrid : register(c48);
float4 cMemoryBeforeU : register(c49);
float4 cMemoryBeforeV : register(c50);
float4 cMemoryBeforeL : register(c51);
float4 cMemoryBeforeGrid : register(c52);
float4 cCloudMap : register(c53);
float4 cCloudShape : register(c54);
float4 cShaftBodyBase : register(c55);
float4 cShaftBodyUp : register(c56);
float4 cHazeFromSky : register(c57);
float4 cSkyReference : register(c58);
float4 cBankFloor : register(c59);
float4 cBankShape : register(c60);
float4 cBankGrounds[3] : register(c61);
float4 cBankShade : register(c64);
float4 cBankMisc : register(c65);
float4 cBankNoise : register(c66);
static const float kBankEdgeFadePerUv = 16;
static const float kBankPoolRange = 40;
static const float kBankMinTopShare = 0.25;
static const float kBankFarLodFrom = 2.5;
static const float kBankFarLodPerLevel = 0.5;
static const float kCeilingTopInFades = 2;
static const float kMinCeilingRampSpan = 1e-3;
static const float kShaftGlarePower = 0.25;

static const float kMemoryDepthSteps = 8;
static const float kMemoryCodesPerDepthStep = 256;
static const float kMemoryCodesPerSlopeClass = 16;
static const float kMemorySubTexels = 4;
static const float kMemorySlopeClassesPerOctave = 1.5;
static const float kMemorySlopeOctaveOffset = 4;

sampler2D sDepth : register(s0);
sampler3D sDriftNoise : register(s1);
sampler2D sTerrainShadow : register(s2);
sampler2D sShaftOccluders : register(s3);
sampler2D sShaftMemoryBefore : register(s4);
sampler2D sClouds : register(s5);
sampler2D sSkyReference : register(s6);
sampler2D sBankFloor : register(s7);

struct FogLayer
{
    float start;
    float density;
    float g;
    float isotropic;
    float3 emissive;
    float strength;
    float3 diffuse;
    float exponent;
    float upperHeight;
    float upperFalloff;
    float lowerHeight;
    float lowerFalloff;
    float3 shadowEmissive;
    float shadowDensity;
    float shadowed;
    float skyFalloff;
    float limit;
    float bankShare;
};

FogLayer LoadFogLayer(int index)
{
    int first = index * kRegistersPerLayer;
    FogLayer layer;
    layer.start = cLayers[first].x;
    layer.density = cLayers[first].y;
    layer.g = cLayers[first].z;
    layer.isotropic = cLayers[first].w;
    layer.emissive = cLayers[first + 1].rgb;
    layer.strength = cLayers[first + 1].w;
    layer.diffuse = cLayers[first + 2].rgb;
    layer.exponent = cLayers[first + 2].w;
    layer.upperHeight = cLayers[first + 3].x;
    layer.upperFalloff = cLayers[first + 3].y;
    layer.lowerHeight = cLayers[first + 3].z;
    layer.lowerFalloff = cLayers[first + 3].w;
    layer.shadowEmissive = cLayers[first + 4].rgb;
    layer.shadowDensity = cLayers[first + 4].w;
    layer.shadowed = cLayers[first + 5].x;
    layer.skyFalloff = cLayers[first + 5].y;
    layer.limit = cLayers[first + 5].z;
    layer.bankShare = cLayers[first + 5].w;
    return layer;
}

float3 DirectionToLightView()
{
    return cLight.xyz;
}

float LightAboveHorizon()
{
    return cLight.w;
}

float3 HazeFromSky(float2 pixel)
{
    float u = (pixel.x - ViewportOrigin().x) / ViewportSize().x;
    float3 ratio = tex2Dlod(sSkyReference, float4(u, 0.5, 0, 0)).rgb;
    return lerp(cHazeFromSky.rgb, ratio * SkyRatioBase(cHazeFromSky.rgb), cSkyReference.x);
}

float HazeFromSkyShare()
{
    return cHazeFromSky.w;
}

float ShadowMinStep()
{
    return cShadowMarch.x;
}

float ShadowStepPerYard()
{
    return cShadowMarch.y;
}

bool ShadowsEnabled()
{
    return cShadowMarch.z > 0;
}

float ShadowThicknessInSteps()
{
    return cShadowMarch.w;
}

bool JitterEnabled()
{
    return cMarch.x > 0;
}

float DistanceCurvePerYard()
{
    return cMarch.y;
}

float HorizonBlendStart()
{
    return cMarch.z;
}

float FarClip()
{
    return cMarch.w;
}

float ShaftStrength()
{
    return cShafts.x;
}

float ShaftFocus()
{
    return cShafts.y;
}

float ShaftRange()
{
    return cShafts.z;
}

float OccluderDepthPerDistance()
{
    return cShafts.w;
}

float3 AirShaftScatterPerYard()
{
    return cShaftAir.rgb;
}

float ShaftLength()
{
    return cShaftAir.w;
}

float3 CoarseDriftOrigin()
{
    return cDriftCoarse.xyz;
}

float DriftSlope()
{
    return cDriftCoarse.w;
}

float3 FineDriftOrigin()
{
    return cDriftFine.xyz;
}

float FineDriftLodOffset()
{
    return cDriftFine.w;
}

float3 CoarseDriftPerYard()
{
    return cDriftScale.xxy;
}

float3 FineDriftPerYard()
{
    return cDriftScale.zzw;
}

float DriftTexelsPerYardOfStep()
{
    return cDriftDetail.x;
}

float SwirlPerNoise()
{
    return cDriftDetail.y;
}

float WispShare()
{
    return cDriftDetail.z;
}

float BankWispErosion()
{
    return cBankNoise.x;
}

float DriftOffset()
{
    return cDriftDetail.w;
}

float MistTop()
{
    return cMist.x;
}

float MistFalloff()
{
    return cMist.y;
}

float MistBoost()
{
    return cMist.z;
}

float MistInverseRange()
{
    return cMist.w;
}

float DriftLod(float stepLength)
{
    return log2(stepLength * DriftTexelsPerYardOfStep());
}

float2 DriftNoise(float3 offsetFromCamera, float lod)
{
    float4 patches =
        tex3Dlod(sDriftNoise, float4(offsetFromCamera * CoarseDriftPerYard() + CoarseDriftOrigin(), lod));
    float3 fine = offsetFromCamera * FineDriftPerYard() + FineDriftOrigin();
    fine.xy += patches.ba * SwirlPerNoise();
    float wisps = tex3Dlod(sDriftNoise, float4(fine, lod + FineDriftLodOffset())).g;
    return float2(lerp(patches.r, wisps, WispShare()), patches.r + (wisps - 0.5) * BankWispErosion());
}

float PlainDrift(float noise)
{
    return max(noise * DriftSlope() + DriftOffset(), 0);
}

float BankStrength()
{
    return cBankMisc.y;
}

float BankReferenceZ()
{
    return cBankMisc.x;
}

float BanksInValleysOnly()
{
    return cBankMisc.z;
}

float BankShadeNoiseScale()
{
    return cBankMisc.w;
}

float BankSharpness()
{
    return cBankShape.x;
}

float BankThresholdRise()
{
    return cBankShape.y;
}

float BankPoolGain()
{
    return cBankShape.z;
}

float BankLift()
{
    return cBankShape.w;
}

float3 BankShadeOffset()
{
    return cBankShade.xyz;
}

float BankShadeStrength()
{
    return cBankShade.w;
}

struct BankPlace
{
    float floorRef;
    float top;
    float gain;
    float gate;
    float shift;
    float4 shape;
};

BankPlace PlaceBank(float2 uv)
{
    float4 floors = tex2Dlod(sBankFloor, float4(uv, 0, 0));
    float2 fromCentre = abs(uv - 0.5);
    float known = cBankFloor.w * saturate((0.5 - max(fromCentre.x, fromCentre.y)) * kBankEdgeFadePerUv);
    float lake = floors.b * known;
    float sea = floors.a * known;
    float land = saturate(1 - lake - sea);
    float depth = (floors.g - floors.r) * known;
    float relief = clamp(depth / kBankPoolRange, -1, 1);
    float pool = 1 + BankPoolGain() * relief;
    BankPlace b;
    b.floorRef = lerp(BankReferenceZ(), floors.r, known) + BankLift() * max(depth, 0);
    b.shape = cBankGrounds[0] * land + cBankGrounds[1] * lake + cBankGrounds[2] * sea;
    b.top = b.shape.z * max(pool, kBankMinTopShare);
    b.gain = max(pool, 0) * b.shape.w;
    b.gate = lerp(1, max(saturate(2 * relief), 1 - land), BanksInValleysOnly());
    b.shift = BankReferenceZ() - b.floorRef;
    return b;
}

float BankRise(float height, BankPlace b)
{
    return saturate((height - b.floorRef) / b.top);
}

float BankCloud(float noise, float rise, BankPlace b)
{
    return saturate((noise - b.shape.x - BankThresholdRise() * rise) * BankSharpness()) * b.shape.y *
           (1 - rise * rise);
}

float BankDensity(float noise, float height, float lod, BankPlace b)
{
    float rise = BankRise(height, b);
    float far = saturate((lod - kBankFarLodFrom) * kBankFarLodPerLevel);
    return lerp(BankCloud(noise, rise, b), 1 - rise * rise, far) * b.gain;
}

float CeilingPerYard()
{
    return cBankNoise.y;
}

float CeilingStrength()
{
    return cBankNoise.z;
}

float CeilingGround()
{
    return cBankNoise.w;
}

float RampMean(float from, float to)
{
    float lo = min(from, to);
    float hi = max(from, to);
    float area = 0.5 * (saturate(hi) * saturate(hi) - saturate(lo) * saturate(lo)) + max(hi - 1, 0) - max(lo - 1, 0);
    return hi - lo > kMinCeilingRampSpan ? area / (hi - lo) : saturate(0.5 * (lo + hi));
}

float CeilingFactor(float2 uv, float2 stepHeights)
{
    float2 fromCentre = abs(uv - 0.5);
    float known = cBankFloor.w * saturate((0.5 - max(fromCentre.x, fromCentre.y)) * kBankEdgeFadePerUv);
    float ground = lerp(CeilingGround(), tex2Dlod(sBankFloor, float4(uv, 0, 0)).r, known);
    float2 ramp = (stepHeights - ground) * CeilingPerYard() - kCeilingTopInFades;
    return 1 - CeilingStrength() * RampMean(ramp.x, ramp.y);
}

float BankLight(float3 offsetFromCamera, float height, float lod, BankPlace b)
{
    float3 coarse = (offsetFromCamera + BankShadeOffset()) * CoarseDriftPerYard() + CoarseDriftOrigin();
    float noise = 0.5 + (tex3Dlod(sDriftNoise, float4(coarse, lod + 1)).r - 0.5) * BankShadeNoiseScale();
    float rise = BankRise(height + BankShadeOffset().z, b);
    return exp(-BankShadeStrength() * BankCloud(noise, rise, b) * b.gain);
}

float2 TerrainShadowUvAtCamera()
{
    return cTerrainShadow.xy;
}

float TerrainShadowUvPerYard()
{
    return cTerrainShadow.z;
}

float TerrainShadowSharpness()
{
    return cTerrainShadow.w;
}

float TerrainLight(float2 uv, float height)
{
    return saturate(height * TerrainShadowSharpness() + tex2Dlod(sTerrainShadow, float4(uv, 0, 0)).r);
}

float MistDensity(float height, float sampleDistance)
{
    return 1 + MistBoost() * saturate(exp((MistTop() - height) * MistFalloff())) *
                   saturate(1 - sampleDistance * MistInverseRange());
}

float4 StepScales(float3 driftOffset, float2 directionXY, float sampleDistance, float sampleHeight, float stepLength,
                  float2 stepHeights, out float ceiling)
{
    float lod = DriftLod(stepLength);
    float2 noise = DriftNoise(driftOffset, lod);
    float4 scales = float4(PlainDrift(noise.x), PlainDrift(noise.x), 0, 1);
    [branch] if (BankStrength() > 0)
    {
        BankPlace bank = PlaceBank(cBankFloor.xy + directionXY * (cBankFloor.z * sampleDistance));
        scales.y = lerp(scales.x, BankDensity(noise.y, sampleHeight, lod, bank), bank.gate);
        scales.z = bank.shift * bank.gate;
        [branch] if (BankShadeStrength() > 0 && scales.y > 0)
            scales.w = lerp(1, BankLight(driftOffset, sampleHeight, lod, bank), bank.gate);
    }
    ceiling = 1;
    [branch] if (CeilingStrength() > 0)
        ceiling = CeilingFactor(cBankFloor.xy + directionXY * (cBankFloor.z * sampleDistance), stepHeights);
    scales.xy *= MistDensity(sampleHeight + BankStrength() * scales.z, sampleDistance);
    return scales;
}

float PhaseHG(float g, float cosAngle)
{
    float r = (1 - g) / sqrt(max(1 + g * g - 2 * g * cosAngle, 1e-6));
    return r * r * r;
}

struct SunMarch
{
    float reach;
    float steps;
    float firstStep;
    float quadraticSpacing;
    float occluderThickness;
    float occluderThicknessPerDistance;
    bool extendPastScreenEdge;
};

SunMarch ShortSunMarch(float sampleDistance)
{
    float stepLength = max(ShadowMinStep(), sampleDistance * ShadowStepPerYard());
    SunMarch march;
    march.reach = stepLength * kShadowSteps;
    march.steps = kShadowSteps;
    march.firstStep = 1;
    march.quadraticSpacing = 0;
    march.occluderThickness = stepLength * ShadowThicknessInSteps();
    march.occluderThicknessPerDistance = 0;
    march.extendPastScreenEdge = false;
    return march;
}

SunMarch ShaftSunMarch()
{
    SunMarch march;
    march.reach = ShaftLength();
    march.steps = kShaftSunSteps;
    march.firstStep = 0;
    march.quadraticSpacing = 1;
    march.occluderThickness = kShaftMinOccluderThickness;
    march.occluderThicknessPerDistance = OccluderDepthPerDistance();
    march.extendPastScreenEdge = true;
    return march;
}

bool BehindOccluder(float occluderViewZ, float sampleViewZ, SunMarch march)
{
    float behindSurface = sampleViewZ - occluderViewZ;
    return behindSurface > kShadowDepthBias &&
           behindSurface < max(march.occluderThickness, occluderViewZ * march.occluderThicknessPerDistance);
}

bool SceneOccludes(float sceneDepth, float sampleViewZ, SunMarch march)
{
    return !BeyondFarClip(sceneDepth) && BehindOccluder(LinearDepth(sceneDepth), sampleViewZ, march);
}

float4 EdgeLevelWeights(float level)
{
    return saturate(1 - abs(level - float4(0, 1, 2, 3)));
}

float EdgeThicknessLevels()
{
    return cShaftFade.z;
}

float EdgeVisibility(float2 viewportUv, float sampleViewZ)
{
    float4 nearer = tex2Dlod(sShaftOccluders, float4(viewportUv, 0, 0));
    float level = clamp(EdgeOccluderLevel(sampleViewZ), 0, 3);
    return 1 - dot(nearer, EdgeLevelWeights(level) - EdgeLevelWeights(max(level - EdgeThicknessLevels(), 0)));
}

float2 ViewToViewportUv(float3 viewPosition)
{
    float2 ndc = viewPosition.xy / viewPosition.z * ViewToNdcScale() + ViewToNdcOffset();
    return float2(0.5, -0.5) * ndc + 0.5;
}

float SunVisibility(float3 viewPosition, SunMarch march, float jitter)
{
    float visibility = 1;
    float marching = 1;
    [loop] for (int k = 0; k < kShaftSunSteps; k++)
    {
        [branch] if (marching > 0 && k < march.steps)
        {
            float fraction = (k + march.firstStep + jitter) / march.steps;
            float spacing = fraction * lerp(1, fraction, march.quadraticSpacing);
            float3 samplePosition = viewPosition + DirectionToLightView() * (march.reach * spacing);
            float2 viewportUv = ViewToViewportUv(samplePosition);
            [branch] if (samplePosition.z < kShadowMinViewZ)
            {
                marching = 0;
            }
            else if (any(saturate(viewportUv) != viewportUv))
            {
                visibility = march.extendPastScreenEdge ? EdgeVisibility(viewportUv, samplePosition.z) : 1;
                marching = 0;
            }
            else
            {
                float2 pixel = ViewportOrigin() + viewportUv * ViewportSize();
                float sceneDepth = SampleDepth(sDepth, pixel);
                [branch] if (SceneOccludes(sceneDepth, samplePosition.z, march) &&
                             !InsideBody(samplePosition * (LinearDepth(sceneDepth) / samplePosition.z),
                                         cShaftBodyBase, cShaftBodyUp))
                {
                    visibility = 0;
                    marching = 0;
                }
            }
        }
    }
    return visibility;
}

struct MemoryLookup
{
    float3 u;
    float3 v;
    float3 l;
    float2 cameraTexel;
    float limitAtCamera;
    float texelsPerYard;
    float texelUv;
    float halfWindowTexels;
};

MemoryLookup LoadMemoryLookup(float4 u, float4 v, float4 l, float4 grid)
{
    MemoryLookup m;
    m.u = u.xyz;
    m.v = v.xyz;
    m.l = l.xyz;
    m.cameraTexel = float2(u.w, v.w);
    m.limitAtCamera = l.w;
    m.texelsPerYard = grid.x;
    m.texelUv = grid.y;
    m.halfWindowTexels = grid.z;
    return m;
}

MemoryLookup CurrentMemory()
{
    return LoadMemoryLookup(cMemoryU, cMemoryV, cMemoryL, cMemoryGrid);
}

MemoryLookup MemoryBefore()
{
    return LoadMemoryLookup(cMemoryBeforeU, cMemoryBeforeV, cMemoryBeforeL, cMemoryBeforeGrid);
}

float MemoryBeforeWeight()
{
    return cMemoryBeforeGrid.w;
}

float MemoryTexelBlocks(sampler2D map, MemoryLookup m, float2 texel, float2 sampleTexel, float sampleDepth)
{
    float code = tex2Dlod(map, float4((texel + 0.5) * m.texelUv, 0, 0)).r;
    float depthStep = floor(code / kMemoryCodesPerDepthStep);
    float rest = code - depthStep * kMemoryCodesPerDepthStep;
    float slopeClass = floor(rest / kMemoryCodesPerSlopeClass);
    float subTexel = rest - slopeClass * kMemoryCodesPerSlopeClass;
    float subRow = floor(subTexel / kMemorySubTexels);
    float2 stored = texel + (float2(subTexel - subRow * kMemorySubTexels, subRow) + 0.5) / kMemorySubTexels;
    float slope = exp2(slopeClass / kMemorySlopeClassesPerOctave - kMemorySlopeOctaveOffset);
    float margin = slope * length(stored - sampleTexel) / m.texelsPerYard;
    return (depthStep + 0.5) / kMemoryDepthSteps > sampleDepth + margin ? 1 : 0;
}

float SunVisibilityInMemory(sampler2D map, MemoryLookup m, float3 offsetFromCamera)
{
    float2 texels = float2(dot(offsetFromCamera, m.u), dot(offsetFromCamera, m.v)) * m.texelsPerYard;
    float visibility = 1;
    [branch] if (all(abs(texels) < m.halfWindowTexels))
    {
        float2 sampleTexel = m.cameraTexel + texels;
        float2 base = floor(sampleTexel - 0.5);
        float2 f = sampleTexel - 0.5 - base;
        float sampleDepth = m.limitAtCamera + dot(offsetFromCamera, m.l);
        float blocked = 0;
        [loop] for (float corner = 0; corner < 4; corner++)
        {
            float2 offset = float2(frac(corner * 0.5) * 2, floor(corner * 0.5));
            float2 weight = lerp(1 - f, f, offset);
            blocked += weight.x * weight.y * MemoryTexelBlocks(map, m, base + offset, sampleTexel, sampleDepth);
        }
        visibility = 1 - blocked;
    }
    return visibility;
}
float StepCoverage(FogLayer layer, float stepStart, float sampleDistance, float stepLength)
{
    return saturate((sampleDistance - layer.start) / max(stepLength, 1e-3)) *
           saturate((layer.limit - stepStart) / max(stepLength, 1e-3));
}

float DistanceCurve(FogLayer layer, float sampleDistance)
{
    return 1 + layer.strength * pow(saturate((sampleDistance - layer.start) * DistanceCurvePerYard()) + 1e-6,
                                    layer.exponent);
}

float HeightProfile(FogLayer layer, float height)
{
    return saturate(exp((layer.upperHeight - height) * layer.upperFalloff)) *
           saturate(exp((height - layer.lowerHeight) * layer.lowerFalloff));
}

void AccumulateLayer(FogLayer layer, float phase, float skyUpward, float3 hazeColour, float hazeFromSky,
                     float stepStart, float sampleDistance, float stepLength, float sampleHeight, float sunVisibility,
                     float4 scales, float ceiling, inout float3 radiance, inout float opticalDepth,
                     inout float3 shaftScatter)
{
    float plainScale = scales.x;
    float bankScale = scales.y;
    float bankShift = scales.z;
    float bankLight = scales.w;
    float bankShare = abs(layer.bankShare);
    float coverage = StepCoverage(layer, stepStart, sampleDistance, stepLength);
    float distanceCurve = DistanceCurve(layer, sampleDistance);
    float heightProfile = HeightProfile(layer, sampleHeight + max(layer.bankShare, 0) * bankShift);
    float banked = lerp(1, bankLight, bankShare);
    float directLight = lerp(1, sunVisibility, layer.shadowed) * banked;
    float shadowDensityScale = lerp(1, lerp(layer.shadowDensity, 1, sunVisibility), layer.shadowed);
    float skyDensityScale = exp(-skyUpward * layer.skyFalloff);
    float layerOpticalDepth = layer.density * skyDensityScale * stepLength * coverage * distanceCurve * heightProfile *
                              shadowDensityScale * lerp(plainScale, bankScale, bankShare) * ceiling;
    float3 own = lerp(layer.emissive, lerp(layer.shadowEmissive, layer.emissive, sunVisibility), layer.shadowed);
    float3 emissive = lerp(own, hazeColour, hazeFromSky);
    radiance += (layer.diffuse * (directLight * phase) + emissive) * layerOpticalDepth;
    opticalDepth += layerOpticalDepth;
    shaftScatter += layer.diffuse * (layerOpticalDepth * banked);
}

float StepBoundary(int boundary, float shaftLength, float farLength)
{
    float shaftFraction = saturate(boundary / (float)SHAFT_STEPS);
    float farFraction = max(boundary - SHAFT_STEPS, 0) / (float)STEPS;
    return shaftLength * shaftFraction * shaftFraction + farLength * farFraction * farFraction;
}

float ShaftFadePerYard()
{
    return cShaftFade.x;
}

float ShaftGlarePerBrightness()
{
    return cShaftFade.w;
}

float ShaftFadeOffset()
{
    return cShaftFade.y;
}

float ShaftRangeFade(float sampleDistance)
{
    float fade = saturate(sampleDistance * ShaftFadePerYard() + ShaftFadeOffset());
    return 1 - fade * fade * (3 - 2 * fade);
}

float StepJitter(float2 lowResTexel)
{
    return JitterEnabled() ? frac(InterleavedGradientNoise(lowResTexel) + FrameIndex() * kGoldenRatioFraction) : 0.5;
}

float4 main(float2 lowResTexel : VPOS) : COLOR0
{
    float2 pixel = LowResTexelToFullPixel(lowResTexel);
    float3 viewRay = ViewRayAtUnitDepth(pixel);
    float distancePerViewZ = length(viewRay);
    float3 viewDirection = viewRay / distancePerViewZ;
    float depth = SampleDepth(sDepth, pixel);
    float skyMask = IsSky(depth) ? 1 : 0;
    float hazeFromSky = HazeFromSkyShare() * (1 - skyMask);
    float3 hazeColour = HazeFromSky(pixel);
    float viewZ = LinearDepth(depth);
    float horizonBlend = max(BeyondFarClip(depth) ? 1 : 0, smoothstep(HorizonBlendStart(), FarClip(), viewZ));
    viewZ = lerp(viewZ, MaxFogDistance(), horizonBlend);
    float marchLength = min(viewZ * distancePerViewZ, MaxFogDistance());

    float jitter = StepJitter(lowResTexel);
    float3 cameraWorld = CameraPositionWorld();
    float3 directionWorld = ViewToWorldDirection(viewDirection);
    float cosToLight = dot(DirectionToLightView(), viewDirection);
    float upward = max(directionWorld.z, 0);
    float riseLevelledAtHorizon = lerp(directionWorld.z, upward, horizonBlend);
    float3 marchDirectionWorld = float3(directionWorld.xy, riseLevelledAtHorizon);

    float phase[kFogLayers];
    float skyUpward = upward * skyMask;
    [unroll] for (int i = 0; i < kFogLayers; i++)
    {
        FogLayer layer = LoadFogLayer(i);
        phase[i] = lerp(PhaseHG(layer.g, cosToLight), 1, layer.isotropic);
    }

    float shaftLength = min(marchLength, ShaftRange());
    float farLength = marchLength - shaftLength;
    float shaftPhase = ShaftStrength() * PhaseHG(ShaftFocus(), cosToLight);
    float2 terrainUvPerDistance = directionWorld.xy * TerrainShadowUvPerYard();

    float3 inScatteredRadiance = 0;
    float3 shaftRadiance = 0;
    float transmittance = 1;
    float stepEnd = 0;
    [loop] for (int s = 0; s < SHAFT_STEPS + STEPS; s++)
    {
        float stepStart = stepEnd;
        stepEnd = StepBoundary(s + 1, shaftLength, farLength);
        float stepLength = stepEnd - stepStart;
        [branch] if (stepLength > 0)
        {
            bool shaftStep = s < SHAFT_STEPS;
            float sampleDistance = lerp(stepStart, stepEnd, jitter);
            float sampleHeight = cameraWorld.z + riseLevelledAtHorizon * sampleDistance;
            float2 stepHeights = cameraWorld.z + riseLevelledAtHorizon * float2(stepStart, stepEnd);
            float ceiling;
            float4 scales = StepScales(marchDirectionWorld * sampleDistance, directionWorld.xy, sampleDistance,
                                       sampleHeight, stepLength, stepHeights, ceiling);
            float3 samplePosition = viewDirection * sampleDistance;
            float sunVisibility =
                LightAboveHorizon() *
                TerrainLight(TerrainShadowUvAtCamera() + terrainUvPerDistance * sampleDistance, sampleHeight);
            [branch] if (ShadowsEnabled())
            {
                SunMarch march = ShortSunMarch(sampleDistance);
                float marchJitter = jitter;
                [flatten] if (shaftStep)
                {
                    march = ShaftSunMarch();
                    marchJitter = frac(jitter + s * kPlasticNumberFraction);
                }
#if SHAFT_MEMORY
                float3 fromCamera = float3(directionWorld.xy * sampleDistance, sampleHeight - cameraWorld.z);
                float memoryVisibility = SunVisibilityInMemory(sShaftOccluders, CurrentMemory(), fromCamera);
                [branch] if (MemoryBeforeWeight() > 0)
                    memoryVisibility =
                        lerp(memoryVisibility, SunVisibilityInMemory(sShaftMemoryBefore, MemoryBefore(), fromCamera),
                             MemoryBeforeWeight());
                sunVisibility *= memoryVisibility;
#else
                sunVisibility *= SunVisibility(samplePosition, march, marchJitter);
#endif
            }
            [branch] if (CloudStrength(cCloudShape) > 0)
                sunVisibility *= 1 - CloudStrength(cCloudShape) *
                                         CloudCoverAt(sClouds, cCloudMap, cCloudShape,
                                                      float3(directionWorld.xy * sampleDistance, sampleHeight - cameraWorld.z),
                                                      cameraWorld.z, sampleDistance);
            float3 stepRadiance = 0;
            float stepOpticalDepth = 0;
            float3 shaftScatter = 0;
            [unroll] for (int j = 0; j < kFogLayers; j++)
                AccumulateLayer(LoadFogLayer(j), phase[j], skyUpward, hazeColour, hazeFromSky, stepStart,
                                sampleDistance, stepLength, sampleHeight, sunVisibility, scales, ceiling,
                                stepRadiance, stepOpticalDepth, shaftScatter);
            float3 stepShaft = 0;
            [branch] if (shaftStep)
                stepShaft = (shaftScatter + AirShaftScatterPerYard() * (stepLength * scales.x)) *
                            (shaftPhase * sunVisibility * ShaftRangeFade(sampleDistance));
            float stepTransmittance = exp(-stepOpticalDepth);
            float extinctionOverStep = (1 - stepTransmittance) / max(stepOpticalDepth, kMinOpticalDepth);
            float inStepAttenuation = stepOpticalDepth > kMinOpticalDepth ? extinctionOverStep : 1;
            inScatteredRadiance += transmittance * stepRadiance * inStepAttenuation;
            shaftRadiance += transmittance * stepShaft * inStepAttenuation;
            transmittance *= stepTransmittance;
        }
    }
    float shaftPeak = max(max(shaftRadiance.r, shaftRadiance.g), shaftRadiance.b) * ShaftGlarePerBrightness();
    return float4(inScatteredRadiance + shaftRadiance * pow(1 + shaftPeak * shaftPeak, -kShaftGlarePower),
                  1 - transmittance);
}
