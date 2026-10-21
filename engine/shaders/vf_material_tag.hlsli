static const float kMaterialTagNearYards = 0.25;
static const float kMaterialTagFarYards = 4096;
static const float kMaterialTagCodes = 65535;

float MaterialTagOctaves()
{
    return log2(kMaterialTagFarYards / kMaterialTagNearYards);
}

float2 EncodeMaterialTag(float yards)
{
    float share = saturate(log2(max(yards, kMaterialTagNearYards) / kMaterialTagNearYards) / MaterialTagOctaves());
    float code = floor(share * kMaterialTagCodes + 0.5);
    float high = floor(code / 256);
    return float2(high, code - 256 * high) / 255;
}

float DecodeMaterialTag(float2 stored)
{
    float code = dot(floor(stored * 255 + 0.5), float2(256, 1));
    return kMaterialTagNearYards * exp2(code / kMaterialTagCodes * MaterialTagOctaves());
}
