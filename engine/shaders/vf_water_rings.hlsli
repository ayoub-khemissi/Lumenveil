float4 cRain : register(c26);
float4 cRainCell : register(c27);
float4 cRipples : register(c28);
float4 cRippleReach : register(c29);

sampler2D sRippleTable : register(s3);
sampler2D sRainTable : register(s4);

static const float kRainTableTexels = 256;
static const float kRainCellYards = 0.5;
static const int kRainSlots = 3;
static const int kRingsPerPart = 27;
static const float kRainFrontYards = 0.35;
static const float kRainPacket2 = 0.05 * 0.05;
static const float kRainWavenumber = 6.28318531 / 0.08;
static const float kRainPeakSlope = 0.35;
static const float kRainLifeSeconds = 1;
static const float kRainFadeInSeconds = 0.06;
static const float kRainCoreYards = 0.02;
static const float kRainTaperYards = 0.1;
static const float kRainMinAmplitude = 0.5;
static const float kRainResolvedFootprint = 0.06;
static const float kRainPooledFootprint = 0.12;
static const float2 kRainSlotStep = float2(17, 59);
static const float2 kRainCycleStep = float2(31, 13);
static const float2 kRainPhaseRow = float2(0, 128);

static const float kRippleTableRows = 2;
static const float kRippleFrontYards = 0.8;
static const float kRipplePacket2 = 0.1 * 0.1;
static const float kRippleWavenumber = 6.28318531 / 0.25;
static const float kRipplePeakSlope = 0.2;
static const float kRippleLifeSeconds = 2.0;
static const float kRippleFadeInSeconds = 0.05;
static const float kRippleCoreYards = 0.05;
static const float kRippleWetYards = 0.05;
static const float kRippleDeepestYards = 2.5;
static const float kRippleFadeYards = 1.0;
static const float kRippleBehindFront = 0.4;
static const float kRippleNoTaper = 1e4;

static const float kRingMinRadius = 1e-4;
static const float kRingFootprintShare = 0.5;

bool RainOn()
{
    return cRain.w > 0;
}

float RainActiveShare()
{
    return cRain.x;
}

float RainCycles()
{
    return cRain.y;
}

float RainVariance()
{
    return cRain.z;
}

float RainStrength()
{
    return cRain.w;
}

float RippleStrength()
{
    return cRipples.x;
}

float4 RainTable(float2 index)
{
    float2 wrapped = index - kRainTableTexels * floor(index / kRainTableTexels);
    return tex2Dlod(sRainTable, float4((wrapped + 0.5) / kRainTableTexels, 0, 0));
}

float4 RippleTable(int i, float row)
{
    return tex2Dlod(sRippleTable, float4((i + 0.5) / kRingsPerPart, (row + 0.5) / kRippleTableRows, 0, 0));
}

float RainPooledShare(float2 dx, float2 dy)
{
    float longest = sqrt(max(dot(dx, dx), dot(dy, dy)));
    return smoothstep(kRainResolvedFootprint, kRainPooledFootprint, kRingFootprintShare * longest);
}

bool RipplesReach(float2 relative, float pad)
{
    float2 d = relative - cRippleReach.xy;
    float reach = cRippleReach.z + pad;
    return RippleStrength() > 0 && dot(d, d) < reach * reach;
}

struct RingShape
{
    float wavenumber;
    float packet2;
    float front;
    float peak;
    float life;
    float fadeIn;
    float core;
    float edge;
    float taper;
};

RingShape Shape(bool rain)
{
    RingShape r;
    r.wavenumber = rain ? kRainWavenumber : kRippleWavenumber;
    r.packet2 = rain ? kRainPacket2 : kRipplePacket2;
    r.front = rain ? kRainFrontYards : kRippleFrontYards;
    r.peak = rain ? kRainPeakSlope : kRipplePeakSlope;
    r.life = rain ? kRainLifeSeconds : kRippleLifeSeconds;
    r.fadeIn = rain ? kRainFadeInSeconds : kRippleFadeInSeconds;
    r.core = rain ? kRainCoreYards : kRippleCoreYards;
    r.edge = rain ? kRainCellYards : kRippleNoTaper;
    r.taper = rain ? kRainTaperYards : 1;
    return r;
}

float3 Ring(RingShape r, float2 toPixel, float radius, float age, float amplitude, float2 dx, float2 dy)
{
    float2 radial = toPixel / max(radius, kRingMinRadius);
    float2 along = float2(dot(dx, radial), dot(dy, radial));
    float w2 = kRingFootprintShare * kRingFootprintShare * dot(along, along);
    float d = radius - age * r.front;
    float left = 1 - age / r.life;
    float strength = r.peak * amplitude * smoothstep(0, r.fadeIn, age) * left * left *
                     smoothstep(0, r.core, radius) * saturate((r.edge - radius) / r.taper);
    float k2 = r.wavenumber * r.wavenumber;
    float sigma2 = r.packet2 + 2 * w2;
    float resolved = strength * sqrt(r.packet2 / sigma2) * exp(-k2 * r.packet2 * w2 / (2 * sigma2) - d * d / sigma2);
    float s = resolved * sin(r.wavenumber * r.packet2 / sigma2 * d);
    float tau2 = 0.5 * r.packet2 + 2 * w2;
    float mean2 = 0.5 * strength * strength * sqrt(0.5 * r.packet2 / tau2) * exp(-d * d / tau2) *
                  (1 - exp(-k2 * r.packet2 * w2 / tau2) * cos(r.wavenumber * r.packet2 / tau2 * d));
    return float3(s * radial, max(mean2 - s * s, 0));
}

float3 WaterRings(float3 relative, float2 dx, float2 dy)
{
    float pooled = RainPooledShare(dx, dy);
    float pad = 2 * sqrt(max(dot(dx, dx), dot(dy, dy)));
    float reach = kRippleBehindFront + pad;
    float2 q = relative.xy / kRainCellYards + cRainCell.xy;
    float2 home = floor(q);
    float3 rain = 0;
    float3 ripples = 0;
    [loop] for (int part = 0; part < 2; part++)
    {
        bool isRain = part == 0;
        RingShape shape = Shape(isRain);
        float3 sum = 0;
        [branch] if (isRain ? RainOn() && pooled < 1 : RipplesReach(relative.xy, pad))
        {
            [loop] for (int n = 0; n < kRingsPerPart; n++)
            {
                float2 toPixel;
                float age;
                float amplitude;
                bool active;
                [branch] if (isRain)
                {
                    float slotIndex = floor(n / 9.0);
                    float inCells = n - 9 * slotIndex;
                    float row = floor(inCells / 3.0);
                    float2 cell = home + float2(inCells - 3 * row - 1, row - 1);
                    float2 slot = cell + cRainCell.zw + slotIndex * kRainSlotStep;
                    float cycles = RainCycles() + RainTable(slot + kRainPhaseRow).a;
                    float whole = floor(cycles);
                    float4 drop = RainTable(slot + whole * kRainCycleStep);
                    toPixel = (q - cell - drop.rg) * kRainCellYards;
                    age = cycles - whole;
                    amplitude = RainStrength() * (kRainMinAmplitude + (1 - kRainMinAmplitude) * drop.a);
                    active = drop.b < RainActiveShare() && length(toPixel) < kRainCellYards;
                }
                else
                {
                    float4 source = RippleTable(n, 0);
                    toPixel = relative.xy - source.xy;
                    age = source.w;
                    float immersion = relative.z - source.z;
                    float wet = smoothstep(0, kRippleWetYards, immersion) *
                                saturate((kRippleDeepestYards + kRippleFadeYards - immersion) / kRippleFadeYards);
                    amplitude = RippleStrength() * wet * RippleTable(n, 1).x;
                    active = wet > 0 && age < kRippleLifeSeconds &&
                             abs(length(toPixel) - age * kRippleFrontYards) < reach;
                }
                [branch] if (active)
                {
                    sum += Ring(shape, toPixel, length(toPixel), age, amplitude, dx, dy);
                }
            }
        }
        if (isRain)
            rain = sum;
        else
            ripples = sum;
    }
    float keep = 1 - pooled;
    return float3(keep * rain.xy + ripples.xy,
                  keep * rain.z + pooled * RainVariance() + pooled * keep * dot(rain.xy, rain.xy) + ripples.z);
}
