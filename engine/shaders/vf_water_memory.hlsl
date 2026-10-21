#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"
#include "vf_water_memory.hlsli"

static const float kMinOnScreenViewZ = 0.02;
static const float kNeighbourTexels = 2;
static const float kAcceptTexels = 1.5;
static const float kNotRemembered = -2;
static const float kFartherThanSky = 2 * kSkyYards;
static const float3 kParallaxTexels = float3(4, 8, 16);
static const float kMinMotionYards = 1e-4;

float3 MovedPoint(float4 remembered)
{
    return RememberedSky(remembered) ? remembered.xyz : remembered.xyz - MemoryMotion();
}

float NextYards(float4 remembered, float yards)
{
    return RememberedPoint(remembered) ? length(MovedPoint(remembered))
                                       : RememberedSky(remembered) ? kSkyYards : yards;
}

float4 Reprojected(float3 direction, float yards)
{
    yards = NextYards(RememberedAt(normalize(direction * yards + MemoryMotion())), yards);
    float2 uv = OctahedralUv(normalize(direction * yards + MemoryMotion()));
    float texel = MemoryTexelSize();
    float4 best = 0;
    float bestCosine = kNotRemembered;
    [loop] for (int k = 0; k < 5; k++)
    {
        float2 offset = k == 0 ? 0 : k == 1 ? float2(texel, 0) : k == 2 ? float2(-texel, 0)
                                                               : k == 3 ? float2(0, texel) : float2(0, -texel);
        float4 remembered = tex2Dlod(sMemory, float4(uv + offset, 0, 0));
        float3 moved = MovedPoint(remembered);
        float cosine = remembered.w != 0 ? dot(moved, direction) * rsqrt(max(dot(moved, moved), 1e-12))
                                          : kNotRemembered;
        bool better = cosine > bestCosine;
        best = better ? float4(moved, remembered.w) : best;
        bestCosine = better ? cosine : bestCosine;
    }
    return best;
}

bool Accepted(float4 candidate, float3 direction, float acceptCosine)
{
    return candidate.w != 0 && dot(candidate.xyz, direction) > acceptCosine * length(candidate.xyz);
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 uv = (vpos + 0.5) * MemoryTexelSize();
    float3 direction = OctahedralDirection(uv);
    float3 view = WorldToViewDirection(direction);
    float2 pixel = ViewToPixel(float3(view.xy, max(view.z, kMinOnScreenViewZ)));
    [branch] if (view.z > kMinOnScreenViewZ && !OutsideViewport(pixel))
    {
        float2 centre = min(floor(pixel) + 0.5, ViewportLastPixelCentre());
        float depth = SampleDepth(sDepthBefore, centre);
        float3 ray = ViewToWorldDirection(ViewRayAtUnitDepth(centre));
        float code = PackColour(SceneAt(sSceneBefore, centre));
        return IsSky(depth) ? float4(normalize(ray), -code) : float4(ray * LinearDepth(depth), code);
    }
    float4 result = 0;
    [branch] if (MemoryHistory())
    {
        float4 here = RememberedAt(direction);
        float step = kNeighbourTexels * MemoryTexelSize();
        float4 around[4];
        around[0] = RememberedAt(OctahedralDirection(uv + float2(step, 0)));
        around[1] = RememberedAt(OctahedralDirection(uv - float2(step, 0)));
        around[2] = RememberedAt(OctahedralDirection(uv + float2(0, step)));
        around[3] = RememberedAt(OctahedralDirection(uv - float2(0, step)));
        float nearest = kSkyYards;
        float4 sky = RememberedSky(here) ? here : 0;
        [unroll] for (int k = 0; k < 4; k++)
        {
            nearest = RememberedPoint(around[k]) ? min(nearest, length(MovedPoint(around[k]))) : nearest;
            sky = RememberedSky(around[k]) && !RememberedSky(sky) ? around[k] : sky;
        }
        float3 parallaxYards = max(length(MemoryMotion()), kMinMotionYards) / (kParallaxTexels * MemoryTexelRadians());
        [unroll] for (int p = 0; p < 3; p++)
        {
            float4 behind = RememberedAt(direction * parallaxYards[p] + MemoryMotion());
            nearest = RememberedPoint(behind) ? min(nearest, length(MovedPoint(behind))) : nearest;
            sky = RememberedSky(behind) && !RememberedSky(sky) ? behind : sky;
        }
        result = RememberedSky(sky) ? float4(direction, sky.w) : 0;
        [branch] if (RememberedPoint(here) || nearest < kSkyYards)
        {
            float texelCosine = min(dot(direction, OctahedralDirection(uv + float2(MemoryTexelSize(), 0))),
                                    dot(direction, OctahedralDirection(uv + float2(0, MemoryTexelSize()))));
            float acceptCosine = 1 - kAcceptTexels * kAcceptTexels * (1 - texelCosine);
            float4 taken = 0;
            float takenYards = kFartherThanSky;
            [loop] for (int seed = 0; seed < 2; seed++)
            {
                float4 candidate = Reprojected(direction, seed == 0 ? NextYards(here, nearest) : nearest);
                float yards = RememberedYards(candidate);
                bool take = Accepted(candidate, direction, acceptCosine) && yards < takenYards;
                taken = take ? candidate : taken;
                takenYards = take ? yards : takenYards;
            }
            taken = RememberedSky(taken) ? float4(direction, taken.w) : taken;
            result = taken.w != 0 ? taken : result;
        }
    }
    return result;
}
