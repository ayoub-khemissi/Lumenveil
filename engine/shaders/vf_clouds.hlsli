static const float kCloudEdge = 0.06;
static const float kCloudTexels = 256;
static const float kCloudFootprintPerYard = 0.003;
static const float kCloudAboveFade = 0.05;

float CloudStrength(float4 shape)
{
    return shape.w;
}

float CloudCoverAt(sampler2D clouds, float4 map, float4 shape, float3 fromCamera, float cameraZ, float distance)
{
    float height = cameraZ + fromCamera.z;
    float below = max(map.w - height, 0);
    float2 uv = map.xy + (fromCamera.xy + below * shape.xy) * map.z;
    float lod = log2(max(distance * kCloudFootprintPerYard * kCloudTexels * map.z, 1));
    float n = tex2Dlod(clouds, float4(uv, 0, lod)).r;
    return smoothstep(shape.z - kCloudEdge, shape.z + kCloudEdge, n) * saturate(below * kCloudAboveFade);
}
