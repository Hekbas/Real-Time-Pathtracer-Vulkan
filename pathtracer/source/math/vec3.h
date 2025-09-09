#pragma once
#include <cmath>

struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x) : x(x), y(x), z(x) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+() const { return *this; }
    Vec3 operator-() const { return { -x, -y, -z }; }

    Vec3 operator+(const Vec3& other) const { return { x + other.x, y + other.y, z + other.z }; }
    Vec3 operator-(const Vec3& other) const { return { x - other.x, y - other.y, z - other.z }; }

    Vec3 operator*(float scalar) const { return { x * scalar, y * scalar, z * scalar }; }
    Vec3 operator/(float scalar) const { return { x / scalar, y / scalar, z / scalar }; }

    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vec3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

    bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const Vec3& other) const { return !(*this == other); }

    float lengthSquared() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSquared()); }

    float dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

    Vec3 cross(const Vec3& other) const {
        return Vec3(y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x);
    }

    Vec3 normalized() const {
        float len = length();
        if (len > 0.0f) return *this / len;
        return Vec3(0.0f, 0.0f, 0.0f);
    }
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}

inline Vec3 normalize(const Vec3& v) {
    float len = v.length();
    if (len > 0.0f) return v / len;
    return { 0.0f, 0.0f, 0.0f };
}

inline Vec3 operator*(float scalar, const Vec3& v) {
    return { v.x * scalar, v.y * scalar, v.z * scalar };
}