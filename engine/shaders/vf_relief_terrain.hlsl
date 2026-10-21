#ifndef LAYERS
#define LAYERS 3
#endif
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

float4 c_modelAxisX : register(c200);
float4 c_modelAxisY : register(c201);
float4 c_modelAxisZ : register(c202);
float4 c_lightToward : register(c203);
float4 c_ambient : register(c204);
float4 c_direct : register(c205);
float4 c_layerDepths : register(c206);
float4 c_reliefFade : register(c207);
float4 c_reliefSampling : register(c208);
float4 c_layerMaterial0 : register(c209);
float4 c_layerMaterial1 : register(c210);
float4 c_layerMaterial2 : register(c211);
float4 c_layerMaterial3 : register(c212);
float4 c_layerGlint : register(c213);
float4 c_layerKnown : register(c214);

sampler2D s_height0 : register(s9);
sampler2D s_height1 : register(s10);
sampler2D s_height2 : register(s11);
sampler2D s_height3 : register(s12);
#if LAYERS == 2
sampler2D s_alpha : register(s2);
#elif LAYERS == 3
sampler2D s_alpha : register(s3);
#elif LAYERS == 4
sampler2D s_alpha : register(s4);
#endif

struct Input
{
    float3 light : COLOR0;
    float2 uv0 : TEXCOORD0;
#if LAYERS >= 2
    float4 uv12 : TEXCOORD1;
#endif
#if LAYERS == 3
    float2 alphaUv : TEXCOORD2;
#elif LAYERS == 4
    float4 uv3Alpha : TEXCOORD2;
#endif
    float3 viewPosition : TEXCOORD3;
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

struct Layers
{
    float2 uv0;
    float2 uv1;
    float2 uv2;
    float2 uv3;
    float4 depthScale;
};

float3 ToModel(float3 v)
{
    return float3(dot(c_modelAxisX.xyz, v), dot(c_modelAxisY.xyz, v), dot(c_modelAxisZ.xyz, v));
}

float HeightAt(sampler2D s, float2 uv, float lod)
{
    return tex2Dlod(s, float4(uv, 0, lod)).r;
}

float DepthAt(Layers layers, float2 offset, float lod)
{
    float depth = layers.depthScale.x * (1 - HeightAt(s_height0, layers.uv0 + offset, lod));
#if LAYERS >= 2
    depth += layers.depthScale.y * (1 - HeightAt(s_height1, layers.uv1 + offset, lod));
#endif
#if LAYERS >= 3
    depth += layers.depthScale.z * (1 - HeightAt(s_height2, layers.uv2 + offset, lod));
#endif
#if LAYERS >= 4
    depth += layers.depthScale.w * (1 - HeightAt(s_height3, layers.uv3 + offset, lod));
#endif
    return depth;
}

float4 BlendWeights(Input input)
{
#if LAYERS == 1
    return float4(1, 0, 0, 0);
#elif LAYERS == 2
    float a1 = tex2D(s_alpha, input.uv12.wz).x;
    return float4(1 - a1, a1, 0, 0);
#elif LAYERS == 3
    float2 a = tex2D(s_alpha, input.alphaUv).xy;
    return float4((1 - a.x) * (1 - a.y), a.x * (1 - a.y), a.y, 0);
#else
    float3 a = tex2D(s_alpha, input.uv3Alpha.wz).xyz;
    float keep3 = 1 - a.z;
    return float4((1 - a.x) * (1 - a.y) * keep3, a.x * (1 - a.y) * keep3, a.y * keep3, a.z);
#endif
}

Layers LayersOf(Input input, float4 weights)
{
    Layers layers;
    layers.uv0 = input.uv0;
    layers.uv1 = 0;
    layers.uv2 = 0;
    layers.uv3 = 0;
#if LAYERS >= 2
    layers.uv1 = input.uv12.xy;
    layers.uv2 = input.uv12.wz;
#endif
#if LAYERS == 4
    layers.uv3 = input.uv3Alpha.xy;
#endif
    layers.depthScale = weights * c_layerDepths;
    return layers;
}

float MarchDepth(Layers layers, float2 uvPerDepth, float maxDepth, float lod)
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
        float surface = DepthAt(layers, uvPerDepth * ray, lod);
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
        float gap = DepthAt(layers, uvPerDepth * middle, lod) - middle;
        float outside = 1 - step(gap, 0);
        above = lerp(above, middle, outside);
        aboveGap = lerp(aboveGap, gap, outside);
        below = lerp(middle, below, outside);
        belowGap = lerp(gap, belowGap, outside);
    }
    float t = saturate(aboveGap / max(aboveGap - belowGap, 1e-6));
    return lerp(above, below, t);
}

float3 DepthAround(Layers layers, float2 offset, float texel, float lod)
{
    float3 depths = 0;
    [loop] for (int k = 0; k < 3; ++k)
    {
        float2 shift = float2(k == 1, k == 2) * texel;
        float depth = DepthAt(layers, offset + shift, lod);
        depths += float3(k == 0, k == 1, k == 2) * depth;
    }
    return depths;
}

float SelfShadow(Layers layers, float2 hitOffset, float hitDepth, float2 uvPerRise, float maxDepth, float lod)
{
    float occlusion = 0;
    [loop] for (int i = 1; i <= RELIEF_SHADOW_STEPS; ++i)
    {
        float rise = hitDepth * i / RELIEF_SHADOW_STEPS;
        float rayDepth = hitDepth - rise;
        float surface = DepthAt(layers, hitOffset + uvPerRise * rise, lod);
        occlusion = max(occlusion, rayDepth - surface);
    }
    return saturate(occlusion / (0.15 * maxDepth + 1e-4));
}

void Relief(Input input, float4 weights, inout Output output)
{
#if RELIEF
    output.offset = 0;
    output.light = float4(input.light, 1);

    float3 eyeView = -input.viewPosition;
    float3 eye = ToModel(eyeView);
    float3 dPdx = -ddx(eye);
    float3 dPdy = -ddy(eye);
    float2 dUvdx = ddx(input.uv0);
    float2 dUvdy = ddy(input.uv0);
    float determinant = dPdx.x * dPdy.y - dPdx.y * dPdy.x;
    float2 uvPerX = (dUvdx * dPdy.y - dUvdy * dPdx.y) / determinant;
    float2 uvPerY = (dUvdy * dPdx.x - dUvdx * dPdy.x) / determinant;
    float3 facet = normalize(cross(dPdx, dPdy));
    facet = facet.z < 0 ? -facet : facet;
    float2 rise = -facet.xy / max(facet.z, 0.2);

    Layers layers = LayersOf(input, weights);
    float maxDepth = dot(layers.depthScale, 1);
    float distanceFade = saturate((c_reliefFade.x - length(eyeView)) * c_reliefFade.y);
    float texelsPerPixel = max(length(dUvdx), length(dUvdy)) * c_reliefSampling.x;
    float lod = max(log2(max(texelsPerPixel, 1e-6)), 0);

    [branch] if (distanceFade > 0 && maxDepth > 1e-5 && abs(determinant) > 1e-12 && texelsPerPixel < c_reliefSampling.z)
    {
        float3 into = -normalize(eye);
        float depthPerLength = max(dot(rise, into.xy) - into.z, c_reliefSampling.y);
        float2 uvPerDepth = (uvPerX * into.x + uvPerY * into.y) / depthPerLength;
        float hitDepth = MarchDepth(layers, uvPerDepth, maxDepth, lod);
        float2 hitOffset = uvPerDepth * hitDepth;

        float texel = exp2(lod) / c_reliefSampling.x;
        float3 around = DepthAround(layers, hitOffset, texel, lod);
        float2 depthPerUv = (around.yz - around.x) / texel;
        float2 depthSlope = float2(dot(depthPerUv, uvPerX), dot(depthPerUv, uvPerY));
        float3 detail = normalize(facet + float3(depthSlope, 0));

        float3 toward = normalize(ToModel(c_lightToward.xyz));
        float lightRise = toward.z - dot(rise, toward.xy);
        float shadow = 1;
        [branch] if (lightRise > 0.05 && c_reliefFade.w > 0)
        {
            float2 uvPerRise = (uvPerX * toward.x + uvPerY * toward.y) / lightRise;
            shadow = 1 - c_reliefFade.w * SelfShadow(layers, hitOffset, hitDepth, uvPerRise, maxDepth, lod);
        }
        float3 base = c_ambient.rgb + c_direct.rgb * saturate(dot(facet, toward));
        float3 lit = c_ambient.rgb + c_direct.rgb * saturate(dot(detail, toward)) * shadow;
        float3 ratio = clamp(lit / max(base, 0.02), 0.3, 3);
        output.light.rgb = input.light * lerp(1, ratio, c_reliefFade.z * distanceFade);
        output.offset = float4(hitOffset * distanceFade, 0, 0);
    }
#endif
}

void Material(Input input, float4 weights, inout Output output)
{
#if MATERIAL
    output.material = weights.x * c_layerMaterial0 + weights.y * c_layerMaterial1 + weights.z * c_layerMaterial2 +
                      weights.w * c_layerMaterial3;
    output.tag = float4(EncodeMaterialTag(length(input.viewPosition)), dot(weights, c_layerKnown),
                        dot(weights, c_layerGlint));
#endif
}

Output main(Input input)
{
    Output output;
    float4 weights = BlendWeights(input);
    Relief(input, weights, output);
    Material(input, weights, output);
    return output;
}
