#include "water_rings.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;

double SmoothStep(double from, double to, double x)
{
    const double t = std::clamp((x - from) / (to - from), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Wavenumber(const WaterRingShape& shape)
{
    return 2.0 * kPi / shape.wavelengthYards;
}
}

double WaterRingEnvelope(const WaterRingShape& shape, double age)
{
    const double left = 1.0 - std::clamp(age / shape.lifeSeconds, 0.0, 1.0);
    return SmoothStep(0.0, shape.fadeInSeconds, age) * left * left;
}

double WaterRingStrength(const WaterRingShape& shape, double radius, double age, double amplitude)
{
    return shape.peakSlope * amplitude * WaterRingEnvelope(shape, age) * SmoothStep(0.0, shape.coreYards, radius) *
           std::clamp((shape.edgeYards - radius) / shape.taperYards, 0.0, 1.0);
}

double WaterRingSlope(const WaterRingShape& shape, double radius, double age, double amplitude)
{
    const double d = radius - age * shape.frontYardsPerSecond;
    const double packet2 = shape.packetYards * shape.packetYards;
    return WaterRingStrength(shape, radius, age, amplitude) * std::exp(-d * d / packet2) *
           std::sin(Wavenumber(shape) * d);
}

void AddWaterRing(const WaterRingShape& shape, double tx, double ty, double age, double amplitude, const double* dx,
                  const double* dy, double out[3])
{
    const double radius = std::sqrt(tx * tx + ty * ty);
    const double length = std::max(radius, kWaterRingMinRadiusYards);
    const double rx = tx / length;
    const double ry = ty / length;
    const double alongX = dx[0] * rx + dx[1] * ry;
    const double alongY = dy[0] * rx + dy[1] * ry;
    const double w2 = kWaterRingFootprintShare * kWaterRingFootprintShare * (alongX * alongX + alongY * alongY);
    const double k = Wavenumber(shape);
    const double packet2 = shape.packetYards * shape.packetYards;
    const double d = radius - age * shape.frontYardsPerSecond;
    const double strength = WaterRingStrength(shape, radius, age, amplitude);
    const double sigma2 = packet2 + 2.0 * w2;
    const double resolved =
        strength * std::sqrt(packet2 / sigma2) * std::exp(-k * k * packet2 * w2 / (2.0 * sigma2) - d * d / sigma2);
    const double s = resolved * std::sin(k * packet2 / sigma2 * d);
    const double tau2 = 0.5 * packet2 + 2.0 * w2;
    const double mean2 = 0.5 * strength * strength * std::sqrt(0.5 * packet2 / tau2) * std::exp(-d * d / tau2) *
                         (1.0 - std::exp(-k * k * packet2 * w2 / tau2) * std::cos(k * packet2 / tau2 * d));
    out[0] += s * rx;
    out[1] += s * ry;
    out[2] += std::max(mean2 - s * s, 0.0);
}
