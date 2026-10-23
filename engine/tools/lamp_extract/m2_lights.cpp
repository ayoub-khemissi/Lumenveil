#include "m2_lights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <ostream>
#include <unordered_set>

#include "archive_set.h"
#include "byte_view.h"
#include "parallel.h"

namespace
{

constexpr size_t kHeaderVersion = 0x04;
constexpr size_t kHeaderBones = 0x2C;
constexpr size_t kHeaderVertices = 0x3C;
constexpr size_t kHeaderTextures = 0x50;
constexpr size_t kHeaderRenderFlags = 0x70;
constexpr size_t kHeaderTextureLookup = 0x80;
constexpr size_t kHeaderBoundsMin = 0xA0;
constexpr size_t kHeaderBoundsMax = 0xAC;
constexpr size_t kHeaderLights = 0x108;
constexpr size_t kHeaderEmitters = 0x128;
constexpr size_t kHeaderSize = 0x130;

constexpr size_t kVertexStride = 48;
constexpr size_t kTextureStride = 16;
constexpr size_t kRenderFlagStride = 4;

constexpr size_t kBoneStride = 0x58;
constexpr size_t kBoneParent = 0x08;
constexpr size_t kBoneTranslation = 0x10;
constexpr size_t kBoneRotation = 0x24;
constexpr size_t kBoneScale = 0x38;
constexpr size_t kBonePivot = 0x4C;
constexpr uint32_t kMaxBoneDepth = 64;

constexpr size_t kLightStride = 0x9C;
constexpr size_t kLightBone = 0x02;
constexpr size_t kLightPosition = 0x04;
constexpr size_t kLightDiffuseColor = 0x38;
constexpr size_t kLightDiffuseIntensity = 0x4C;
constexpr size_t kLightVisibility = 0x88;
constexpr uint16_t kLightTypeDirectional = 0;
constexpr uint16_t kLightTypePoint = 1;

constexpr size_t kEmitterStride = 0x1DC;
constexpr size_t kEmitterPosition = 0x08;
constexpr size_t kEmitterBone = 0x14;
constexpr size_t kEmitterTexture = 0x16;
constexpr size_t kEmitterBlend = 0x28;
constexpr size_t kEmitterType = 0x29;
constexpr size_t kEmitterRate = 0xB0;
constexpr size_t kEmitterColor = 0x104;
constexpr size_t kEmitterAlpha = 0x114;
constexpr size_t kEmitterScale = 0x124;
constexpr uint8_t kBlendNoAlphaAdd = 3;
constexpr uint8_t kBlendAdd = 4;

constexpr uint32_t kSkinMagic = 0x4E494B53u;
constexpr size_t kSkinVertices = 0x04;
constexpr size_t kSkinSubmeshes = 0x1C;
constexpr size_t kSkinBatches = 0x24;
constexpr size_t kSubmeshStride = 0x30;
constexpr size_t kSubmeshLevel = 0x02;
constexpr size_t kSubmeshVertexStart = 0x04;
constexpr size_t kSubmeshVertexCount = 0x06;
constexpr size_t kBatchStride = 0x18;
constexpr size_t kBatchSubmesh = 0x04;
constexpr size_t kBatchMaterial = 0x0A;
constexpr size_t kBatchTextureCount = 0x0E;
constexpr size_t kBatchTextureCombo = 0x10;
constexpr uint16_t kMaterialUnlit = 0x01;

constexpr uint32_t kMaxArrayCount = 1u << 20;
constexpr float kMinLightIntensity = 0.01f;
constexpr float kClampMargin = 4.0f;
constexpr float kMaxClampedFlameSize = 1.0f;

struct M2Array
{
    uint32_t count = 0;
    uint32_t offset = 0;
};

struct Vec2
{
    float x;
    float y;
};

M2Array ArrayAt(ByteView view, size_t at)
{
    return {view.Get<uint32_t>(at), view.Get<uint32_t>(at + 4)};
}

bool ArrayFits(ByteView view, M2Array array, size_t stride)
{
    return array.count <= kMaxArrayCount && view.Contains(array.offset, static_cast<size_t>(array.count) * stride);
}

template <typename T>
std::vector<T> TrackValues(ByteView m2, size_t track, size_t limit = 4096)
{
    std::vector<T> values;
    M2Array outer = ArrayAt(m2, track + 0x0C);
    if (!ArrayFits(m2, outer, 8))
        return values;
    for (uint32_t i = 0; i < outer.count && values.size() < limit; ++i)
    {
        M2Array inner = ArrayAt(m2, outer.offset + static_cast<size_t>(i) * 8);
        if (!ArrayFits(m2, inner, sizeof(T)))
            continue;
        for (uint32_t j = 0; j < inner.count && values.size() < limit; ++j)
            values.push_back(m2.Get<T>(inner.offset + static_cast<size_t>(j) * sizeof(T)));
    }
    return values;
}

template <typename T>
std::optional<T> FirstTrackValue(ByteView m2, size_t track)
{
    std::vector<T> values = TrackValues<T>(m2, track, 1);
    if (values.empty())
        return std::nullopt;
    return values.front();
}

template <typename T>
std::vector<T> FakeTrackKeys(ByteView m2, size_t block)
{
    std::vector<T> keys;
    M2Array array = ArrayAt(m2, block + 8);
    if (!ArrayFits(m2, array, sizeof(T)) || array.count > 64)
        return keys;
    for (uint32_t i = 0; i < array.count; ++i)
        keys.push_back(m2.Get<T>(array.offset + static_cast<size_t>(i) * sizeof(T)));
    return keys;
}

std::string TextureName(ByteView m2, uint32_t index)
{
    M2Array textures = ArrayAt(m2, kHeaderTextures);
    if (index >= textures.count || !ArrayFits(m2, textures, kTextureStride))
        return std::string();
    size_t at = textures.offset + static_cast<size_t>(index) * kTextureStride;
    if (m2.Get<uint32_t>(at) != 0)
        return std::string();
    M2Array name = ArrayAt(m2, at + 8);
    if (!ArrayFits(m2, name, 1))
        return std::string();
    return m2.CString(name.offset, name.count);
}

bool Finite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::string Text(Vec3 v)
{
    char text[64];
    std::snprintf(text, sizeof(text), "(%.2f %.2f %.2f)", v.x, v.y, v.z);
    return text;
}

struct Affine
{
    Mat3 linear;
    Vec3 offset;

    Vec3 Apply(Vec3 v) const { return linear * v + offset; }
    Affine Then(const Affine& inner) const { return {linear * inner.linear, linear * inner.offset + offset}; }
};

struct CompQuat
{
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t w;
};

float QuatComponent(int16_t packed)
{
    int value = packed < 0 ? packed + 32768 : packed - 32767;
    return static_cast<float>(value) / 32767.0f;
}

Mat3 Scaling(Vec3 s)
{
    Mat3 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}

class BonePose
{
public:
    explicit BonePose(ByteView m2) : m_m2(m2), m_bones(ArrayAt(m2, kHeaderBones))
    {
        if (!ArrayFits(m2, m_bones, kBoneStride))
            m_bones = {};
        m_rest.resize(m_bones.count);
        m_known.resize(m_bones.count, false);
    }

    uint32_t Count() const { return m_bones.count; }

    std::string Chain(int32_t bone) const
    {
        std::string text;
        for (uint32_t depth = 0; bone >= 0 && static_cast<uint32_t>(bone) < m_bones.count && depth < kMaxBoneDepth;
             ++depth)
        {
            size_t at = m_bones.offset + static_cast<size_t>(bone) * kBoneStride;
            Vec3 pivot = m_m2.Get<Vec3>(at + kBonePivot);
            char line[200];
            std::snprintf(line, sizeof(line), "\n      bone %d flags %x pivot (%.2f %.2f %.2f) keys t%zu r%zu s%zu",
                          bone, m_m2.Get<uint32_t>(at + 4), pivot.x, pivot.y, pivot.z,
                          TrackValues<Vec3>(m_m2, at + kBoneTranslation).size(),
                          TrackValues<CompQuat>(m_m2, at + kBoneRotation).size(),
                          TrackValues<Vec3>(m_m2, at + kBoneScale).size());
            text += line;
            int16_t parent = m_m2.Get<int16_t>(at + kBoneParent);
            if (parent == bone)
                break;
            bone = parent;
        }
        return text;
    }

    Affine Rest(int32_t bone, uint32_t depth = 0)
    {
        if (bone < 0 || static_cast<uint32_t>(bone) >= m_bones.count || depth > kMaxBoneDepth)
            return Affine{};
        if (m_known[bone])
            return m_rest[bone];
        size_t at = m_bones.offset + static_cast<size_t>(bone) * kBoneStride;
        Vec3 pivot = m_m2.Get<Vec3>(at + kBonePivot);
        Vec3 translation = FirstTrackValue<Vec3>(m_m2, at + kBoneTranslation).value_or(Vec3{});
        Vec3 scale = FirstTrackValue<Vec3>(m_m2, at + kBoneScale).value_or(Vec3{1.0f, 1.0f, 1.0f});
        Mat3 rotation;
        if (std::optional<CompQuat> q = FirstTrackValue<CompQuat>(m_m2, at + kBoneRotation))
            rotation = QuaternionMatrix(QuatComponent(q->x), QuatComponent(q->y), QuatComponent(q->z),
                                        QuatComponent(q->w));
        if (!Finite(pivot) || !Finite(translation) || !Finite(scale))
            return Affine{};
        Affine local;
        local.linear = rotation * Scaling(scale);
        local.offset = pivot + translation - local.linear * pivot;
        int16_t parent = m_m2.Get<int16_t>(at + kBoneParent);
        Affine rest = parent == bone ? local : Rest(parent, depth + 1).Then(local);
        m_rest[bone] = rest;
        m_known[bone] = true;
        return rest;
    }

private:
    ByteView m_m2;
    M2Array m_bones;
    std::vector<Affine> m_rest;
    std::vector<bool> m_known;
};

struct Bounds
{
    Vec3 low{1e30f, 1e30f, 1e30f};
    Vec3 high{-1e30f, -1e30f, -1e30f};

    bool Valid() const { return low.x <= high.x; }
};

Bounds VertexBounds(ByteView m2)
{
    Bounds bounds;
    M2Array vertices = ArrayAt(m2, kHeaderVertices);
    if (!ArrayFits(m2, vertices, kVertexStride))
        return bounds;
    for (uint32_t i = 0; i < vertices.count; ++i)
    {
        Vec3 v = m2.Get<Vec3>(vertices.offset + static_cast<size_t>(i) * kVertexStride);
        if (!Finite(v))
            continue;
        bounds.low = {std::min(bounds.low.x, v.x), std::min(bounds.low.y, v.y), std::min(bounds.low.z, v.z)};
        bounds.high = {std::max(bounds.high.x, v.x), std::max(bounds.high.y, v.y), std::max(bounds.high.z, v.z)};
    }
    return bounds;
}

struct Emitter
{
    Vec3 rawPosition;
    Vec3 position;
    uint16_t bone = 0;
    uint8_t type = 0;
    std::string texture;
    uint8_t blend = 0;
    float peakSize = 0.0f;
    float peakRate = 0.0f;
    std::vector<Rgb> colors;
    std::vector<float> alphas;
    std::vector<float> sizes;
    bool valid = false;
};

Emitter ReadEmitter(ByteView m2, size_t at, uint32_t textureCount, BonePose& pose)
{
    Emitter emitter;
    emitter.rawPosition = m2.Get<Vec3>(at + kEmitterPosition);
    emitter.bone = m2.Get<uint16_t>(at + kEmitterBone);
    uint16_t texture = m2.Get<uint16_t>(at + kEmitterTexture);
    emitter.blend = m2.Get<uint8_t>(at + kEmitterBlend);
    emitter.type = m2.Get<uint8_t>(at + kEmitterType);
    emitter.valid = texture < textureCount && emitter.blend <= 7 && emitter.type <= 4 &&
                    Finite(emitter.rawPosition) && std::fabs(emitter.rawPosition.x) < 1000.0f &&
                    std::fabs(emitter.rawPosition.z) < 1000.0f;
    if (!emitter.valid)
        return emitter;
    emitter.position = pose.Rest(emitter.bone).Apply(emitter.rawPosition);
    emitter.texture = TextureName(m2, texture);
    for (float rate : TrackValues<float>(m2, at + kEmitterRate))
        if (std::isfinite(rate))
            emitter.peakRate = std::max(emitter.peakRate, rate);
    for (Vec2 scale : FakeTrackKeys<Vec2>(m2, at + kEmitterScale))
    {
        if (!std::isfinite(scale.x) || scale.x >= 1000.0f)
            continue;
        emitter.peakSize = std::max(emitter.peakSize, scale.x);
        emitter.sizes.push_back(scale.x);
    }
    for (Vec3 color : FakeTrackKeys<Vec3>(m2, at + kEmitterColor))
        if (Finite(color))
            emitter.colors.push_back({color.x / 255.0f, color.y / 255.0f, color.z / 255.0f});
    for (uint16_t alpha : FakeTrackKeys<uint16_t>(m2, at + kEmitterAlpha))
        emitter.alphas.push_back(static_cast<float>(alpha) / 32767.0f);
    return emitter;
}

bool FireEmitter(const Emitter& emitter)
{
    return emitter.valid && FireTexture(emitter.texture) &&
           (emitter.blend == kBlendAdd || emitter.blend == kBlendNoAlphaAdd) && emitter.peakSize >= kMinFlameSize &&
           emitter.peakRate > 0.0f;
}

float FireStrength(const Emitter& emitter)
{
    return emitter.peakSize * std::sqrt(std::sqrt(std::max(emitter.peakRate, 0.0f)));
}

struct Batch
{
    std::string texture;
    bool unlit = false;
    bool blended = false;
    std::vector<Vec3> positions;
};

std::vector<Batch> ReadBatches(ByteView m2, ByteView skin)
{
    std::vector<Batch> batches;
    if (skin.Size() < 0x30 || skin.Get<uint32_t>(0) != kSkinMagic)
        return batches;
    M2Array skinVertices = ArrayAt(skin, kSkinVertices);
    M2Array submeshes = ArrayAt(skin, kSkinSubmeshes);
    M2Array skinBatches = ArrayAt(skin, kSkinBatches);
    M2Array vertices = ArrayAt(m2, kHeaderVertices);
    M2Array materials = ArrayAt(m2, kHeaderRenderFlags);
    M2Array lookup = ArrayAt(m2, kHeaderTextureLookup);
    if (!ArrayFits(skin, skinVertices, 2) || !ArrayFits(skin, submeshes, kSubmeshStride) ||
        !ArrayFits(skin, skinBatches, kBatchStride) || !ArrayFits(m2, vertices, kVertexStride) ||
        !ArrayFits(m2, materials, kRenderFlagStride) || !ArrayFits(m2, lookup, 2))
        return batches;
    for (uint32_t i = 0; i < skinBatches.count; ++i)
    {
        size_t at = skinBatches.offset + static_cast<size_t>(i) * kBatchStride;
        uint16_t submeshIndex = skin.Get<uint16_t>(at + kBatchSubmesh);
        uint16_t materialIndex = skin.Get<uint16_t>(at + kBatchMaterial);
        uint16_t textureCount = skin.Get<uint16_t>(at + kBatchTextureCount);
        uint16_t textureCombo = skin.Get<uint16_t>(at + kBatchTextureCombo);
        if (submeshIndex >= submeshes.count || materialIndex >= materials.count)
            continue;
        Batch batch;
        size_t material = materials.offset + static_cast<size_t>(materialIndex) * kRenderFlagStride;
        uint16_t materialFlags = m2.Get<uint16_t>(material);
        uint16_t blend = m2.Get<uint16_t>(material + 2);
        batch.unlit = (materialFlags & kMaterialUnlit) != 0;
        batch.blended = blend == 2 || blend == 3 || blend == 4 || blend == 7;
        if (textureCount > 0 && textureCombo < lookup.count)
            batch.texture = TextureName(m2, m2.Get<uint16_t>(lookup.offset + static_cast<size_t>(textureCombo) * 2));
        size_t submesh = submeshes.offset + static_cast<size_t>(submeshIndex) * kSubmeshStride;
        uint32_t start = skin.Get<uint16_t>(submesh + kSubmeshVertexStart) +
                         (static_cast<uint32_t>(skin.Get<uint16_t>(submesh + kSubmeshLevel)) << 16);
        uint32_t count = skin.Get<uint16_t>(submesh + kSubmeshVertexCount);
        for (uint32_t v = 0; v < count && start + v < skinVertices.count; ++v)
        {
            uint16_t global = skin.Get<uint16_t>(skinVertices.offset + static_cast<size_t>(start + v) * 2);
            if (global < vertices.count)
                batch.positions.push_back(m2.Get<Vec3>(vertices.offset + static_cast<size_t>(global) * kVertexStride));
        }
        batches.push_back(std::move(batch));
    }
    return batches;
}

Vec3 Centroid(const std::vector<Vec3>& points)
{
    Vec3 sum;
    for (Vec3 p : points)
        sum = sum + p;
    return points.empty() ? sum : sum * (1.0f / static_cast<float>(points.size()));
}

const Batch* LargestUnlitBatch(const std::vector<Batch>& batches, bool glowOnly)
{
    const Batch* best = nullptr;
    for (const Batch& batch : batches)
    {
        if (!batch.unlit || batch.positions.empty() || (glowOnly && !GlowTexture(batch.texture)))
            continue;
        if (!best || batch.positions.size() > best->positions.size())
            best = &batch;
    }
    return best;
}

lampfile::LampKind KindOf(std::optional<NameKind> nameKind, float intensity)
{
    return nameKind ? LampKindOf(nameKind) : KindFromIntensity(intensity);
}

void AddAuthoredLights(ByteView m2, M2Array lights, const std::string& path, bool hasFire,
                       std::optional<NameKind> nameKind, BonePose& pose, ModelLights& out, std::ostream* report)
{
    for (uint32_t i = 0; i < lights.count; ++i)
    {
        size_t at = lights.offset + static_cast<size_t>(i) * kLightStride;
        uint16_t type = m2.Get<uint16_t>(at);
        if (type == kLightTypeDirectional || type == kLightTypePoint)
            ++out.authoredBlocks;
        if (type != kLightTypePoint)
            continue;
        std::optional<uint8_t> visible = FirstTrackValue<uint8_t>(m2, at + kLightVisibility);
        std::optional<float> intensity = FirstTrackValue<float>(m2, at + kLightDiffuseIntensity);
        std::optional<Vec3> color = FirstTrackValue<Vec3>(m2, at + kLightDiffuseColor);
        if ((visible && *visible == 0) || !intensity || !std::isfinite(*intensity) || *intensity < kMinLightIntensity)
            continue;
        ModelLight light;
        Vec3 raw = m2.Get<Vec3>(at + kLightPosition);
        if (!Finite(raw))
            continue;
        int16_t bone = m2.Get<int16_t>(at + kLightBone);
        light.position = pose.Rest(bone).Apply(raw);
        if (report)
            *report << "  light " << i << " bone " << bone << " raw " << Text(raw) << " posed " << Text(light.position)
                    << pose.Chain(bone) << "\n";
        light.intensity = std::min(*intensity, 4.0f);
        light.reach = ReachFromIntensity(light.intensity);
        light.kind = KindOf(nameKind, light.intensity);
        light.color = SourceColor(path, light.kind,
                                  color ? std::optional<Rgb>(Rgb{color->x, color->y, color->z}) : std::nullopt);
        light.flags = static_cast<uint16_t>(lampfile::kLampAuthored | (hasFire ? lampfile::kLampFlame : 0));
        out.lights.push_back(light);
    }
}

bool FarOutsideModel(const Bounds& bounds, Vec3 p)
{
    return bounds.Valid() &&
           (p.x < bounds.low.x - kClampMargin || p.y < bounds.low.y - kClampMargin ||
            p.z < bounds.low.z - kClampMargin || p.x > bounds.high.x + kClampMargin ||
            p.y > bounds.high.y + kClampMargin || p.z > bounds.high.z + kClampMargin);
}

void ClampFlamesIntoModel(const Bounds& bounds, ModelLights& result)
{
    for (ModelLight& light : result.lights)
    {
        Vec3& p = light.position;
        if (!FarOutsideModel(bounds, p))
            continue;
        p = {std::clamp(p.x, bounds.low.x, bounds.high.x), std::clamp(p.y, bounds.low.y, bounds.high.y),
             std::clamp(p.z, bounds.low.z, bounds.high.z)};
        light.flags |= lampfile::kLampClampedToModel;
        ++result.clampedLights;
    }
}

void DropRigLights(const Bounds& bounds, ModelLights& result)
{
    size_t before = result.lights.size();
    result.lights.erase(std::remove_if(result.lights.begin(), result.lights.end(),
                                       [&](const ModelLight& l) { return FarOutsideModel(bounds, l.position); }),
                        result.lights.end());
    result.rigLights += static_cast<uint32_t>(before - result.lights.size());
}

std::string KeyText(const Emitter& emitter)
{
    std::string text;
    char key[80];
    for (const Rgb& c : emitter.colors)
    {
        std::snprintf(key, sizeof(key), " rgb(%.2f %.2f %.2f)", c.r, c.g, c.b);
        text += key;
    }
    for (float a : emitter.alphas)
    {
        std::snprintf(key, sizeof(key), " a%.2f", a);
        text += key;
    }
    for (float s : emitter.sizes)
    {
        std::snprintf(key, sizeof(key), " s%.2f", s);
        text += key;
    }
    return text;
}

void Summarise(const ModelLights& result, std::ostream* report)
{
    if (!report)
        return;
    for (const ModelLight& light : result.lights)
        *report << "  light at " << Text(light.position) << " rgb "
                << Text({light.color.r, light.color.g, light.color.b})
                << " intensity " << light.intensity << " reach " << light.reach << " kind "
                << static_cast<int>(light.kind) << " flags " << light.flags << "\n";
}

}

const char* RouteName(ModelRoute route)
{
    switch (route)
    {
    case ModelRoute::Missing:
        return "missing";
    case ModelRoute::Unreadable:
        return "unreadable";
    case ModelRoute::NoLight:
        return "none";
    case ModelRoute::AuthoredDark:
        return "authored-dark";
    case ModelRoute::Authored:
        return "authored";
    case ModelRoute::Flame:
        return "flame";
    case ModelRoute::Name:
        return "name";
    case ModelRoute::Emissive:
        return "emissive";
    }
    return "?";
}

std::string M2PathOf(std::string_view name)
{
    std::string path = UpperPath(name);
    for (std::string_view old : {std::string_view(".MDX"), std::string_view(".MDL")})
        if (path.size() > 4 && path.compare(path.size() - 4, 4, old) == 0)
            path.replace(path.size() - 4, 4, ".M2");
    return path;
}

const ModelLights& ModelLightCatalogue::Of(const std::string& modelName)
{
    std::string path = M2PathOf(modelName);
    auto found = m_models.find(path);
    if (found != m_models.end())
        return found->second;
    return m_models.emplace(path, Analyse(path)).first->second;
}

void ModelLightCatalogue::Prepare(const std::vector<std::string>& modelNames,
                                  const std::vector<const ArchiveSet*>& readers)
{
    std::vector<std::string> paths;
    std::unordered_set<std::string> wanted;
    for (const std::string& name : modelNames)
    {
        if (name.empty())
            continue;
        std::string path = M2PathOf(name);
        if (m_models.count(path) == 0 && wanted.insert(path).second)
            paths.push_back(std::move(path));
    }
    std::vector<ModelLights> found(paths.size());
    ParallelFor(paths.size(), readers.size(), [&](size_t i, size_t worker) {
        found[i] = ModelLightCatalogue(*readers[worker]).Analyse(paths[i]);
    });
    for (size_t i = 0; i < paths.size(); ++i)
        m_models.emplace(paths[i], std::move(found[i]));
}

ModelLights ModelLightCatalogue::Analyse(const std::string& path, std::ostream* report) const
{
    ModelLights result;
    std::optional<std::vector<uint8_t>> file = m_archives.Read(path);
    if (!file)
    {
        if (report)
            *report << path << ": not found\n";
        return result;
    }
    ByteView m2(*file);
    uint32_t version = m2.Get<uint32_t>(kHeaderVersion);
    if (m2.Size() < kHeaderSize || std::memcmp(file->data(), "MD20", 4) != 0 || version < 260 || version > 264)
    {
        result.route = ModelRoute::Unreadable;
        if (report)
            *report << path << ": unreadable\n";
        return result;
    }
    result.route = ModelRoute::NoLight;
    if (IsSpellPath(path))
        return result;

    BonePose pose(m2);
    Bounds bounds = VertexBounds(m2);
    std::optional<NameKind> nameKind = LampNameKind(path);
    if (report)
    {
        Vec3 low = m2.Get<Vec3>(kHeaderBoundsMin);
        Vec3 high = m2.Get<Vec3>(kHeaderBoundsMax);
        *report << path << "\n  version " << version << ", bones " << pose.Count() << "\n  header box "
                << Text(low) << " .. " << Text(high) << "\n  vertex box " << Text(bounds.low) << " .. "
                << Text(bounds.high) << "\n";
    }

    M2Array textures = ArrayAt(m2, kHeaderTextures);
    M2Array emitterArray = ArrayAt(m2, kHeaderEmitters);
    std::vector<Emitter> emitters;
    if (ArrayFits(m2, emitterArray, kEmitterStride))
    {
        for (uint32_t i = 0; i < emitterArray.count; ++i)
        {
            Emitter emitter = ReadEmitter(m2, emitterArray.offset + static_cast<size_t>(i) * kEmitterStride,
                                          textures.count, pose);
            ++result.emitters;
            if (!emitter.valid)
                ++result.invalidEmitters;
            if (FireEmitter(emitter))
                ++result.fireEmitters;
            if (report)
                *report << "  emitter " << i << (emitter.valid ? "" : " (invalid)") << " bone " << emitter.bone
                        << " raw " << Text(emitter.rawPosition) << " posed " << Text(emitter.position) << " blend "
                        << int(emitter.blend) << " type " << int(emitter.type) << " size " << emitter.peakSize
                        << " rate " << emitter.peakRate << " colours " << emitter.colors.size()
                        << (FireEmitter(emitter) ? " FIRE strength " + std::to_string(FireStrength(emitter)) : "")
                        << "  " << emitter.texture << pose.Chain(emitter.bone) << "\n      keys" << KeyText(emitter)
                        << "\n";
            emitters.push_back(std::move(emitter));
        }
    }

    M2Array lights = ArrayAt(m2, kHeaderLights);
    if (ArrayFits(m2, lights, kLightStride) && lights.count > 0)
    {
        AddAuthoredLights(m2, lights, path, result.fireEmitters > 0, nameKind, pose, result, report);
        if (report)
            *report << "  light blocks " << lights.count << ", emitting point lights " << result.lights.size()
                    << "\n";
        if (result.authoredBlocks > 0)
        {
            DropRigLights(bounds, result);
            result.route = result.lights.empty() ? ModelRoute::AuthoredDark : ModelRoute::Authored;
            Summarise(result, report);
            return result;
        }
    }

    const Emitter* bestFire = nullptr;
    for (const Emitter& emitter : emitters)
    {
        if (!FireEmitter(emitter))
            continue;
        float strength = FireStrength(emitter);
        if (!bestFire || strength > result.bestFireStrength)
        {
            bestFire = &emitter;
            result.bestFireStrength = strength;
        }
    }
    if (bestFire)
    {
        ModelLight light;
        light.position = bestFire->position;
        light.intensity = FireIntensity(result.bestFireStrength);
        light.reach = ReachFromIntensity(light.intensity);
        light.kind = KindOf(nameKind, light.intensity);
        light.color = SourceColor(path, light.kind,
                                  MostVisibleKey(bestFire->colors, bestFire->alphas, bestFire->sizes));
        light.flags = lampfile::kLampFlame;
        result.lights.push_back(light);
        result.route = ModelRoute::Flame;
        if (bestFire->peakSize < kMaxClampedFlameSize)
            ClampFlamesIntoModel(bounds, result);
        Summarise(result, report);
        return result;
    }

    if (IsBrokenOrOff(path) || IsLightBeam(path) || !LampFamilyOk(path))
        return result;
    bool dirHit = !nameKind && InLampDir(path);
    if (!nameKind && !dirHit)
        return result;

    std::string skinPath = path.substr(0, path.size() - 3) + "00.SKIN";
    std::optional<std::vector<uint8_t>> skinFile = m_archives.Read(skinPath);
    std::vector<Batch> batches = skinFile ? ReadBatches(m2, ByteView(*skinFile)) : std::vector<Batch>();
    result.batches = static_cast<uint32_t>(batches.size());
    for (const Batch& batch : batches)
    {
        if (batch.unlit)
            ++result.unlitBatches;
        if (report)
            *report << "  batch " << (batch.unlit ? "unlit" : "lit") << (batch.blended ? " blended" : "")
                    << " vertices " << batch.positions.size() << " centre " << Text(Centroid(batch.positions))
                    << "  " << batch.texture << "\n";
    }

    if (dirHit)
    {
        bool glowGlass = std::any_of(batches.begin(), batches.end(), [](const Batch& batch) {
            return batch.unlit && batch.blended && GlowTexture(batch.texture);
        });
        if (!glowGlass)
            return result;
    }

    ModelLight light;
    const Batch* glass = LargestUnlitBatch(batches, true);
    if (!glass)
        glass = LargestUnlitBatch(batches, false);
    if (glass)
    {
        light.position = Centroid(glass->positions);
    }
    else
    {
        if ((nameKind && NameKindIsFire(*nameKind)) || !bounds.Valid())
            return result;
        light.position = {(bounds.low.x + bounds.high.x) * 0.5f, (bounds.low.y + bounds.high.y) * 0.5f,
                          bounds.high.z};
        light.flags |= lampfile::kLampBoundsFallback;
    }
    const Emitter* glow = nullptr;
    float glowVisibility = 0.0f;
    for (const Emitter& emitter : emitters)
    {
        float visibility = emitter.valid ? KeyVisibility(emitter.colors, emitter.alphas, emitter.sizes) : 0.0f;
        if (visibility > glowVisibility)
        {
            glow = &emitter;
            glowVisibility = visibility;
        }
    }
    light.color = GlowLampColor(path, glow ? MostVisibleKey(glow->colors, glow->alphas, glow->sizes) : std::nullopt);
    light.intensity = LampIntensity(nameKind);
    light.reach = ReachFromIntensity(light.intensity);
    light.kind = LampKindOf(nameKind);
    light.flags |= nameKind ? lampfile::kLampNameRoute : lampfile::kLampEmissiveRoute;
    result.lights.push_back(light);
    result.route = nameKind ? ModelRoute::Name : ModelRoute::Emissive;
    Summarise(result, report);
    return result;
}

void ModelLightCatalogue::Describe(const std::string& modelName, std::ostream& report) const
{
    std::string path = M2PathOf(modelName);
    ModelLights result = Analyse(path, &report);
    report << "  route " << RouteName(result.route) << "\n  in";
    for (const std::string& holder : m_archives.Holders(path))
        report << " " << holder;
    report << "\n";
}
