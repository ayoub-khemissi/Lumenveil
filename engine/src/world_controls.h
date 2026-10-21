#pragma once

#include "engine.h"

#include <string>

constexpr int kGameMinutesPerDay = 1440;
constexpr float kClientSlowestClock = 1.0f / 60.0f;
constexpr float kClientFastestClock = 60.0f;
constexpr float kRealTimeClockSpeed = 1.0f / 60.0f;
constexpr int kClockResyncMinutes = 2;
constexpr float kClockJumpMinutes = 5.0f;
constexpr float kClockJumpSeconds = 2.0f;
constexpr float kWeatherTargetMoved = 0.001f;
constexpr float kMinForcedWeather = 0.3f;
constexpr double kWeatherReapplySeconds = 1.0;
constexpr int kWeatherTypeClear = 0;
constexpr int kWeatherTypeRain = 1;
constexpr int kWeatherTypeSnow = 2;
constexpr int kWeatherTypeSand = 3;

struct ClientClockState
{
    bool valid;
    int minute;
    float speed;
};

class ClientClockAccess
{
public:
    virtual ~ClientClockAccess() = default;
    virtual ClientClockState Read() = 0;
    virtual bool Set(int minute, float speed) = 0;
};

enum class ClockMode
{
    Server,
    Hold,
    Run,
};

int WrapMinute(int minute);
float MinutesBetween(float from, float to);
float ClockSpeedFor(float timesRealTime);

class ClockOverride
{
public:
    void Hold(int minute, double now);
    void Run(int minute, float timesRealTime, double now);
    void Release();
    void Update(ClientClockAccess& clock, double now);

    ClockMode Mode() const { return m_mode; }
    bool Releasing() const { return m_releasing; }
    bool ServerKnown() const { return m_serverKnown; }
    int ServerMinute(double now) const;
    float ServerSpeed() const { return m_serverSpeed; }
    int TargetMinute(double now) const;
    float TimesRealTime() const { return m_targetSpeed / kRealTimeClockSpeed; }
    int Applications() const { return m_applications; }
    int ServerResets() const { return m_serverResets; }

private:
    float TargetMinuteAt(double now) const;
    float AppliedSpeed() const;
    void Apply(ClientClockAccess& clock, double now);
    void RecordServer(const ClientClockState& state, double now);

    ClockMode m_mode = ClockMode::Server;
    bool m_releasing = false;
    bool m_applied = false;
    float m_startMinute = 0.0f;
    double m_startAt = 0.0;
    float m_targetSpeed = 0.0f;
    float m_appliedMinute = 0.0f;
    double m_appliedAt = 0.0;
    float m_appliedSpeed = 0.0f;
    bool m_serverKnown = false;
    float m_serverMinute = 0.0f;
    double m_serverAt = 0.0;
    float m_serverSpeed = kRealTimeClockSpeed;
    int m_applications = 0;
    int m_serverResets = 0;
};

struct ClientWeatherState
{
    bool valid;
    int type;
    float target;
    float current;
};

class ClientWeatherAccess
{
public:
    virtual ~ClientWeatherAccess() = default;
    virtual ClientWeatherState Read() = 0;
    virtual bool Set(int type, float grade, bool instant) = 0;
};

enum class WeatherMode
{
    Server,
    Forced,
};

int WeatherTypeOf(WeatherKind kind);
int WeatherStateFor(int type, float grade);
const char* WeatherTypeName(int type);

class WeatherOverride
{
public:
    void Force(int type, float grade, bool instant);
    void Release(bool instant);
    void Update(ClientWeatherAccess& weather, double now);

    WeatherMode Mode() const { return m_mode; }
    bool Releasing() const { return m_releasing; }
    bool ServerKnown() const { return m_serverKnown; }
    int ServerType() const { return m_serverType; }
    float ServerGrade() const { return m_serverGrade; }
    int ForcedType() const { return m_type; }
    float ForcedGrade() const { return m_grade; }
    int Applications() const { return m_applications; }
    int ServerChanges() const { return m_serverChanges; }

private:
    void Apply(ClientWeatherAccess& weather, double now);

    WeatherMode m_mode = WeatherMode::Server;
    bool m_releasing = false;
    bool m_releaseInstant = true;
    bool m_applied = false;
    bool m_everApplied = false;
    double m_lastApply = 0.0;
    int m_type = kWeatherTypeClear;
    float m_grade = 0.0f;
    bool m_instant = true;
    ClientWeatherState m_seen = {};
    bool m_serverKnown = false;
    int m_serverType = kWeatherTypeClear;
    float m_serverGrade = 0.0f;
    int m_applications = 0;
    int m_serverChanges = 0;
};

class FarClipRefresh
{
public:
    void Seen(float setting);
    void Request();
    int Apply(engine::FarClipSetFn set);
    bool Pending() const { return m_pending; }
    float Setting() const { return m_setting; }
    int Requests() const { return m_requests; }

private:
    bool m_seen = false;
    float m_setting = 0.0f;
    bool m_pending = false;
    int m_requests = 0;
};

