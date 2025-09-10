#pragma once
#include "vec3.h"
#include "vec4.h"
#include "mat3.h"

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

    static Mat4 fromQuaternion(float x, float y, float z, float w) {
        Mat4 mat;

        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float xw = x * w;
        float yz = y * z;
        float yw = y * w;
        float zw = z * w;

        mat.m[0][0] = 1.0f - 2.0f * (yy + zz);
        mat.m[0][1] = 2.0f * (xy + zw);
        mat.m[0][2] = 2.0f * (xz - yw);
        mat.m[0][3] = 0.0f;

        mat.m[1][0] = 2.0f * (xy - zw);
        mat.m[1][1] = 1.0f - 2.0f * (xx + zz);
        mat.m[1][2] = 2.0f * (yz + xw);
        mat.m[1][3] = 0.0f;

        mat.m[2][0] = 2.0f * (xz + yw);
        mat.m[2][1] = 2.0f * (yz - xw);
        mat.m[2][2] = 1.0f - 2.0f * (xx + yy);
        mat.m[2][3] = 0.0f;

        mat.m[3][0] = 0.0f;
        mat.m[3][1] = 0.0f;
        mat.m[3][2] = 0.0f;
        mat.m[3][3] = 1.0f;

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

    static Mat4 fromGLTF(const double* matrix) {
        Mat4 result;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                // transpose
                result.m[col][row] = static_cast<float>(matrix[row + col * 4]);
            }
        }
        return result;
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

    Vec4 operator*(const Vec4& v) const {
        return Vec4(
            m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
            m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
            m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
            m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w
        );
    }

    // Transform Vec3 (assumes w = 1)
    Vec3 transformPoint(const Vec3& v) const {
        Vec4 result = *this * Vec4(v, 1.0f);
        if (result.w != 0.0f) {
            return Vec3(result.x / result.w, result.y / result.w, result.z / result.w);
        }
        return Vec3(result.x, result.y, result.z);
    }

    // Transform Vec3 as vector (assumes w = 0)
    Vec3 transformVector(const Vec3& v) const {
        Vec4 result = *this * Vec4(v, 0.0f);
        return Vec3(result.x, result.y, result.z);
    }

    // Extract the 3x3 part of the matrix
    Mat3 toMat3() const {
        Mat3 result;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                result.m[i][j] = m[i][j];
            }
        }
        return result;
    }
};