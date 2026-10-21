float4 cWaveLevel : register(c9);

sampler2D sWaveTable : register(s0);

static const int kWavesPerLayer = 48;
static const float kTau = 6.28318531;
static const float kMinCycles = 0.001;

float LevelTexels()
{
    return cWaveLevel.x;
}

float LevelTexelSize()
{
    return cWaveLevel.y;
}

float FadeFromTexels()
{
    return cWaveLevel.z;
}

float FadePerTexel()
{
    return cWaveLevel.w;
}

void AddWave(float4 wave, float2 uv, inout float4 sum)
{
    float cycles = max(length(wave.xy), kMinCycles);
    float fade = saturate((LevelTexels() / cycles - FadeFromTexels()) * FadePerTexel());
    float2 direction = wave.xy / cycles;
    float crest = cos(kTau * frac(dot(wave.xy, uv) - wave.w));
    sum.xy += direction * (wave.z * fade * crest);
    sum.zw += direction * direction * (0.5 * wave.z * wave.z * (1 - fade * fade));
}

float4 Moments(float4 sum)
{
    return float4(sum.xy, sum.xy * sum.xy + sum.zw);
}

struct LayerMoments
{
    float4 first : COLOR0;
    float4 second : COLOR1;
    float4 third : COLOR2;
};

LayerMoments main(float2 vpos : VPOS)
{
    float2 uv = (vpos + 0.5) * LevelTexelSize();
    float4 first = 0;
    float4 second = 0;
    float4 third = 0;
    [loop] for (int i = 0; i < kWavesPerLayer; i++)
    {
        float u = (i + 0.5) / kWavesPerLayer;
        AddWave(tex2Dlod(sWaveTable, float4(u, 1.0 / 6, 0, 0)), uv, first);
        AddWave(tex2Dlod(sWaveTable, float4(u, 3.0 / 6, 0, 0)), uv, second);
        AddWave(tex2Dlod(sWaveTable, float4(u, 5.0 / 6, 0, 0)), uv, third);
    }
    LayerMoments o;
    o.first = Moments(first);
    o.second = Moments(second);
    o.third = Moments(third);
    return o;
}
