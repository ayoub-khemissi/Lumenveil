#include "client_world.h"

#include "client_code.h"
#include "engine.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace
{
constexpr uintptr_t kGameClock = 0x00D37F98;
constexpr uintptr_t kClockMinute = 0x00;
constexpr uintptr_t kClockHour = 0x04;
constexpr uintptr_t kClockMinuteOffset = 0x24;
constexpr uintptr_t kClockSpeed = 0x30;
constexpr size_t kClockTimeBytes = 0x20;
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr uintptr_t kClockSet = 0x0076D810;
constexpr uintptr_t kClockNotify = 0x007E2730;
constexpr uintptr_t kClockSpeedSet = 0x0076CFA0;
constexpr uintptr_t kTimeSpeedHandlerSite = 0x007E2950;
constexpr ClientCode kTimeSpeedHandlerCode = {kTimeSpeedHandlerSite, 41, 0x3578b8d18c3924a9ull};
constexpr ClientCode kClockSetCode = {kClockSet, 33, 0x489f0c3e619179e7ull};
constexpr uintptr_t kClockStepBack = 0x0076D89E;
constexpr ClientCode kClockStepBackCode = {kClockStepBack, 17, 0xe7312f04dc5d48f3ull};
constexpr ClientCode kClockSpeedSetCode = {kClockSpeedSet, 15, 0x89473ac4bb298007ull};
constexpr ClientCode kClockNotifyCode = {kClockNotify, 11, 0x4fd572eeaf9d8f6full};

constexpr uintptr_t kWeatherObject = 0x00CD7544;
constexpr uintptr_t kWeatherTarget = 0x00;
constexpr uintptr_t kWeatherCurrent = 0x08;
constexpr uintptr_t kWeatherType = 0x20;
constexpr uintptr_t kWeatherHandlerSite = 0x00526A96;
constexpr ClientCode kWeatherHandlerCode = {kWeatherHandlerSite, 77, 0x6541020e1fb1789aull};
constexpr uintptr_t kWeatherSet = 0x007846A0;
constexpr ClientCode kWeatherSetCode = {kWeatherSet, 27, 0xf891718ffb3fbcbbull};
constexpr uintptr_t kWeatherTypeStoreSite = 0x0078476A;
constexpr ClientCode kWeatherTypeStoreCode = {kWeatherTypeStoreSite, 20, 0xc2493cfa04958026ull};
constexpr uintptr_t kWeatherTargetStoreSite = 0x007847A3;
constexpr ClientCode kWeatherTargetStoreCode = {kWeatherTargetStoreSite, 7, 0x7cf889e3eda1e304ull};
constexpr uintptr_t kWeatherStates = 0x00AD4EB0;
constexpr uintptr_t kWeatherStateLookup = 0x0065C290;
constexpr ClientCode kWeatherStateLookupCode = {kWeatherStateLookup, 26, 0x61fd8d74682f25ddull};
constexpr uintptr_t kWeatherAmbience = 0x004C8610;
constexpr ClientCode kWeatherAmbienceCode = {kWeatherAmbience, 18, 0x85fff6d560257851ull};
constexpr uintptr_t kStateSound = 0x04;
constexpr uintptr_t kStateType = 0x08;
constexpr uintptr_t kStateFactor = 0x0C;
constexpr int kNoAmbience = -1;


constexpr size_t kAreaStoreOffset = 42;
constexpr ClientCode kAreaStoreCode = {engine::kZoneSetter + kAreaStoreOffset, 5, 0xf787686d6337becfull};
constexpr uintptr_t kAreaId = 0x00BD0810;

using ClockSetFn = void(__fastcall*)(void* clock, void* unused, const void* time, int stepMinute);
using ClockNotifyFn = void(__cdecl*)();
using ClockSpeedFn = void(__fastcall*)(void* clock, void* unused, float speed);
using WeatherSetFn = void(__fastcall*)(void* weather, void* unused, int type, float grade, const void* state,
                                       int smooth, float factor);
using StateLookupFn = const void*(__fastcall*)(void* table, void* unused, int id);
using AmbienceFn = void(__cdecl*)(int sound);

bool g_enabled = false;
bool g_failed = false;

int Fault(unsigned code, const char* where)
{
    VF_LOG_ERROR("exception 0x%08X while %s; the region panel's clock, weather and commands are off for this session",
                 code, where);
    g_failed = true;
    return EXCEPTION_EXECUTE_HANDLER;
}

bool Usable()
{
    return g_enabled && !g_failed;
}

bool ReadClock(ClientClockState& out)
{
    __try
    {
        const int minute = *reinterpret_cast<const int*>(kGameClock + kClockMinute);
        const int hour = *reinterpret_cast<const int*>(kGameClock + kClockHour);
        const float speed = *reinterpret_cast<const float*>(kGameClock + kClockSpeed);
        out.valid = minute >= 0 && minute < kMinutesPerHour && hour >= 0 && hour < kHoursPerDay && speed > 0.0f;
        out.minute = hour * kMinutesPerHour + minute;
        out.speed = speed;
        return out.valid;
    }
    __except (Fault(GetExceptionCode(), "reading the game clock"))
    {
        return false;
    }
}

bool SetClock(int minute, float speed)
{
    __try
    {
        unsigned char time[kClockTimeBytes];
        std::memcpy(time, reinterpret_cast<const void*>(kGameClock), sizeof(time));
        const int offset = *reinterpret_cast<const int*>(kGameClock + kClockMinuteOffset);
        const int stored = WrapMinute(minute - offset);
        const int storedMinute = stored % kMinutesPerHour;
        const int storedHour = stored / kMinutesPerHour;
        std::memcpy(time + kClockMinute, &storedMinute, sizeof(storedMinute));
        std::memcpy(time + kClockHour, &storedHour, sizeof(storedHour));
        void* clock = reinterpret_cast<void*>(kGameClock);
        reinterpret_cast<ClockSetFn>(kClockSet)(clock, nullptr, time, 1);
        reinterpret_cast<ClockNotifyFn>(kClockNotify)();
        reinterpret_cast<ClockSpeedFn>(kClockSpeedSet)(clock, nullptr,
                                                       std::clamp(speed, kClientSlowestClock, kClientFastestClock));
        return true;
    }
    __except (Fault(GetExceptionCode(), "setting the game clock"))
    {
        return false;
    }
}

bool ReadWeather(ClientWeatherState& out)
{
    __try
    {
        const uintptr_t weather = *reinterpret_cast<const uintptr_t*>(kWeatherObject);
        out.valid = weather != 0;
        if (!weather)
            return false;
        out.type = *reinterpret_cast<const int*>(weather + kWeatherType);
        out.target = *reinterpret_cast<const float*>(weather + kWeatherTarget);
        out.current = *reinterpret_cast<const float*>(weather + kWeatherCurrent);
        return true;
    }
    __except (Fault(GetExceptionCode(), "reading the weather"))
    {
        return false;
    }
}

bool SetWeather(int type, float grade, bool instant)
{
    __try
    {
        void* weather = *reinterpret_cast<void* const*>(kWeatherObject);
        if (!weather)
            return false;
        const void* state = reinterpret_cast<StateLookupFn>(kWeatherStateLookup)(
            reinterpret_cast<void*>(kWeatherStates), nullptr, WeatherStateFor(type, grade));
        const uintptr_t record = reinterpret_cast<uintptr_t>(state);
        const int stateType = record ? *reinterpret_cast<const int*>(record + kStateType) : kWeatherTypeClear;
        const int sound = record ? *reinterpret_cast<const int*>(record + kStateSound) : 0;
        const float factor = record ? *reinterpret_cast<const float*>(record + kStateFactor) : 1.0f;
        reinterpret_cast<WeatherSetFn>(kWeatherSet)(weather, nullptr, stateType, grade, state, instant ? 0 : 1,
                                                     factor);
        reinterpret_cast<AmbienceFn>(kWeatherAmbience)(sound > 0 ? sound : kNoAmbience);
        return true;
    }
    __except (Fault(GetExceptionCode(), "setting the weather"))
    {
        return false;
    }
}


int ReadArea()
{
    __try
    {
        return *reinterpret_cast<const int*>(kAreaId);
    }
    __except (Fault(GetExceptionCode(), "reading the area id"))
    {
        return -1;
    }
}
}

bool ClientWorldMatchesClient()
{
    const ClientCode codes[] = {kTimeSpeedHandlerCode, kClockSetCode,           kClockStepBackCode,
                                kClockSpeedSetCode,    kClockNotifyCode,        kWeatherHandlerCode,
                                kWeatherSetCode,       kWeatherTypeStoreCode,   kWeatherTargetStoreCode,
                                kWeatherStateLookupCode, kWeatherAmbienceCode,  kAreaStoreCode,
    };
    for (const ClientCode& code : codes)
        if (!ClientCodeMatches(code))
            return false;
    return true;
}

void EnableClientWorld()
{
    g_enabled = true;
}

bool ClientWorldEnabled()
{
    return g_enabled;
}

bool ClientWorldFailed()
{
    return g_failed;
}

ClientClockState GameClock::Read()
{
    ClientClockState state = {};
    if (Usable())
        ReadClock(state);
    return state;
}

bool GameClock::Set(int minute, float speed)
{
    return Usable() && SetClock(WrapMinute(minute), speed);
}

ClientWeatherState GameWeather::Read()
{
    ClientWeatherState state = {};
    if (Usable())
        ReadWeather(state);
    return state;
}

bool GameWeather::Set(int type, float grade, bool instant)
{
    return Usable() && SetWeather(type, std::clamp(grade, 0.0f, 1.0f), instant);
}


int CurrentAreaId()
{
    return Usable() ? ReadArea() : -1;
}
