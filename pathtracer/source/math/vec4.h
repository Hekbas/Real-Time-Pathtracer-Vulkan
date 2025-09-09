#pragma once
#include "vec3.h"

struct Vec4 {
    float x, y, z, w;

    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    Vec4 operator+() const { return *this; }
    Vec4 operator-() const { return { -x, -y, -z, -w }; }

    Vec4 operator+(const Vec4& other) const { return { x + other.x, y + other.y, z + other.z, w + other.w }; }
    Vec4 operator-(const Vec4& other) const { return { x - other.x, y - other.y, z - other.z, w - other.w }; }

    Vec4 operator*(float scalar) const { return { x * scalar, y * scalar, z * scalar, w * scalar }; }
    Vec4 operator/(float scalar) const { return { x / scalar, y / scalar, z / scalar, w / scalar }; }

    Vec4& operator+=(const Vec4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    Vec4& operator-=(const Vec4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
    Vec4& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this; }
    Vec4& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; w /= scalar; return *this; }

    bool operator==(const Vec4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
    bool operator!=(const Vec4& other) const { return !(*this == other); }

    float lengthSquared() const { return x * x + y * y + z * z + w * w; }
    float length() const { return std::sqrt(lengthSquared()); }

    Vec3 xyz() const { return Vec3(x, y, z); }
};

inline Vec4 operator*(float scalar, const Vec4& v) {
    return { v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar };
}

inline float dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline Vec4 normalize(const Vec4& v) {
    float len = v.length();
    if (len > 0.0f) return v / len;
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}