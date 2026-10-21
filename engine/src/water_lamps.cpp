#include "water_lamps.h"

#include "fog_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265f;

float Brightest(const float* rgb)
{
    return std::max({rgb[0], rgb[1], rgb[2]});
}

float Length3(const float* v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

float Dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Normalize(float* v)
{
    const float length = Length3(v);
    if (length > 0.0f)
        for (int k = 0; k < 3; ++k)
            v[k] /= length;
}

bool RecoverFarHaloEnergy(const float* sphere, const float* light, float lampHaze, float lampLight, float* energy)
{
    if (!(Brightest(light) > 0.0f) || !(lampHaze > 0.0f) || !(lampLight > 0.0f))
        return false;
    const bool indoor = light[3] == kHiddenIndoorLampShare;
    const float indoorShare = indoor ? kIndoorLampLight / kIndoorLampHaze : 1.0f;
    const float scale = lampLight * indoorShare / (lampHaze * LampNearCameraShare(Length3(sphere)));
    for (int k = 0; k < 3; ++k)
        energy[k] = light[k] * scale;
    return true;
}
}

WaterLampRecords RecordWaterLamps(const LampTable& table, const FrameInputs& in, float lampHaze, float lampLight,
                                  const std::bitset<kLampSlots>& leftOutSlots)
{
    WaterLampRecords records = {};
    records.mapId = in.mapId;
    records.seconds = in.seconds;
    float viewToCamera[16];
    const bool turns = Invert4x4(in.cameraRelativeView, viewToCamera);
    struct Found
    {
        int slot;
        float energy[3];
        bool recovered;
        float rank;
    };
    Found found[kLampSlots];
    int count = 0;
    for (int slot = 0; slot < std::min(table.slots, kLampSlots); ++slot)
    {
        if (leftOutSlots[slot])
            continue;
        const float* sphere = table.texels[kLampSphereRow][slot];
        const float* surface = table.texels[kLampSurfaceRow][slot];
        const float* emit = table.texels[kLampEmitRow][slot];
        const bool window = emit[3] > 0.0f;
        const bool carried = emit[3] < 0.0f;
        float energy[3] = {surface[0], surface[1], surface[2]};
        const bool recovered = !window && !carried && !(Brightest(energy) > 0.0f) &&
                               RecoverFarHaloEnergy(sphere, table.texels[kLampLightRow][slot], lampHaze, lampLight,
                                                    energy);
        if (!(Brightest(energy) > 0.0f) || (window && !turns))
            continue;
        Found& f = found[count++];
        f.slot = slot;
        std::memcpy(f.energy, energy, sizeof(energy));
        f.recovered = recovered;
        f.rank = Brightest(energy) / std::max(Dot3(sphere, sphere), 1.0f);
    }
    if (count > kWaterLampSlots)
    {
        std::nth_element(found, found + kWaterLampSlots, found + count, [](const Found& a, const Found& b) {
            return a.rank != b.rank ? a.rank > b.rank : a.slot < b.slot;
        });
        count = kWaterLampSlots;
        std::sort(found, found + count, [](const Found& a, const Found& b) { return a.slot < b.slot; });
    }
    for (int i = 0; i < count; ++i)
    {
        const int slot = found[i].slot;
        const float* sphere = table.texels[kLampSphereRow][slot];
        const float* emit = table.texels[kLampEmitRow][slot];
        const bool window = emit[3] > 0.0f;
        const bool recovered = found[i].recovered;
        WaterLamp& lamp = records.lamps[records.count++];
        for (int k = 0; k < 3; ++k)
        {
            lamp.world[k] = static_cast<double>(in.camPos[k]) + static_cast<double>(sphere[k]);
            lamp.energy[k] = found[i].energy[k];
        }
        lamp.window = window;
        lamp.sizeYards = window ? emit[3] : kWaterLampFlameYards;
        lamp.nearYards = emit[3] != 0.0f ? std::fabs(emit[3]) : kWaterLampNearYards;
        if (window)
        {
            TransformDirection(emit, viewToCamera, lamp.windowNormal);
            Normalize(lamp.windowNormal);
        }
        records.windows += window ? 1 : 0;
        records.recovered += recovered ? 1 : 0;
    }
    return records;
}

bool WaterLampsCurrent(const WaterLampRecords& records, const FrameInputs& in)
{
    const double age = in.seconds - records.seconds;
    return records.count > 0 && records.mapId == in.mapId && age >= 0.0 && age <= kWaterLampMaxAgeSeconds;
}

WaterLampTable BuildWaterLampTable(const WaterLampRecords& records, const FrameInputs& in)
{
    WaterLampTable table = {};
    if (!WaterLampsCurrent(records, in))
        return table;
    for (int i = 0; i < records.count; ++i)
    {
        const WaterLamp& lamp = records.lamps[i];
        float* position = table.texels[kWaterLampPositionRow][i];
        float* energy = table.texels[kWaterLampEnergyRow][i];
        float* normal = table.texels[kWaterLampNormalRow][i];
        for (int k = 0; k < 3; ++k)
        {
            position[k] = static_cast<float>(lamp.world[k] - static_cast<double>(in.camPos[k]));
            energy[k] = lamp.energy[k];
            normal[k] = lamp.windowNormal[k];
        }
        position[3] = lamp.sizeYards;
        energy[3] = lamp.window ? 1.0f : 0.0f;
        normal[3] = lamp.nearYards;
    }
    table.count = records.count;
    return table;
}

float WaterLampLobe(float halfwayCosine, float roughness)
{
    const float c2 = halfwayCosine * halfwayCosine;
    return std::exp((c2 - 1.0f) / (c2 * roughness)) / (kPi * roughness * c2 * c2);
}

void WaterLampGlint(const WaterLampTable& table, int slot, const WaterLampView& view, float* out)
{
    const float* position = table.texels[kWaterLampPositionRow][slot];
    const float* energy = table.texels[kWaterLampEnergyRow][slot];
    const float* normal = table.texels[kWaterLampNormalRow][slot];
    float toLamp[3];
    for (int k = 0; k < 3; ++k)
        toLamp[k] = position[k] - view.relative[k];
    const float distanceSquared = std::max(Dot3(toLamp, toLamp), kWaterLampMinSquareYards);
    const float distance = std::sqrt(distanceSquared);
    float light[3];
    float halfway[3];
    for (int k = 0; k < 3; ++k)
    {
        light[k] = toLamp[k] / distance;
        halfway[k] = light[k] + view.toCamera[k];
    }
    Normalize(halfway);
    const float cosine = std::max(Dot3(view.normal, halfway), kWaterLampMinHalfwayCosine);
    const float roughness = std::max(view.roughness, kWaterLampMinRoughness) +
                            kWaterLampSourceSpread * position[3] * position[3] / distanceSquared;
    const float specular = WaterLampLobe(cosine, roughness) * WaterFresnel(Dot3(view.toCamera, halfway)) /
                           (4.0f * std::max(Dot3(view.normal, view.toCamera), kWaterLampMinGrazing)) *
                           std::clamp(Dot3(view.normal, light), 0.0f, 1.0f);
    const float spread = distanceSquared + normal[3] * normal[3];
    const float falloff = 1.0f / (kPointFalloffPerYard * std::sqrt(spread) + kPointFalloffPerSquareYard * spread);
    const float side =
        energy[3] > 0.0f
            ? std::clamp((kWaterLampWindowWrap - Dot3(normal, light)) / (1.0f + kWaterLampWindowWrap), 0.0f, 1.0f)
            : 1.0f;
    for (int k = 0; k < 3; ++k)
        out[k] = energy[k] * specular * falloff * side;
}

float WaterLampShown(float glint, float gain)
{
    return 1.0f - std::exp(-gain * glint);
}
