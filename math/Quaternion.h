#pragma once
#include <string>

#include "math/Vec3.h"

namespace Horo {
    struct Mat3;

    struct Quaternion {
        float x;
        float y;
        float z;
        float w;

        Quaternion() : x(0), y(0), z(0), w(1) {
        }

        Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {
        }

        static Quaternion Identity() { return {0, 0, 0, 1}; }

        static Quaternion FromAxisAngle(const Vec3 &axis, float radians);

        static Quaternion FromEuler(float pitch, float yaw,
                                    float roll); // radians, XYZ order
        static Quaternion Slerp(const Quaternion &a, const Quaternion &b, float t);

        static Quaternion Lerp(const Quaternion &a, const Quaternion &b, float t);

        static float Dot(const Quaternion &a, const Quaternion &b);

        static Quaternion LookRotation(const Vec3 &forward,
                                       const Vec3 &up = Vec3::Up());

        Quaternion Conjugate() const { return {-x, -y, -z, w}; }

        Quaternion Inverse() const;

        Quaternion Normalized() const;

        float Length() const;

        Vec3 ToEuler() const; // returns pitch/yaw/roll in radians
        Mat3 ToMat3() const;

        Vec3 Forward() const { return *this * Vec3::Forward(); }
        Vec3 Up() const { return *this * Vec3::Up(); }
        Vec3 Right() const { return *this * Vec3::Right(); }

        friend Quaternion operator*(const Quaternion &a, const Quaternion &b) {
            return {
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
            };
        }

        friend Vec3 operator*(const Quaternion &q, const Vec3 &v) {
            // Optimised rotation: t = 2*(q.xyz × v); v + q.w*t + (q.xyz × t)
            const Vec3 qv = {q.x, q.y, q.z};
            const Vec3 t = Vec3::Cross(qv, v) * 2.0f;
            return v + t * q.w + Vec3::Cross(qv, t);
        }

        friend Quaternion operator*(const Quaternion &q, float s) {
            return {q.x * s, q.y * s, q.z * s, q.w * s};
        }

        friend Quaternion operator+(const Quaternion &a, const Quaternion &b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
        }

        std::string ToString() const;
    };
} // namespace Horo
