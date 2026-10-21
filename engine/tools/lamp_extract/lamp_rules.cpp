#include "lamp_rules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace
{

constexpr std::array<std::string_view, 8> kFireTextureKeys = {"FLAMELICK", "BONFIRE", "BRAZIER", "CANDLE",
                                                               "TORCH",     "FLAME",   "EMBER",   "FIRE"};

constexpr std::array<std::string_view, 13> kLampNameKeys = {
    "CHANDELIER", "CANDELABRA", "LAMPPOST", "LAMP POST", "LIGHTPOST", "STREETLAMP", "STREETLIGHT",
    "LANTERN",    "SCONCE",     "LAMP",     "CANDLE",    "BRAZIER",   "TORCH"};

constexpr std::array<std::string_view, 7> kLampDirKeys = {"LAMP",       "LANTERN", "CANDLE", "SCONCE",
                                                           "CHANDELIER", "LIGHTS",  "TORCH"};

constexpr std::array<std::string_view, 3> kBeamKeys = {"SHAFT", "BEAM", "SKYLIGHT"};

constexpr std::array<std::string_view, 9> kGlowTextureKeys = {"GLOW", "LIGHT", "LAMP",  "LANTERN", "FLARE",
                                                               "FIRE", "FLAME", "CANDLE", "TORCH"};

constexpr std::array<std::string_view, 7> kBrokenKeys = {"BROKEN",  "BUSTED", "WRECKED", "SMASHED",
                                                          "DESTROY", "FALLEN", "UNLIT"};

constexpr std::array<std::string_view, 10> kLightSourceHints = {"LAMP",   "LANTERN", "TORCH", "BRAZIER",
                                                                 "CANDLE", "FIRE",    "SCONCE", "CHANDELIER",
                                                                 "LIGHT",  "FORGE"};

template <size_t N>
bool ContainsAny(std::string_view text, const std::array<std::string_view, N>& keys)
{
    return std::any_of(keys.begin(), keys.end(),
                       [&](std::string_view key) { return text.find(key) != std::string_view::npos; });
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

}

std::string UpperPath(std::string_view path)
{
    std::string upper(path);
    for (char& c : upper)
    {
        if (c == '/')
            c = '\\';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper;
}

std::string UpperStem(std::string_view path)
{
    std::string upper = UpperPath(path);
    size_t slash = upper.find_last_of('\\');
    std::string base = slash == std::string::npos ? upper : upper.substr(slash + 1);
    size_t dot = base.find('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

bool IsSpellPath(std::string_view path)
{
    std::string upper = UpperPath(path);
    return StartsWith(upper, "SPELLS\\") || upper.find("\\SPELLS\\") != std::string::npos;
}

bool FireTexture(std::string_view texture)
{
    return !texture.empty() && ContainsAny(UpperStem(texture), kFireTextureKeys);
}

bool GlowTexture(std::string_view texture)
{
    if (texture.empty())
        return false;
    std::string stem = UpperStem(texture);
    return !ContainsAny(stem, kBeamKeys) && ContainsAny(stem, kGlowTextureKeys);
}

bool IsBrokenOrOff(std::string_view path)
{
    std::string stem = UpperStem(path);
    if (ContainsAny(stem, kBrokenKeys))
        return true;
    while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back())))
        stem.pop_back();
    while (!stem.empty() && (stem.back() == '_' || stem.back() == '-'))
        stem.pop_back();
    return stem.size() >= 3 && stem.compare(stem.size() - 3, 3, "OFF") == 0;
}

bool IsLightBeam(std::string_view path)
{
    return ContainsAny(UpperStem(path), kBeamKeys);
}

bool InLampDir(std::string_view path)
{
    std::string upper = UpperPath(path);
    size_t slash = upper.find_last_of('\\');
    if (slash == std::string::npos)
        return false;
    return ContainsAny(std::string_view(upper).substr(0, slash), kLampDirKeys);
}

bool LampFamilyOk(std::string_view path)
{
    std::string upper = UpperPath(path);
    bool world = StartsWith(upper, "WORLD\\") || StartsWith(upper, "DUNGEONS\\");
    return world && upper.find("\\SPELLS\\") == std::string::npos && !StartsWith(upper, "ITEM\\OBJECTCOMPONENTS\\");
}

bool LooksLikeLightSource(std::string_view path)
{
    return ContainsAny(UpperStem(path), kLightSourceHints);
}

std::optional<NameKind> LampNameKind(std::string_view path)
{
    std::string stem = UpperStem(path);
    for (std::string_view key : kLampNameKeys)
    {
        if (stem.find(key) == std::string::npos)
            continue;
        if (key == "CHANDELIER")
            return NameKind::Chandelier;
        if (key == "CANDLE" || key == "CANDELABRA")
            return NameKind::Candle;
        if (key == "BRAZIER")
            return NameKind::Brazier;
        if (key == "TORCH")
            return NameKind::Torch;
        return NameKind::Lamp;
    }
    return std::nullopt;
}

bool NameKindIsFire(NameKind kind)
{
    return kind == NameKind::Brazier || kind == NameKind::Torch;
}

std::optional<Rgb> NameTint(std::string_view path)
{
    std::string stem = UpperStem(path);
    const std::array<std::pair<std::string_view, Rgb>, 4> tints = {{{"BLUE", {0.45f, 0.65f, 1.0f}},
                                                                    {"GREEN", {0.40f, 1.0f, 0.45f}},
                                                                    {"PURPLE", {0.70f, 0.40f, 1.0f}},
                                                                    {"ARCANE", {0.70f, 0.40f, 1.0f}}}};
    for (const auto& [key, rgb] : tints)
        if (stem.find(key) != std::string::npos)
            return rgb;
    std::string trimmed = stem;
    while (!trimmed.empty() && std::isdigit(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    bool redAtEnd = trimmed.size() >= 3 && trimmed.compare(trimmed.size() - 3, 3, "RED") == 0;
    if (redAtEnd || stem.find("_RED_") != std::string::npos || StartsWith(stem, "RED_"))
        return Rgb{1.0f, 0.35f, 0.25f};
    return std::nullopt;
}

Rgb LampColor(std::string_view path)
{
    if (std::optional<Rgb> tint = NameTint(path))
        return *tint;
    std::string stem = UpperStem(path);
    const std::array<std::string_view, 6> lampFamily = {"LAMP", "LANTERN", "POST", "SCONCE", "CANDLE", "CHANDELIER"};
    return ContainsAny(stem, lampFamily) ? kDefaultLamp : kDefaultWarm;
}

Rgb GlowLampColor(std::string_view path, std::optional<Rgb> glow)
{
    if (std::optional<Rgb> tint = NameTint(path))
        return *tint;
    if (glow && !WarmHue(*glow))
        return SoftenedColdColor(*glow);
    return LampColor(path);
}

bool WarmHue(Rgb color)
{
    float top = std::max({color.r, color.g, color.b});
    float bottom = std::min({color.r, color.g, color.b});
    float chroma = top - bottom;
    if (top <= 1e-4f || chroma / top < kMinFlameSaturation)
        return true;
    float hue = 0.0f;
    if (top == color.r)
        hue = 60.0f * std::fmod((color.g - color.b) / chroma + 6.0f, 6.0f);
    else if (top == color.g)
        hue = 60.0f * ((color.b - color.r) / chroma + 2.0f);
    else
        hue = 60.0f * ((color.r - color.g) / chroma + 4.0f);
    return hue < kWarmHueEnd || hue >= kWarmHueStart;
}

Rgb KindColor(lampfile::LampKind kind)
{
    switch (kind)
    {
    case lampfile::LampKind::Candle:
    case lampfile::LampKind::Lamp:
    case lampfile::LampKind::Chandelier:
        return kDefaultLamp;
    default:
        return kDefaultWarm;
    }
}

Rgb SourceColor(std::string_view path, lampfile::LampKind kind, std::optional<Rgb> detected)
{
    if (std::optional<Rgb> tint = NameTint(path))
        return *tint;
    if (detected && !WarmHue(*detected))
        return SoftenedColdColor(*detected);
    return KindColor(kind);
}

float LampIntensity(std::optional<NameKind> kind)
{
    if (!kind)
        return 1.5f;
    switch (*kind)
    {
    case NameKind::Chandelier:
    case NameKind::Brazier:
        return 2.0f;
    case NameKind::Candle:
        return 0.6f;
    default:
        return 1.5f;
    }
}

lampfile::LampKind LampKindOf(std::optional<NameKind> kind)
{
    if (!kind)
        return lampfile::LampKind::Lamp;
    switch (*kind)
    {
    case NameKind::Chandelier:
        return lampfile::LampKind::Chandelier;
    case NameKind::Candle:
        return lampfile::LampKind::Candle;
    case NameKind::Brazier:
        return lampfile::LampKind::Brazier;
    case NameKind::Torch:
        return lampfile::LampKind::Torch;
    default:
        return lampfile::LampKind::Lamp;
    }
}

float FireIntensity(float strength)
{
    if (strength < 0.25f)
        return 0.6f;
    if (strength < 0.70f)
        return 1.5f;
    if (strength < 1.60f)
        return 2.0f;
    return 3.0f;
}

lampfile::LampKind KindFromIntensity(float intensity)
{
    if (intensity < 1.0f)
        return lampfile::LampKind::Candle;
    if (intensity < 1.75f)
        return lampfile::LampKind::Torch;
    if (intensity < 2.5f)
        return lampfile::LampKind::Brazier;
    return lampfile::LampKind::Bonfire;
}

float ReachFromIntensity(float intensity)
{
    if (intensity < 1.0f)
        return 6.0f;
    if (intensity < 1.75f)
        return 12.0f;
    if (intensity < 2.5f)
        return 16.0f;
    return 24.0f;
}

Rgb PeakNormalized(Rgb color, float* peak)
{
    float top = std::max({color.r, color.g, color.b});
    if (peak)
        *peak = top;
    if (top <= 1e-4f)
        return kDefaultWarm;
    return {color.r / top, color.g / top, color.b / top};
}

std::optional<Rgb> MostSaturatedKey(const std::vector<Rgb>& keys)
{
    const Rgb* best = nullptr;
    float bestSaturation = 0.0f;
    for (const Rgb& key : keys)
    {
        float saturation = std::max({key.r, key.g, key.b}) - std::min({key.r, key.g, key.b});
        if (saturation >= kMinFlameSaturation && (!best || saturation > bestSaturation))
        {
            best = &key;
            bestSaturation = saturation;
        }
    }
    if (!best)
        return std::nullopt;
    return *best;
}

std::optional<Rgb> MostVisibleKey(const std::vector<Rgb>& keys, const std::vector<float>& alphas,
                                  const std::vector<float>& sizes)
{
    const Rgb* best = nullptr;
    float bestWeight = 0.0f;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        float alpha = i < alphas.size() ? alphas[i] : 1.0f;
        float size = i < sizes.size() ? sizes[i] : 1.0f;
        float weight = std::max(alpha, 0.0f) * std::max(size, 0.0f);
        if (!best || weight > bestWeight)
        {
            best = &keys[i];
            bestWeight = weight;
        }
    }
    if (!best)
        return std::nullopt;
    return *best;
}

float KeyVisibility(const std::vector<Rgb>& keys, const std::vector<float>& alphas, const std::vector<float>& sizes)
{
    float best = 0.0f;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        float alpha = i < alphas.size() ? alphas[i] : 1.0f;
        float size = i < sizes.size() ? sizes[i] : 1.0f;
        best = std::max(best, std::max(alpha, 0.0f) * std::max(size, 0.0f));
    }
    return best;
}

Rgb SoftenedColdColor(Rgb color)
{
    Rgb peak = PeakNormalized(color, nullptr);
    float low = std::min({peak.r, peak.g, peak.b});
    if (low >= kMinColdChannel)
        return peak;
    float toWhite = (kMinColdChannel - low) / (1.0f - low);
    return {peak.r + (1.0f - peak.r) * toWhite, peak.g + (1.0f - peak.g) * toWhite,
            peak.b + (1.0f - peak.b) * toWhite};
}
