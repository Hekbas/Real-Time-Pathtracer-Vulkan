#pragma once

#include <cmath>
#include <cassert>

struct Vec2 {
    float x, y;

    Vec2() : x(0), y(0) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    // element access
    float& operator[](int i) { assert(i >= 0 && i < 2); return (&x)[i]; }
    const float& operator[](int i) const { assert(i >= 0 && i < 2); return (&x)[i]; }

    // arithmetic
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(float s) const { return Vec2(x / s, y / s); }

    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vec2& v) const { return !(*this == v); }
};

// scalar * vec2
inline Vec2 operator*(float s, const Vec2& v) { return Vec2(v.x * s, v.y * s); }

// dot product
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

// length
inline float length(const Vec2& v) { return std::sqrt(dot(v, v)); }

// normalization
inline Vec2 normalize(const Vec2& v) {
    float len = length(v);
    return (len > 1e-8f) ? (v / len) : Vec2(0, 0);
}
