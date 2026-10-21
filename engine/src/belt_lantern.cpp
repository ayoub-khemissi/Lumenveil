#include "belt_lantern.h"

#include "fog_model.h"
#include "log.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.141592653589793;
constexpr float kSpeedSmoothingSeconds = 0.2f;
constexpr int kMaxSwingSubsteps = 48;
bool g_autoLit = false;

float SmoothStep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Length3(const float* v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void HangingStraight(float* chain)
{
    chain[0] = 0.0f;
    chain[1] = 0.0f;
    chain[2] = -kSwingYards;
}

void KeepOnTheChain(float* chain)
{
    const float length = Length3(chain);
    if (!(length > 1e-6f))
    {
        HangingStraight(chain);
        return;
    }
    for (int k = 0; k < 3; ++k)
        chain[k] *= kSwingYards / length;
    const float lowest = -kSwingYards * std::cos(kMaxSwingRadians);
    if (chain[2] <= lowest)
        return;
    const float across = std::sqrt(chain[0] * chain[0] + chain[1] * chain[1]);
    const float scale = across > 1e-6f ? kSwingYards * std::sin(kMaxSwingRadians) / across : 0.0f;
    chain[0] *= scale;
    chain[1] *= scale;
    chain[2] = lowest;
}
}

bool LanternAllowed(const FrameInputs& in)
{
    return in.playerKnown && !(in.playerMoveFlags & engine::kMovementSwimming);
}

float LanternAutoWeight(const FrameInputs& in)
{
    return std::max(LampNightWeight(in.sunElevation), std::clamp(in.indoors, 0.0f, 1.0f));
}

float LanternTarget(const FrameInputs& in, const Config& cfg, float lanternLight)
{
    if (!LanternAllowed(in) || !(lanternLight > 0.0f))
        return 0.0f;
    switch (cfg.lanternMode)
    {
    case kLanternOn:
        return 1.0f;
    case kLanternAuto:
        return LanternAutoWeight(in);
    default:
        return 0.0f;
    }
}

int LanternModeAfterKey(int mode, bool autoLit)
{
    const bool lit = mode == kLanternOn || (mode == kLanternAuto && autoLit);
    if (lit)
        return kLanternOff;
    return autoLit ? kLanternAuto : kLanternOn;
}

bool EstimatePivotHeight(const FrameInputs& in, float& height)
{
    const float toPlayer[2] = {in.playerPos[0] - in.camPos[0], in.playerPos[1] - in.camPos[1]};
    if (toPlayer[0] * toPlayer[0] + toPlayer[1] * toPlayer[1] < kFirstPersonYards * kFirstPersonYards)
    {
        height = in.camPos[2] - in.playerPos[2];
    }
    else
    {
        float look[3] = {in.camTarget[0] - in.camPos[0], in.camTarget[1] - in.camPos[1],
                         in.camTarget[2] - in.camPos[2]};
        const float length = Length3(look);
        if (!(length > 1e-4f))
            return false;
        for (float& v : look)
            v /= length;
        const float level = look[0] * look[0] + look[1] * look[1];
        if (level < kMinLevelViewShare * kMinLevelViewShare)
            return false;
        const float along = (toPlayer[0] * look[0] + toPlayer[1] * look[1]) / level;
        if (!(along > 0.0f))
            return false;
        const float miss[2] = {toPlayer[0] - look[0] * along, toPlayer[1] - look[1] * along};
        if (miss[0] * miss[0] + miss[1] * miss[1] > kPivotMissYards * kPivotMissYards)
            return false;
        height = in.camPos[2] + look[2] * along - in.playerPos[2];
    }
    return height >= kMinPivotHeight && height <= kMaxPivotHeight;
}

void LanternHook(const float* feet, float facing, float pivotHeight, float* hook)
{
    const float forward[2] = {std::cos(facing), std::sin(facing)};
    const float right[2] = {forward[1], -forward[0]};
    const float side = std::clamp(kBeltSideShareOfPivot * pivotHeight, kMinBeltSideYards, kMaxBeltSideYards);
    const float front = kBeltFrontShareOfPivot * pivotHeight;
    hook[0] = feet[0] + right[0] * side + forward[0] * front;
    hook[1] = feet[1] + right[1] * side + forward[1] * front;
    hook[2] = feet[2] + kBeltShareOfPivot * pivotHeight;
}

float GaitAmount(float speed)
{
    return SmoothStep(kGaitFromSpeed, kGaitFullAtSpeed, speed) *
           (1.0f - SmoothStep(kGaitFadeFromSpeed, kGaitGoneAtSpeed, speed));
}

float GaitStepYards(float speed)
{
    return std::clamp(kStepYardsPerSpeed * speed + kStepYardsAtRest, kMinStepYards, kMaxStepYards);
}

const char* LanternModeName(int mode)
{
    switch (mode)
    {
    case kLanternOff:
        return "off";
    case kLanternAuto:
        return "auto";
    case kLanternOn:
        return "on";
    default:
        return "?";
    }
}

namespace
{
CarrierBody BodyInView(const FrameInputs& in, float belowFeet, float radius, float height)
{
    CarrierBody body = {};
    const float up[3] = {0.0f, 0.0f, 1.0f};
    const float base[3] = {in.playerPos[0] - in.camPos[0], in.playerPos[1] - in.camPos[1],
                           in.playerPos[2] - belowFeet - in.camPos[2]};
    TransformDirection(base, in.cameraRelativeView, body.base);
    TransformDirection(up, in.cameraRelativeView, body.up);
    body.radius = radius;
    body.height = height;
    return body;
}

CarrierBody OnFootBodyInView(const FrameInputs& in, float pivot)
{
    return BodyInView(in, kBodyBelowFeetYards,
                      std::clamp(kBodyRadiusShareOfPivot * pivot, kMinBodyRadius, kMaxBodyRadius),
                      kBodyBelowFeetYards + kBodyHeightShareOfPivot * pivot);
}
}

CarrierBody CarrierBodyInView(const FrameInputs& in, const LanternLamp& lamp)
{
    if (!in.playerKnown || !lamp.placed)
        return {};
    return OnFootBodyInView(in, lamp.pivotHeight);
}

CarrierBody ShaftBodyInView(const FrameInputs& in, float pivotHeight)
{
    if (!in.playerKnown)
        return {};
    if (in.playerMountDisplay)
        return BodyInView(in, kMountedBodyBelowFeetYards, kMountedBodyRadius, kMountedBodyHeight);
    return OnFootBodyInView(in, pivotHeight > 0.0f ? pivotHeight : kDefaultPivotHeight);
}

const LanternLamp& BeltLantern::Update(const FrameInputs& in, const Config& cfg, float lanternLight)
{
    const double step = m_clockValid ? std::clamp(in.seconds - m_seconds, 0.0, kMaxLanternStepSeconds) : 0.0;
    m_seconds = in.seconds;
    m_clockValid = true;
    m_lamp.autoLit = LanternAutoWeight(in) > kLanternAutoLitShare;
    g_autoLit = m_lamp.autoLit;
    m_lamp.target = LanternTarget(in, cfg, lanternLight);
    const float fade = static_cast<float>(step) / kLanternFadeSeconds;
    m_lamp.weight = m_lamp.target > m_lamp.weight ? std::min(m_lamp.weight + fade, m_lamp.target)
                                                  : std::max(m_lamp.weight - fade, m_lamp.target);
    if (!in.playerKnown)
    {
        m_lamp.weight = 0.0f;
        m_lamp.placed = false;
        m_swingValid = false;
        return m_lamp;
    }

    float measured = 0.0f;
    if (EstimatePivotHeight(in, measured))
    {
        const float follow = 1.0f - std::exp(-static_cast<float>(step) / kPivotSmoothingSeconds);
        m_pivot = m_pivotValid ? m_pivot + (measured - m_pivot) * follow : measured;
        m_pivotValid = true;
    }
    m_lamp.pivotHeight = m_pivot;
    m_lamp.pivotMeasured = m_pivotValid;
    float hook[3];
    LanternHook(in.playerPos, in.playerFacing, m_pivot, hook);
    m_lamp.hookHeight = hook[2] - in.playerPos[2];

    const float moved[2] = {in.playerPos[0] - m_feet[0], in.playerPos[1] - m_feet[1]};
    const float distance = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1]);
    const bool continuous = m_swingValid && distance < kLanternJumpYards;
    m_feet[0] = in.playerPos[0];
    m_feet[1] = in.playerPos[1];
    const float speed = continuous && step > 0.0 ? distance / static_cast<float>(step) : 0.0f;
    const float follow = step > 0.0 ? 1.0f - std::exp(-static_cast<float>(step) / kSpeedSmoothingSeconds) : 0.0f;
    m_speed = continuous ? m_speed + (speed - m_speed) * follow : 0.0f;
    if (continuous)
        m_gaitPhase += kPi * distance / GaitStepYards(m_speed);
    const float gait = GaitAmount(m_speed);
    const float right[2] = {std::sin(in.playerFacing), -std::cos(in.playerFacing)};
    const float sway = kGaitSwayYards * gait * static_cast<float>(std::sin(m_gaitPhase));
    const float bob = kGaitBobYards * gait * static_cast<float>(std::cos(2.0 * m_gaitPhase));
    const float moving[3] = {hook[0] + right[0] * sway, hook[1] + right[1] * sway, hook[2] + bob};
    if (!continuous)
        m_swingValid = false;
    Swing(moving, std::min(step, kMaxSwingStepSeconds));

    float deviation[3];
    for (int k = 0; k < 3; ++k)
        deviation[k] = (moving[k] - hook[k] + (m_chain[k] - (k == 2 ? -kSwingYards : 0.0f)) * kChainYards / kSwingYards) *
                       cfg.lanternSwing;
    const float swung = Length3(deviation);
    if (swung > kMaxSwingYards)
        for (float& v : deviation)
            v *= kMaxSwingYards / swung;
    for (int k = 0; k < 3; ++k)
        m_lamp.position[k] = hook[k] + (k == 2 ? -kChainYards : 0.0f) + deviation[k];
    m_lamp.swingYards = std::min(swung, kMaxSwingYards);
    m_largestSwing = std::max(m_largestSwing, m_lamp.swingYards);
    m_lamp.placed = true;
    return m_lamp;
}

float BeltLantern::TakeLargestSwing()
{
    const float largest = m_largestSwing;
    m_largestSwing = 0.0f;
    return largest;
}

void BeltLantern::Reset(const float* hook)
{
    for (int k = 0; k < 3; ++k)
    {
        m_hook[k] = hook[k];
        m_hookVelocity[k] = 0.0f;
    }
    HangingStraight(m_chain);
    HangingStraight(m_chainBefore);
    m_swingValid = true;
}

void BeltLantern::Swing(const float* hook, double step)
{
    if (!m_swingValid)
    {
        Reset(hook);
        return;
    }
    if (!(step > 0.0))
    {
        for (int k = 0; k < 3; ++k)
            m_hook[k] = hook[k];
        return;
    }
    const int substeps = std::clamp(static_cast<int>(std::ceil(step / kSwingSubstepSeconds)), 1, kMaxSwingSubsteps);
    const float h = static_cast<float>(step / substeps);
    float velocity[3];
    float kick[3];
    for (int k = 0; k < 3; ++k)
    {
        velocity[k] = (hook[k] - m_hook[k]) / static_cast<float>(step);
        kick[k] = velocity[k] - m_hookVelocity[k];
    }
    const float kickSize = Length3(kick);
    if (kickSize > kMaxHookKick)
        for (float& v : kick)
            v *= kMaxHookKick / kickSize;
    for (int k = 0; k < 3; ++k)
    {
        m_hookVelocity[k] = velocity[k];
        m_hook[k] = hook[k];
        m_chainBefore[k] += kick[k] * h * std::sqrt(kSwingYards / kChainYards);
    }
    const float damping = 2.0f * kSwingDampingRatio * std::sqrt(kGravityYards / kSwingYards) * h;
    for (int i = 0; i < substeps; ++i)
    {
        float next[3];
        for (int k = 0; k < 3; ++k)
            next[k] = m_chain[k] + (m_chain[k] - m_chainBefore[k]) * (1.0f - damping);
        next[2] -= kGravityYards * h * h;
        KeepOnTheChain(next);
        for (int k = 0; k < 3; ++k)
        {
            m_chainBefore[k] = m_chain[k];
            m_chain[k] = next[k];
        }
    }
}

void PressLanternKey()
{
    ConfigStore& store = GlobalConfig();
    const int before = store.Get().lanternMode;
    const int after = LanternModeAfterKey(before, g_autoLit);
    const bool saved = store.SaveLanternMode(after);
    VF_LOG_INFO("lantern: %s pressed, %s -> %s%s", HotkeyName(store.Get().lanternKey).c_str(),
                LanternModeName(before), LanternModeName(after), saved ? ", saved to the ini" : "");
}

bool LanternAutoLitNow()
{
    return g_autoLit;
}
