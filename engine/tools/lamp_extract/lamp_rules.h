#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lamp_data.h"

struct Rgb
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

enum class NameKind
{
    Chandelier,
    Candle,
    Lamp,
    Brazier,
    Torch,
};

constexpr float kMinFlameSize = 0.04f;
constexpr float kMinFlameSaturation = 0.15f;
constexpr Rgb kDefaultWarm{1.0f, 0.55f, 0.2f};
constexpr Rgb kDefaultLamp{1.0f, 0.72f, 0.40f};
constexpr float kMaxReach = 48.0f;
constexpr float kWarmHueEnd = 60.0f;
constexpr float kWarmHueStart = 345.0f;
constexpr float kMinColdChannel = 0.4f;

std::string UpperPath(std::string_view path);
std::string UpperStem(std::string_view path);

bool IsSpellPath(std::string_view path);
bool FireTexture(std::string_view texture);
bool GlowTexture(std::string_view texture);
bool IsBrokenOrOff(std::string_view path);
bool IsLightBeam(std::string_view path);
bool InLampDir(std::string_view path);
bool LampFamilyOk(std::string_view path);
bool LooksLikeLightSource(std::string_view path);

std::optional<NameKind> LampNameKind(std::string_view path);
bool NameKindIsFire(NameKind kind);
std::optional<Rgb> NameTint(std::string_view path);
Rgb LampColor(std::string_view path);
Rgb GlowLampColor(std::string_view path, std::optional<Rgb> glow);
bool WarmHue(Rgb color);
Rgb KindColor(lampfile::LampKind kind);
Rgb SourceColor(std::string_view path, lampfile::LampKind kind, std::optional<Rgb> detected);
float LampIntensity(std::optional<NameKind> kind);
lampfile::LampKind LampKindOf(std::optional<NameKind> kind);

float FireIntensity(float strength);
lampfile::LampKind KindFromIntensity(float intensity);
float ReachFromIntensity(float intensity);
std::optional<Rgb> MostSaturatedKey(const std::vector<Rgb>& keys);
std::optional<Rgb> MostVisibleKey(const std::vector<Rgb>& keys, const std::vector<float>& alphas,
                                  const std::vector<float>& sizes);
float KeyVisibility(const std::vector<Rgb>& keys, const std::vector<float>& alphas, const std::vector<float>& sizes);
Rgb SoftenedColdColor(Rgb color);
Rgb PeakNormalized(Rgb color, float* peak);
