#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"
#include "vf_water_memory.hlsli"

sampler2D sTerrain : register(s4);
sampler2D sSlopes : register(s5);

static const int kReflectionSteps = 24;
static const int kRefineSteps = 4;
static const float kFirstStepYards = 0.4;
static const float kStepGrowth = 1.36;
static const float kNearViewZ = 0.5;
static const float kMinThickness = 1;
static const float kThicknessPerViewZ = 0.03;
static const float kThicknessPerStep = 1.5;
static const float kEdgeFadePixels = 32;
static const float kReachFadeShare = 0.25;
static const float kMinSkyViewZ = 0.01;
static const float kMinHalfwayCosine = 0.01;
static const float kMaxGlitter = 8;
static const int kMemorySteps = 24;
static const int kMemoryRefineSteps = 6;
static const float kMemoryFirstStepYards = 0.5;
static const float kMemoryFarYards = 3200;
static const float kMemoryMinThickness = 1;
static const float kMemoryThicknessPerYard = 0.03;
static const float kMemoryThicknessPerStep = 1.5;
static const float kFullConfidence = 0.999;
static const float kAheadMarginYards = 0.1;
static const float kAheadMarginPerYard = 0.003;
static const float kMinHorizontal = 0.001;

struct Remembered
{
    float found;
    float3 colour;
    float seenYards;
    float travel;
};

float2 Heading(float3 direction)
{
    return direction.xy * rsqrt(max(dot(direction.xy, direction.xy), kMinHorizontal * kMinHorizontal));
}

bool SeenPastTheWater(float3 probe, float sceneZ, float3 ahead, float surfaceAhead)
{
    return (dot(probe, ahead) / probe.z + kAheadMarginPerYard) * sceneZ > surfaceAhead;
}

bool RememberedPastTheWater(float4 seen, float seenYards, float2 heading, float startAhead)
{
    return dot(seen.xy, heading) + kAheadMarginPerYard * seenYards > startAhead;
}

Remembered MarchMemory(float3 start, float3 ray, float from, float jitter)
{
    float2 heading = Heading(ray);
    float startAhead = dot(start.xy, heading) - kAheadMarginYards;
    from = max(from, kMemoryFirstStepYards);
    float growth = pow(kMemoryFarYards / from, 1.0 / kMemorySteps);
    float travel = from * pow(growth, jitter - 1);
    float previous = travel / growth;
    float found = 0;
    float low = 0;
    float high = 0;
    float4 hitSeen = 0;
    [loop] for (int i = 0; i < kMemorySteps; i++)
    {
        float3 probe = start + ray * travel;
        float yards = length(probe);
        float4 seen = RememberedAt(probe);
        float seenYards = length(seen.xyz);
        float behind = yards - seenYards;
        float thickness = max(kMemoryMinThickness + kMemoryThicknessPerYard * seenYards,
                              (travel - previous) * abs(dot(ray, probe)) / yards * kMemoryThicknessPerStep);
        bool here = found == 0 && RememberedPoint(seen) && behind > 0 && behind < thickness &&
                    RememberedPastTheWater(seen, seenYards, heading, startAhead);
        low = here ? previous : low;
        high = here ? travel : high;
        hitSeen = here ? seen : hitSeen;
        found = here ? 1 : found;
        previous = travel;
        travel *= growth;
    }
    [branch] if (found > 0)
    {
        [loop] for (int k = 0; k < kMemoryRefineSteps; k++)
        {
            float middle = 0.5 * (low + high);
            float3 probe = start + ray * middle;
            float4 seen = RememberedAt(probe);
            float seenYards = RememberedYards(seen);
            bool beyond = length(probe) > seenYards && RememberedPastTheWater(seen, seenYards, heading, startAhead);
            high = beyond ? middle : high;
            low = beyond ? low : middle;
            hitSeen = beyond ? seen : hitSeen;
        }
    }
    Remembered r;
    r.found = found;
    r.colour = UnpackColour(hitSeen.w);
    r.seenYards = length(hitSeen.xyz);
    r.travel = high;
    return r;
}

float3 SkyGradient(float3 direction)
{
    return lerp(cSkyHorizon.rgb, cSkyZenith.rgb, sqrt(saturate(direction.z)));
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 pixel = vpos + 0.5;
    WaterSurface s = ReadSurface(sSlopes, pixel);
    float3 normal = SurfaceNormal(s);
    float3 toCamera = -s.toSurfaceWorld;

    float reflectWeight = saturate(Fresnel(dot(normal, toCamera)) * Reflectivity());
    float3 reflectedWorld = reflect(s.toSurfaceWorld, normal);
    reflectedWorld.z = abs(reflectedWorld.z);
    float3 reflected = WorldToViewDirection(reflectedWorld);
    float3 ahead = WorldToViewDirection(float3(Heading(reflectedWorld), 0));
    float surfaceAhead = dot(s.surface, ahead) - kAheadMarginYards;

    float reach = MaxReflectionYards();
    [flatten] if (reflected.z < 0)
        reach = min(reach, max(s.surfaceZ - kNearViewZ, 0) / -reflected.z);
    float jitter = InterleavedGradientNoise(vpos);
    float travel = kFirstStepYards * pow(kStepGrowth, jitter);
    float previous = 0;
    float hit = 0;
    [loop] for (int i = 0; i < kReflectionSteps; i++)
    {
        float3 probe = s.surface + reflected * travel;
        float2 probePixel = ViewToPixel(probe);
        [branch] if (travel > reach || OutsideViewport(probePixel))
            break;
        float sceneZ = ViewZBefore(probePixel);
        float behind = probe.z - sceneZ;
        float thickness = max(kMinThickness + kThicknessPerViewZ * sceneZ,
                              (travel - previous) * abs(reflected.z) * kThicknessPerStep);
        [branch] if (behind > 0 && behind < thickness && SeenPastTheWater(probe, sceneZ, ahead, surfaceAhead))
        {
            hit = 1;
            break;
        }
        previous = travel;
        travel *= kStepGrowth;
    }
    [branch] if (hit > 0)
    {
        float low = previous;
        float high = travel;
        [loop] for (int r = 0; r < kRefineSteps; r++)
        {
            float middle = 0.5 * (low + high);
            float3 probe = s.surface + reflected * middle;
            float sceneZ = ViewZBefore(ViewToPixel(probe));
            bool beyond = probe.z > sceneZ && SeenPastTheWater(probe, sceneZ, ahead, surfaceAhead);
            high = beyond ? middle : high;
            low = beyond ? low : middle;
        }
        travel = high;
    }
    float3 hitView = s.surface + reflected * travel;
    float2 hitPixel = ViewToPixel(hitView);
    float2 edge = min(hitPixel - ViewportOrigin(), ViewportOrigin() + ViewportSize() - hitPixel);
    float confidence = hit * (MemoryOn() ? 1 : saturate(min(edge.x, edge.y) / kEdgeFadePixels)) *
                       saturate((1 - travel / MaxReflectionYards()) / kReachFadeShare);
    float surfaceYards = s.surfaceZ * s.rayLength;
    float3 hitColour = lerp(SceneAt(sSceneBefore, hitPixel), cSkyHorizon.rgb,
                            ExtraFog(length(hitView), surfaceYards + travel));

    float3 sky = SkyGradient(reflectedWorld);
    float4 skySeen = 0;
    float remembered = 0;
    float3 rememberedColour = 0;
    [branch] if (MemoryOn() && confidence < kFullConfidence)
    {
        Remembered m = MarchMemory(s.relative, reflectedWorld,
                                   min(previous, MaxReflectionYards() * (1 - kReachFadeShare)), jitter);
        remembered = m.found;
        rememberedColour = lerp(m.colour, cSkyHorizon.rgb, ExtraFog(m.seenYards, surfaceYards + m.travel));
        skySeen = RememberedAt(reflectedWorld);
    }
    [branch] if (RememberedSky(skySeen))
        sky = UnpackColour(skySeen.w);
    else
    {
        float2 skyPixel = clamp(ViewToPixel(reflected), ViewportOrigin() + 0.5, ViewportLastPixelCentre());
        [branch] if (reflected.z > kMinSkyViewZ && IsSky(SampleDepth(sDepthBefore, skyPixel)))
            sky = SceneAt(sSceneBefore, skyPixel);
    }
    float3 reflection = lerp(lerp(sky, rememberedColour, remembered), hitColour, confidence);

    float3 halfway = normalize(cToLight.xyz + toCamera);
    float halfwayCosine = max(dot(normal, halfway), kMinHalfwayCosine);
    float cosine2 = halfwayCosine * halfwayCosine;
    float lobe = exp((cosine2 - 1) / (cosine2 * s.roughness)) / (kPi * s.roughness * cosine2 * cosine2);
    float terrainLight = saturate((cWaveOrigin.z + s.relative.z) * cTerrain.w +
                                  tex2Dlod(sTerrain, float4(cTerrain.xy + s.relative.xy * cTerrain.z, 0, 0)).r);
    float glitter = min(cToLight.w * lobe * Fresnel(dot(toCamera, halfway)) /
                            (4 * max(dot(normal, toCamera), kMinGrazing)),
                        kMaxGlitter) *
                    saturate(dot(normal, cToLight.xyz)) * terrainLight * (1 - confidence) * (1 - remembered);

    return float4(reflection * reflectWeight + glitter * cGlitterColour.rgb, reflectWeight);
}
