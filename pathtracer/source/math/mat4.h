#pragma once
#include <cmath>
#include "vec3.h"

struct Mat4 {
    float m[4][4];

    Mat4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = 0.0f;
    }

    static Mat4 identity() {
        Mat4 mat;
        for (int i = 0; i < 4; i++) mat.m[i][i] = 1.0f;
        return mat;
    }

    static Mat4 translate(const Vec3& t) {
        Mat4 mat = Mat4::identity();
        mat.m[3][0] = t.x;
        mat.m[3][1] = t.y;
        mat.m[3][2] = t.z;
        return mat;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 mat = Mat4::identity();
        mat.m[0][0] = s.x;
        mat.m[1][1] = s.y;
        mat.m[2][2] = s.z;
        return mat;
    }

    static Mat4 rotateX(float angleRad) {
        Mat4 mat = Mat4::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[1][1] = c;  mat.m[1][2] = s;
        mat.m[2][1] = -s; mat.m[2][2] = c;
        return mat;
    }

    static Mat4 rotateY(float angleRad) {
        Mat4 mat = Mat4::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[0][0] = c;  mat.m[0][2] = -s;
        mat.m[2][0] = s;  mat.m[2][2] = c;
        return mat;
    }

    static Mat4 rotateZ(float angleRad) {
        Mat4 mat = Mat4::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[0][0] = c;  mat.m[0][1] = s;
        mat.m[1][0] = -s; mat.m[1][1] = c;
        return mat;
    }

    static Mat4 perspective(float fovRad, float aspect, float near, float far) {
        Mat4 mat;
        float f = 1.0f / std::tan(fovRad * 0.5f);
        mat.m[0][0] = f / aspect;
        mat.m[1][1] = f;
        mat.m[2][2] = (far + near) / (near - far);
        mat.m[2][3] = -1.0f;
        mat.m[3][2] = (2.0f * far * near) / (near - far);
        return mat;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        Mat4 mat = Mat4::identity();
        mat.m[0][0] = 2.0f / (right - left);
        mat.m[1][1] = 2.0f / (top - bottom);
        mat.m[2][2] = -2.0f / (far - near);
        mat.m[3][0] = -(right + left) / (right - left);
        mat.m[3][1] = -(top + bottom) / (top - bottom);
        mat.m[3][2] = -(far + near) / (far - near);
        return mat;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);

        Mat4 mat = Mat4::identity();
        mat.m[0][0] = s.x; mat.m[1][0] = s.y; mat.m[2][0] = s.z;
        mat.m[0][1] = u.x; mat.m[1][1] = u.y; mat.m[2][1] = u.z;
        mat.m[0][2] = -f.x; mat.m[1][2] = -f.y; mat.m[2][2] = -f.z;
        mat.m[3][0] = -dot(s, eye);
        mat.m[3][1] = -dot(u, eye);
        mat.m[3][2] = dot(f, eye);
        return mat;
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result.m[i][j] = 0.0f;
                for (int k = 0; k < 4; k++)
                    result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
        return result;
    }

    // Transform Vec3 (assumes w = 1)
    Vec3 transformPoint(const Vec3& v) const {
        float x = v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0];
        float y = v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1];
        float z = v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2];
        float w = v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + m[3][3];
        if (w != 0.0f) { x /= w; y /= w; z /= w; }
        return { x, y, z };
    }

    // General 4x4 inverse (not the fastest, but works :3)
    Mat4 inverse() const {
        Mat4 inv;
        const float* a = &m[0][0];
        float invOut[16];

        invOut[0] = a[5] * a[10] * a[15] -
            a[5] * a[11] * a[14] -
            a[9] * a[6] * a[15] +
            a[9] * a[7] * a[14] +
            a[13] * a[6] * a[11] -
            a[13] * a[7] * a[10];

        invOut[4] = -a[4] * a[10] * a[15] +
            a[4] * a[11] * a[14] +
            a[8] * a[6] * a[15] -
            a[8] * a[7] * a[14] -
            a[12] * a[6] * a[11] +
            a[12] * a[7] * a[10];

        invOut[8] = a[4] * a[9] * a[15] -
            a[4] * a[11] * a[13] -
            a[8] * a[5] * a[15] +
            a[8] * a[7] * a[13] +
            a[12] * a[5] * a[11] -
            a[12] * a[7] * a[9];

        invOut[12] = -a[4] * a[9] * a[14] +
            a[4] * a[10] * a[13] +
            a[8] * a[5] * a[14] -
            a[8] * a[6] * a[13] -
            a[12] * a[5] * a[10] +
            a[12] * a[6] * a[9];

        invOut[1] = -a[1] * a[10] * a[15] +
            a[1] * a[11] * a[14] +
            a[9] * a[2] * a[15] -
            a[9] * a[3] * a[14] -
            a[13] * a[2] * a[11] +
            a[13] * a[3] * a[10];

        invOut[5] = a[0] * a[10] * a[15] -
            a[0] * a[11] * a[14] -
            a[8] * a[2] * a[15] +
            a[8] * a[3] * a[14] +
            a[12] * a[2] * a[11] -
            a[12] * a[3] * a[10];

        invOut[9] = -a[0] * a[9] * a[15] +
            a[0] * a[11] * a[13] +
            a[8] * a[1] * a[15] -
            a[8] * a[3] * a[13] -
            a[12] * a[1] * a[11] +
            a[12] * a[3] * a[9];

        invOut[13] = a[0] * a[9] * a[14] -
            a[0] * a[10] * a[13] -
            a[8] * a[1] * a[14] +
            a[8] * a[2] * a[13] +
            a[12] * a[1] * a[10] -
            a[12] * a[2] * a[9];

        invOut[2] = a[1] * a[6] * a[15] -
            a[1] * a[7] * a[14] -
            a[5] * a[2] * a[15] +
            a[5] * a[3] * a[14] +
            a[13] * a[2] * a[7] -
            a[13] * a[3] * a[6];

        invOut[6] = -a[0] * a[6] * a[15] +
            a[0] * a[7] * a[14] +
            a[4] * a[2] * a[15] -
            a[4] * a[3] * a[14] -
            a[12] * a[2] * a[7] +
            a[12] * a[3] * a[6];

        invOut[10] = a[0] * a[5] * a[15] -
            a[0] * a[7] * a[13] -
            a[4] * a[1] * a[15] +
            a[4] * a[3] * a[13] +
            a[12] * a[1] * a[7] -
            a[12] * a[3] * a[5];

        invOut[14] = -a[0] * a[5] * a[14] +
            a[0] * a[6] * a[13] +
            a[4] * a[1] * a[14] -
            a[4] * a[2] * a[13] -
            a[12] * a[1] * a[6] +
            a[12] * a[2] * a[5];

        invOut[3] = -a[1] * a[6] * a[11] +
            a[1] * a[7] * a[10] +
            a[5] * a[2] * a[11] -
            a[5] * a[3] * a[10] -
            a[9] * a[2] * a[7] +
            a[9] * a[3] * a[6];

        invOut[7] = a[0] * a[6] * a[11] -
            a[0] * a[7] * a[10] -
            a[4] * a[2] * a[11] +
            a[4] * a[3] * a[10] +
            a[8] * a[2] * a[7] -
            a[8] * a[3] * a[6];

        invOut[11] = -a[0] * a[5] * a[11] +
            a[0] * a[7] * a[9] +
            a[4] * a[1] * a[11] -
            a[4] * a[3] * a[9] -
            a[8] * a[1] * a[7] +
            a[8] * a[3] * a[5];

        invOut[15] = a[0] * a[5] * a[10] -
            a[0] * a[6] * a[9] -
            a[4] * a[1] * a[10] +
            a[4] * a[2] * a[9] +
            a[8] * a[1] * a[6] -
            a[8] * a[2] * a[5];

        float det = a[0] * invOut[0] + a[1] * invOut[4] + a[2] * invOut[8] + a[3] * invOut[12];
        if (det == 0.0f) return Mat4::identity(); // fallback

        det = 1.0f / det;
        for (int i = 0; i < 16; i++) invOut[i] *= det;

        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                inv.m[i][j] = invOut[i * 4 + j];

        return inv;
    }
};