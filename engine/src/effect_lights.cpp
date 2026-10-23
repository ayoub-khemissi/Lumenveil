#include "effect_lights.h"

#include "fog_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kLuminance[3] = {0.2126f, 0.7152f, 0.0722f};

struct CellSums
{
    float colour[3];
    float energy;
    float energyPerDepth;
    float energyU;
    float energyV;
    float pixels;
    float nearestInverseDepth;
    float spriteInverseDepth;
};

struct Cell
{
    CellSums sums;
    float u;
    float v;
    float depth;
    float nearest;
    bool open;
};

float NearestDepth(const CellSums& s, float depth)
{
    return s.nearestInverseDepth > 0.0f ? std::min(1.0f / s.nearestInverseDepth, depth) : depth;
}

bool SeenAsSprite(float nearest, float depth)
{
    return nearest < kEffectSpriteDepthShare * depth;
}

CellSums ReadCell(const EffectMeasurement& m, int x, int y)
{
    const size_t stride = static_cast<size_t>(2 * m.columns) * 4;
    const float* light = &m.texels[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4];
    const float* place = &m.texels[static_cast<size_t>(y) * stride + static_cast<size_t>(m.columns + x) * 4];
    CellSums s = {};
    for (int k = 0; k < 3; ++k)
        s.colour[k] = light[k] * kEffectPixelsPerCell;
    s.energy = EffectLuminance(s.colour);
    s.energyPerDepth = light[3] * kEffectPixelsPerCell;
    s.energyU = place[0] * kEffectPixelsPerCell;
    s.energyV = place[1] * kEffectPixelsPerCell;
    s.pixels = place[2] * kEffectPixelsPerCell;
    s.nearestInverseDepth = place[3];
    return s;
}

void Accumulate(CellSums& into, const CellSums& from)
{
    for (int k = 0; k < 3; ++k)
        into.colour[k] += from.colour[k];
    into.energy += from.energy;
    into.energyPerDepth += from.energyPerDepth;
    into.energyU += from.energyU;
    into.energyV += from.energyV;
    into.pixels += from.pixels;
    into.nearestInverseDepth = std::max(into.nearestInverseDepth, from.nearestInverseDepth);
    into.spriteInverseDepth = std::max(into.spriteInverseDepth, from.spriteInverseDepth);
}

float Saturation(const float* rgb)
{
    const float peak = std::max({rgb[0], rgb[1], rgb[2]});
    return peak > 0.0f ? 1.0f - std::max(std::min({rgb[0], rgb[1], rgb[2]}), 0.0f) / peak : 0.0f;
}

float Blend(float from, float to, float share)
{
    return from + (to - from) * share;
}

float Approach(float seconds, float step)
{
    return seconds > 0.0f ? 1.0f - std::exp(-step / seconds) : 1.0f;
}

float Hotness(float brightening)
{
    const float t = std::clamp((brightening - kEffectHotFrom) / (kEffectHotFull - kEffectHotFrom), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct ProjectedLamp
{
    float u;
    float v;
    float radiusU;
    float radiusV;
    float depth;
};

bool ProjectLamp(const EffectFrame& f, const lampfile::Lamp& lamp, ProjectedLamp& out)
{
    const float relative[3] = {lamp.position[0] - f.camPos[0], lamp.position[1] - f.camPos[1],
                               lamp.position[2] - f.camPos[2]};
    float view[3];
    TransformDirection(relative, f.cameraRelativeView, view);
    if (view[2] < kEffectNearestYards)
        return false;
    const float* p = f.glProjection;
    const float ndcX = view[0] / view[2] * p[0] + p[8];
    const float ndcY = view[1] / view[2] * p[5] + p[9];
    const float flame = kEffectLampFlameYards + kEffectLampFlamePerReach * lamp.reach;
    out.u = ndcX * 0.5f + 0.5f;
    out.v = 0.5f - ndcY * 0.5f;
    out.radiusU = flame * p[0] * 0.5f / view[2];
    out.radiusV = flame * p[5] * 0.5f / view[2];
    out.depth = view[2];
    return true;
}

void SuppressLampFlames(const EffectMeasurement& m, const LampMap& lamps, std::vector<Cell>& cells, int& flames,
                        int& glows)
{
    const EffectFrame& f = m.frame;
    const float search = kEffectFarthestYards + kMaxEffectReach;
    ForEachLampNear(lamps, f.camPos[0], f.camPos[1], search, [&](uint32_t index, const lampfile::Lamp& lamp) {
        ProjectedLamp p;
        if (!ProjectLamp(f, lamp, p) || p.depth > search)
            return;
        const bool beam = index < lamps.beams.size() && lamps.beams[index] != 0;
        const float glowYards = beam ? kEffectBeamGlowYards : kEffectLampGlowYards;
        const float glowU = std::max(p.radiusU, glowYards * f.glProjection[0] * 0.5f / p.depth);
        const float glowV = std::max(p.radiusV, glowYards * f.glProjection[5] * 0.5f / p.depth);
        const float cellsPerU = f.viewportWidth / kEffectCellPixels;
        const float cellsPerV = f.viewportHeight / kEffectCellPixels;
        const int x0 = std::max(0, static_cast<int>(std::floor((p.u - glowU) * cellsPerU)));
        const int x1 = std::min(m.columns - 1, static_cast<int>(std::floor((p.u + glowU) * cellsPerU)));
        const int y0 = std::max(0, static_cast<int>(std::floor((p.v - glowV) * cellsPerV)));
        const int y1 = std::min(m.rows - 1, static_cast<int>(std::floor((p.v + glowV) * cellsPerV)));
        const float nearest = p.depth - std::max(kEffectLampDepthYards, kEffectLampDepthShare * p.depth);
        const float behind = beam ? p.depth - kEffectBeamGlowYards : p.depth + kEffectLampGlowBehindYards;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                Cell& c = cells[static_cast<size_t>(y) * m.columns + x];
                if (!c.open)
                    continue;
                const float du = (c.u - p.u) / std::max(p.radiusU, 1e-6f);
                const float dv = (c.v - p.v) / std::max(p.radiusV, 1e-6f);
                const float gu = (c.u - p.u) / std::max(glowU, 1e-6f);
                const float gv = (c.v - p.v) / std::max(glowV, 1e-6f);
                if (du * du + dv * dv <= 1.0f && c.depth >= nearest)
                {
                    c.open = false;
                    ++flames;
                }
                else if (gu * gu + gv * gv <= 1.0f && c.depth >= behind)
                {
                    c.open = false;
                    ++glows;
                }
            }
    });
}

int SuppressTargetRing(const EffectMeasurement& m, std::vector<Cell>& cells)
{
    const EffectFrame& f = m.frame;
    float inverse[16];
    if (!f.targetKnown || !(f.targetRingYards > 0.0f) || !Invert4x4(f.cameraRelativeView, inverse))
        return 0;
    const float* p = f.glProjection;
    int suppressed = 0;
    for (Cell& c : cells)
    {
        if (!c.open)
            continue;
        const float view[3] = {(c.u * 2.0f - 1.0f - p[8]) / p[0] * c.depth, (1.0f - c.v * 2.0f - p[9]) / p[5] * c.depth,
                               c.depth};
        float relative[3];
        TransformDirection(view, inverse, relative);
        const float dx = f.camPos[0] + relative[0] - f.targetFeet[0];
        const float dy = f.camPos[1] + relative[1] - f.targetFeet[1];
        const float dz = f.camPos[2] + relative[2] - f.targetFeet[2];
        const float across = std::sqrt(dx * dx + dy * dy);
        const float slope = kEffectRingSlope * across;
        if (across <= f.targetRingYards && dz >= -(kEffectRingBelowYards + slope) && dz <= kEffectRingAboveYards + slope)
        {
            c.open = false;
            ++suppressed;
        }
    }
    return suppressed;
}

bool ViewToWorld(const EffectFrame& f, const float* view, float* world)
{
    float inverse[16];
    if (!Invert4x4(f.cameraRelativeView, inverse))
        return false;
    float relative[3];
    TransformDirection(view, inverse, relative);
    for (int k = 0; k < 3; ++k)
        world[k] = f.camPos[k] + relative[k];
    return true;
}

float Dot(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool AnchorOnRay(const EffectAnchor& a, const float* from, const float* ray, float nearest, float farthest,
                 float& along, float& share)
{
    float axis[3];
    float start[3];
    for (int k = 0; k < 3; ++k)
    {
        axis[k] = a.to[k] - a.from[k];
        start[k] = from[k] - a.from[k];
    }
    const float b = Dot(ray, axis);
    const float c = Dot(axis, axis);
    const float d = Dot(ray, start);
    const float e = Dot(axis, start);
    float u = 0.0f;
    if (c > 1e-6f)
    {
        const float across = c - b * b;
        const float t = across > 1e-6f * c ? (b * e - c * d) / across : -d;
        u = std::clamp((e + b * t) / c, 0.0f, 1.0f);
    }
    float t = std::clamp(b * u - d, nearest, farthest);
    if (c > 1e-6f)
        u = std::clamp((e + b * t) / c, 0.0f, 1.0f);
    float off = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float gap = start[k] + ray[k] * t - axis[k] * u;
        off += gap * gap;
    }
    along = t;
    share = std::sqrt(off) / std::max(a.radius, 1e-3f);
    return share <= 1.0f;
}

bool ClusterToLight(const EffectFrame& f, const CellSums& s, EffectCluster& out)
{
    if (!(s.energy > 0.0f) || !(s.energyPerDepth > 0.0f))
        return false;
    const float* p = f.glProjection;
    const float u = s.energyU / s.energy;
    const float v = s.energyV / s.energy;
    const float seenOn = s.energy / s.energyPerDepth;
    out.sprite = s.spriteInverseDepth > 0.0f && 1.0f / s.spriteInverseDepth < seenOn;
    float depth = out.sprite ? 1.0f / s.spriteInverseDepth : seenOn;
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    float view[3] = {(ndcX - p[8]) / p[0] * depth, (ndcY - p[9]) / p[5] * depth, depth};
    const float length = std::sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
    float surface[3];
    if (!ViewToWorld(f, view, surface))
        return false;
    float along = 0.0f;
    if (f.playerKnown && length > 0.0f)
    {
        float toZone[3];
        float ray[3];
        for (int k = 0; k < 3; ++k)
        {
            ray[k] = (surface[k] - f.camPos[k]) / length;
            toZone[k] = f.playerZone[k] - f.camPos[k];
        }
        const float t = toZone[0] * ray[0] + toZone[1] * ray[1] + toZone[2] * ray[2];
        float off = 0.0f;
        for (int k = 0; k < 3; ++k)
            off += (toZone[k] - ray[k] * t) * (toZone[k] - ray[k] * t);
        if (t > kEffectNearestYards && off <= kEffectPlayerZoneYards * kEffectPlayerZoneYards &&
            length * (seenOn / depth) > t + kEffectPlayerZoneYards)
            along = t;
    }
    out.atWeapon = along > 0.0f;
    out.handGlow = out.atWeapon && f.handGlows;
    out.atUnit = false;
    if (!out.atWeapon && !out.sprite && length > 0.0f)
    {
        float ray[3];
        for (int k = 0; k < 3; ++k)
            ray[k] = (surface[k] - f.camPos[k]) / length;
        const float farthest = length * (seenOn / depth) - kEffectAnchorClearYards;
        float bestShare = std::numeric_limits<float>::infinity();
        for (int i = 0; i < std::min(f.anchorCount, kMaxEffectAnchors); ++i)
        {
            float t = 0.0f;
            float share = 0.0f;
            if (farthest > kEffectNearestYards &&
                AnchorOnRay(f.anchors[i], f.camPos, ray, kEffectNearestYards, farthest, t, share) &&
                (share < bestShare || (share == bestShare && t < along)))
            {
                bestShare = share;
                along = t;
            }
        }
        out.atUnit = along > 0.0f;
    }
    if (along > 0.0f)
    {
        for (int k = 0; k < 3; ++k)
            out.position[k] = f.camPos[k] + (surface[k] - f.camPos[k]) * (along / length);
        depth *= along / length;
    }
    else
    {
        const float pull = std::min(kEffectStandOffYards, kEffectMaxStandOffShare * length) / length;
        for (float& axis : view)
            axis -= axis * pull;
        if (!ViewToWorld(f, view, out.position))
            return false;
        out.position[2] += kEffectLiftYards;
    }
    const float yardsPerPixelX = 2.0f * depth / (p[0] * f.viewportWidth);
    const float yardsPerPixelY = 2.0f * depth / (p[5] * f.viewportHeight);
    const float pixelArea = yardsPerPixelX * yardsPerPixelY;
    const float peak = std::max({s.colour[0], s.colour[1], s.colour[2]});
    if (!(peak > 0.0f))
        return false;
    for (int k = 0; k < 3; ++k)
        out.colour[k] = std::max(s.colour[k], 0.0f) / peak;
    out.litArea = s.energy * pixelArea;
    out.hot = out.atWeapon && !out.handGlow ? 1.0f : Hotness(s.pixels > 0.0f ? s.energy / s.pixels : 0.0f);
    out.intensity = EffectIntensity(out.litArea * (1.0f + kEffectHotAreaGain * (out.atWeapon ? 1.0f : out.hot)));
    out.nearYards = std::clamp(std::sqrt(s.pixels * pixelArea / kPi), kMinEffectNearYards, kMaxEffectNearYards);
    out.surfaceReach = EffectSurfaceReach(out.intensity);
    out.viewDepth = depth;
    return out.intensity >= kMinEffectIntensity;
}

float DistanceSquared(const float* a, const float* b)
{
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}
}

float EffectLuminance(const float* rgb)
{
    return kLuminance[0] * rgb[0] + kLuminance[1] * rgb[1] + kLuminance[2] * rgb[2];
}

float EffectIntensity(float litArea)
{
    return kMaxEffectIntensity *
           (1.0f - std::exp(-kEffectIntensityPerLitArea * std::max(litArea, 0.0f) / kMaxEffectIntensity));
}

void EffectPlayerZone(const float* feet, float facing, float pivotHeight, float* centre)
{
    centre[0] = feet[0] + std::cos(facing) * kEffectPlayerZoneForwardYards;
    centre[1] = feet[1] + std::sin(facing) * kEffectPlayerZoneForwardYards;
    centre[2] = feet[2] + kEffectPlayerZoneHeightShare * pivotHeight;
}

bool EffectHandGlows(uint32_t playerClass)
{
    return playerClass != 0 && playerClass != kEffectMuzzleFlashClass;
}

void EffectFramePlayer(const FrameInputs& in, float pivotHeight, EffectFrame& f)
{
    f.playerKnown = in.playerKnown;
    f.handGlows = in.playerKnown && EffectHandGlows(in.playerClass);
    if (in.playerKnown)
        EffectPlayerZone(in.playerPos, in.playerFacing, pivotHeight, f.playerZone);
}

void EffectFlashColour(const float* measured, float* out)
{
    const bool warmOrWhite = (measured[1] <= measured[0] * kEffectWhiteTolerance &&
                              measured[2] <= measured[1] * kEffectWhiteTolerance) ||
                             Saturation(measured) < kEffectFlashWhiteSaturation;
    for (int k = 0; k < 3; ++k)
        out[k] = warmOrWhite ? kEffectFlameColour[k] : measured[k];
}

void EffectLightColour(const float* measured, float* out)
{
    const float peak = std::max({measured[0], measured[1], measured[2]});
    const float low = std::min({measured[0], measured[1], measured[2]});
    const float toWhite =
        peak > 0.0f && low < kEffectMinChannel * peak ? (kEffectMinChannel * peak - low) / (peak - low) : 0.0f;
    for (int k = 0; k < 3; ++k)
        out[k] = measured[k] + (peak - measured[k]) * toWhite;
}

float EffectTargetRingYards(float boundingRadius)
{
    const float radius = std::isfinite(boundingRadius) ? boundingRadius : 0.0f;
    return std::clamp(kEffectRingPerBoundingRadius * radius, kMinEffectRingYards, kMaxEffectRingYards) +
           kEffectRingMarginYards;
}

int EffectUnitAnchors(const UnitInput* units, int count, EffectAnchor* out)
{
    const int n = std::clamp(count, 0, kMaxUnitInputs);
    float height[kMaxUnitInputs] = {};
    float radius[kMaxUnitInputs] = {};
    int anchors = 0;
    for (int i = 0; i < n; ++i)
    {
        const UnitInput& u = units[i];
        const float r =
            std::isfinite(u.boundingRadius) ? std::clamp(u.boundingRadius, 0.0f, kMaxEffectUnitRadius) : 0.0f;
        height[i] = std::clamp(kEffectUnitHeightPerRadius * r, kMinEffectUnitHeight, kMaxEffectUnitHeight);
        radius[i] = std::max(kMinEffectUnitRadius, kEffectUnitRadiusShare * r) + kEffectUnitMarginYards;
        EffectAnchor& a = out[anchors++];
        std::memcpy(a.from, u.feet, sizeof(a.from));
        std::memcpy(a.to, u.feet, sizeof(a.to));
        a.to[2] += height[i];
        a.radius = radius[i];
        a.unit = u.guid;
    }
    for (int i = 0; i < n; ++i)
    {
        const int t = units[i].target;
        if (t < 0 || t >= n || t == i)
            continue;
        EffectAnchor& a = out[anchors];
        std::memcpy(a.from, units[i].feet, sizeof(a.from));
        std::memcpy(a.to, units[t].feet, sizeof(a.to));
        a.from[2] += kEffectMissileHeightShare * height[i];
        a.to[2] += kEffectMissileHeightShare * height[t];
        const float gap[3] = {a.to[0] - a.from[0], a.to[1] - a.from[1], a.to[2] - a.from[2]};
        if (Dot(gap, gap) < kMinEffectMissileYards * kMinEffectMissileYards)
            continue;
        a.radius = kEffectMissileRadius;
        a.unit = 0;
        ++anchors;
    }
    return anchors;
}

bool CarriedByUnit(const EffectFrame& f, const float* position, uint64_t& unit, float* offset)
{
    unit = 0;
    float best = std::numeric_limits<float>::infinity();
    for (int i = 0; i < std::min(f.anchorCount, kMaxEffectAnchors); ++i)
    {
        const EffectAnchor& a = f.anchors[i];
        if (!a.unit || position[2] < a.from[2] - kEffectCarriedMarginYards ||
            position[2] > a.to[2] + kEffectCarriedMarginYards)
            continue;
        const float dx = position[0] - a.from[0];
        const float dy = position[1] - a.from[1];
        const float share = std::sqrt(dx * dx + dy * dy) / (a.radius + kEffectCarriedMarginYards);
        if (share <= 1.0f && share < best)
        {
            best = share;
            unit = a.unit;
            for (int k = 0; k < 3; ++k)
                offset[k] = position[k] - a.from[k];
        }
    }
    return unit != 0;
}

float EffectSurfaceReach(float intensity)
{
    const float reach = std::clamp(kEffectReachPerRootIntensity * std::sqrt(std::max(intensity, 0.0f)),
                                   kMinEffectReach, kMaxEffectReach);
    return std::min(2.0f * reach, 2.0f * kMaxEffectReach);
}

std::vector<EffectCluster> ClusterEffects(const EffectMeasurement& m, const LampMap* lamps, EffectClusterStats* stats)
{
    std::vector<EffectCluster> lights;
    EffectClusterStats local = {};
    if (m.columns <= 0 || m.rows <= 0 ||
        m.texels.size() < static_cast<size_t>(2 * m.columns) * static_cast<size_t>(m.rows) * 4)
    {
        if (stats)
            *stats = local;
        return lights;
    }
    std::vector<Cell> cells(static_cast<size_t>(m.columns) * m.rows);
    std::vector<float> energies(cells.size());
    for (int y = 0; y < m.rows; ++y)
        for (int x = 0; x < m.columns; ++x)
        {
            const size_t i = static_cast<size_t>(y) * m.columns + x;
            cells[i].sums = ReadCell(m, x, y);
            energies[i] = cells[i].sums.energy;
        }
    const size_t backgroundIndex =
        std::min(energies.size() - 1, static_cast<size_t>(kEffectBackgroundShare * energies.size()));
    std::nth_element(energies.begin(), energies.begin() + backgroundIndex, energies.end());
    local.background = std::max(energies[backgroundIndex], 0.0f);
    const float weather = std::clamp(m.frame.weatherIntensity, 0.0f, 1.0f);
    float least =
        std::max(kMinEffectCellEnergy + weather * kEffectWeatherCellEnergy, kEffectStandOut * local.background);
    if (weather > 0.0f)
    {
        std::vector<float> touched;
        for (const Cell& c : cells)
            if (c.sums.energy > 0.0f)
                touched.push_back(c.sums.energy);
        local.fieldCells = static_cast<int>(touched.size());
        const size_t fieldCells = std::max<size_t>(1, static_cast<size_t>(kEffectFieldShareOfCells * cells.size()));
        if (touched.size() >= fieldCells)
        {
            const size_t at = std::min(touched.size() - 1, static_cast<size_t>(kEffectFieldShare * touched.size()));
            std::nth_element(touched.begin(), touched.begin() + at, touched.end());
            local.field = touched[at];
            least = std::max(least, kEffectFieldStandOut * local.field);
        }
    }
    local.least = least;
    std::vector<size_t> order;
    for (int y = 0; y < m.rows; ++y)
        for (int x = 0; x < m.columns; ++x)
        {
            Cell& c = cells[static_cast<size_t>(y) * m.columns + x];
            c.open = c.sums.energy >= least && c.sums.energyPerDepth > 0.0f;
            if (c.open && weather > 0.0f && Saturation(c.sums.colour) < kEffectWeatherSaturation)
            {
                c.open = false;
                ++local.colourless;
            }
            if (!c.open)
                continue;
            c.u = c.sums.energyU / c.sums.energy;
            c.v = c.sums.energyV / c.sums.energy;
            c.depth = c.sums.energy / c.sums.energyPerDepth;
            c.nearest = NearestDepth(c.sums, c.depth);
            c.sums.spriteInverseDepth = SeenAsSprite(c.nearest, c.depth) ? 1.0f / c.nearest : 0.0f;
            ++local.cells;
        }
    local.ringCells = SuppressTargetRing(m, cells);
    if (lamps)
        SuppressLampFlames(m, *lamps, cells, local.suppressedCells, local.lampGlowCells);
    for (size_t i = 0; i < cells.size(); ++i)
        if (cells[i].open)
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const bool spriteA = cells[a].sums.spriteInverseDepth > 0.0f;
        const bool spriteB = cells[b].sums.spriteInverseDepth > 0.0f;
        if (spriteA != spriteB)
            return spriteA;
        return cells[a].sums.energy != cells[b].sums.energy ? cells[a].sums.energy > cells[b].sums.energy : a < b;
    });
    const float* p = m.frame.glProjection;
    const float halfCellU = 0.5f * kEffectCellPixels / m.frame.viewportWidth;
    const float halfCellV = 0.5f * kEffectCellPixels / m.frame.viewportHeight;
    for (size_t seedIndex : order)
    {
        Cell& seed = cells[seedIndex];
        if (!seed.open)
            continue;
        if (local.clusters == kMaxEffectClusters)
        {
            ++local.droppedClusters;
            seed.open = false;
            continue;
        }
        seed.open = false;
        CellSums sums = seed.sums;
        const bool sprite = seed.sums.spriteInverseDepth > 0.0f;
        const float seedDepth = sprite ? seed.nearest : seed.depth;
        const float radiusU = kEffectClusterYards * p[0] * 0.5f / seedDepth + halfCellU;
        const float radiusV = kEffectClusterYards * p[5] * 0.5f / seedDepth + halfCellV;
        const float depthSpan = std::max(kEffectClusterDepthYards, kEffectClusterDepthShare * seedDepth);
        for (size_t other : order)
        {
            Cell& c = cells[other];
            if (!c.open)
                continue;
            const float du = (c.u - seed.u) / radiusU;
            const float dv = (c.v - seed.v) / radiusV;
            const bool sameDepth = std::fabs(c.depth - seedDepth) <= depthSpan;
            const bool behindSprite = sprite && c.nearest >= seedDepth - depthSpan;
            if (du * du + dv * dv <= 1.0f && (sameDepth || behindSprite))
            {
                Accumulate(sums, c.sums);
                c.open = false;
            }
        }
        ++local.clusters;
        EffectCluster light = {};
        if (ClusterToLight(m.frame, sums, light))
        {
            if (m.frame.falling && m.frame.weatherIntensity > 0.0f && !light.atWeapon)
            {
                ++local.fallingDropped;
                continue;
            }
            local.sprites += light.sprite ? 1 : 0;
            local.atWeapon += light.atWeapon ? 1 : 0;
            local.atUnits += light.atUnit ? 1 : 0;
            local.carried += CarriedByUnit(m.frame, light.position, light.unit, light.unitOffset) ? 1 : 0;
            lights.push_back(light);
        }
    }
    if (stats)
        *stats = local;
    return lights;
}

EffectVelocity FitEffectVelocity(const double* seconds, const float (*positions)[3], int count)
{
    EffectVelocity out = {{0.0f, 0.0f, 0.0f}, 0.0f};
    if (count <= 0)
        return out;
    const double newest = seconds[count - 1];
    int first = count - 1;
    while (first > 0 && newest - seconds[first - 1] <= kEffectVelocityWindowSeconds)
        --first;
    const int n = count - first;
    if (n < kMinEffectVelocitySamples || newest - seconds[first] < kMinEffectVelocitySpanSeconds)
        return out;
    double meanT = 0.0;
    double meanP[3] = {0.0, 0.0, 0.0};
    for (int i = first; i < count; ++i)
    {
        meanT += seconds[i];
        for (int k = 0; k < 3; ++k)
            meanP[k] += positions[i][k];
    }
    meanT /= n;
    for (double& m : meanP)
        m /= n;
    double spread = 0.0;
    double slope[3] = {0.0, 0.0, 0.0};
    for (int i = first; i < count; ++i)
    {
        const double dt = seconds[i] - meanT;
        spread += dt * dt;
        for (int k = 0; k < 3; ++k)
            slope[k] += dt * (positions[i][k] - meanP[k]);
    }
    if (!(spread > 0.0))
        return out;
    for (double& v : slope)
        v /= spread;
    double residual = 0.0;
    for (int i = first; i < count; ++i)
    {
        const double dt = seconds[i] - meanT;
        for (int k = 0; k < 3; ++k)
        {
            const double r = positions[i][k] - meanP[k] - slope[k] * dt;
            residual += r * r;
        }
    }
    const double speed = std::sqrt(slope[0] * slope[0] + slope[1] * slope[1] + slope[2] * slope[2]);
    const double spreadOfSpeed = std::sqrt(residual / (3.0 * (n - 2)) / spread);
    const double significance = speed / std::max(spreadOfSpeed, 1e-4);
    const float t = std::clamp(static_cast<float>((significance - kEffectMovingFrom) / (kEffectMovingFull - kEffectMovingFrom)),
                               0.0f, 1.0f);
    out.moving = t * t * (3.0f - 2.0f * t);
    const double scale = speed > kMaxEffectSpeed ? kMaxEffectSpeed / speed : 1.0;
    for (int k = 0; k < 3; ++k)
        out.velocity[k] = static_cast<float>(slope[k] * scale);
    return out;
}

void EffectTracker::Reset()
{
    m_tracked.clear();
    m_lights.clear();
    m_stats = {};
    m_clockValid = false;
}

namespace
{
const UnitInput* FindUnit(const UnitInput* units, int count, uint64_t guid)
{
    if (!units || !guid)
        return nullptr;
    for (int i = 0; i < std::clamp(count, 0, kMaxUnitInputs); ++i)
        if (units[i].guid == guid)
            return &units[i];
    return nullptr;
}

float Lead(double from, double to)
{
    return static_cast<float>(std::clamp(to - from, 0.0, kMaxEffectLeadSeconds));
}
}

void EffectTracker::Carry(const UnitInput* units, int unitCount)
{
    m_units = units;
    m_unitCount = unitCount;
    for (Tracked& t : m_tracked)
    {
        const UnitInput* u = FindUnit(units, unitCount, t.unit);
        t.carried = u != nullptr;
        if (u)
            std::memcpy(t.feet, u->feet, sizeof(t.feet));
        else
            t.unit = 0;
    }
}

void EffectTracker::Match(const std::vector<EffectCluster>& measured, const float* camPos, double measuredSeconds)
{
    for (Tracked& t : m_tracked)
        t.matched = false;
    std::vector<const EffectCluster*> order;
    for (const EffectCluster& c : measured)
        order.push_back(&c);
    std::sort(order.begin(), order.end(),
              [](const EffectCluster* a, const EffectCluster* b) { return a->intensity > b->intensity; });
    for (const EffectCluster* c : order)
    {
        const float yards =
            std::max(kEffectMatchYards, kEffectMatchShare * std::sqrt(DistanceSquared(c->position, camPos)));
        Tracked* best = nullptr;
        float bestSquared = yards * yards;
        for (Tracked& t : m_tracked)
        {
            if (t.matched)
                continue;
            float d = 0.0f;
            if (t.carried && c->unit == t.unit)
                d = DistanceSquared(t.targetOffset, c->unitOffset);
            else
            {
                const float lead = Lead(t.measuredSeconds, measuredSeconds) * t.velocity.moving;
                float expected[3];
                for (int k = 0; k < 3; ++k)
                    expected[k] = t.targetPosition[k] + t.velocity.velocity[k] * lead;
                d = DistanceSquared(expected, c->position);
            }
            if (d <= bestSquared)
            {
                best = &t;
                bestSquared = d;
            }
        }
        const UnitInput* carrier = FindUnit(m_units, m_unitCount, c->unit);
        bool created = false;
        if (!best)
        {
            if (c->intensity < kEffectSpawnIntensity || m_tracked.size() >= static_cast<size_t>(kMaxEffectLights))
                continue;
            Tracked fresh = {};
            fresh.now.id = m_nextId++;
            std::memcpy(fresh.now.position, c->position, sizeof(fresh.now.position));
            std::memcpy(fresh.now.colour, c->colour, sizeof(fresh.now.colour));
            fresh.flashing = fresh.bornFlash = c->hot >= kEffectPopHot;
            fresh.weapon = c->atWeapon && !c->handGlow;
            fresh.now.intensity = fresh.flashing ? c->intensity : 0.0f;
            fresh.now.nearYards = c->nearYards;
            fresh.now.surfaceReach = c->surfaceReach;
            m_tracked.push_back(fresh);
            best = &m_tracked.back();
            created = true;
        }
        if (carrier)
        {
            if (created)
                std::memcpy(best->offset, c->unitOffset, sizeof(best->offset));
            else if (!best->carried || best->unit != c->unit)
            {
                for (int k = 0; k < 3; ++k)
                    best->offset[k] = best->now.intensity > kEffectGoneIntensity
                                          ? best->now.position[k] - carrier->feet[k]
                                          : c->unitOffset[k];
            }
            best->unit = c->unit;
            best->carried = true;
            std::memcpy(best->feet, carrier->feet, sizeof(best->feet));
            std::memcpy(best->targetOffset, c->unitOffset, sizeof(best->targetOffset));
        }
        else
        {
            best->unit = 0;
            best->carried = false;
        }
        if (best->samples == kEffectVelocitySamples)
        {
            std::memmove(best->sampleSeconds, best->sampleSeconds + 1, sizeof(double) * (kEffectVelocitySamples - 1));
            std::memmove(best->samplePositions, best->samplePositions + 1,
                         sizeof(best->samplePositions[0]) * (kEffectVelocitySamples - 1));
            --best->samples;
        }
        if (best->samples == 0 || best->sampleSeconds[best->samples - 1] < measuredSeconds)
        {
            best->sampleSeconds[best->samples] = measuredSeconds;
            std::memcpy(best->samplePositions[best->samples], c->position, sizeof(best->samplePositions[0]));
            ++best->samples;
        }
        best->velocity = FitEffectVelocity(best->sampleSeconds, best->samplePositions, best->samples);
        best->measuredSeconds = measuredSeconds;
        std::memcpy(best->targetPosition, c->position, sizeof(best->targetPosition));
        std::memcpy(best->targetColour, c->colour, sizeof(best->targetColour));
        best->targetIntensity = c->intensity;
        best->targetNear = c->nearYards;
        best->targetReach = c->surfaceReach;
        best->hot = c->hot;
        best->atWeapon = c->atWeapon;
        best->misses = 0;
        best->matched = true;
    }
    for (Tracked& t : m_tracked)
        if (!t.matched && (t.flashing || ++t.misses >= kEffectMissesBeforeFade))
            t.targetIntensity = 0.0f;
}

void EffectTracker::Aim(double seconds)
{
    for (Tracked& t : m_tracked)
    {
        if (t.carried)
        {
            for (int k = 0; k < 3; ++k)
                t.aim[k] = t.feet[k] + t.targetOffset[k];
            continue;
        }
        const float lead = Lead(t.measuredSeconds, seconds) * t.velocity.moving;
        for (int k = 0; k < 3; ++k)
            t.aim[k] = t.targetPosition[k] + t.velocity.velocity[k] * lead;
    }
}

void EffectTracker::Smooth(float step)
{
    const float position = Approach(kEffectPositionSeconds, step);
    const float colour = Approach(kEffectColourSeconds, step);
    const float rise = Approach(kEffectRiseSeconds, step);
    const float fall = Approach(kEffectFallSeconds, step);
    const float quickFall = Approach(kEffectFlashFallSeconds, step);
    for (Tracked& t : m_tracked)
    {
        EffectLamp& now = t.now;
        if (t.targetIntensity > 0.0f)
            t.life += step;
        if (t.flashing)
        {
            std::memcpy(now.position, t.aim, sizeof(now.position));
            std::memcpy(t.offset, t.targetOffset, sizeof(t.offset));
            now.nearYards = t.targetNear;
            now.surfaceReach = t.targetReach;
            if (t.targetIntensity > 0.0f && t.life <= kEffectFlashSeconds)
            {
                EffectFlashColour(t.targetColour, now.colour);
                now.intensity =
                    std::max(now.intensity, t.targetIntensity * (t.weapon ? kEffectWeaponFlashGain : 1.0f));
                if (t.weapon)
                    now.surfaceReach = std::max(t.targetReach, EffectSurfaceReach(now.intensity));
                continue;
            }
            t.flashing = false;
            t.outlasted = t.targetIntensity > 0.0f;
            t.held = t.weapon;
            if (!t.held)
            {
                std::memcpy(now.colour, t.targetColour, sizeof(now.colour));
                now.intensity = t.targetIntensity;
                continue;
            }
        }
        if (t.held)
        {
            std::memcpy(now.position, t.aim, sizeof(now.position));
            std::memcpy(t.offset, t.targetOffset, sizeof(t.offset));
            now.nearYards = t.targetNear;
            now.surfaceReach = t.targetReach;
            now.intensity = 0.0f;
            if (t.targetIntensity > 0.0f && !t.atWeapon && !t.bullet)
            {
                t.bullet = true;
                ++m_flashes.bullets;
            }
            continue;
        }
        if (t.targetIntensity > 0.0f)
            t.quick = t.hot >= kEffectPopHot && t.life <= kEffectFlashLifeSeconds;
        const bool lit = now.intensity > kEffectGoneIntensity;
        for (int k = 0; k < 3; ++k)
        {
            if (t.carried)
            {
                t.offset[k] = lit ? Blend(t.offset[k], t.targetOffset[k], position) : t.targetOffset[k];
                now.position[k] = t.feet[k] + t.offset[k];
            }
            else
                now.position[k] = lit ? Blend(now.position[k] + t.velocity.velocity[k] * t.velocity.moving * step,
                                              t.aim[k], position)
                                      : t.aim[k];
            now.colour[k] = lit ? Blend(now.colour[k], t.targetColour[k], colour) : t.targetColour[k];
        }
        now.nearYards = Blend(now.nearYards, t.targetNear, colour);
        now.surfaceReach = Blend(now.surfaceReach, t.targetReach, colour);
        now.intensity = Blend(now.intensity, t.targetIntensity,
                              t.targetIntensity > now.intensity ? rise : (t.quick ? quickFall : fall));
    }
    auto gone = [](const Tracked& t) { return t.targetIntensity <= 0.0f && t.now.intensity < kEffectGoneIntensity; };
    for (const Tracked& t : m_tracked)
        if (t.bornFlash && gone(t))
        {
            m_flashes.shortestSeconds = m_flashes.flashes ? std::min(m_flashes.shortestSeconds, t.life) : t.life;
            m_flashes.longestSeconds = std::max(m_flashes.longestSeconds, t.life);
            ++m_flashes.flashes;
            if (t.outlasted)
                ++m_flashes.outlasted;
            if (t.weapon)
                ++m_flashes.weaponFlashes;
        }
    m_tracked.erase(std::remove_if(m_tracked.begin(), m_tracked.end(), gone), m_tracked.end());
}

void EffectTracker::Update(const std::vector<EffectCluster>* measured, const float* camPos, double seconds, int mapId,
                           double measuredSeconds, const UnitInput* units, int unitCount)
{
    if (mapId != m_mapId)
    {
        Reset();
        m_mapId = mapId;
    }
    const double step = m_clockValid ? std::clamp(seconds - m_seconds, 0.0, kMaxEffectStepSeconds) : 0.0;
    if (!m_clockValid)
        m_lastMeasured = seconds;
    m_seconds = seconds;
    m_clockValid = true;
    Carry(units, unitCount);
    if (measured)
    {
        m_lastMeasured = seconds;
        Match(*measured, camPos, measuredSeconds < 0.0 ? seconds : std::min(measuredSeconds, seconds));
    }
    else if (seconds - m_lastMeasured > kEffectStaleSeconds)
        for (Tracked& t : m_tracked)
            t.targetIntensity = 0.0f;
    Aim(seconds);
    Smooth(static_cast<float>(step));
    m_units = nullptr;
    m_unitCount = 0;
    m_lights.clear();
    m_stats = {};
    for (const Tracked& t : m_tracked)
        if (t.now.intensity > 0.0f)
        {
            m_lights.push_back(t.now);
            if (!t.flashing)
                EffectLightColour(t.now.colour, m_lights.back().colour);
            m_stats.carried += t.carried ? 1 : 0;
            m_stats.moving += !t.carried && t.velocity.moving > 0.5f ? 1 : 0;
        }
    std::sort(m_lights.begin(), m_lights.end(), [](const EffectLamp& a, const EffectLamp& b) {
        return a.intensity != b.intensity ? a.intensity > b.intensity : a.id < b.id;
    });
}
