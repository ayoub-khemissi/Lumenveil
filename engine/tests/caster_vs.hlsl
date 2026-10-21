float4 lightProjection[4] : register(c2);
float4 cameraToLight[3] : register(c14);
float4 bone[3] : register(c31);

struct VsOut
{
    float4 position : POSITION;
    float4 lightView : TEXCOORD0;
};

VsOut main(float3 position : POSITION)
{
    float4 p = float4(position, 1.0);
    float4 view = float4(dot(bone[0], p), dot(bone[1], p), dot(bone[2], p), 1.0);
    float4 light = float4(dot(cameraToLight[0], view), dot(cameraToLight[1], view), dot(cameraToLight[2], view), 1.0);
    VsOut o;
    o.position = float4(dot(lightProjection[0], light), dot(lightProjection[1], light), dot(lightProjection[2], light),
                        dot(lightProjection[3], light));
    o.lightView = light;
    return o;
}
