#include "object_lamps.h"

#include <algorithm>
#include <cmath>

namespace
{
uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

bool SamePlace(const lampfile::Lamp& a, const lampfile::Lamp& b)
{
    const float d[3] = {a.position[0] - b.position[0], a.position[1] - b.position[1], a.position[2] - b.position[2]};
    return a.kind == b.kind &&
           d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < kObjectLampDuplicateYards * kObjectLampDuplicateYards;
}

bool AtAPlacedLamp(const LampMap* map, const lampfile::Lamp& lamp)
{
    bool found = false;
    if (map)
        ForEachLampNear(*map, lamp.position[0], lamp.position[1], kObjectLampDuplicateYards,
                        [&](uint32_t, const lampfile::Lamp& placed) { found = found || SamePlace(placed, lamp); });
    return found;
}
}

uint32_t ObjectLampId(uint64_t guid, uint32_t index)
{
    const uint32_t high = Mix(static_cast<uint32_t>(guid >> 32) + index * 0x9e3779b9u);
    return Mix(static_cast<uint32_t>(guid) ^ high) & kObjectLampIdMask;
}

void PlaceObjectLamp(const lampfile::Lamp& local, const ObjectInput& object, lampfile::Lamp& out)
{
    out = local;
    const float c = std::cos(object.facing);
    const float s = std::sin(object.facing);
    const float x = local.position[0] * object.scale;
    const float y = local.position[1] * object.scale;
    out.position[0] = object.position[0] + c * x - s * y;
    out.position[1] = object.position[1] + s * x + c * y;
    out.position[2] = object.position[2] + local.position[2] * object.scale;
}

const std::vector<ObjectLamp>& ObjectLamps::Update(const LampCatalog& catalog, const FrameInputs& in, bool enabled)
{
    m_lamps.clear();
    m_objects = 0;
    m_duplicates = 0;
    if (!enabled)
        return m_lamps;
    const LampMap* map = catalog.Map(in.mapId);
    for (int i = 0; i < std::clamp(in.objectCount, 0, kMaxObjectInputs); ++i)
    {
        const ObjectInput& object = in.objects[i];
        ObjectModelLamps model = {};
        if (!catalog.ObjectModel(object.displayId, model))
            continue;
        ++m_objects;
        for (uint32_t k = 0; k < model.count; ++k)
        {
            ObjectLamp lit = {};
            PlaceObjectLamp(model.lamps[k], object, lit.lamp);
            const bool twice = std::any_of(m_lamps.begin(), m_lamps.end(),
                                           [&](const ObjectLamp& kept) { return SamePlace(kept.lamp, lit.lamp); });
            if (twice || AtAPlacedLamp(map, lit.lamp))
            {
                ++m_duplicates;
                continue;
            }
            lit.beam = model.beams[k] != 0;
            lit.size = object.scale;
            lit.id = ObjectLampId(object.guid, k);
            const auto taken = [&](uint32_t id) {
                return std::any_of(m_lamps.begin(), m_lamps.end(),
                                   [&](const ObjectLamp& kept) { return kept.id == id; });
            };
            while (taken(lit.id))
                lit.id = (lit.id + 1) & kObjectLampIdMask;
            m_lamps.push_back(lit);
        }
    }
    return m_lamps;
}
