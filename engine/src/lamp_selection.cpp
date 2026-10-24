#include "lamp_selection.h"

#include "fog_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kTau = 6.283185307179586;
constexpr double kMaxLampStepSeconds = 0.25;
constexpr float kSlowWeight = 0.42f;
constexpr float kMidWeight = 0.28f;
constexpr float kFastWeight = 0.18f;
constexpr float kNoiseWeight = 0.12f;
constexpr float kBlueWobble = 0.03f;
constexpr float kBlueRate = 0.37f;
constexpr float kFrequencyJitter = 0.24f;
constexpr float kLowestFrequencyShare = 0.85f;
constexpr float kSeedPositionScale = 16.0f;
constexpr float kMinLampViewDepth = 0.5f;
constexpr float kLampTapYards = 0.4f;
constexpr float kMinLampTapPixels = 1.5f;
constexpr float kMaxLampTapPixels = 12.0f;
constexpr float kLampDepthMargin = 0.75f;
constexpr float kLampDepthMarginPerYard = 0.02f;
constexpr float kIndoorLampRank = 0.25f;
constexpr float kSamePaneYards = 0.5f;

struct FlameProfile
{
    float amplitude;
    float low;
    float high;
};

constexpr FlameProfile kCandleFlame = {0.12f, 4.0f, 7.0f};
constexpr FlameProfile kTorchFlame = {0.10f, 3.0f, 5.0f};
constexpr FlameProfile kBrazierFlame = {0.10f, 3.0f, 5.0f};
constexpr FlameProfile kBonfireFlame = {0.10f, 2.5f, 4.5f};
constexpr FlameProfile kLanternFlame = {0.05f, 3.0f, 6.0f};
constexpr float kLanternSeedPosition[3] = {17.0f, -29.0f, 5.0f};

float FireShare(const lampfile::Lamp& lamp, const Config& cfg)
{
    const auto kind = static_cast<lampfile::LampKind>(lamp.kind);
    return kind == lampfile::LampKind::Brazier || kind == lampfile::LampKind::Bonfire ? cfg.fireStrength : 1.0f;
}

float IndoorShare(bool indoor, const FrameInputs& in, const Config& cfg)
{
    const float inside = indoor ? 1.0f : std::clamp(in.indoors, 0.0f, 1.0f);
    return 1.0f + (cfg.lampIndoors - 1.0f) * inside;
}

const FlameProfile* FlameOf(const lampfile::Lamp& lamp)
{
    switch (static_cast<lampfile::LampKind>(lamp.kind))
    {
    case lampfile::LampKind::Candle:
    case lampfile::LampKind::Chandelier:
        return &kCandleFlame;
    case lampfile::LampKind::Torch:
        return &kTorchFlame;
    case lampfile::LampKind::Brazier:
        return &kBrazierFlame;
    case lampfile::LampKind::Bonfire:
        return &kBonfireFlame;
    case lampfile::LampKind::Lamp:
        return (lamp.flags & lampfile::kLampFlame) ? &kCandleFlame : nullptr;
    default:
        return nullptr;
    }
}

uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float Unit(uint32_t x)
{
    return static_cast<float>(Mix(x) >> 8) / 16777216.0f;
}

uint32_t FlickerSeed(const float* position)
{
    auto quantised = [](float v) {
        return static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * kSeedPositionScale)));
    };
    return Mix(quantised(position[0]) ^ Mix(quantised(position[1]) ^ Mix(quantised(position[2]))));
}

float ValueNoise(double t, uint32_t seed)
{
    const double knot = std::floor(t);
    float u = static_cast<float>(t - knot);
    u = u * u * (3.0f - 2.0f * u);
    const auto k = static_cast<uint32_t>(static_cast<int64_t>(knot));
    const float a = Unit(seed ^ Mix(k)) * 2.0f - 1.0f;
    const float b = Unit(seed ^ Mix(k + 1u)) * 2.0f - 1.0f;
    return a + (b - a) * u;
}

float SearchEdgeFade(float yards, float start, float end)
{
    const float t = std::clamp((yards - start) / (end - start), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

bool SphereInView(const float* view, float radius, const float* proj)
{
    if (view[2] < -radius)
        return false;
    const float planes[4][3] = {
        {proj[0], 0.0f, proj[8] - 1.0f},
        {-proj[0], 0.0f, -proj[8] - 1.0f},
        {0.0f, proj[5], proj[9] - 1.0f},
        {0.0f, -proj[5], -proj[9] - 1.0f},
    };
    for (const float* n : planes)
    {
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if ((n[0] * view[0] + n[1] * view[1] + n[2] * view[2]) > radius * length)
            return false;
    }
    return true;
}

float LampImportance(float intensity, bool indoor, float distanceSquared)
{
    const float share = indoor ? kIndoorLampRank : 1.0f;
    return intensity * share * PointLightFalloff(std::max(std::sqrt(distanceSquared), kMinRankYards));
}

void WindowLightPosition(const lampfile::Window& window, float* out)
{
    for (int k = 0; k < 3; ++k)
        out[k] = window.center[k] + window.normal[k] * kWindowOutsetYards;
}

float DistanceSquared(const float* a, const float* b)
{
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}

LampFlickerValue FlameFlicker(const FlameProfile& flame, const float* seedPosition, double seconds, float gain)
{
    const uint32_t seed = FlickerSeed(seedPosition);
    const float span = flame.high - flame.low;
    auto frequency = [&](uint32_t k, float share) {
        const float jitter = (Unit(seed ^ Mix(0x9E3779B9u * (k + 1u))) - 0.5f) * kFrequencyJitter;
        return std::clamp(flame.low + span * (share + jitter), flame.low * kLowestFrequencyShare, flame.high);
    };
    auto phase = [&](uint32_t k) { return Unit(Mix((seed + 0x85EBCA6Bu) * (k + 3u))); };
    auto wave = [&](uint32_t k, float share) {
        return static_cast<float>(std::sin(kTau * (frequency(k, share) * seconds + phase(k))));
    };
    const float w = kSlowWeight * wave(0, 0.0f) + kMidWeight * wave(1, 0.5f) + kFastWeight * wave(2, 1.0f) +
                    kNoiseWeight * ValueNoise(seconds * flame.low, seed ^ 0x12345678u);
    const float blue = static_cast<float>(std::sin(kTau * (flame.low * kBlueRate * seconds + phase(7))));
    return {std::max(1.0f + flame.amplitude * gain * w, 0.0f), std::max(1.0f + kBlueWobble * gain * blue, 0.0f)};
}
}

float CarriedShadowStrength(const Config& cfg)
{
    return std::clamp(cfg.lampShadows * cfg.carriedShadows, 0.0f, 1.0f);
}

float PointLightFalloff(float yards)
{
    return 1.0f / (kPointFalloffPerYard * yards + kPointFalloffPerSquareYard * yards * yards);
}

float LampSurfaceReach(float reach)
{
    return std::min(kSurfaceReachPerLampReach * std::clamp(reach, kMinLampReach, kMaxLampReach), kMaxLampReach);
}

float LampDistanceFade(float yards)
{
    return SearchEdgeFade(yards, kLampFadeStartYards, kLampSearchYards);
}

float WindowDistanceFade(float yards)
{
    return SearchEdgeFade(yards, kWindowFadeStartYards, kWindowSearchYards);
}

float FarHaloRank(float intensity, float reach, float distanceSquared)
{
    return intensity * std::clamp(reach, kMinLampReach, kMaxLampReach) /
           std::max(std::sqrt(distanceSquared), kMinRankYards);
}

float FarCoreShare(float yards)
{
    const float from = std::clamp((yards - kFarCoreFromYards) / (kFarCoreFullYards - kFarCoreFromYards), 0.0f, 1.0f);
    return from * from * (3.0f - 2.0f * from) * std::sqrt(kFarCoreFullYards / std::max(yards, kFarCoreFullYards));
}

float FarCoreAnglePerTexel(const FrameInputs& in, int lowHeight)
{
    const float cotHalfFov = std::fabs(in.glProjection[5]);
    return cotHalfFov > 0.0f && lowHeight > 0 ? 2.0f / (cotHalfFov * static_cast<float>(lowHeight)) : 0.0f;
}

float WindowIntensity(const lampfile::Window& window)
{
    return std::clamp(window.area * kWindowLightPerSquareYard, kMinWindowLight, kMaxWindowLight);
}

float WindowReach(const lampfile::Window& window)
{
    return std::min(kWindowReachYards + kWindowReachPerYard * std::sqrt(std::max(window.area, 0.0f)),
                    kMaxWindowReach);
}

float WindowHaloReach(const lampfile::Window& window)
{
    return std::min(kWindowHaloReachYards + kWindowHaloReachPerYard * std::sqrt(std::max(window.area, 0.0f)),
                    kMaxWindowHaloReach);
}

float WindowNearYards(const lampfile::Window& window)
{
    const float* u = window.halfWidth;
    return std::max(kMinWindowNearYards,
                    std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2] + window.halfHeight * window.halfHeight));
}

bool WindowGivesLight(const lampfile::Window& window)
{
    return !(window.flags & lampfile::kWindowFacesUp) &&
           std::max({window.color[0], window.color[1], window.color[2]}) > kMinWindowColour;
}

bool LampFlickers(const lampfile::Lamp& lamp)
{
    return FlameOf(lamp) != nullptr;
}

LampFlickerValue LampFlicker(const lampfile::Lamp& lamp, double seconds, float gain)
{
    const FlameProfile* flame = FlameOf(lamp);
    if (!flame || gain <= 0.0f)
        return {1.0f, 1.0f};
    return FlameFlicker(*flame, lamp.position, seconds, gain);
}

LampFlickerValue LanternFlicker(double seconds, float gain)
{
    if (gain <= 0.0f)
        return {1.0f, 1.0f};
    return FlameFlicker(kLanternFlame, kLanternSeedPosition, seconds, gain);
}

LampSelection::LampSelection()
{
    Reset();
}

void LampSelection::Reset()
{
    for (Slot& slot : m_slots)
        slot = Slot{};
    m_table = {};
}

void LampSelection::ForgetVisibility()
{
    for (Slot& slot : m_slots)
        slot.fresh = true;
}

float LampSelection::SlotFade(uint32_t lamp) const
{
    for (const Slot& slot : m_slots)
        if (slot.lamp == lamp)
            return slot.fade;
    return 0.0f;
}

int LampSelection::EffectSlot(uint32_t effect) const
{
    const uint32_t id = kEffectBit | (effect & kEffectIdMask);
    for (int k = 0; k < kLampSlots; ++k)
        if (m_slots[k].lamp == id)
            return k;
    return -1;
}

bool LampSelection::Wanted(uint32_t lamp, size_t count) const
{
    for (size_t i = 0; i < count; ++i)
        if (m_candidates[i].lamp == lamp)
            return true;
    return false;
}

void LampSelection::WriteSlot(int slot, const lampfile::Lamp& lamp, const FrameInputs& in, const Config& cfg,
                              float haze, float surfaceLight, float lanternGlow, bool haloOnly)
{
    Slot& s = m_slots[slot];
    float relative[3];
    for (int k = 0; k < 3; ++k)
        relative[k] = lamp.position[k] - in.camPos[k];
    const float cameraYards =
        std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
    const bool indoor = (lamp.flags & lampfile::kLampIndoor) != 0;
    const LampFlickerValue flicker = LampFlicker(lamp, in.seconds, cfg.lampFlicker);
    const float burning =
        lamp.intensity * flicker.intensity * s.fade * FireShare(lamp, cfg) * IndoorShare(indoor, in, cfg);
    const float scale = burning * haze * LampNearCameraShare(cameraYards) * LampDistanceFade(cameraYards) *
                        (indoor ? kIndoorLampHaze : 1.0f);
    const float reach = std::clamp(lamp.reach, kMinLampReach, kMaxLampReach);
    float* sphere = m_table.texels[kLampSphereRow][slot];
    float* light = m_table.texels[kLampLightRow][slot];
    std::memcpy(sphere, relative, sizeof(relative));
    sphere[3] = reach;
    light[0] = lamp.color[0] * scale;
    light[1] = lamp.color[1] * scale;
    light[2] = lamp.color[2] * scale * flicker.blue;
    light[3] = indoor ? kHiddenIndoorLampShare : kHiddenOutdoorLampShare;

    const float glowing = (lamp.flags & lampfile::kLampWmoLantern) ? lanternGlow : 1.0f;
    const float onSurfaces = haloOnly ? 0.0f
                                      : burning * surfaceLight * glowing * (indoor ? kIndoorLampLight : 1.0f) *
                                            LampDistanceFade(cameraYards);
    float* surface = m_table.texels[kLampSurfaceRow][slot];
    surface[0] = lamp.color[0] * onSurfaces;
    surface[1] = lamp.color[1] * onSurfaces;
    surface[2] = lamp.color[2] * onSurfaces * flicker.blue;
    surface[3] = cfg.lampShadows;

    float view[3];
    TransformDirection(relative, in.cameraRelativeView, view);
    float* viewRow = m_table.texels[kLampViewRow][slot];
    std::memcpy(viewRow, view, sizeof(view));
    viewRow[3] = LampSurfaceReach(lamp.reach);
    std::memset(m_table.texels[kLampEmitRow][slot], 0, sizeof(m_table.texels[kLampEmitRow][slot]));
    const float core = indoor ? 0.0f
                              : lamp.intensity * flicker.intensity * s.fade * FireShare(lamp, cfg) * m_farCore *
                                    kFarCoreGain * FarCoreShare(cameraYards) * LampDistanceFade(cameraYards);
    float* coreRow = m_table.texels[kLampCoreRow][slot];
    coreRow[0] = lamp.color[0] * core;
    coreRow[1] = lamp.color[1] * core;
    coreRow[2] = lamp.color[2] * core * flicker.blue;
    coreRow[3] = 0.0f;
    m_stats.cores += core > 0.0f ? 1 : 0;
    WriteScreenRows(slot, view, in);
}

void LampSelection::WriteScreenRows(int slot, const float* view, const FrameInputs& in)
{
    Slot& s = m_slots[slot];
    const float* proj = in.glProjection;
    const D3DVIEWPORT9& vp = in.viewport;
    bool onScreen = view[2] > kMinLampViewDepth;
    float pixel[2] = {};
    float tapPixels = kMinLampTapPixels;
    if (onScreen)
    {
        const float ndcX = view[0] / view[2] * proj[0] + proj[8];
        const float ndcY = view[1] / view[2] * proj[5] + proj[9];
        pixel[0] = vp.X + (ndcX * 0.5f + 0.5f) * vp.Width;
        pixel[1] = vp.Y + (0.5f - ndcY * 0.5f) * vp.Height;
        onScreen = pixel[0] >= vp.X && pixel[0] < vp.X + vp.Width && pixel[1] >= vp.Y && pixel[1] < vp.Y + vp.Height;
        tapPixels = std::clamp(kLampTapYards / view[2] * proj[0] * 0.5f * vp.Width, kMinLampTapPixels,
                               kMaxLampTapPixels);
    }
    float* screen = m_table.texels[kLampScreenRow][slot];
    float* test = m_table.texels[kLampTestRow][slot];
    screen[0] = pixel[0];
    screen[1] = pixel[1];
    screen[2] = view[2];
    screen[3] = tapPixels;
    test[0] = s.fresh ? 0.0f : 1.0f;
    test[1] = onScreen ? 1.0f : 0.0f;
    test[2] = std::max(kLampDepthMargin, kLampDepthMarginPerYard * view[2]);
    test[3] = 0.0f;
    s.fresh = false;
}

size_t LampSelection::PickLamps(const LampMap* map, const std::vector<ObjectLamp>* objects, const FrameInputs& in,
                                size_t wanted, size_t wantedFar, bool lanterns)
{
    m_candidates.clear();
    if (wanted == 0)
        return 0;
    const float searchSquared = kLampSearchYards * kLampSearchYards;
    const auto consider = [&](uint32_t id, const lampfile::Lamp& lamp) {
        if ((lamp.flags & lampfile::kLampWmoFixture) || (!lanterns && (lamp.flags & lampfile::kLampWmoLantern)))
            return;
        float relative[3];
        for (int k = 0; k < 3; ++k)
            relative[k] = lamp.position[k] - in.camPos[k];
        const float distanceSquared = relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2];
        if (distanceSquared > searchSquared)
            return;
        ++m_stats.nearby;
        float view[3];
        TransformDirection(relative, in.cameraRelativeView, view);
        if (SphereInView(view, LampSurfaceReach(lamp.reach), in.glProjection))
            m_candidates.push_back(
                {LampImportance(lamp.intensity, (lamp.flags & lampfile::kLampIndoor) != 0, distanceSquared),
                 distanceSquared, id, &lamp});
    };
    if (map)
        ForEachLampNear(*map, in.camPos[0], in.camPos[1], kLampSearchYards, consider);
    if (objects)
        for (const ObjectLamp& object : *objects)
            consider(kObjectBit | (object.id & kObjectLampIdMask), object.lamp);
    m_stats.inView = static_cast<int>(m_candidates.size());
    const auto nearest = std::min_element(m_candidates.begin(), m_candidates.end(),
                                          [](const Candidate& a, const Candidate& b) {
                                              return a.distanceSquared < b.distanceSquared;
                                          });
    if (nearest != m_candidates.end())
    {
        m_stats.nearestYards = std::sqrt(nearest->distanceSquared);
        m_stats.nearestName = nearest->record->name;
    }
    const auto ranked = [](const Candidate& a, const Candidate& b) {
        return a.importance != b.importance ? a.importance > b.importance : a.lamp < b.lamp;
    };
    const size_t kept = std::min(wanted, m_candidates.size());
    std::partial_sort(m_candidates.begin(), m_candidates.begin() + kept, m_candidates.end(), ranked);
    const float farSquared = kFarHaloMinYards * kFarHaloMinYards;
    const auto farBegin = m_candidates.begin() + kept;
    const auto farEnd = std::partition(farBegin, m_candidates.end(),
                                       [&](const Candidate& c) { return c.distanceSquared >= farSquared; });
    for (auto c = farBegin; c != farEnd; ++c)
        c->importance = FarHaloRank(c->record->intensity, c->record->reach, c->distanceSquared);
    const size_t farKept = std::min(wantedFar, static_cast<size_t>(farEnd - farBegin));
    std::partial_sort(farBegin, farBegin + farKept, farEnd, ranked);
    for (size_t i = kept; i < kept + farKept; ++i)
        m_candidates[i].lamp |= kHaloOnlyBit;
    m_candidates.resize(kept + farKept);
    return kept + farKept;
}

size_t LampSelection::PickWindows(const LampMap& map, const FrameInputs& in, size_t wanted)
{
    m_windowCandidates.clear();
    if (wanted == 0)
        return 0;
    const float searchSquared = kWindowSearchYards * kWindowSearchYards;
    ForEachWindowNear(map, in.camPos[0], in.camPos[1], kWindowSearchYards,
                      [&](uint32_t index, const lampfile::Window& window) {
                          if (!WindowGivesLight(window))
                              return;
                          float position[3];
                          WindowLightPosition(window, position);
                          const float distanceSquared = DistanceSquared(position, in.camPos);
                          if (distanceSquared > searchSquared)
                              return;
                          ++m_stats.windowsNearby;
                          float relative[3];
                          for (int k = 0; k < 3; ++k)
                              relative[k] = position[k] - in.camPos[k];
                          float view[3];
                          TransformDirection(relative, in.cameraRelativeView, view);
                          const float reach = WindowReach(window);
                          if (SphereInView(view, reach, in.glProjection))
                              m_windowCandidates.push_back(
                                  {LampImportance(WindowIntensity(window), false, distanceSquared),
                                   distanceSquared, index});
                      });
    m_stats.windowsInView = static_cast<int>(m_windowCandidates.size());
    std::sort(m_windowCandidates.begin(), m_windowCandidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.importance != b.importance ? a.importance > b.importance : a.lamp < b.lamp;
    });
    size_t kept = 0;
    for (size_t i = 0; i < m_windowCandidates.size() && kept < wanted; ++i)
    {
        const float* center = map.windows[m_windowCandidates[i].lamp].center;
        bool samePane = false;
        for (size_t j = 0; j < kept && !samePane; ++j)
            samePane = DistanceSquared(center, map.windows[m_windowCandidates[j].lamp].center) <
                       kSamePaneYards * kSamePaneYards;
        if (!samePane)
            m_windowCandidates[kept++] = m_windowCandidates[i];
    }
    m_windowCandidates.resize(kept);
    return kept;
}

const LampTable& LampSelection::Update(const LampCatalog& catalog, const FrameInputs& in, const Config& cfg,
                                           float haze, float light, float windowLight, float lanternGlow,
                                           const LanternLamp* lantern, float lanternLight,
                                           const std::vector<EffectLamp>* effects, float effectLight, float farCore,
                                           const std::vector<ObjectLamp>* objects)
{
    const double step = m_clockValid ? std::clamp(in.seconds - m_seconds, 0.0, kMaxLampStepSeconds) : 0.0;
    m_seconds = in.seconds;
    m_clockValid = true;
    m_visibilityBlend = 1.0f - std::exp(-static_cast<float>(step) / kLampVisibilitySeconds);
    m_stats = {};
    if (catalog.Version() != m_catalogVersion || in.mapId != m_mapId)
    {
        Reset();
        m_catalogVersion = catalog.Version();
        m_mapId = in.mapId;
    }
    const LampMap* map = catalog.Map(in.mapId);
    const bool lampsHere = map || (objects && !objects->empty());
    const bool carried = lantern && lantern->placed && lantern->weight > 0.0f && lanternLight > 0.0f;
    const size_t wantedEffects =
        effects && effectLight > 0.0f ? std::min(effects->size(), static_cast<size_t>(kEffectSlots)) : 0;
    const size_t wantedLamps =
        lampsHere && (haze > 0.0f || light > 0.0f || farCore > 0.0f)
            ? static_cast<size_t>(std::clamp(cfg.lampCount, 0, kMaxLampCount))
            : 0;
    const size_t wantedWindows = map && windowLight > 0.0f ? static_cast<size_t>(kWindowSlots) : 0;
    m_farCore = farCore;
    const size_t wantedFar = lampsHere && (haze > 0.0f || farCore > 0.0f) ? static_cast<size_t>(kFarHaloSlots) : 0;
    if (wantedLamps + wantedWindows + wantedEffects + wantedFar == 0 && !carried)
    {
        Reset();
        return m_table;
    }

    m_candidates.clear();
    m_windowCandidates.clear();
    if (lampsHere)
        PickLamps(map, objects, in, wantedLamps, wantedFar, lanternGlow > 0.0f);
    if (map)
        PickWindows(*map, in, wantedWindows);
    for (const Candidate& window : m_windowCandidates)
        m_candidates.push_back({window.importance, window.distanceSquared, window.lamp | kWindowBit});
    for (size_t i = 0; i < wantedEffects; ++i)
        m_candidates.push_back({(*effects)[i].intensity, 0.0f, kEffectBit | ((*effects)[i].id & kEffectIdMask)});
    if (carried)
        m_candidates.push_back({0.0f, 0.0f, kLanternSlotId});
    const size_t kept = m_candidates.size();

    const float fadeStep = static_cast<float>(step) / kLampFadeSeconds;
    for (Slot& slot : m_slots)
    {
        if (slot.lamp == kNoLamp)
            continue;
        const bool keep = Wanted(slot.lamp, kept);
        slot.fade = keep ? std::min(slot.fade + fadeStep, 1.0f) : slot.fade - fadeStep;
        if (!keep && slot.fade <= 0.0f)
            slot = Slot{};
    }
    for (size_t i = 0; i < kept; ++i)
    {
        const uint32_t lamp = m_candidates[i].lamp;
        Slot* placed = nullptr;
        for (Slot& slot : m_slots)
            placed = slot.lamp == lamp ? &slot : placed;
        const bool farHalo = (lamp & kSlotKindMask) == kHaloOnlyBit;
        for (int k = farHalo ? kSurfaceSlots : 0; k < (farHalo ? kLampSlots : kSurfaceSlots) && !placed; ++k)
            if (m_slots[k].lamp == kNoLamp)
            {
                m_slots[k] = Slot{lamp, fadeStep, true};
                placed = &m_slots[k];
            }
        if (placed && ObjectSlot(lamp) && m_candidates[i].record)
            placed->object = *m_candidates[i].record;
    }

    m_table.slots = 0;
    m_table.surfaceSlots = 0;
    for (int k = 0; k < kLampSlots; ++k)
    {
        const Slot& slot = m_slots[k];
        const bool effect = slot.lamp != kNoLamp && (slot.lamp & kSlotKindMask) == kEffectBit;
        const bool object = slot.lamp != kNoLamp && ObjectSlot(slot.lamp);
        if (slot.lamp == kNoLamp || (!map && slot.lamp != kLanternSlotId && !effect && !object))
        {
            for (auto& row : m_table.texels)
                std::memset(row[k], 0, sizeof(row[k]));
            continue;
        }
        std::memset(m_table.texels[kLampCoreRow][k], 0, sizeof(m_table.texels[kLampCoreRow][k]));
        if (slot.lamp == kLanternSlotId)
        {
            static const LanternLamp kUnlit = {};
            WriteLanternSlot(k, lantern && lantern->placed ? *lantern : kUnlit, in, cfg, haze, lanternLight);
            m_stats.lanternDrawn = true;
        }
        else if (effect)
        {
            const EffectLamp* lit = nullptr;
            for (size_t i = 0; effects && i < effects->size() && !lit; ++i)
                if (((*effects)[i].id & kEffectIdMask) == (slot.lamp & kEffectIdMask))
                    lit = &(*effects)[i];
            WriteEffectSlot(k, lit, in, cfg, effectLight);
            m_stats.effectsDrawn += lit ? 1 : 0;
        }
        else if (slot.lamp & kWindowBit)
        {
            WriteWindowSlot(k, map->windows[slot.lamp & ~kWindowBit], in, cfg, windowLight, haze);
            ++m_stats.windowsDrawn;
        }
        else
        {
            const bool haloOnly = (slot.lamp & kHaloOnlyBit) != 0;
            WriteSlot(k, object ? slot.object : map->lamps[slot.lamp & ~kHaloOnlyBit], in, cfg, haze, light,
                      lanternGlow, haloOnly);
            ++(haloOnly ? m_stats.farDrawn : m_stats.drawn);
        }
        m_table.slots = k + 1;
        if (k < kSurfaceSlots)
            m_table.surfaceSlots = k + 1;
    }
    return m_table;
}

void LampSelection::WriteWindowSlot(int slot, const lampfile::Window& window, const FrameInputs& in,
                                    const Config& cfg, float windowLight, float haze)
{
    Slot& s = m_slots[slot];
    float position[3];
    WindowLightPosition(window, position);
    float relative[3];
    for (int k = 0; k < 3; ++k)
        relative[k] = position[k] - in.camPos[k];
    const float cameraYards =
        std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
    const float reach = WindowReach(window);
    float* sphere = m_table.texels[kLampSphereRow][slot];
    std::memcpy(sphere, relative, sizeof(relative));
    sphere[3] = WindowHaloReach(window);

    const float peak = std::max({window.color[0], window.color[1], window.color[2]});
    const float scale = WindowIntensity(window) * s.fade * windowLight / peak * WindowDistanceFade(cameraYards);
    float* surface = m_table.texels[kLampSurfaceRow][slot];
    for (int k = 0; k < 3; ++k)
        surface[k] = window.color[k] * scale;
    surface[3] = cfg.lampShadows;

    const float glow =
        scale * haze * kWindowHaloShare * LampNearCameraShare(cameraYards);
    float* light = m_table.texels[kLampLightRow][slot];
    for (int k = 0; k < 3; ++k)
        light[k] = window.color[k] * glow;
    light[3] = 0.0f;

    float* viewRow = m_table.texels[kLampViewRow][slot];
    TransformDirection(relative, in.cameraRelativeView, viewRow);
    viewRow[3] = reach;
    float* emit = m_table.texels[kLampEmitRow][slot];
    TransformDirection(window.normal, in.cameraRelativeView, emit);
    emit[3] = WindowNearYards(window);
    WriteScreenRows(slot, viewRow, in);
}

void LampSelection::WriteEffectSlot(int slot, const EffectLamp* effect, const FrameInputs& in, const Config& cfg,
                                    float effectLight)
{
    Slot& s = m_slots[slot];
    s.fresh = false;
    for (auto& row : m_table.texels)
        std::memset(row[slot], 0, sizeof(row[slot]));
    if (!effect)
        return;
    float relative[3];
    for (int k = 0; k < 3; ++k)
        relative[k] = effect->position[k] - in.camPos[k];
    float* sphere = m_table.texels[kLampSphereRow][slot];
    std::memcpy(sphere, relative, sizeof(relative));
    sphere[3] = effect->surfaceReach;
    const float onSurfaces = effect->intensity * effectLight;
    float* surface = m_table.texels[kLampSurfaceRow][slot];
    for (int k = 0; k < 3; ++k)
        surface[k] = effect->colour[k] * onSurfaces;
    surface[3] = CarriedShadowStrength(cfg);
    float* viewRow = m_table.texels[kLampViewRow][slot];
    TransformDirection(relative, in.cameraRelativeView, viewRow);
    viewRow[3] = effect->surfaceReach;
    float* emit = m_table.texels[kLampEmitRow][slot];
    emit[0] = kEffectClearYards;
    emit[2] = kEffectShadowStartYards;
    emit[3] = -effect->nearYards;
}

void LampSelection::WriteLanternSlot(int slot, const LanternLamp& lantern, const FrameInputs& in, const Config& cfg,
                                     float haze, float lanternLight)
{
    Slot& s = m_slots[slot];
    float relative[3];
    for (int k = 0; k < 3; ++k)
        relative[k] = lantern.position[k] - in.camPos[k];
    const float cameraYards =
        std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
    const float indoors = std::clamp(in.indoors, 0.0f, 1.0f);
    const LampFlickerValue flicker = LanternFlicker(in.seconds, cfg.lampFlicker);
    const float burning = kLanternIntensity * lantern.weight * s.fade * flicker.intensity;
    const float halo = burning * haze * cfg.lanternHalo * LampNearCameraShare(cameraYards);
    float* sphere = m_table.texels[kLampSphereRow][slot];
    std::memcpy(sphere, relative, sizeof(relative));
    sphere[3] = kLanternHaloReach;
    float* light = m_table.texels[kLampLightRow][slot];
    light[0] = kLanternColour[0] * halo;
    light[1] = kLanternColour[1] * halo;
    light[2] = kLanternColour[2] * halo * flicker.blue;
    light[3] = kHiddenOutdoorLampShare + (kHiddenIndoorLampShare - kHiddenOutdoorLampShare) * indoors;

    const float onSurfaces = burning * lanternLight;
    float* surface = m_table.texels[kLampSurfaceRow][slot];
    surface[0] = kLanternColour[0] * onSurfaces;
    surface[1] = kLanternColour[1] * onSurfaces;
    surface[2] = kLanternColour[2] * onSurfaces * flicker.blue;
    surface[3] = CarriedShadowStrength(cfg);

    float view[3];
    TransformDirection(relative, in.cameraRelativeView, view);
    float* viewRow = m_table.texels[kLampViewRow][slot];
    std::memcpy(viewRow, view, sizeof(view));
    viewRow[3] = kLanternReach;
    float* emit = m_table.texels[kLampEmitRow][slot];
    emit[0] = kLanternClearYards;
    emit[1] = kLanternSliverShadows;
    emit[2] = kLanternShadowStartYards;
    emit[3] = -kLanternNearYards;
    WriteScreenRows(slot, view, in);
}
