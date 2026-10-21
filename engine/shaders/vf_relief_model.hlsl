#ifndef RELIEF
#define RELIEF 1
#endif
#ifndef MATERIAL
#define MATERIAL 0
#endif

#include "vf_material_tag.hlsli"

#define RELIEF_STEPS 24
#define RELIEF_REFINE 4
#define RELIEF_SHADOW_STEPS 12

static const float kReliefGrazingFull = 0.4;
static const float kReliefDepthPerRepeat = 0.125;

float4 c_lightToward : register(c200);
float4 c_direct : register(c201);
float4 c_ambient : register(c202);
float4 c_multiplier : register(c203);
float4 c_emissive : register(c204);
float4 c_layer : register(c205);
float4 c_reliefFade : register(c206);
float4 c_reliefSampling : register(c207);
float4 c_material : register(c209);
float4 c_materialGlint : register(c213);
float4 c_materialKnown : register(c214);

sampler2D s_height : register(s9);

struct Input
{
#if RELIEF
    float4 light : COLOR0;
    float2 uv : TEXCOORD0;
#endif
    float3 viewPosition : TEXCOORD1;
#if RELIEF
    float3 normal : TEXCOORD2;
#endif
};

struct Output
{
#if RELIEF
    float4 offset : COLOR0;
    float4 light : COLOR1;
#endif
#if MATERIAL && RELIEF
    float4 material : COLOR2;
    float4 tag : COLOR3;
#elif MATERIAL
    float4 material : COLOR0;
    float4 tag : COLOR1;
#endif
};

struct Surface
{
    float3 normal;
    float3 gradU;
    float3 gradV;
};

float DepthAt(float2 uv, float depth, float lod)
{
    return depth * (1 - tex2Dlod(s_height, float4(uv, 0, lod)).r);
}

float MarchDepth(float2 uv, float2 uvPerDepth, float maxDepth, float lod)
{
    float stepDepth = maxDepth / RELIEF_STEPS;
    float previousRay = 0;
    float previousSurface = 0;
    float above = maxDepth;
    float aboveGap = 0;
    float below = maxDepth;
    float belowGap = 0;
    float found = 0;
    [loop] for (int i = 0; i <= RELIEF_STEPS; ++i)
    {
        float ray = stepDepth * i;
        float surface = DepthAt(uv + uvPerDepth * ray, maxDepth, lod);
        float crossing = (1 - found) * step(surface, ray) * step(0.5, i);
        float atTop = (1 - found) * step(surface, 0) * step(i, 0.5);
        above = lerp(above, previousRay, crossing);
        aboveGap = lerp(aboveGap, previousSurface - previousRay, crossing);
        below = lerp(below, ray, crossing);
        belowGap = lerp(belowGap, surface - ray, crossing);
        above = lerp(above, 0, atTop);
        below = lerp(below, 0, atTop);
        found = max(found, crossing + atTop);
        previousRay = ray;
        previousSurface = surface;
    }
    [loop] for (int k = 0; k < RELIEF_REFINE; ++k)
    {
        float middle = 0.5 * (above + below);
        float gap = DepthAt(uv + uvPerDepth * middle, maxDepth, lod) - middle;
        float outside = 1 - step(gap, 0);
        above = lerp(above, middle, outside);
        aboveGap = lerp(aboveGap, gap, outside);
        below = lerp(middle, below, outside);
        belowGap = lerp(gap, belowGap, outside);
    }
    float t = saturate(aboveGap / max(aboveGap - belowGap, 1e-6));
    return lerp(above, below, t);
}

float3 DepthAround(float2 uv, float texel, float depth, float lod)
{
    float3 depths = 0;
    [loop] for (int k = 0; k < 3; ++k)
    {
        float2 shift = float2(k == 1, k == 2) * texel;
        depths += float3(k == 0, k == 1, k == 2) * DepthAt(uv + shift, depth, lod);
    }
    return depths;
}

float SelfShadow(float2 hitUv, float hitDepth, float2 uvPerRise, float maxDepth, float lod)
{
    float occlusion = 0;
    [loop] for (int i = 1; i <= RELIEF_SHADOW_STEPS; ++i)
    {
        float rise = hitDepth * i / RELIEF_SHADOW_STEPS;
        float surface = DepthAt(hitUv + uvPerRise * rise, maxDepth, lod);
        occlusion = max(occlusion, hitDepth - rise - surface);
    }
    return saturate(occlusion / (0.15 * maxDepth + 1e-4));
}

Surface SurfaceOf(float3 normal, float3 dPdx, float3 dPdy, float2 dUvdx, float2 dUvdy)
{
    Surface s;
    s.normal = normal;
    float3 acrossY = cross(dPdy, normal);
    float3 acrossX = cross(normal, dPdx);
    float determinant = dot(dPdx, acrossY);
    s.gradU = (dUvdx.x * acrossY + dUvdy.x * acrossX) / determinant;
    s.gradV = (dUvdx.y * acrossY + dUvdy.y * acrossX) / determinant;
    return s;
}

float2 UvAlong(Surface s, float3 direction)
{
    float3 tangential = direction - dot(direction, s.normal) * s.normal;
    return float2(dot(s.gradU, tangential), dot(s.gradV, tangential));
}

float2 InsideTheTexture(float2 uv, float2 offset)
{
    float2 limited = clamp(uv + offset, c_layer.z, 1 - c_layer.z) - uv;
    return lerp(offset, limited, c_layer.y);
}

float3 Relit(float3 light, float facing, float facingLit)
{
    float3 additive = light + c_multiplier.rgb * c_direct.rgb * (facingLit - facing);
    float3 before = saturate(c_direct.rgb * facing + c_ambient.rgb);
    float3 after = saturate(c_direct.rgb * facingLit + c_ambient.rgb);
    float3 ratio = c_emissive.rgb + (light - c_emissive.rgb) * max(after, 0.02) / max(before, 0.02);
    return saturate(light + c_ambient.w * (additive - light) + c_direct.w * (ratio - light));
}

#if RELIEF
void Relief(Input input, inout Output output)
{
    output.offset = 0;
    output.light = input.light;

    float3 normal = normalize(input.normal);
    float3 dPdx = ddx(input.viewPosition);
    float3 dPdy = ddy(input.viewPosition);
    float2 dUvdx = ddx(input.uv);
    float2 dUvdy = ddy(input.uv);
    float3 eyeView = -input.viewPosition;
    float determinant = dot(dPdx, cross(dPdy, normal));
    float maxDepth = c_layer.x;
    float distanceFade = saturate((c_reliefFade.x - length(eyeView)) * c_reliefFade.y);
    float texelsPerPixel = max(length(dUvdx), length(dUvdy)) * c_reliefSampling.x;
    float lod = max(log2(max(texelsPerPixel, 1e-6)), 0);

    [branch] if (distanceFade > 0 && maxDepth > 1e-5 && abs(determinant) > 1e-12 &&
                 texelsPerPixel < c_reliefSampling.z && dot(normal, eyeView) > 0)
    {
        Surface s = SurfaceOf(normal, dPdx, dPdy, dUvdx, dUvdy);
        float3 into = normalize(input.viewPosition);
        float depthPerLength = max(-dot(into, normal), c_reliefSampling.y);
        float repeatYards = 1 / max(max(length(s.gradU), length(s.gradV)), 1e-6);
        float depthHere = min(maxDepth, kReliefDepthPerRepeat * repeatYards) *
                          smoothstep(0, kReliefGrazingFull, -dot(into, normal));
        float2 uvPerDepth = UvAlong(s, into) / depthPerLength;
        float hitDepth = MarchDepth(input.uv, uvPerDepth, depthHere, lod);
        float2 hitOffset = InsideTheTexture(input.uv, uvPerDepth * hitDepth);
        float2 hitUv = input.uv + hitOffset;

        float texel = exp2(lod) / c_reliefSampling.x;
        float3 around = DepthAround(hitUv, texel, depthHere, lod);
        float2 depthPerUv = (around.yz - around.x) / texel;
        float3 detail = normalize(normal + depthPerUv.x * s.gradU + depthPerUv.y * s.gradV);

        float3 toward = normalize(c_lightToward.xyz);
        float lightRise = dot(toward, normal);
        float shadow = 1;
        [branch] if (lightRise > 0.05 && c_reliefFade.w > 0)
        {
            float2 uvPerRise = UvAlong(s, toward) / lightRise;
            shadow = 1 - c_reliefFade.w * SelfShadow(hitUv, hitDepth, uvPerRise, depthHere, lod);
        }
        float facing = saturate(dot(normal, toward));
        float facingLit = lerp(facing, saturate(dot(detail, toward)) * shadow, c_reliefFade.z * distanceFade);
        output.light.rgb = Relit(input.light.rgb, facing, facingLit);
        output.offset = float4(hitOffset * distanceFade, 0, 0);
    }
}
#endif

Output main(Input input)
{
    Output output;
#if RELIEF
    Relief(input, output);
#endif
#if MATERIAL
    output.material = c_material;
    output.tag = float4(EncodeMaterialTag(length(input.viewPosition)), c_materialKnown.x, c_materialGlint.x);
#endif
    return output;
}
