float4 main(float4 lightView : TEXCOORD0) : COLOR
{
    return float4(lightView.z, 0, 0, 1);
}
