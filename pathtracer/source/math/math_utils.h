#pragma once

constexpr float PI = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * PI / 180.0f; }
inline float degrees(float radians) { return radians * 180.0f / PI; }