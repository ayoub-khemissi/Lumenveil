#pragma once

#include <cmath>

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }

inline Vec3 Normalized(Vec3 a)
{
    float length = Length(a);
    return length > 1e-8f ? a * (1.0f / length) : Vec3{0.0f, 0.0f, 1.0f};
}

struct Mat3
{
    float m[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    Vec3 operator*(Vec3 v) const
    {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }

    Mat3 operator*(const Mat3& o) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
};

inline float Radians(float degrees) { return degrees * 0.017453292519943295f; }

inline Mat3 RotationX(float radians)
{
    float c = std::cos(radians), s = std::sin(radians);
    Mat3 r;
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
}

inline Mat3 RotationY(float radians)
{
    float c = std::cos(radians), s = std::sin(radians);
    Mat3 r;
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
}

inline Mat3 RotationZ(float radians)
{
    float c = std::cos(radians), s = std::sin(radians);
    Mat3 r;
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    return r;
}

inline Mat3 QuaternionMatrix(float x, float y, float z, float w)
{
    float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n < 1e-6f)
        return Mat3{};
    x /= n;
    y /= n;
    z /= n;
    w /= n;
    Mat3 r;
    r.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    r.m[0][1] = 2.0f * (x * y - z * w);
    r.m[0][2] = 2.0f * (x * z + y * w);
    r.m[1][0] = 2.0f * (x * y + z * w);
    r.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    r.m[1][2] = 2.0f * (y * z - x * w);
    r.m[2][0] = 2.0f * (x * z - y * w);
    r.m[2][1] = 2.0f * (y * z + x * w);
    r.m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    return r;
}

struct Placement
{
    Vec3 origin;
    Mat3 rotation;
    float scale = 1.0f;

    Vec3 Point(Vec3 local) const { return origin + rotation * (local * scale); }
    Vec3 Direction(Vec3 local) const { return rotation * local; }

    Placement Then(const Placement& child) const
    {
        Placement combined;
        combined.origin = Point(child.origin);
        combined.rotation = rotation * child.rotation;
        combined.scale = scale * child.scale;
        return combined;
    }
};

constexpr float kMapHalfExtent = 17066.666f;

inline Mat3 FlipXY()
{
    Mat3 r;
    r.m[0][0] = -1.0f;
    r.m[1][1] = -1.0f;
    return r;
}

inline Placement AdtPlacement(Vec3 position, Vec3 rotationDegrees, float scale, bool globalWmo)
{
    Vec3 internal{position.z, position.x, position.y};
    if (globalWmo)
        internal = internal + Vec3{kMapHalfExtent, kMapHalfExtent, 0.0f};
    Mat3 internalRotation = RotationZ(Radians(rotationDegrees.y)) * RotationY(Radians(rotationDegrees.x)) *
                            RotationX(Radians(rotationDegrees.z));
    Placement placement;
    placement.origin = FlipXY() * internal + Vec3{kMapHalfExtent, kMapHalfExtent, 0.0f};
    placement.rotation = FlipXY() * internalRotation;
    placement.scale = scale;
    return placement;
}
