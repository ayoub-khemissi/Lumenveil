#include "world_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr float kHalfDay = 0.5f * kGameMinutesPerDay;
constexpr float kLightGrade = 0.40f;
constexpr float kMediumGrade = 0.70f;
constexpr float kFineGrade = 0.27f;
constexpr int kStateFine = 0;
constexpr int kStateLightRain = 3;
constexpr int kStateLightSnow = 6;
constexpr int kStateLightSand = 22;
constexpr int kStateMediumSand = 41;
constexpr int kStateHeavySand = 42;

float WrapMinuteF(float minute)
{
    float m = std::fmod(minute, static_cast<float>(kGameMinutesPerDay));
    return m < 0.0f ? m + kGameMinutesPerDay : m;
}

int Rounded(float minute)
{
    return WrapMinute(static_cast<int>(std::lround(minute)));
}

}

int WrapMinute(int minute)
{
    const int m = minute % kGameMinutesPerDay;
    return m < 0 ? m + kGameMinutesPerDay : m;
}

float MinutesBetween(float from, float to)
{
    float d = WrapMinuteF(to - from);
    return d > kHalfDay ? d - kGameMinutesPerDay : d;
}

float ClockSpeedFor(float timesRealTime)
{
    return std::clamp(timesRealTime * kRealTimeClockSpeed, kClientSlowestClock, kClientFastestClock);
}

void ClockOverride::Hold(int minute, double now)
{
    m_mode = ClockMode::Hold;
    m_releasing = false;
    m_applied = false;
    m_startMinute = static_cast<float>(WrapMinute(minute));
    m_startAt = now;
    m_targetSpeed = 0.0f;
}

void ClockOverride::Run(int minute, float timesRealTime, double now)
{
    m_mode = ClockMode::Run;
    m_releasing = false;
    m_applied = false;
    m_startMinute = static_cast<float>(WrapMinute(minute));
    m_startAt = now;
    m_targetSpeed = ClockSpeedFor(timesRealTime);
}

void ClockOverride::Release()
{
    if (m_mode == ClockMode::Server)
        return;
    m_mode = ClockMode::Server;
    m_releasing = true;
    m_applied = false;
}

float ClockOverride::TargetMinuteAt(double now) const
{
    return WrapMinuteF(m_startMinute + static_cast<float>(now - m_startAt) * m_targetSpeed);
}

int ClockOverride::TargetMinute(double now) const
{
    return Rounded(TargetMinuteAt(now));
}

int ClockOverride::ServerMinute(double now) const
{
    return Rounded(m_serverMinute + static_cast<float>(now - m_serverAt) * m_serverSpeed);
}

float ClockOverride::AppliedSpeed() const
{
    return m_mode == ClockMode::Hold ? kClientSlowestClock : m_targetSpeed;
}

void ClockOverride::RecordServer(const ClientClockState& state, double now)
{
    m_serverKnown = true;
    m_serverMinute = static_cast<float>(state.minute);
    m_serverAt = now;
    m_serverSpeed = state.speed;
}

void ClockOverride::Apply(ClientClockAccess& clock, double now)
{
    const int minute = TargetMinute(now);
    const float speed = AppliedSpeed();
    if (!clock.Set(minute, speed))
        return;
    m_applied = true;
    m_appliedMinute = static_cast<float>(minute);
    m_appliedAt = now;
    m_appliedSpeed = speed;
    ++m_applications;
}

void ClockOverride::Update(ClientClockAccess& clock, double now)
{
    const ClientClockState state = clock.Read();
    if (!state.valid)
        return;
    if (m_releasing)
    {
        m_releasing = false;
        if (m_serverKnown && clock.Set(ServerMinute(now), m_serverSpeed))
            ++m_applications;
        return;
    }
    if (m_mode == ClockMode::Server)
    {
        RecordServer(state, now);
        return;
    }
    if (!m_serverKnown)
        RecordServer(state, now);
    if (m_applied)
    {
        const float expected = m_appliedMinute + static_cast<float>(now - m_appliedAt) * m_appliedSpeed;
        const bool speedMoved = std::fabs(state.speed - m_appliedSpeed) > 1e-4f * std::max(1.0f, m_appliedSpeed);
        const float tolerance = std::max(kClockJumpMinutes, m_appliedSpeed * kClockJumpSeconds);
        const bool jumped = std::fabs(MinutesBetween(expected, static_cast<float>(state.minute))) > tolerance;
        if (speedMoved || jumped)
        {
            RecordServer(state, now);
            ++m_serverResets;
            m_applied = false;
        }
    }
    if (!m_applied ||
        std::fabs(MinutesBetween(TargetMinuteAt(now), static_cast<float>(state.minute))) >= kClockResyncMinutes)
        Apply(clock, now);
}

int WeatherTypeOf(WeatherKind kind)
{
    switch (kind)
    {
    case WeatherKind::Rain:
        return kWeatherTypeRain;
    case WeatherKind::Snow:
        return kWeatherTypeSnow;
    case WeatherKind::Sand:
        return kWeatherTypeSand;
    default:
        return kWeatherTypeClear;
    }
}

int WeatherStateFor(int type, float grade)
{
    if (grade < kFineGrade)
        return kStateFine;
    const int step = grade < kLightGrade ? 0 : grade < kMediumGrade ? 1 : 2;
    switch (type)
    {
    case kWeatherTypeRain:
        return kStateLightRain + step;
    case kWeatherTypeSnow:
        return kStateLightSnow + step;
    case kWeatherTypeSand:
        return step == 0 ? kStateLightSand : step == 1 ? kStateMediumSand : kStateHeavySand;
    default:
        return kStateFine;
    }
}

const char* WeatherTypeName(int type)
{
    switch (type)
    {
    case kWeatherTypeRain:
        return "rain";
    case kWeatherTypeSnow:
        return "snow";
    case kWeatherTypeSand:
        return "sand";
    default:
        return "clear";
    }
}

void WeatherOverride::Force(int type, float grade, bool instant)
{
    m_mode = WeatherMode::Forced;
    m_releasing = false;
    m_applied = false;
    m_type = std::clamp(type, kWeatherTypeClear, kWeatherTypeSand);
    m_grade = m_type == kWeatherTypeClear ? 0.0f : std::clamp(grade, kMinForcedWeather, 1.0f);
    m_instant = instant;
}

void WeatherOverride::Release(bool instant)
{
    if (m_mode == WeatherMode::Server)
        return;
    m_mode = WeatherMode::Server;
    m_releasing = true;
    m_releaseInstant = instant;
    m_applied = false;
}

void WeatherOverride::Apply(ClientWeatherAccess& weather, double now)
{
    m_lastApply = now;
    m_everApplied = true;
    if (!weather.Set(m_type, m_grade, m_instant))
        return;
    m_seen = weather.Read();
    m_applied = true;
    ++m_applications;
}

void WeatherOverride::Update(ClientWeatherAccess& weather, double now)
{
    const ClientWeatherState state = weather.Read();
    if (!state.valid)
        return;
    if (m_releasing)
    {
        m_releasing = false;
        if (m_serverKnown && weather.Set(m_serverType, m_serverGrade, m_releaseInstant))
            ++m_applications;
        return;
    }
    if (m_mode == WeatherMode::Server)
    {
        m_serverKnown = true;
        m_serverType = state.type;
        m_serverGrade = state.target;
        return;
    }
    if (!m_serverKnown)
    {
        m_serverKnown = true;
        m_serverType = state.type;
        m_serverGrade = state.target;
    }
    if (m_applied &&
        (state.type != m_seen.type || std::fabs(state.target - m_seen.target) > kWeatherTargetMoved))
    {
        m_serverType = state.type;
        m_serverGrade = state.target;
        ++m_serverChanges;
        m_applied = false;
    }
    if (!m_applied && (!m_everApplied || now - m_lastApply >= kWeatherReapplySeconds))
        Apply(weather, now);
}

void FarClipRefresh::Seen(float setting)
{
    if (!std::isfinite(setting))
        return;
    m_seen = true;
    m_setting = setting;
}

void FarClipRefresh::Request()
{
    m_pending = true;
    ++m_requests;
}

int FarClipRefresh::Apply(engine::FarClipSetFn set)
{
    if (!m_pending)
        return 0;
    m_pending = false;
    if (!m_seen || !set)
        return 0;
    const float setting = m_setting;
    set(setting + 1.0f);
    set(setting);
    return 2;
}

