#ifndef LAYERS
#define LAYERS 4
#endif

sampler2D s_layer0 : register(s0);
sampler2D s_layer1 : register(s1);
sampler2D s_layer2 : register(s2);
sampler2D s_layer3 : register(s3);
sampler2D s_alpha4 : register(s4);
float4 c_fogColour : register(c2);

struct PsIn
{
    float3 light : COLOR0;
    float3 specular : COLOR1;
    float2 uv0 : TEXCOORD0;
#if LAYERS >= 2
    float4 uv12 : TEXCOORD1;
#endif
#if LAYERS == 3
    float2 uv3 : TEXCOORD2;
#elif LAYERS == 4
    float4 uv34 : TEXCOORD2;
#endif
    float fog : FOG;
};

float4 main(PsIn i) : COLOR
{
    float4 c = tex2D(s_layer0, i.uv0);
#if LAYERS == 2
    float4 l1 = tex2D(s_layer1, i.uv12.xy);
    float4 a = tex2D(s_layer2, i.uv12.wz);
    c = lerp(c, l1, a.x);
#elif LAYERS == 3
    float4 l1 = tex2D(s_layer1, i.uv12.xy);
    float4 l2 = tex2D(s_layer2, i.uv12.wz);
    float4 a = tex2D(s_layer3, i.uv3);
    c = lerp(lerp(c, l1, a.x), l2, a.y);
#elif LAYERS == 4
    float4 l1 = tex2D(s_layer1, i.uv12.xy);
    float4 l2 = tex2D(s_layer2, i.uv12.wz);
    float4 l3 = tex2D(s_layer3, i.uv34.xy);
    float4 a = tex2D(s_alpha4, i.uv34.wz);
    c = lerp(lerp(lerp(c, l1, a.x), l2, a.y), l3, a.z);
#endif
    float3 lit = c.rgb * i.light * 2.0 + c.a * i.specular;
    return float4(lerp(c_fogColour.rgb, lit, i.fog), 1.0);
}
