#ifndef LAYERS
#define LAYERS 4
#endif

float4 modelView[4] : register(c0);
float4 projection[4] : register(c4);
float4 layerOffset[4] : register(c13);
float4 alphaScale : register(c17);
float4 layerScale[4] : register(c18);
float4 chunkOrigin : register(c23);
float4 lightToward : register(c24);
float4 ambient : register(c25);
float4 direct : register(c26);

struct VsOut
{
    float4 position : POSITION;
    float4 light : COLOR0;
    float4 specular : COLOR1;
    float2 uv0 : TEXCOORD0;
    float4 uv12 : TEXCOORD1;
    float4 uv34 : TEXCOORD2;
#ifndef NO_VIEW_POSITION
    float3 viewPosition : TEXCOORD3;
#endif
    float fog : FOG;
};

float2 LayerUv(float2 local, int k)
{
    return local * layerScale[k].xy + layerOffset[k].xy;
}

VsOut main(float3 position : POSITION, float3 normal : NORMAL)
{
    float4 view = modelView[0] * position.x + modelView[1] * position.y + modelView[2] * position.z + modelView[3];
    float3 n = modelView[0].xyz * normal.x + modelView[1].xyz * normal.y + modelView[2].xyz * normal.z;
    float2 local = position.xy - chunkOrigin.xy;
    float2 alpha = local * alphaScale.xy;
    VsOut o;
    o.position = projection[0] * view.x + projection[1] * view.y + projection[2] * view.z + projection[3] * view.w;
    o.light = float4(0.5 * min(direct.rgb * saturate(dot(n, lightToward.xyz)) + ambient.rgb, 1.0), 1.0);
    o.specular = float4(0.0, 0.0, 0.0, 1.0);
    o.uv0 = LayerUv(local, 0);
    o.uv12 = float4(LayerUv(local, 1), 0.0, 0.0);
    o.uv34 = float4(0.0, 0.0, 0.0, 0.0);
#if LAYERS == 2
    o.uv12.zw = alpha.yx;
#elif LAYERS == 3
    o.uv12.zw = LayerUv(local, 2).yx;
    o.uv34.xy = alpha;
#elif LAYERS == 4
    o.uv12.zw = LayerUv(local, 2).yx;
    o.uv34 = float4(LayerUv(local, 3), alpha.yx);
#endif
#ifndef NO_VIEW_POSITION
    o.viewPosition = view.xyz;
#endif
    o.fog = 1.0;
    return o;
}
