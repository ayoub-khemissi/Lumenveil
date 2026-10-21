#pragma once

struct WaterRingShape
{
    double wavelengthYards;
    double packetYards;
    double frontYardsPerSecond;
    double peakSlope;
    double lifeSeconds;
    double fadeInSeconds;
    double coreYards;
    double edgeYards;
    double taperYards;
};

constexpr double kWaterRingFootprintShare = 0.5;
constexpr double kWaterRingMinRadiusYards = 1e-4;

double WaterRingEnvelope(const WaterRingShape& shape, double age);
double WaterRingStrength(const WaterRingShape& shape, double radius, double age, double amplitude);
double WaterRingSlope(const WaterRingShape& shape, double radius, double age, double amplitude);
void AddWaterRing(const WaterRingShape& shape, double tx, double ty, double age, double amplitude, const double* dx,
                  const double* dy, double out[3]);
