#pragma once

#include "engine.h"
#include "lamp_selection.h"

#include <bitset>

constexpr int kWaterLampSlots = 64;
constexpr double kWaterLampMaxAgeSeconds = 0.25;
constexpr float kWaterLampFlameYards = 0.3f;
constexpr float kWaterLampNearYards = 0.5f;
constexpr float kWaterLampSourceSpread = 0.25f;
constexpr float kWaterLampMinRoughness = 1e-5f;
constexpr float kWaterLampMinSquareYards = 1e-4f;
constexpr float kWaterLampMinHalfwayCosine = 0.01f;
constexpr float kWaterLampMinGrazing = 0.05f;
constexpr float kWaterLampWindowWrap = 0.1f;
constexpr float kWaterLampGain = 0.35f;
constexpr float kWaterLampMinOccludedLight = 0.002f;
constexpr int kWaterLampOcclusionSteps = 8;

enum WaterLampTableRow
{
    kWaterLampPositionRow,
    kWaterLampEnergyRow,
    kWaterLampNormalRow,
    kWaterLampTableRows,
};

struct WaterLamp
{
    double world[3];
    float energy[3];
    float sizeYards;
    float nearYards;
    float windowNormal[3];
    bool window;
};

struct WaterLampRecords
{
    WaterLamp lamps[kWaterLampSlots];
    int count;
    int windows;
    int recovered;
    int mapId;
    double seconds;
};

struct WaterLampTable
{
    float texels[kWaterLampTableRows][kWaterLampSlots][4];
    int count;
};

struct WaterLampView
{
    float relative[3];
    float toCamera[3];
    float normal[3];
    float roughness;
};

WaterLampRecords RecordWaterLamps(const LampTable& table, const FrameInputs& in, float lampHaze, float lampLight,
                                  const std::bitset<kLampSlots>& leftOutSlots = {});
bool WaterLampsCurrent(const WaterLampRecords& records, const FrameInputs& in);
WaterLampTable BuildWaterLampTable(const WaterLampRecords& records, const FrameInputs& in);
float WaterLampLobe(float halfwayCosine, float roughness);
void WaterLampGlint(const WaterLampTable& table, int slot, const WaterLampView& view, float* out);
float WaterLampShown(float glint, float gain);
