float4 cEffectReduce : register(c9);
float4 cEffectGatherSize : register(c10);

sampler2D sLight : register(s0);
sampler2D sPlace : register(s1);

static const int kTexelsPerCell = 4;

float CellsAcross()
{
    return cEffectReduce.x;
}

float SumScale()
{
    return cEffectReduce.y;
}

float2 GatherSize()
{
    return cEffectGatherSize.xy;
}

float2 GatherTexelSize()
{
    return cEffectGatherSize.zw;
}

float4 main(float2 texel : VPOS) : COLOR0
{
    bool place = texel.x >= CellsAcross();
    float2 cell = float2(place ? texel.x - CellsAcross() : texel.x, texel.y);
    float4 sum = 0;
    [loop] for (int y = 0; y < kTexelsPerCell; y++)
    {
        [loop] for (int x = 0; x < kTexelsPerCell; x++)
        {
            float2 gather = cell * kTexelsPerCell + float2(x, y);
            float4 uv = float4((gather + 0.5) * GatherTexelSize(), 0, 0);
            float4 value = place ? tex2Dlod(sPlace, uv) : tex2Dlod(sLight, uv);
            value = all(gather < GatherSize()) ? value : 0;
            sum.xyz += value.xyz;
            sum.w = place ? max(sum.w, value.w) : sum.w + value.w;
        }
    }
    return float4(sum.xyz * SumScale(), place ? sum.w : sum.w * SumScale());
}
