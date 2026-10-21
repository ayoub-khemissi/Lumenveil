#pragma once

#include "world_controls.h"

#include <string>

bool ClientWorldMatchesClient();
void EnableClientWorld();
bool ClientWorldEnabled();
bool ClientWorldFailed();

class GameClock : public ClientClockAccess
{
public:
    ClientClockState Read() override;
    bool Set(int minute, float speed) override;
};

class GameWeather : public ClientWeatherAccess
{
public:
    ClientWeatherState Read() override;
    bool Set(int type, float grade, bool instant) override;
};

int CurrentAreaId();
