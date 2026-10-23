static const float kMapUnknownGround = 5e4;
static const float kMapCoverClearance = 3;
static const float kMapGroundCoverTop = 1;
static const float kMapSolidAboveGround = 2.5;
static const float kMapAgreeFrom = 0.5;
static const float kMapAgreeTo = 0.77;

struct MapGround
{
    float onGround;
    float3 normal;
};

MapGround GroundOnTheMap(sampler2D terrain, float4 map, float4 reach, float3 fromCamera)
{
    MapGround g;
    g.onGround = 0;
    g.normal = float3(0, 0, 1);
    [branch] if (map.z > 0)
    {
        float2 texel = (map.xy + fromCamera.xy * map.z) * reach.x - 0.5;
        float2 firstCell = floor(texel);
        float2 fraction = texel - firstCell;
        static const float2 kCorners[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
        float4 ground;
        [unroll] for (int k = 0; k < 4; k++)
            ground[k] = tex2Dlod(terrain, float4((firstCell + kCorners[k] + 0.5) / reach.x, 0, 0)).g;
        bool known = max(max(abs(ground.x), abs(ground.y)), max(abs(ground.z), abs(ground.w))) < kMapUnknownGround;
        float groundHere =
            lerp(lerp(ground.x, ground.y, fraction.x), lerp(ground.z, ground.w, fraction.x), fraction.y);
        float heightTerm = (CameraPositionWorld().z + fromCamera.z) * map.w;
        float aboveGround = (heightTerm + groundHere - 1) / map.w - kMapCoverClearance;
        float2 rise = float2(lerp(ground.y - ground.x, ground.w - ground.z, fraction.y),
                             lerp(ground.z - ground.x, ground.w - ground.y, fraction.x)) *
                      (-map.z * reach.x / map.w);
        g.onGround = known ? 1 - smoothstep(kMapGroundCoverTop, kMapSolidAboveGround, aboveGround) : 0;
        g.normal = known ? normalize(float3(-rise, 1)) : g.normal;
    }
    return g;
}

float OnTheMap(MapGround g, float3 worldNormal)
{
    return g.onGround * smoothstep(kMapAgreeFrom, kMapAgreeTo, dot(worldNormal, g.normal));
}
