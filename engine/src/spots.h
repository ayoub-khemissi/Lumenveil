#pragma once

#include <string>

struct Config;
struct FrameInputs;

struct SpotView
{
    float yaw;
    float pitchDegrees;
    float distance;
    float fovDegrees;
};

SpotView SpotViewOf(const FrameInputs& in);
std::string SpotGameTime(float dayFraction);
std::string SpotRecord(int number, const std::string& savedAt, const FrameInputs& in, const std::string& settingsFile);
int NextSpotNumber(const std::string& spotsFile);

void SpotsSetDirectory(const std::string& dir);
void ArmSpot();
bool SpotArmed();
bool SaveSpotIfArmed(const FrameInputs& in, const Config& cfg);
bool SpotToast(std::string& text);
