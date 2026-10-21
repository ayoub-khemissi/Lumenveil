#include "vf_common.hlsli"
#include "vf_clouds.hlsli"

float4 cComposite : register(c9);
float4 cGodRayColour : register(c10);
float4 cSun : register(c11);
float4 cGlow : register(c12);
float4 cSurfaceShadowMap : register(c13);
float4 cSurfaceShade : register(c14);
float4 cSurfaceShadowReach : register(c15);
float4 cSurfaceShadowOutward : register(c16);
float4 cLamps : register(c17);
float4 cLampSurfaceLight : register(c18);
float4 cAlbedoPerSceneLight : register(c19);
float4 cRootSceneLight : register(c20);
float4 cOvercastSky : register(c21);
float4 cLowSunLift : register(c22);
float4 cLowSunDirection : register(c23);
float4 cCloudMap : register(c24);
float4 cCloudShape : register(c25);
float4 cCloudShade : register(c26);
float4 cGrade : register(c27);
float4 cLightBalance : register(c28);
float4 cSkyGlow : register(c29);
float4 cWhiteBalance : register(c30);
float4 cSkyWhiteBalance : register(c31);
float4 cSunAndMoon : register(c32);

sampler2D sDepth : register(s0);
sampler2D sFog : register(s1);
sampler2D sGodRays : register(s2);
sampler2D sSceneBeforeFog : register(s3);
sampler2D sLightGlow : register(s4);
sampler2D sTerrainShadow : register(s5);
sampler2D sLampHalos : register(s6);
sampler2D sLampSurfaces : register(s7);
sampler2D sClouds : register(s8);
sampler2D sSurfaceBeforeLateDraws : register(s9);
sampler2D sDepthBeforeLateDraws : register(s10);
sampler2D sSunAndMoon : register(s11);
sampler2D sWetSurfaces : register(s12);

static const float kHighlightKnee = 0.8;
static const float kSunMarkerRadius = 6;
static const float4 kSunMarkerColour = float4(1, 0, 0, 1);
static const float kDebugRadiance = 1;
static const float kDebugTransmittance = 2;
static const float kDebugLinearDepth = 3;
static const float kDebugSurfaceShadow = 4;
static const float kDebugLampHalos = 5;
static const float kDebugLampLight = 6;
static const float kFixedFunctionNightScene = 0.15;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
static const float kSelfLitFrom = 0.9;
static const float kSelfLitSpan = 0.9;
static const float kAlbedoChroma = 0.5;
static const float kOverdriveWhitening = 1;
static const float kPixelFacingUntil = 60;
static const float kTerrainFacingFrom = 160;
static const float kUnknownGround = 5e4;
static const float kThinFacingCode = 0.75;
static const float kFacingCodeScale = 2;
static const float kAverageSunFacing = 0.5;
static const float kSkyHaloShare = 0.35;
static const float kSkyHaloFocus = 0.6;
static const float kSkyDiscFocus = 0.98;
static const float kCoverClearance = 3;
static const float kGroundCoverTop = 1;
static const float kSolidAboveGround = 2.5;
static const float kSunAndMoonFogShare = 0.5;

float Exposure()
{
    return cComposite.x;
}

float GodRayStrength()
{
    return cComposite.y;
}

float DebugView()
{
    return cComposite.z;
}

bool DebugViewEnabled()
{
    return DebugView() > 0.5;
}

bool DebugViewAtMost(float view)
{
    return DebugView() < view + 0.5;
}

float BlendMode()
{
    return cComposite.w;
}

bool BlendsInLinearLight()
{
    return BlendMode() > 0.5;
}

bool BlendsOverSceneCopy()
{
    return BlendMode() > 0.5 && BlendMode() < 1.5;
}

float2 SunPixel()
{
    return cSun.xy;
}

bool SunMarkerEnabled()
{
    return cSun.z > 0;
}

float ClientGlowToCompensate()
{
    return cSun.w;
}

float LightGlowStrength()
{
    return cGlow.x;
}

float LightGlowCeiling()
{
    return cGlow.y;
}

float HorizonTerrainAsLand(float depth)
{
    return BeyondFarClip(depth) && !IsSky(depth) ? cGlow.z : 0;
}

float2 SurfaceShadowUvAtCamera()
{
    return cSurfaceShadowMap.xy;
}

float SurfaceShadowUvPerYard()
{
    return cSurfaceShadowMap.z;
}

float SurfaceShadowSharpness()
{
    return cSurfaceShadowMap.w;
}

float3 FullSurfaceShadowShade()
{
    return cSurfaceShade.rgb;
}

bool SurfaceShadowsEnabled()
{
    return cSurfaceShade.w > 0.5;
}

float TerrainMapTexels()
{
    return cSurfaceShadowReach.x;
}

float MapEdgeFadePerUv()
{
    return cSurfaceShadowReach.y;
}

float MapEdgeFadeOffset()
{
    return cSurfaceShadowReach.z;
}

float UncoveredGain()
{
    return cSurfaceShadowReach.w;
}

float OutwardLookupCells()
{
    return cSurfaceShadowOutward.x;
}

float GroundRisePerTexel()
{
    return cSurfaceShadowOutward.y;
}

bool LampHalosEnabled()
{
    return cLamps.x > 0.5;
}

bool LampSurfaceLightEnabled()
{
    return cLampSurfaceLight.x > 0.5;
}

float FixedFunctionAlbedo()
{
    return cLampSurfaceLight.y;
}

float AlbedoCap()
{
    return cLampSurfaceLight.z;
}

float NightDarkness()
{
    return cLampSurfaceLight.w;
}

float SelfLitShare()
{
    return cLamps.y;
}

bool SurfaceBeforeLateDrawsEnabled()
{
    return cLamps.z > 0;
}

bool WetSurfacesEnabled()
{
    return cLamps.w > 0;
}

float3 AlbedoPerSceneLight()
{
    return cAlbedoPerSceneLight.rgb;
}

float3 RootSceneLight()
{
    return cRootSceneLight.rgb;
}

float SceneLightFacingNothing()
{
    return cAlbedoPerSceneLight.w;
}

float SceneLightPerFacing()
{
    return cRootSceneLight.w;
}

float3 Graded(float3 colour, float depth, float sunGlowShare)
{
    [branch] if (cGrade.z > 0.5)
    {
        bool sky = BeyondFarClip(depth);
        float ownColour = sky ? lerp(sunGlowShare * (1 - cGrade.w), 1, HorizonTerrainAsLand(depth)) : 1;
        float amount = cGrade.x * lerp(cGrade.y, 1, ownColour);
        colour *= lerp(cSkyWhiteBalance.rgb, cWhiteBalance.rgb, ownColour);
        colour = max(lerp(dot(colour, kLuminanceWeights).xxx, colour, amount), 0);
    }
    return colour;
}

float LateDrawsInFront()
{
    return cLightBalance.w;
}

float LateDrawYards()
{
    return cSkyGlow.w;
}

float LateDrawTransmittance(float fogOpacity, float viewZ, float sceneTransmittance)
{
    float nearTransmittance = pow(max(1 - fogOpacity, 1e-4), saturate(LateDrawYards() / max(viewZ, 1e-3)));
    return lerp(sceneTransmittance, nearTransmittance, LateDrawsInFront());
}

float3 BalancedShade(float3 sceneShade, float depth)
{
    return BeyondFarClip(depth) ? sceneShade : sceneShade * cLightBalance.rgb;
}

float ScatteringPeakShare(float towardSun, float g)
{
    return pow((1 - g) * (1 - g) / (1 + g * g - 2 * g * towardSun), 1.5);
}

float SunGlowInHaze(float3 fromCamera)
{
    float towardSun = dot(normalize(fromCamera), cLowSunDirection.xyz);
    return kSkyHaloShare * ScatteringPeakShare(towardSun, kSkyHaloFocus) +
           (1 - kSkyHaloShare) * ScatteringPeakShare(towardSun, kSkyDiscFocus);
}

float HazeTowardZenith(float3 fromCamera)
{
    float belowZenith = 1 - saturate(normalize(fromCamera).z);
    float squared = belowZenith * belowZenith;
    return lerp(cSkyWhiteBalance.w, 1, squared * squared * squared);
}

float4 WithOvercastSky(float4 fog, float depth, float3 fromCamera, out float sunGlowLight)
{
    float overcast = BeyondFarClip(depth) ? cOvercastSky.w * (1 - HorizonTerrainAsLand(depth)) : 0;
    float3 sky = cOvercastSky.rgb;
    sunGlowLight = 0;
    [branch] if (overcast > 0)
    {
        float3 glow = cSkyGlow.rgb * SunGlowInHaze(fromCamera);
        sky = sky * HazeTowardZenith(fromCamera) + glow;
        sunGlowLight = (1 - fog.a) * overcast * Exposure() * dot(glow, kLuminanceWeights);
    }
    return float4(fog.rgb + (1 - fog.a) * overcast * sky * Exposure(), 1 - (1 - fog.a) * (1 - overcast));
}

float3 PointLightGain(float rootOfKeptLight, float3 pointLight)
{
    float3 base = RootSceneLight() * rootOfKeptLight;
    float3 sum = base + pointLight;
    float brightest = max(max(sum.r, sum.g), sum.b);
    float3 hueKept = sum / max(brightest, 1);
    float whitening = saturate((brightest - 1) / (brightest + kOverdriveWhitening));
    float3 lit = lerp(hueKept, min(sum, 1), whitening);
    return max(lit * lit - base * base, 0);
}

float3 GammaToLinear(float3 colour)
{
    return pow(colour, 2.2);
}

float3 LinearToGamma(float3 colour)
{
    return pow(colour, 1 / 2.2);
}

float3 RollOffHighlights(float3 colour, float3 knee)
{
    float3 span = max(1 - knee, 1e-4);
    return colour <= knee ? colour : knee + span * (1 - exp(-(colour - knee) / span));
}

float3 BeforeClientGlow(float3 onScreen, float glow)
{
    return (sqrt(1 + 4 * glow * onScreen) - 1) / (2 * glow);
}

float4 DepthAwareUpsample(float2 pixel, float viewZ, out float3 lampHalos)
{
    float2 lowResCoord = FullPixelToLowResTexel(pixel);
    float2 baseTexel = floor(lowResCoord);
    float2 bilinearFraction = lowResCoord - baseTexel;

    static const float2 kTapOffsets[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
    float4 weightedFog = 0;
    float weightSum = 0;
    float4 tapWeights = 0;
    [unroll] for (int j = 0; j < 4; j++)
    {
        float2 tapOffset = kTapOffsets[j];
        float2 tapTexel = clamp(baseTexel + tapOffset, 0, LowResSize() - 1);
        float tapViewZ = LinearDepth(SampleDepth(sDepth, LowResTexelToFullPixel(tapTexel)));
        float2 bilinearWeight = lerp(1 - bilinearFraction, bilinearFraction, tapOffset);
        float relativeDepthDifference = abs(tapViewZ - viewZ) / max(viewZ, 1e-3);
        float weight = bilinearWeight.x * bilinearWeight.y / (1e-3 + relativeDepthDifference) + 1e-6;
        weightedFog += tex2Dlod(sFog, float4(LowResTexelToUv(tapTexel), 0, 0)) * weight;
        weightSum += weight;
        tapWeights[j] = weight;
    }
    lampHalos = 0;
    [branch] if (LampHalosEnabled())
    {
        [unroll] for (int k = 0; k < 4; k++)
        {
            float2 tapTexel = clamp(baseTexel + kTapOffsets[k], 0, LowResSize() - 1);
            lampHalos += tex2Dlod(sLampHalos, float4(LowResTexelToUv(tapTexel), 0, 0)).rgb * tapWeights[k];
        }
        lampHalos /= weightSum;
    }
    return weightedFog / weightSum;
}

float3 OutwardNormal(float3 fromCamera, float3 facing)
{
    float lengthSquared = dot(facing, facing);
    float3 normal = lengthSquared > 1e-12 ? facing * rsqrt(lengthSquared) : 0;
    return dot(normal, fromCamera) > 0 ? -normal : normal;
}

float4 SurfaceLightCoverAndSlopeFacing(float3 fromCamera, float3 facing)
{
    float2 uv = SurfaceShadowUvAtCamera() + fromCamera.xy * SurfaceShadowUvPerYard();
    float2 outward = OutwardNormal(fromCamera, facing).xy;
    float2 texel = uv * TerrainMapTexels() - 0.5 + outward * OutwardLookupCells();
    float2 firstCell = floor(texel);
    float2 fraction = texel - firstCell;
    float heightTerm = (CameraPositionWorld().z + fromCamera.z) * SurfaceShadowSharpness();

    static const float2 kCorners[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
    float weightedLight = 0;
    float uncoveredWeight = 0;
    float4 ground = 0;
    [unroll] for (int k = 0; k < 4; k++)
    {
        float2 corner = kCorners[k];
        float2 cellUv = (firstCell + corner + 0.5) / TerrainMapTexels();
        float2 cell = tex2Dlod(sTerrainShadow, float4(cellUv, 0, 0)).rg;
        ground[k] = cell.y;
        float2 lightAndUncovered = saturate(heightTerm + cell);
        float2 bilinear = lerp(1 - fraction, fraction, corner);
        float weight = bilinear.x * bilinear.y * lightAndUncovered.y;
        weightedLight += weight * lightAndUncovered.x;
        uncoveredWeight += weight;
    }
    float light = uncoveredWeight > 1e-4 ? weightedLight / uncoveredWeight : 1;
    float2 edgeDistance = min(uv, 1 - uv);
    float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) * MapEdgeFadePerUv() + MapEdgeFadeOffset());
    float2 rise = float2(lerp(ground.y - ground.x, ground.w - ground.z, fraction.y),
                         lerp(ground.z - ground.x, ground.w - ground.y, fraction.x)) *
                  GroundRisePerTexel();
    bool known = max(max(abs(ground.x), abs(ground.y)), max(abs(ground.z), abs(ground.w))) < kUnknownGround;
    float slopeFacing = saturate(dot(normalize(float3(known ? -rise : 0, 1)), cLowSunDirection.xyz));
    float groundHere = lerp(lerp(ground.x, ground.y, fraction.x), lerp(ground.z, ground.w, fraction.x), fraction.y);
    float2 shiftYards = outward * OutwardLookupCells() / (SurfaceShadowUvPerYard() * TerrainMapTexels());
    float aboveGround =
        (heightTerm + groundHere - 1) / SurfaceShadowSharpness() - kCoverClearance + dot(rise, shiftYards);
    float nearGround = known ? 1 - smoothstep(kGroundCoverTop, kSolidAboveGround, aboveGround) : 0;
    return float4(light, saturate(uncoveredWeight * UncoveredGain()) * edgeFade, slopeFacing, nearGround);
}

float3 SurfaceShadowShade(float depth, float viewZ, float3 fromCamera, float3 facing, float pixelSunFacing)
{
    float3 shade = 1;
    [branch] if (SurfaceShadowsEnabled() && !BeyondFarClip(depth))
    {
        float4 lit = SurfaceLightCoverAndSlopeFacing(fromCamera, facing);
        float slopeShare =
            pixelSunFacing < 0 ? 1 : max(lit.w, smoothstep(kPixelFacingUntil, kTerrainFacingFrom, viewZ));
        float towardSun = lerp(pixelSunFacing, lit.z, slopeShare);
        float sunlit = lit.x * lit.y;
        float clouded = 0;
        [branch] if (CloudStrength(cCloudShape) > 0)
            clouded = CloudStrength(cCloudShape) *
                      CloudCoverAt(sClouds, cCloudMap, cCloudShape, fromCamera, CameraPositionWorld().z,
                                   length(fromCamera));
        shade = lerp(1, FullSurfaceShadowShade(), (1 - lit.x) * lit.y) * lerp(1, cCloudShade.rgb, clouded * sunlit) +
                cLowSunLift.rgb * (sunlit * towardSun * (1 - clouded));
    }
    return shade;
}

float3 LinearLightGlow(float2 viewportUv)
{
    float3 glow = 0;
    [branch] if (LightGlowStrength() > 0)
    {
        float3 blurred = tex2Dlod(sLightGlow, float4(viewportUv, 0, 0)).rgb;
        glow = LightGlowCeiling() * (1 - exp(-blurred * LightGlowStrength() / LightGlowCeiling()));
    }
    return glow;
}

float4 WithLightGlowInFog(float4 fog, float3 linearGlow, bool linearLight)
{
    float3 glow = linearLight ? linearGlow : LinearToGamma(linearGlow);
    return float4(fog.rgb + glow * fog.a, fog.a);
}

float3 DisplaySpaceGodRays(float2 viewportUv)
{
    float3 godRays = 0;
    [branch] if (GodRayStrength() > 0)
        godRays = tex2Dlod(sGodRays, float4(viewportUv, 0, 0)).rgb * cGodRayColour.rgb * GodRayStrength();
    return godRays;
}

float4 LampLightOnSurfaces(float2 viewportUv)
{
    float4 light = float4(0, 0, 0, 1);
    [branch] if (LampSurfaceLightEnabled())
        light = tex2Dlod(sLampSurfaces, float4(viewportUv, 0, 0));
    return light;
}

float Moonlit(float albedoLuminance)
{
    return 1 - saturate((albedoLuminance * SelfLitShare() - kSelfLitFrom) / kSelfLitSpan);
}

float3 SurfaceUnderLateDraws(float3 before, float2 viewportUv, float2 pixel, float depth)
{
    float3 surface = before;
    [branch] if (SurfaceBeforeLateDrawsEnabled())
    {
        float3 atLiquidEnd = GammaToLinear(tex2Dlod(sSurfaceBeforeLateDraws, float4(viewportUv, 0, 0)).rgb);
        surface = SampleDepth(sDepthBeforeLateDraws, pixel) == depth ? min(before, atLiquidEnd) : before;
    }
    return surface;
}

float3 SceneWithLampLight(float3 before, float3 surface, float3 sceneShade, float4 lampLight, out float3 lateDraws)
{
    float3 perLight = AlbedoPerSceneLight() / lampLight.a;
    float3 albedo = surface * perLight;
    float albedoLuminance = dot(albedo, kLuminanceWeights);
    float3 kept = sceneShade * (1 - NightDarkness() * Moonlit(albedoLuminance));
    float3 added = before - surface;
    float3 addedKept = sceneShade * (1 - NightDarkness() * Moonlit(dot(added * perLight, kLuminanceWeights)));
    lateDraws = added * addedKept;
    return surface * kept + min(lerp(albedoLuminance, albedo, kAlbedoChroma), AlbedoCap()) *
                                PointLightGain(sqrt(dot(kept, kLuminanceWeights) * lampLight.a), lampLight.rgb);
}

float3 WithWetSurfaces(float3 scene, float2 viewportUv)
{
    [branch] if (WetSurfacesEnabled())
    {
        float4 wet = tex2Dlod(sWetSurfaces, float4(viewportUv, 0, 0));
        scene = scene * wet.a + wet.rgb * cLightBalance.rgb;
    }
    return scene;
}

float3 WithSunAndMoon(float3 colour, float2 pixel, float depth, float fogOpacity)
{
    [branch] if (cSunAndMoon.x > 0 && IsSky(depth))
    {
        float4 drawn = tex2Dlod(sSunAndMoon, float4(pixel * cSunAndMoon.yz, 0, 0));
        float shown = cSunAndMoon.x * (1 - kSunAndMoonFogShare * fogOpacity);
        colour += drawn.rgb * shown * (1 - saturate(colour));
    }
    return colour;
}

float4 BlendOverSceneCopy(float2 viewportUv, float2 pixel, float depth, float viewZ, float4 fog, float fogOpacity,
                          float3 godRays, float3 sceneShade, float3 lampHalos, float4 lampLight, float sunGlowLight)
{
    float3 before = GammaToLinear(tex2Dlod(sSceneBeforeFog, float4(viewportUv, 0, 0)).rgb);
    float3 lateDraws;
    float3 scene = WithWetSurfaces(SceneWithLampLight(before, SurfaceUnderLateDraws(before, viewportUv, pixel, depth),
                                                      sceneShade, lampLight, lateDraws),
                                   viewportUv);
    float transmittance = 1 - fog.a;
    float3 light = scene * transmittance + lateDraws * LateDrawTransmittance(fogOpacity, viewZ, transmittance) +
                   fog.rgb + lampHalos;
    float3 colour = LinearToGamma(RollOffHighlights(light, max(kHighlightKnee, scene + lateDraws)));
    [branch] if (ClientGlowToCompensate() > 0)
        colour = lerp(colour, BeforeClientGlow(colour, ClientGlowToCompensate()), fog.a);
    float sunGlowShare = saturate(sunGlowLight / max(dot(light, kLuminanceWeights), 1e-4));
    return float4(WithSunAndMoon(Graded(colour + godRays, depth, sunGlowShare), pixel, depth, fogOpacity), 1);
}

float3 DisplayLampHalos(float3 lampHalos)
{
    float3 nightScene = GammaToLinear(kFixedFunctionNightScene.xxx);
    return LinearToGamma(nightScene + lampHalos) - kFixedFunctionNightScene;
}

float4 PremultipliedForFixedFunctionBlend(float4 fog, float3 godRays, bool linearLight, float3 sceneShade,
                                          float3 lampHalos, float3 lampLight)
{
    float opacity = max(fog.a, 1e-4);
    float3 unpremultiplied = RollOffHighlights(fog.rgb / opacity, kHighlightKnee);
    if (linearLight)
        unpremultiplied = LinearToGamma(unpremultiplied);
    float displayShade = LinearToGamma(saturate(dot(sceneShade, kLuminanceWeights) * (1 - NightDarkness())).xxx).x;
    float3 lampGain = PointLightGain(displayShade, lampLight) * FixedFunctionAlbedo();
    return float4(unpremultiplied * fog.a + godRays + DisplayLampHalos(lampHalos + lampGain),
                  1 - (1 - fog.a) * displayShade);
}

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 pixel = pixelIndex + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    float viewZ = LinearDepth(depth);
    float3 lampHalos;
    float4 fog = DepthAwareUpsample(pixel, viewZ, lampHalos);
    float3 fromCamera = ViewToWorldDirection(ViewRayAtUnitDepth(pixel) * viewZ);
    float2 viewportUv = (pixel - ViewportOrigin()) / ViewportSize();
    float4 lampLight = LampLightOnSurfaces(viewportUv);
    bool thin = lampLight.a > kThinFacingCode;
    float sunFacing = lampLight.a * kFacingCodeScale;
    lampLight.a = SceneLightFacingNothing() + SceneLightPerFacing() * (thin ? kAverageSunFacing : sunFacing);
    float3 sceneShade =
        SurfaceShadowShade(depth, viewZ, fromCamera, cross(ddx(fromCamera), ddy(fromCamera)), thin ? -1 : sunFacing);
    fog.rgb *= Exposure();
    float fogOpacity = fog.a;
    float sunGlowLight;
    fog = WithOvercastSky(fog, depth, fromCamera, sunGlowLight);

    float3 godRays = DisplaySpaceGodRays(viewportUv);
    const bool linearLight = BlendsInLinearLight();
    fog = WithLightGlowInFog(fog, LinearLightGlow(viewportUv), linearLight);

    [branch] if (DebugViewEnabled())
    {
        if (DebugViewAtMost(kDebugRadiance))
            return float4((linearLight ? LinearToGamma(saturate(fog.rgb)) : fog.rgb) + godRays, 1);
        if (DebugViewAtMost(kDebugTransmittance))
            return float4(1 - fog.aaa, 1);
        if (DebugViewAtMost(kDebugLinearDepth))
            return float4(saturate(viewZ / MaxFogDistance()).xxx, 1);
        float3 shownLight = DebugViewAtMost(kDebugSurfaceShadow) ? sceneShade
                            : DebugViewAtMost(kDebugLampHalos)   ? lampHalos
                                                                 : lampLight.rgb;
        if (DebugViewAtMost(kDebugLampLight))
            return float4(LinearToGamma(saturate(shownLight)), 1);
    }
    [branch] if (SunMarkerEnabled() && distance(pixel, SunPixel()) < kSunMarkerRadius)
        return kSunMarkerColour;

    [branch] if (BlendsOverSceneCopy())
        return BlendOverSceneCopy(viewportUv, pixel, depth, viewZ, fog, fogOpacity, godRays,
                                  BalancedShade(sceneShade, depth), lampHalos, lampLight, sunGlowLight);
    return PremultipliedForFixedFunctionBlend(fog, godRays, linearLight, sceneShade, lampHalos, lampLight.rgb);
}
