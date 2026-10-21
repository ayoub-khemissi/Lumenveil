float4 main(float4 clip : TEXCOORD0) : COLOR
{
    return clip.z / clip.w;
}
