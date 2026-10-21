#pragma once

#include "config.h"
#include "engine.h"

constexpr float kLanternColour[3] = {1.0f, 0.55f, 0.2f};
constexpr float kLanternIntensity = 1.0f;
constexpr float kLanternReach = 16.0f;
constexpr float kLanternHaloReach = 6.0f;
constexpr float kLanternNearYards = 0.6f;
constexpr float kLanternClearYards = 0.25f;
constexpr float kLanternSliverShadows = 1.0f;
constexpr float kLanternShadowStartYards = 0.75f;
constexpr float kLanternFadeSeconds = 0.6f;
constexpr float kLanternAutoLitShare = 0.5f;
constexpr float kDefaultPivotHeight = 1.6f;
constexpr float kMinPivotHeight = 0.5f;
constexpr float kMaxPivotHeight = 6.0f;
constexpr float kPivotMissYards = 0.35f;
constexpr float kFirstPersonYards = 0.3f;
constexpr float kMinLevelViewShare = 0.2f;
constexpr float kPivotSmoothingSeconds = 0.3f;
constexpr float kBeltShareOfPivot = 0.5f;
constexpr float kBeltSideShareOfPivot = 0.2f;
constexpr float kMinBeltSideYards = 0.15f;
constexpr float kMaxBeltSideYards = 0.6f;
constexpr float kBeltFrontShareOfPivot = 0.05f;
constexpr float kChainYards = 0.18f;
constexpr float kSwingYards = 0.5f;
constexpr float kGravityYards = 10.73f;
constexpr float kSwingDampingRatio = 0.3f;
constexpr float kMaxSwingRadians = 1.0f;
constexpr float kMaxSwingYards = 0.5f;
constexpr float kMaxHookKick = 20.0f;
constexpr double kMaxLanternStepSeconds = 0.25;
constexpr double kMaxSwingStepSeconds = 0.1;
constexpr double kSwingSubstepSeconds = 1.0 / 240.0;
constexpr float kLanternJumpYards = 5.0f;
constexpr float kGaitSwayYards = 0.02f;
constexpr float kGaitBobYards = 0.01f;
constexpr float kGaitFromSpeed = 0.3f;
constexpr float kGaitFullAtSpeed = 1.5f;
constexpr float kGaitFadeFromSpeed = 10.0f;
constexpr float kGaitGoneAtSpeed = 16.0f;
constexpr float kStepYardsPerSpeed = 0.32f;
constexpr float kStepYardsAtRest = 0.2f;
constexpr float kMinStepYards = 0.8f;
constexpr float kMaxStepYards = 2.3f;
constexpr float kBodyRadiusShareOfPivot = 0.35f;
constexpr float kMinBodyRadius = 0.45f;
constexpr float kMaxBodyRadius = 1.5f;
constexpr float kBodyHeightShareOfPivot = 1.25f;
constexpr float kBodyBelowFeetYards = 0.25f;
constexpr float kMountedBodyRadius = 4.5f;
constexpr float kMountedBodyBelowFeetYards = 1.0f;
constexpr float kMountedBodyHeight = 7.0f;

struct LanternLamp
{
    float position[3];
    float weight;
    float target;
    bool autoLit;
    bool placed;
    float pivotHeight;
    bool pivotMeasured;
    float hookHeight;
    float swingYards;
};

struct CarrierBody
{
    float base[3];
    float radius;
    float up[3];
    float height;
};

bool LanternAllowed(const FrameInputs& in);
CarrierBody CarrierBodyInView(const FrameInputs& in, const LanternLamp& lamp);
CarrierBody ShaftBodyInView(const FrameInputs& in, float pivotHeight);
float LanternAutoWeight(const FrameInputs& in);
float LanternTarget(const FrameInputs& in, const Config& cfg, float lanternLight);
int LanternModeAfterKey(int mode, bool autoLit);
bool EstimatePivotHeight(const FrameInputs& in, float& height);
void LanternHook(const float* feet, float facing, float pivotHeight, float* hook);
float GaitAmount(float speed);
float GaitStepYards(float speed);
const char* LanternModeName(int mode);

class BeltLantern
{
public:
    const LanternLamp& Update(const FrameInputs& in, const Config& cfg, float lanternLight);
    const LanternLamp& Lamp() const { return m_lamp; }
    float TakeLargestSwing();

private:
    void Reset(const float* hook);
    void Swing(const float* hook, double step);

    LanternLamp m_lamp = {};
    double m_seconds = 0.0;
    bool m_clockValid = false;
    bool m_swingValid = false;
    float m_pivot = kDefaultPivotHeight;
    bool m_pivotValid = false;
    float m_feet[2] = {};
    float m_speed = 0.0f;
    float m_hook[3] = {};
    float m_hookVelocity[3] = {};
    float m_chain[3] = {0.0f, 0.0f, -kChainYards};
    float m_chainBefore[3] = {0.0f, 0.0f, -kChainYards};
    double m_gaitPhase = 0.0;
    float m_largestSwing = 0.0f;
};

void PressLanternKey();
bool LanternAutoLitNow();
