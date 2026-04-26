#include "math/Quaternion.h"

#include <sstream>

#include "math/Mat3.h"
#include "math/MathUtils.h"

namespace Horo {
    Quaternion Quaternion::FromAxisAngle(const Vec3 &axis, float radians) {
        float half = radians * 0.5f;
        float s = Sin(half);
        Vec3 a = axis.Normalized();
        return {a.x * s, a.y * s, a.z * s, Cos(half)};
    }

    Quaternion Quaternion::FromEuler(float pitch, float yaw, float roll) {
        const float cp = Cos(pitch * 0.5f);
        const float sp = Sin(pitch * 0.5f);
        const float cy = Cos(yaw * 0.5f);
        const float sy = Sin(yaw * 0.5f);
        const float cr = Cos(roll * 0.5f);
        const float sr = Sin(roll * 0.5f);

        return {
            sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy
        };
    }

    float Quaternion::Dot(const Quaternion &a, const Quaternion &b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    float Quaternion::Length() const { return Sqrt(x * x + y * y + z * z + w * w); }

    Quaternion Quaternion::Normalized() const {
        float len = Length();
        if (NearlyZero(len))
            return Identity();
        float inv = 1.0f / len;
        return {x * inv, y * inv, z * inv, w * inv};
    }

    Quaternion Quaternion::Inverse() const {
        float lenSq = x * x + y * y + z * z + w * w;
        if (NearlyZero(lenSq))
            return Identity();
        float inv = 1.0f / lenSq;
        return {-x * inv, -y * inv, -z * inv, w * inv};
    }

    Quaternion Quaternion::Slerp(const Quaternion &a, const Quaternion &b,
                                 float t) {
        Quaternion bq = b;
        float d = Dot(a, bq);

        // Ensure shortest path
        if (d < 0.0f) {
            bq = {-bq.x, -bq.y, -bq.z, -bq.w};
            d = -d;
        }

        if (d > 1.0f - EPSILON)
            return Lerp(a, bq, t);

        float theta0 = Acos(d);
        float theta = theta0 * t;
        float sinT0 = Sin(theta0);
        float sinT = Sin(theta);

        float s0 = Cos(theta) - d * sinT / sinT0;
        float s1 = sinT / sinT0;
        return (a * s0 + bq * s1).Normalized();
    }

    Quaternion Quaternion::Lerp(const Quaternion &a, const Quaternion &b, float t) {
        return (a * (1.0f - t) + b * t).Normalized();
    }

    Quaternion Quaternion::LookRotation(const Vec3 &forward, const Vec3 &up) {
        Vec3 f = forward.Normalized();
        Vec3 r = Vec3::Cross(f, up).Normalized();
        Vec3 u = Vec3::Cross(r, f);

        Mat3 rot = Mat3::FromColumns(r, u, {-f.x, -f.y, -f.z});

        // Mat3 → Quaternion
        float trace = rot.m[0][0] + rot.m[1][1] + rot.m[2][2];
        Quaternion q;
        if (trace > 0) {
            float s = 0.5f / Sqrt(trace + 1.0f);
            q.w = 0.25f / s;
            q.x = (rot.m[1][2] - rot.m[2][1]) * s;
            q.y = (rot.m[2][0] - rot.m[0][2]) * s;
            q.z = (rot.m[0][1] - rot.m[1][0]) * s;
        } else if (rot.m[0][0] > rot.m[1][1] && rot.m[0][0] > rot.m[2][2]) {
            float s = 2.0f * Sqrt(1.0f + rot.m[0][0] - rot.m[1][1] - rot.m[2][2]);
            q.w = (rot.m[1][2] - rot.m[2][1]) / s;
            q.x = 0.25f * s;
            q.y = (rot.m[0][1] + rot.m[1][0]) / s;
            q.z = (rot.m[2][0] + rot.m[0][2]) / s;
        } else if (rot.m[1][1] > rot.m[2][2]) {
            float s = 2.0f * Sqrt(1.0f + rot.m[1][1] - rot.m[0][0] - rot.m[2][2]);
            q.w = (rot.m[2][0] - rot.m[0][2]) / s;
            q.x = (rot.m[0][1] + rot.m[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (rot.m[1][2] + rot.m[2][1]) / s;
        } else {
            float s = 2.0f * Sqrt(1.0f + rot.m[2][2] - rot.m[0][0] - rot.m[1][1]);
            q.w = (rot.m[0][1] - rot.m[1][0]) / s;
            q.x = (rot.m[2][0] + rot.m[0][2]) / s;
            q.y = (rot.m[1][2] + rot.m[2][1]) / s;
            q.z = 0.25f * s;
        }
        return q.Normalized();
    }

    Mat3 Quaternion::ToMat3() const {
        const float x2 = x + x;
        const float y2 = y + y;
        const float z2 = z + z;
        const float xx = x * x2;
        const float xy = x * y2;
        const float xz = x * z2;
        const float yy = y * y2;
        const float yz = y * z2;
        const float zz = z * z2;
        const float wx = w * x2;
        const float wy = w * y2;
        const float wz = w * z2;

        return Mat3::FromColumns({1.0f - (yy + zz), xy + wz, xz - wy},
                                 {xy - wz, 1.0f - (xx + zz), yz + wx},
                                 {xz + wy, yz - wx, 1.0f - (xx + yy)});
    }

    Vec3 Quaternion::ToEuler() const {
        // Returns pitch (X), yaw (Y), roll (Z) in radians
        float sinP = 2.0f * (w * x + y * z);
        float cosP = 1.0f - 2.0f * (x * x + y * y);
        float pitch = Atan2(sinP, cosP);

        float sinY = 2.0f * (w * y - z * x);
        float yaw;
        if (Abs(sinY) >= 1.0f)
            yaw = Cos(sinY) * HALF_PI;
        else
            yaw = Acos(sinY) - HALF_PI;

        float sinR = 2.0f * (w * z + x * y);
        float cosR = 1.0f - 2.0f * (y * y + z * z);
        float roll = Atan2(sinR, cosR);

        return {pitch, yaw, roll};
    }

    std::string Quaternion::ToString() const {
        std::ostringstream ss;
        ss << "Quat(" << x << ", " << y << ", " << z << ", " << w << ")";
        return ss.str();
    }
} // namespace Horo
