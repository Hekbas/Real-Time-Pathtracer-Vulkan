#pragma once

#include <cmath>
#include "vec3.h"

struct Mat3 {
    float m[3][3];

    Mat3() {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                m[i][j] = 0.0f;
    }

    static Mat3 identity() {
        Mat3 mat;
        for (int i = 0; i < 3; i++) mat.m[i][i] = 1.0f;
        return mat;
    }

    static Mat3 scale(const Vec3& s) {
        Mat3 mat = Mat3::identity();
        mat.m[0][0] = s.x;
        mat.m[1][1] = s.y;
        mat.m[2][2] = s.z;
        return mat;
    }

    static Mat3 scale(float s) {
        Mat3 mat = Mat3::identity();
        mat.m[0][0] = s;
        mat.m[1][1] = s;
        mat.m[2][2] = s;
        return mat;
    }

    static Mat3 rotateX(float angleRad) {
        Mat3 mat = Mat3::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[1][1] = c;  mat.m[1][2] = s;
        mat.m[2][1] = -s; mat.m[2][2] = c;
        return mat;
    }

    static Mat3 rotateY(float angleRad) {
        Mat3 mat = Mat3::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[0][0] = c;  mat.m[0][2] = -s;
        mat.m[2][0] = s;  mat.m[2][2] = c;
        return mat;
    }

    static Mat3 rotateZ(float angleRad) {
        Mat3 mat = Mat3::identity();
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        mat.m[0][0] = c;  mat.m[0][1] = s;
        mat.m[1][0] = -s; mat.m[1][1] = c;
        return mat;
    }

    // Create rotation matrix from axis and angle
    static Mat3 rotate(float angleRad, const Vec3& axis) {
        Mat3 mat;
        Vec3 n = normalize(axis);
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        float t = 1.0f - c;

        mat.m[0][0] = t * n.x * n.x + c;
        mat.m[0][1] = t * n.x * n.y + s * n.z;
        mat.m[0][2] = t * n.x * n.z - s * n.y;

        mat.m[1][0] = t * n.x * n.y - s * n.z;
        mat.m[1][1] = t * n.y * n.y + c;
        mat.m[1][2] = t * n.y * n.z + s * n.x;

        mat.m[2][0] = t * n.x * n.z + s * n.y;
        mat.m[2][1] = t * n.y * n.z - s * n.x;
        mat.m[2][2] = t * n.z * n.z + c;

        return mat;
    }

    // Transpose the matrix
    Mat3 transpose() const {
        Mat3 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[j][i];
        return result;
    }

    // Matrix determinant
    float determinant() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
            m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
            m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    // Matrix inverse (returns identity if matrix is singular)
    Mat3 inverse() const {
        Mat3 inv;
        float det = determinant();

        if (std::fabs(det) < 1e-8f) {
            return Mat3::identity(); // Return identity if matrix is singular
        }

        float invDet = 1.0f / det;

        inv.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
        inv.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet;
        inv.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;

        inv.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invDet;
        inv.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
        inv.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet;

        inv.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
        inv.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet;
        inv.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;

        return inv;
    }

    // Matrix multiplication
    Mat3 operator*(const Mat3& other) const {
        Mat3 result;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                result.m[i][j] = 0.0f;
                for (int k = 0; k < 3; k++)
                    result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
        return result;
    }

    // Matrix-vector multiplication
    Vec3 operator*(const Vec3& v) const {
        return Vec3(
            m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
            m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
            m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z
        );
    }

    // Matrix addition
    Mat3 operator+(const Mat3& other) const {
        Mat3 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[i][j] + other.m[i][j];
        return result;
    }

    // Matrix subtraction
    Mat3 operator-(const Mat3& other) const {
        Mat3 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[i][j] - other.m[i][j];
        return result;
    }

    // Scalar multiplication
    Mat3 operator*(float scalar) const {
        Mat3 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[i][j] * scalar;
        return result;
    }

    // Scalar division
    Mat3 operator/(float scalar) const {
        Mat3 result;
        float invScalar = 1.0f / scalar;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[i][j] * invScalar;
        return result;
    }
};