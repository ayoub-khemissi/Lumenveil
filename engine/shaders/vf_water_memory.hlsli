float4 cMemoryMotion : register(c24);
float4 cMemoryGrid : register(c25);

sampler2D sMemory : register(s6);

static const float kColourCodes = 255;
static const float kSkyYards = 100000;

float MemoryTexelSize()
{
    return cMemoryGrid.y;
}

float MemoryTexelRadians()
{
    return cMemoryGrid.z;
}

bool MemoryOn()
{
    return cMemoryGrid.w > 0;
}

float3 MemoryMotion()
{
    return cMemoryMotion.xyz;
}

bool MemoryHistory()
{
    return cMemoryMotion.w > 0;
}

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0 ? 1 : -1, v.y >= 0 ? 1 : -1);
}

float2 OctahedralUv(float3 direction)
{
    float3 d = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    float2 p = d.z >= 0 ? d.xy : (1 - abs(d.yx)) * SignNotZero(d.xy);
    return p * 0.5 + 0.5;
}

float3 OctahedralDirection(float2 uv)
{
    float2 p = uv * 2 - 1;
    float z = 1 - abs(p.x) - abs(p.y);
    float2 xy = z >= 0 ? p : (1 - abs(p.yx)) * SignNotZero(p);
    return normalize(float3(xy, z));
}

float4 RememberedAt(float3 direction)
{
    return tex2Dlod(sMemory, float4(OctahedralUv(direction), 0, 0));
}

bool RememberedPoint(float4 remembered)
{
    return remembered.w > 0;
}

bool RememberedSky(float4 remembered)
{
    return remembered.w < 0;
}

float RememberedYards(float4 remembered)
{
    return RememberedPoint(remembered) ? length(remembered.xyz) : kSkyYards;
}

float PackColour(float3 colour)
{
    float3 codes = floor(saturate(colour) * kColourCodes + 0.5);
    return 1 + codes.r * 65536 + codes.g * 256 + codes.b;
}

float3 UnpackColour(float code)
{
    float value = abs(code) - 1;
    float red = floor(value / 65536);
    float rest = value - red * 65536;
    float green = floor(rest / 256);
    return float3(red, green, rest - green * 256) / kColourCodes;
}
