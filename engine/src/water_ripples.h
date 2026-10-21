#pragma once

#include "water_rings.h"

constexpr int kWaterRippleSources = 27;
constexpr int kWaterRippleTableRows = 2;
constexpr double kWaterRippleStepSeconds = 0.075;
constexpr double kWaterRippleSpacingYards = 0.5;
constexpr double kWaterRippleSwimSeconds = 0.3;
constexpr double kWaterRippleStillSeconds = 0.6;
constexpr double kWaterRippleFullSpeed = 7.0;
constexpr double kWaterRippleSpeedSeconds = 0.2;
constexpr double kWaterRippleStillAmplitude = 0.3;
constexpr double kWaterRippleSwimAmplitude = 0.7;
constexpr double kWaterRippleForgetYards = 30.0;
constexpr double kWaterRippleWetYards = 0.05;
constexpr double kWaterRippleDeepestYards = 2.5;
constexpr double kWaterRippleFadeYards = 1.0;
constexpr double kWaterRippleBehindFront = 0.4;
constexpr double kWaterRippleReachMargin = 0.5;
constexpr double kWaterRippleUnusedAge = 1e4;
constexpr WaterRingShape kWaterRippleRing = {0.25, 0.1, 0.8, 0.2, 2.0, 0.05, 0.05, 1e4, 1.0};

struct WaterRippleSource
{
    double x;
    double y;
    double z;
    double born;
    double amplitude;
};

struct WaterRippleConstants
{
    float ripples[4];
    float reach[4];
    float table[kWaterRippleTableRows][kWaterRippleSources][4];
};

double WaterRippleAmplitude(double speed, bool swimming);
double WaterRippleWetness(double immersion);

class WaterRipples
{
public:
    bool Update(bool known, const float* feet, bool swimming, double seconds);
    void Forget();
    int Count() const { return m_count; }
    const WaterRippleSource& Source(int newest) const;
    double Speed() const { return m_speed; }
    WaterRippleConstants Constants(const float* camPos, double seconds, float strength) const;
    void SlopeAt(double seconds, float strength, double x, double y, double waterZ, const double* dx,
                 const double* dy, double out[3]) const;

private:
    WaterRippleSource m_sources[kWaterRippleSources] = {};
    int m_count = 0;
    int m_next = 0;
    double m_lastEmit = 0.0;
    double m_lastSeconds = 0.0;
    double m_last[3] = {};
    double m_speed = 0.0;
    bool m_have = false;
};
