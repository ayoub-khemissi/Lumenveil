sampler2D s_diffuse : register(s0);
float4 c_fogColour : register(c2);
float4 c_shadowFade : register(c3);
float4 c_sunToward : register(c4);

struct PsIn
{
    float4 light : COLOR0;
    float2 uv : TEXCOORD0;
    float3 viewPosition : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float fog : FOG;
};

float4 main(PsIn i) : COLOR
{
    float4 c = tex2D(s_diffuse, i.uv);
    float fade = saturate(dot(i.viewPosition, c_shadowFade.xyz) + c_shadowFade.w);
    float edge = saturate(1.2 - abs(dot(c_sunToward.xyz, i.normal)));
    float shade = lerp(fade, 1, edge * edge * edge * edge) * 0.3 + 0.7;
    float3 lit = c.rgb * (shade * i.light.rgb) * 2;
    return float4(lerp(c_fogColour.rgb, lit, i.fog), c.a * i.light.a);
}
