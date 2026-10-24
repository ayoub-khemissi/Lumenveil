#pragma once

#include "engine.h"
#include "lamp_catalog.h"

#include <cstdint>
#include <vector>

constexpr uint32_t kObjectLampIdMask = 0x07FFFFFFu;
constexpr float kObjectLampDuplicateYards = 0.75f;

struct ObjectLamp
{
    uint32_t id;
    lampfile::Lamp lamp;
    float size;
    bool beam;
};

uint32_t ObjectLampId(uint64_t guid, uint32_t index);
void PlaceObjectLamp(const lampfile::Lamp& local, const ObjectInput& object, lampfile::Lamp& out);

class ObjectLamps
{
public:
    const std::vector<ObjectLamp>& Update(const LampCatalog& catalog, const FrameInputs& in, bool enabled);
    const std::vector<ObjectLamp>& Lamps() const { return m_lamps; }
    int Objects() const { return m_objects; }
    int Duplicates() const { return m_duplicates; }

private:
    std::vector<ObjectLamp> m_lamps;
    int m_objects = 0;
    int m_duplicates = 0;
};
