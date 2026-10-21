#pragma once

constexpr int kWaterWaveLayers = 3;
constexpr int kWaterWavesPerLayer = 48;
constexpr int kWaterWaveTexels = 256;
constexpr int kWaterWaveLevels = 9;
constexpr int kWaterWaveLookups = 7;
constexpr double kWaterGravity = 10.73;
constexpr double kWaterYardsPerMetre = 1.0 / 0.9144;
constexpr double kWaterCapillaryYards = 0.015;
constexpr float kWaterMinWind = 0.5f;
constexpr float kWaterWaveFadeFromTexels = 2.0f;
constexpr float kWaterWaveFadeWidthTexels = 1.0f;
constexpr float kWaterGlitterSharpness = 0.0008f;
constexpr float kWaterUnresolvedGlitterShare = 0.25f;

struct WaterWaveLayerSpec
{
    double tileYards;
    double shortestYards;
    double longestYards;
};

constexpr WaterWaveLayerSpec kWaterWaveLayerSpecs[kWaterWaveLayers] = {
    {417.0, 6.4, 102.4},
    {53.3, 0.8, 6.4},
    {13.7, 0.2, 0.8},
};

struct WaterWaveLookup
{
    int layer;
    double turnDegrees;
};

constexpr WaterWaveLookup kWaterWaveLookupSpecs[kWaterWaveLookups] = {
    {0, 0.0}, {1, 0.0}, {1, 40.0}, {1, 80.0}, {2, 0.0}, {2, 40.0}, {2, 80.0},
};

struct WaterWave
{
    float cyclesX;
    float cyclesY;
    float slope;
    float phase;
};

struct WaterWaveField
{
    WaterWave waves[kWaterWaveLayers][kWaterWavesPerLayer];
    float windSpeed;
    float headingDegrees;
    float peakYards;
    float meanSquareSlope;
    float resolvedMeanSquareSlope;
    float unresolvedMeanSquareSlope;
};

float WaterMeanSquareSlope(float windYardsPerSecond);
float WaterPeakYards(float windYardsPerSecond);
double WaterSpectrumShare(float windYardsPerSecond, double longestYards, double shortestYards);
double WaterAngularFrequency(const WaterWave& wave, int layer);
int WaterWaveLookupsOf(int layer);
double WaterLookupTurnRadians(int lookup);
WaterWaveField BuildWaterWaveField(float windYardsPerSecond, float headingDegrees, float wavesScale, double seconds);
float WaterBaseRoughness(const WaterWaveField& field);
int WaterWaveLevelTexels(int level);
float WaterWaveFade(float cycles, int level);
void WaterWaveTexel(const WaterWaveField& field, int layer, int level, double u, double v, float out[4]);
void WaterLookupUv(int lookup, double x, double y, double* u, double* v);
void WaterSlopeAt(const WaterWaveField& field, double x, double y, int level, double out[2]);
