#pragma once
#include <cassert>
#include <cmath>
#include <string>

namespace Monolith {
    struct Vec3 {
        float x, y, z;

        Vec3() : x(0), y(0), z(0) {
        }

        Vec3(float x, float y, float z) : x(x), y(y), z(z) {
        }

        explicit Vec3(float s) : x(s), y(s), z(s) {
        }

        Vec3 operator-() const { return {-x, -y, -z}; }

        friend Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) {
            return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
        }

        friend Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs) {
            return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        friend Vec3 operator*(const Vec3 &lhs, float scalar) {
            return {lhs.x * scalar, lhs.y * scalar, lhs.z * scalar};
        }

        friend Vec3 operator*(float scalar, const Vec3 &rhs) { return rhs * scalar; }

        friend Vec3 operator/(const Vec3 &lhs, float scalar) {
            return {lhs.x / scalar, lhs.y / scalar, lhs.z / scalar};
        }

        friend Vec3 operator*(const Vec3 &lhs, const Vec3 &rhs) {
            // component-wise
            return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
        }

        Vec3 &operator+=(const Vec3 &o) {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }

        Vec3 &operator-=(const Vec3 &o) {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }

        Vec3 &operator*=(float s) {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }

        Vec3 &operator/=(float s) {
            x /= s;
            y /= s;
            z /= s;
            return *this;
        }

        bool operator==(const Vec3 &o) const = default;

        float LengthSq() const { return x * x + y * y + z * z; }
        float Length() const { return std::sqrt(LengthSq()); }

        Vec3 Normalized() const;

        float &operator[](int i) {
            assert(i >= 0 && i < 3);
            switch (i) {
                case 0:
                    return x;
                case 1:
                    return y;
                default:
                    return z;
            }
        }

        float operator[](int i) const {
            assert(i >= 0 && i < 3);
            switch (i) {
                case 0:
                    return x;
                case 1:
                    return y;
                default:
                    return z;
            }
        }

        static float Dot(const Vec3 &a, const Vec3 &b);

        static Vec3 Cross(const Vec3 &a, const Vec3 &b);

        static Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t);

        static float Distance(const Vec3 &a, const Vec3 &b);

        static Vec3 Reflect(const Vec3 &v, const Vec3 &n);

        static Vec3 Zero() { return {0, 0, 0}; }
        static Vec3 One() { return {1, 1, 1}; }
        static Vec3 Up() { return {0, 1, 0}; }
        static Vec3 Down() { return {0, -1, 0}; }
        static Vec3 Right() { return {1, 0, 0}; }
        static Vec3 Left() { return {-1, 0, 0}; }
        static Vec3 Forward() { return {0, 0, -1}; }
        static Vec3 Back() { return {0, 0, 1}; }

        std::string ToString() const;
    };
} // namespace Monolith
