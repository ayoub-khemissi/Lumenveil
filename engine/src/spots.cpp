#include "spots.h"

#include "config.h"
#include "engine.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDegreesPerRadian = 180.0f / kPi;
constexpr int kMinutesPerDay = 24 * 60;
constexpr int kMinutesPerHour = 60;
constexpr ULONGLONG kToastMilliseconds = 3000;
constexpr const char* kSpotsFile = "CoAVolFog-spots.txt";
constexpr const char* kSpotsFolder = "CoAVolFog-spots";
constexpr const char* kSpotHeader = "[spot ";

std::string g_directory;
std::atomic<bool> g_armed{false};
std::string g_toast;
ULONGLONG g_toastSince = 0;

const char* SpotWeatherName(WeatherKind weather)
{
    static const char* const kNames[] = {"clear", "rain", "snow", "sand"};
    return kNames[static_cast<int>(weather)];
}

std::string Format(const char* format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

std::string LocalTimeText()
{
    SYSTEMTIME t = {};
    GetLocalTime(&t);
    return Format("%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

bool WriteText(const std::string& path, const std::string& text, bool append)
{
    HANDLE file = CreateFileA(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE &&
                    written == text.size();
    CloseHandle(file);
    return ok;
}

std::string ReadText(const std::string& path)
{
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return std::string();
    std::string text;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(file, buf, sizeof(buf), &got, nullptr) && got > 0)
        text.append(buf, got);
    CloseHandle(file);
    return text;
}

std::string Triple(const float* v)
{
    return Format("%.2f %.2f %.2f", v[0], v[1], v[2]);
}
}

SpotView SpotViewOf(const FrameInputs& in)
{
    const float dx = in.camTarget[0] - in.camPos[0];
    const float dy = in.camTarget[1] - in.camPos[1];
    const float dz = in.camTarget[2] - in.camPos[2];
    const float flat = std::sqrt(dx * dx + dy * dy);
    SpotView v = {};
    v.distance = std::sqrt(flat * flat + dz * dz);
    float yaw = std::atan2(dy, dx);
    if (yaw < 0.0f)
        yaw += kTwoPi;
    v.yaw = yaw;
    v.pitchDegrees = std::atan2(dz, flat) * kDegreesPerRadian;
    const float focal = in.glProjection[5];
    v.fovDegrees = focal > 0.0f ? 2.0f * std::atan(1.0f / focal) * kDegreesPerRadian : 0.0f;
    return v;
}

std::string SpotGameTime(float dayFraction)
{
    const float day = dayFraction - std::floor(dayFraction);
    const int minutes = std::clamp(static_cast<int>(std::floor(day * kMinutesPerDay)), 0, kMinutesPerDay - 1);
    return Format("%02d:%02d", minutes / kMinutesPerHour, minutes % kMinutesPerHour);
}

std::string SpotRecord(int number, const std::string& savedAt, const FrameInputs& in, const std::string& settingsFile)
{
    const SpotView v = SpotViewOf(in);
    std::string r = Format("[spot %d]\r\n", number);
    r += "saved = " + savedAt + "\r\n";
    r += Format("map = %d\r\n", in.mapId);
    r += "player = " + (in.playerKnown ? Triple(in.playerPos) : std::string("unknown")) + "\r\n";
    if (in.playerKnown)
        r += Format("facing = %.3f\r\n", in.playerFacing);
    r += "camera = " + Triple(in.camPos) + "\r\n";
    r += "target = " + Triple(in.camTarget) + "\r\n";
    r += Format("view = yaw %.3f pitch %.1f distance %.1f fov %.1f\r\n", v.yaw, v.pitchDegrees, v.distance,
                v.fovDegrees);
    r += "game time = " + SpotGameTime(in.dayFraction) + Format(" (day %.4f)\r\n", in.dayFraction);
    r += Format("light = sun %.3f moon %.3f, the %s lights the fog\r\n", in.sunElevation, in.moonElevation,
                in.lightIsMoon ? "moon" : "sun");
    r += Format("weather = %s %.2f\r\n", SpotWeatherName(in.weather), in.weatherIntensity);
    r += Format("indoors = %.2f\r\n", in.indoors);
    r += Format("far clip = %.0f (the client's own cap %.0f), fog end %.0f\r\n", in.farClip, in.clientFarClip,
                in.fogEnd);
    r += "settings = " + settingsFile + "\r\n\r\n";
    return r;
}

int NextSpotNumber(const std::string& spotsFile)
{
    const std::string text = ReadText(spotsFile);
    int highest = 0;
    size_t at = 0;
    while ((at = text.find(kSpotHeader, at)) != std::string::npos)
    {
        if (at == 0 || text[at - 1] == '\n')
            highest = std::max(highest, std::atoi(text.c_str() + at + std::strlen(kSpotHeader)));
        at += std::strlen(kSpotHeader);
    }
    return highest + 1;
}

void SpotsSetDirectory(const std::string& dir)
{
    g_directory = dir;
}

void ArmSpot()
{
    g_armed = true;
}

bool SpotArmed()
{
    return g_armed;
}

bool SaveSpotIfArmed(const FrameInputs& in, const Config& cfg)
{
    if (!g_armed.exchange(false))
        return false;
    g_toastSince = GetTickCount64();
    if (g_directory.empty())
    {
        g_toast = "The spot could not be saved: no folder for it";
        VF_LOG_ERROR("spot: no folder to save it in");
        return false;
    }
    const std::string spotsFile = g_directory + kSpotsFile;
    const int number = NextSpotNumber(spotsFile);
    const std::string settingsFile = Format("%s\\spot-%03d.ini", kSpotsFolder, number);
    CreateDirectoryA((g_directory + kSpotsFolder).c_str(), nullptr);
    const bool settingsSaved = WriteText(g_directory + settingsFile, LookSettingsText(cfg), false);
    const bool recordSaved =
        WriteText(spotsFile, SpotRecord(number, LocalTimeText(), in, settingsSaved ? settingsFile : "not saved"), true);
    if (!recordSaved)
    {
        g_toast = "The spot could not be saved; see CoAVolFog.log";
        VF_LOG_ERROR("spot %d could not be written to %s (error %lu)", number, spotsFile.c_str(), GetLastError());
        return false;
    }
    g_toast = Format("Spot %d saved: %s, %s %.1f", number, SpotGameTime(in.dayFraction).c_str(),
                     SpotWeatherName(in.weather), in.weatherIntensity);
    const SpotView v = SpotViewOf(in);
    VF_LOG_INFO("spot %d saved to %s: map %d, camera (%s) yaw %.3f pitch %.1f distance %.1f, game time %s, %s %.2f%s",
                number, spotsFile.c_str(), in.mapId, Triple(in.camPos).c_str(), v.yaw, v.pitchDegrees, v.distance,
                SpotGameTime(in.dayFraction).c_str(), SpotWeatherName(in.weather), in.weatherIntensity,
                settingsSaved ? "" : "; the settings could not be saved");
    return true;
}

bool SpotToast(std::string& text)
{
    if (g_toast.empty() || GetTickCount64() - g_toastSince >= kToastMilliseconds)
        return false;
    text = g_toast;
    return true;
}
