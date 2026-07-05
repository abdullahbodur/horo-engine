#pragma once
#include <array>
#include <string>

#include "math/Vec3.h"

namespace Horo {
    // Column-major 3x3 matrix.
    // m[col][row]
    struct Mat3 {
        std::array<std::array<float, 3>, 3> m{};

        Mat3();

        explicit Mat3(float diagonal);

        static Mat3 Identity();

        static Mat3 Zero();

        // Construct from columns
        static Mat3 FromColumns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2);

        Vec3 GetColumn(int col) const;

        Vec3 GetRow(int row) const;

        void SetColumn(int col, const Vec3 &v);

        Mat3 Transposed() const;

        float Determinant() const;

        Mat3 Inverse() const;

        friend Mat3 operator+(const Mat3 &lhs, const Mat3 &rhs) {
            Mat3 r;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    r.m[i][j] = lhs.m[i][j] + rhs.m[i][j];
            return r;
        }

        friend Mat3 operator-(const Mat3 &lhs, const Mat3 &rhs) {
            Mat3 r;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    r.m[i][j] = lhs.m[i][j] - rhs.m[i][j];
            return r;
        }

        friend Mat3 operator*(const Mat3 &lhs, const Mat3 &rhs) {
            Mat3 r;
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 3; row++) {
                    float sum = 0;
                    for (int k = 0; k < 3; k++)
                        sum += lhs.m[k][row] * rhs.m[col][k];
                    r.m[col][row] = sum;
                }
            return r;
        }

        friend Vec3 operator*(const Mat3 &lhs, const Vec3 &rhs) {
            return {
                lhs.m[0][0] * rhs.x + lhs.m[1][0] * rhs.y + lhs.m[2][0] * rhs.z,
                lhs.m[0][1] * rhs.x + lhs.m[1][1] * rhs.y + lhs.m[2][1] * rhs.z,
                lhs.m[0][2] * rhs.x + lhs.m[1][2] * rhs.y + lhs.m[2][2] * rhs.z
            };
        }

        friend Mat3 operator*(const Mat3 &lhs, float scalar) {
            Mat3 r;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    r.m[i][j] = lhs.m[i][j] * scalar;
            return r;
        }

        friend Mat3 operator*(float scalar, const Mat3 &rhs) { return rhs * scalar; }

        bool operator==(const Mat3 &o) const;

        float &operator()(int row, int col) { return m[col][row]; }
        const float &operator()(int row, int col) const { return m[col][row]; }

        std::string ToString() const;
    };
} // namespace Horo
