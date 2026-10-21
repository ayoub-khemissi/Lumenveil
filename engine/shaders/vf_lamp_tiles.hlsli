static const float kLampTileBlockWidth = 16;
static const float kLampTileBlockHeight = 8;
static const float kLampTileCapacity = kLampTileBlockWidth * kLampTileBlockHeight - 1;
static const float kLampTileEveryLamp = 255;

float LampTileEntry(sampler2D tiles, float4 layout, float2 tile, float index)
{
    float row = floor(index / kLampTileBlockWidth);
    float column = index - row * kLampTileBlockWidth;
    float2 texel = float2(tile.x * kLampTileBlockWidth + column, layout.z + tile.y * kLampTileBlockHeight + row);
    return floor(tex2Dlod(tiles, float4((texel + 0.5) * layout.xy, 0, 0)).r * 255 + 0.5);
}

float2 LampTileOf(float4 layout, float2 pixel)
{
    return floor((pixel - ViewportOrigin()) / max(layout.w, 1));
}

float LampTileListed(sampler2D tiles, float4 layout, float2 tile)
{
    return layout.w > 0 ? LampTileEntry(tiles, layout, tile, 0) : kLampTileEveryLamp;
}
