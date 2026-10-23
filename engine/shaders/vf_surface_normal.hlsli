sampler2D sDepth : register(s0);

static const float kMissingNeighbour = 1e9;
static const float kThinStep = 0.1;
static const float kThinStepPerViewZ = 0.005;
static const float kSliverPixels = 3;
static const float kGroundUpFrom = 0.35;
static const float kGroundUpTo = 0.55;
static const float kGroundStraightening = 0.9;

float3 ViewPositionAt(float2 pixel)
{
    return ViewRayAtUnitDepth(pixel) * LinearDepth(SampleDepth(sDepth, pixel));
}

float StepScore(float3 step)
{
    return dot(step, step) > 1e-10 ? abs(step.z) : kMissingNeighbour;
}

float DepthAt(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepth, clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre())));
}

bool Sliver(float2 pixel, float2 step, float z, float threshold)
{
    float ahead = DepthAt(pixel + step) - z;
    float behind = DepthAt(pixel - step) - z;
    return ahead * behind > 0 && min(abs(ahead), abs(behind)) > threshold;
}

float4 SurfaceNormal(float2 pixel, float3 centre)
{
    float2 low = ViewportOrigin() + 0.5;
    float2 high = ViewportLastPixelCentre();
    float3 right = ViewPositionAt(min(pixel + float2(1, 0), high)) - centre;
    float3 left = centre - ViewPositionAt(max(pixel - float2(1, 0), low));
    float3 below = ViewPositionAt(min(pixel + float2(0, 1), high)) - centre;
    float3 above = centre - ViewPositionAt(max(pixel - float2(0, 1), low));
    float3 alongX = StepScore(right) < StepScore(left) ? right : left;
    float3 alongY = StepScore(below) < StepScore(above) ? below : above;
    float3 facing = cross(alongX, alongY);
    float lengthSquared = dot(facing, facing);
    float3 normal = lengthSquared > 1e-12 ? facing * rsqrt(lengthSquared) : -normalize(centre);
    float threshold = kThinStep + kThinStepPerViewZ * centre.z;
    bool thin = Sliver(pixel, float2(kSliverPixels, 0), centre.z, threshold) ||
                Sliver(pixel, float2(0, kSliverPixels), centre.z, threshold);
    return float4(dot(normal, centre) > 0 ? -normal : normal, thin ? 1 : 0);
}

float3 LitNormal(float4 normal)
{
    float3 up = normalize(mul((float3x3)cViewToWorld, float3(0, 0, 1)));
    float ground = smoothstep(kGroundUpFrom, kGroundUpTo, dot(normal.xyz, up));
    float straightening = normal.w > 0 ? 0 : kGroundStraightening * ground;
    return normalize(lerp(normal.xyz, up, straightening));
}
