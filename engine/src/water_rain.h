#pragma once

#include "water_rings.h"

#include <cstdint>

constexpr int kWaterRainTableTexels = 256;
constexpr double kWaterRainCellYards = 0.5;
constexpr int kWaterRainSlots = 3;
constexpr double kWaterRainLifeSeconds = 1.0;
constexpr double kWaterRainFrontYards = 0.35;
constexpr double kWaterRainPacketYards = 0.05;
constexpr double kWaterRainWavelengthYards = 0.08;
constexpr double kWaterRainPeakSlope = 0.35;
constexpr double kWaterRainFadeInLife = 0.06;
constexpr double kWaterRainCoreYards = 0.02;
constexpr double kWaterRainTaperYards = 0.1;
constexpr double kWaterRainActivePerIntensity = 0.5;
constexpr double kWaterRainResolvedFootprint = 0.06;
constexpr double kWaterRainPooledFootprint = 0.12;
constexpr double kWaterRainCycleWrap = 256.0;
constexpr int kWaterRainSlotStep[2] = {17, 59};
constexpr int kWaterRainCycleStep[2] = {31, 13};
constexpr int kWaterRainPhaseRow = 128;
constexpr double kWaterRainMinAmplitude = 0.5;
constexpr float kWaterRainMostStrength = 2.0f;
constexpr WaterRingShape kWaterRainRing = {kWaterRainWavelengthYards, kWaterRainPacketYards, kWaterRainFrontYards,
                                           kWaterRainPeakSlope,       kWaterRainLifeSeconds, kWaterRainFadeInLife,
                                           kWaterRainCoreYards,       kWaterRainCellYards,   kWaterRainTaperYards};

enum WaterRainChannel
{
    kWaterRainX,
    kWaterRainY,
    kWaterRainActive,
    kWaterRainAmplitudeOrPhase,
};

struct WaterRainConstants
{
    float rain[4];
    float cell[4];
};

void BuildWaterRainTable(uint32_t* texels);
float WaterRainTableValue(int u, int v, WaterRainChannel channel);
float WaterRainActiveShare(bool raining, float intensity);
float WaterRainStrength(float waterRain);
double WaterRainLifeEnvelope(double age);
double WaterRainRingSlope(double radius, double age, double amplitude);
double WaterRainRingEnergy(double age);
double WaterRainMeanRingEnergy();
double WaterRainMeanSquareAmplitude();
float WaterRainVariance(float activeShare);
float WaterRainCycles(double seconds);
WaterRainConstants BuildWaterRainConstants(const float* camPos, double seconds, float activeShare, float strength);
double WaterRainPooledShare(const double* dx, const double* dy);
void WaterRainSlopeAt(const WaterRainConstants& c, double x, double y, const double* dx, const double* dy,
                      double out[3]);
