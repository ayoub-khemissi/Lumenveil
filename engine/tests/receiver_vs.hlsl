float4 projection[4] : register(c2);
float4 worldView[3] : register(c31);
float4 shadowRows[12] : register(SHADOW_ROWS);

struct VsOut
{
    float4 position : POSITION;
    float4 shadow0 : TEXCOORD0;
    float4 shadow1 : TEXCOORD1;
    float4 shadow2 : TEXCOORD2;
};

VsOut main(float3 position : POSITION)
{
    float4 p = float4(position, 1.0);
    float4 view = float4(dot(worldView[0], p), dot(worldView[1], p), dot(worldView[2], p), 1.0);
    VsOut o;
    o.position = float4(dot(projection[0], view), dot(projection[1], view), dot(projection[2], view),
                        dot(projection[3], view));
    o.shadow0 = float4(dot(shadowRows[0], view), dot(shadowRows[1], view), dot(shadowRows[2], view),
                       dot(shadowRows[9], view));
    o.shadow1 = float4(dot(shadowRows[3], view), dot(shadowRows[4], view), dot(shadowRows[5], view),
                       dot(shadowRows[10], view));
    o.shadow2 = float4(dot(shadowRows[6], view), dot(shadowRows[7], view), dot(shadowRows[8], view),
                       dot(shadowRows[11], view));
    return o;
}
