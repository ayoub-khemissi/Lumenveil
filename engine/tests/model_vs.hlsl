#ifndef FORM
#define FORM 1
#endif
#ifndef UV_SETS
#define UV_SETS 1
#endif
#ifndef SPHERE_UV
#define SPHERE_UV 0
#endif

float4 projection[4] : register(c2);
float4 c10Light : register(c10);
float4 c11Light : register(c11);
float4 lightDirection : register(c12);
float4 material : register(c28);
float4 emissive : register(c29);
float4 modelView[3] : register(c31);

struct VsIn
{
    float4 position : POSITION;
    float3 normal : NORMAL;
#if FORM != 3
    float4 colour : COLOR0;
#endif
    float2 uv : TEXCOORD0;
};

struct VsOut
{
    float4 position : POSITION;
    float4 light : COLOR0;
    float2 uv : TEXCOORD0;
#if UV_SETS == 2
    float2 uv2 : TEXCOORD1;
    float3 viewPosition : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float4 shadow : TEXCOORD4;
#else
    float3 viewPosition : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 shadow : TEXCOORD3;
#endif
    float fog : FOG;
};

VsOut main(VsIn i)
{
    VsOut o;
    float4 view = float4(dot(modelView[0], i.position), dot(modelView[1], i.position), dot(modelView[2], i.position), 1);
    float3 n = normalize(float3(dot(modelView[0].xyz, i.normal), dot(modelView[1].xyz, i.normal),
                                dot(modelView[2].xyz, i.normal)));
    o.position = float4(dot(projection[0], view), dot(projection[1], view), dot(projection[2], view),
                        dot(projection[3], view));
    float facing = saturate(dot(-lightDirection.xyz, n));
#if FORM == 1
    float3 light = saturate(facing * c11Light.rgb + c10Light.rgb);
    o.light = float4(saturate(i.colour.rgb * light + emissive.rgb), saturate(i.colour.a));
#elif FORM == 2
    float3 light = saturate(facing * c11Light.rgb + c10Light.rgb);
    o.light = float4(saturate(material.rgb * light + i.colour.rgb + emissive.rgb), saturate(i.colour.a));
#elif FORM == 3
    float3 light = saturate(facing * c10Light.rgb + c11Light.rgb);
    o.light = saturate(material * float4(light, 1) + emissive);
#else
    o.light = i.colour + 0 * facing;
#endif
#if SPHERE_UV
    o.uv = n.xy * 0.5 + 0.5;
#else
    o.uv = i.uv;
#endif
#if UV_SETS == 2
    o.uv2 = i.uv * 2;
#endif
    o.viewPosition = view.xyz;
    o.normal = n;
    o.shadow = view * 0;
    o.fog = 1;
    return o;
}
