float4 projection[4] : register(c2);
float4 worldView[3] : register(c31);

struct VsIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VsOut
{
    float4 position : POSITION;
    float4 clip : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

VsOut main(VsIn v)
{
    float4 p = float4(v.position, 1.0);
    float4 view = float4(dot(worldView[0], p), dot(worldView[1], p), dot(worldView[2], p), 1.0);
    VsOut o;
    o.position = float4(dot(projection[0], view), dot(projection[1], view), dot(projection[2], view),
                        dot(projection[3], view));
    o.clip = o.position;
    o.normal = float3(dot(worldView[0].xyz, v.normal), dot(worldView[1].xyz, v.normal),
                      dot(worldView[2].xyz, v.normal));
    o.uv = v.uv;
    return o;
}
