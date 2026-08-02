#pragma once

/**
 * @file SceneMath.h
 * @brief Backend-neutral scene vectors, transforms, bounds, rays, and camera projection math.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <optional>

namespace Horo::Math
{
inline constexpr float DefaultEpsilon = 0.000001F;
inline constexpr float Pi = 3.14159265358979323846F;

/** @brief Two-dimensional float vector. */
struct Vec2
{
    float x{0.0F};
    float y{0.0F};

    [[nodiscard]] constexpr Vec2 operator+() const noexcept
    {
        return *this;
    }
    [[nodiscard]] constexpr Vec2 operator-() const noexcept
    {
        return {-x, -y};
    }
    [[nodiscard]] constexpr Vec2 operator+(Vec2 rhs) const noexcept
    {
        return {x + rhs.x, y + rhs.y};
    }
    [[nodiscard]] constexpr Vec2 operator-(Vec2 rhs) const noexcept
    {
        return {x - rhs.x, y - rhs.y};
    }
    [[nodiscard]] constexpr Vec2 operator*(float scalar) const noexcept
    {
        return {x * scalar, y * scalar};
    }
    [[nodiscard]] constexpr Vec2 operator/(float scalar) const noexcept
    {
        return {x / scalar, y / scalar};
    }
    constexpr Vec2 &operator+=(Vec2 rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr Vec2 &operator-=(Vec2 rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    constexpr Vec2 &operator*=(float scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    constexpr Vec2 &operator/=(float scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }
    [[nodiscard]] constexpr auto operator<=>(const Vec2 &) const noexcept = default;
};

[[nodiscard]] constexpr Vec2 operator*(float scalar, Vec2 value) noexcept
{
    return value * scalar;
}

/** @brief Three-dimensional float vector in Horo's right-handed, Y-up scene space. */
struct Vec3
{
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    [[nodiscard]] constexpr Vec3 operator+() const noexcept
    {
        return *this;
    }
    [[nodiscard]] constexpr Vec3 operator-() const noexcept
    {
        return {-x, -y, -z};
    }
    [[nodiscard]] constexpr Vec3 operator+(Vec3 rhs) const noexcept
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    [[nodiscard]] constexpr Vec3 operator-(Vec3 rhs) const noexcept
    {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    [[nodiscard]] constexpr Vec3 operator*(float scalar) const noexcept
    {
        return {x * scalar, y * scalar, z * scalar};
    }
    [[nodiscard]] constexpr Vec3 operator/(float scalar) const noexcept
    {
        return {x / scalar, y / scalar, z / scalar};
    }
    constexpr Vec3 &operator+=(Vec3 rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    constexpr Vec3 &operator-=(Vec3 rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    constexpr Vec3 &operator*=(float scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
    constexpr Vec3 &operator/=(float scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
    [[nodiscard]] constexpr auto operator<=>(const Vec3 &) const noexcept = default;
};

[[nodiscard]] constexpr Vec3 operator*(float scalar, Vec3 value) noexcept
{
    return value * scalar;
}

/** @brief Four-dimensional float vector used for homogeneous coordinates. */
struct Vec4
{
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};

    [[nodiscard]] constexpr Vec4 operator+() const noexcept
    {
        return *this;
    }
    [[nodiscard]] constexpr Vec4 operator-() const noexcept
    {
        return {-x, -y, -z, -w};
    }
    [[nodiscard]] constexpr Vec4 operator+(Vec4 rhs) const noexcept
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }
    [[nodiscard]] constexpr Vec4 operator-(Vec4 rhs) const noexcept
    {
        return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }
    [[nodiscard]] constexpr Vec4 operator*(float scalar) const noexcept
    {
        return {x * scalar, y * scalar, z * scalar, w * scalar};
    }
    [[nodiscard]] constexpr Vec4 operator/(float scalar) const noexcept
    {
        return {x / scalar, y / scalar, z / scalar, w / scalar};
    }
    constexpr Vec4 &operator+=(Vec4 rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }
    constexpr Vec4 &operator-=(Vec4 rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }
    constexpr Vec4 &operator*=(float scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }
    constexpr Vec4 &operator/=(float scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }
    [[nodiscard]] constexpr auto operator<=>(const Vec4 &) const noexcept = default;
};

[[nodiscard]] constexpr Vec4 operator*(float scalar, Vec4 value) noexcept
{
    return value * scalar;
}

[[nodiscard]] bool IsFinite(Vec2 value) noexcept;
[[nodiscard]] bool IsFinite(Vec3 value) noexcept;
[[nodiscard]] bool IsFinite(Vec4 value) noexcept;
[[nodiscard]] constexpr float Dot(Vec2 lhs, Vec2 rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y;
}
[[nodiscard]] constexpr float Dot(Vec3 lhs, Vec3 rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}
[[nodiscard]] constexpr float Dot(Vec4 lhs, Vec4 rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}
[[nodiscard]] constexpr Vec3 Cross(Vec3 lhs, Vec3 rhs) noexcept
{
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}
[[nodiscard]] constexpr float LengthSquared(Vec2 value) noexcept
{
    return Dot(value, value);
}
[[nodiscard]] constexpr float LengthSquared(Vec3 value) noexcept
{
    return Dot(value, value);
}
[[nodiscard]] constexpr float LengthSquared(Vec4 value) noexcept
{
    return Dot(value, value);
}
[[nodiscard]] float Length(Vec2 value) noexcept;
[[nodiscard]] float Length(Vec3 value) noexcept;
[[nodiscard]] float Length(Vec4 value) noexcept;
[[nodiscard]] Vec2 Normalize(Vec2 value) noexcept;
[[nodiscard]] Vec3 Normalize(Vec3 value) noexcept;
[[nodiscard]] Vec4 Normalize(Vec4 value) noexcept;
/**
 * @brief Normalizes a boundary-provided vector.
 * @param value Finite vector with non-zero length.
 * @return Unit vector, or a typed non-finite/zero-length error.
 */
[[nodiscard]] Result<Vec2> TryNormalize(Vec2 value) noexcept;
/**
 * @brief Normalizes a boundary-provided three-dimensional vector.
 * @param value Finite vector with non-zero length.
 * @return Unit vector, or a typed non-finite/zero-length error.
 */
[[nodiscard]] Result<Vec3> TryNormalize(Vec3 value) noexcept;
/**
 * @brief Normalizes a boundary-provided four-dimensional vector.
 * @param value Finite vector with non-zero length.
 * @return Unit vector, or a typed non-finite/zero-length error.
 */
[[nodiscard]] Result<Vec4> TryNormalize(Vec4 value) noexcept;
[[nodiscard]] constexpr Vec2 Lerp(Vec2 from, Vec2 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}
[[nodiscard]] constexpr Vec3 Lerp(Vec3 from, Vec3 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}
[[nodiscard]] constexpr Vec4 Lerp(Vec4 from, Vec4 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}
[[nodiscard]] bool NearlyEqual(float lhs, float rhs, float epsilon = DefaultEpsilon) noexcept;
[[nodiscard]] bool NearlyEqual(Vec2 lhs, Vec2 rhs, float epsilon = DefaultEpsilon) noexcept;
[[nodiscard]] bool NearlyEqual(Vec3 lhs, Vec3 rhs, float epsilon = DefaultEpsilon) noexcept;
[[nodiscard]] bool NearlyEqual(Vec4 lhs, Vec4 rhs, float epsilon = DefaultEpsilon) noexcept;
[[nodiscard]] constexpr float DegreesToRadians(float degrees) noexcept
{
    return degrees * (Pi / 180.0F);
}
[[nodiscard]] constexpr float RadiansToDegrees(float radians) noexcept
{
    return radians * (180.0F / Pi);
}

/** @brief Quaternion rotation applied to column vectors in right-handed scene space. */
struct Quaternion
{
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{1.0F};

    [[nodiscard]] static constexpr Quaternion Identity() noexcept
    {
        return {};
    }
    [[nodiscard]] static Quaternion FromAxisAngle(Vec3 axis, float radians) noexcept;
    /**
     * @brief Creates a quaternion from a validated axis and radian angle.
     * @param axis Finite, non-zero rotation axis.
     * @param radians Finite angle in radians.
     * @return Quaternion, or a typed input error.
     */
    [[nodiscard]] static Result<Quaternion> TryFromAxisAngle(Vec3 axis, float radians) noexcept;
    [[nodiscard]] static Quaternion FromEulerRadians(Vec3 radians) noexcept;
    [[nodiscard]] static Result<Quaternion> TryFromEulerRadians(Vec3 radians) noexcept;
    [[nodiscard]] Vec3 ToEulerRadians() const noexcept;
    [[nodiscard]] Quaternion Normalized() const noexcept;
    /** @brief Returns a unit quaternion or a typed non-finite/zero-length error. */
    [[nodiscard]] Result<Quaternion> TryNormalized() const noexcept;
    [[nodiscard]] Quaternion Inverse() const noexcept;
    /** @brief Returns the true quaternion inverse or a typed invalid-input error. */
    [[nodiscard]] Result<Quaternion> TryInverse() const noexcept;
    [[nodiscard]] Vec3 Rotate(Vec3 value) const noexcept;
    /**
     * @brief Rotates boundary-provided vector data.
     * @param value Finite scene vector.
     * @return Rotated vector, or a typed quaternion/input error.
     */
    [[nodiscard]] Result<Vec3> TryRotate(Vec3 value) const noexcept;
    [[nodiscard]] Quaternion operator*(const Quaternion &rhs) const noexcept;
    [[nodiscard]] constexpr bool operator==(const Quaternion &) const noexcept = default;
};

[[nodiscard]] bool IsFinite(Quaternion value) noexcept;
[[nodiscard]] Quaternion Nlerp(Quaternion from, Quaternion to, float alpha) noexcept;
[[nodiscard]] Quaternion Slerp(Quaternion from, Quaternion to, float alpha) noexcept;
[[nodiscard]] Result<Quaternion> TryNlerp(Quaternion from, Quaternion to, float alpha) noexcept;
[[nodiscard]] Result<Quaternion> TrySlerp(Quaternion from, Quaternion to, float alpha) noexcept;

/** @brief Column-major 4x4 matrix multiplied with column vectors. */
struct Mat4
{
    std::array<float, 16> values{};

    [[nodiscard]] static constexpr Mat4 Identity() noexcept
    {
        return Mat4{{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    }
    [[nodiscard]] constexpr float At(int row, int column) const noexcept
    {
        return values[static_cast<std::size_t>(column * 4 + row)];
    }
    [[nodiscard]] constexpr auto operator<=>(const Mat4 &) const noexcept = default;
};

/** @brief Local translation, rotation, and scale authored in scene space. */
struct Transform
{
    Vec3 translation{};
    Quaternion rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] Mat4 ToMatrix() const noexcept;
    [[nodiscard]] Result<Mat4> TryToMatrix() const noexcept;
    [[nodiscard]] constexpr bool operator==(const Transform &) const noexcept = default;
};

/** @brief Clip-space depth convention required by a graphics API family. */
enum class ClipDepthRange
{
    NegativeOneToOne,
    ZeroToOne,
};

/** @brief Finite ray segment with a normalized direction. */
struct Ray
{
    Vec3 origin{};
    Vec3 direction{0.0F, 0.0F, -1.0F};
    float minimumDistance{0.0F};
    float maximumDistance{1.0F};
};

/** @brief Plane represented by `dot(normal, point) + distance = 0`. */
struct Plane
{
    Vec3 normal{0.0F, 1.0F, 0.0F};
    float distance{0.0F};
};

/** @brief Axis-aligned bounds in one coordinate space. */
struct Aabb
{
    Vec3 minimum{};
    Vec3 maximum{};

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] constexpr Vec3 Center() const noexcept
    {
        return (minimum + maximum) * 0.5F;
    }
    [[nodiscard]] constexpr Vec3 Extents() const noexcept
    {
        return (maximum - minimum) * 0.5F;
    }
};

/** @brief Conservative sphere bounds. */
struct BoundingSphere
{
    Vec3 center{};
    float radius{0.0F};

    [[nodiscard]] bool IsValid() const noexcept;
};

/** @brief Nearest surface intersection along a ray. */
struct RayHit
{
    float distance{0.0F};
    Vec3 position{};
    Vec3 normal{};
};

[[nodiscard]] bool IsFinite(const Mat4 &value) noexcept;
[[nodiscard]] Mat4 Multiply(const Mat4 &lhs, const Mat4 &rhs) noexcept;
[[nodiscard]] Mat4 TranslationMatrix(Vec3 value) noexcept;
[[nodiscard]] Mat4 RotationMatrix(Quaternion value) noexcept;
[[nodiscard]] Mat4 ScaleMatrix(Vec3 value) noexcept;
/**
 * @brief Inverts a finite general 4x4 matrix.
 * @param matrix Matrix that may contain projective terms.
 * @return Inverse matrix, or a typed non-finite/singular error.
 */
[[nodiscard]] Result<Mat4> TryInverse(const Mat4 &matrix) noexcept;
/**
 * @brief Inverts a finite affine matrix without a general projective solve.
 * @param matrix Matrix whose final row is `(0,0,0,1)`.
 * @return Affine inverse, or a typed invalid/singular error.
 */
[[nodiscard]] Result<Mat4> TryInverseAffine(const Mat4 &matrix) noexcept;
/**
 * @brief Projects an affine matrix onto Horo's nearest representable TRS.
 * @param matrix Finite, non-singular affine matrix.
 * @return Deterministic TRS, or a typed invalid/singular error.
 */
[[nodiscard]] Result<Transform> TryDecomposeAffineTRS(const Mat4 &matrix) noexcept;
[[nodiscard]] Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept;
/**
 * @brief Builds a right-handed negative-Z-forward view matrix.
 * @param eye Camera position.
 * @param target Distinct look target.
 * @param up Non-zero vector not parallel to the view direction.
 * @return View matrix, or a typed invalid-view error.
 */
[[nodiscard]] Result<Mat4> TryLookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept;
[[nodiscard]] Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane,
                               ClipDepthRange depthRange) noexcept;
/**
 * @brief Builds a finite right-handed perspective projection.
 * @param verticalFovRadians Vertical field of view in `(0, Pi)`.
 * @param aspect Positive width/height ratio.
 * @param nearPlane Positive near distance.
 * @param farPlane Far distance greater than the near distance.
 * @param depthRange Required backend clip-depth convention.
 * @return Projection matrix, or a typed invalid-projection error.
 */
[[nodiscard]] Result<Mat4> TryPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane,
                                          ClipDepthRange depthRange) noexcept;
[[nodiscard]] Mat4 Orthographic(float verticalHeight, float aspect, float nearPlane, float farPlane,
                                ClipDepthRange depthRange) noexcept;
/**
 * @brief Builds a finite centered right-handed orthographic projection.
 * @param verticalHeight Positive visible scene height.
 * @param aspect Positive width/height ratio.
 * @param nearPlane Positive near distance.
 * @param farPlane Far distance greater than the near distance.
 * @param depthRange Required backend clip-depth convention.
 * @return Projection matrix, or a typed invalid-projection error.
 */
[[nodiscard]] Result<Mat4> TryOrthographic(float verticalHeight, float aspect, float nearPlane, float farPlane,
                                           ClipDepthRange depthRange) noexcept;
[[nodiscard]] Vec4 TransformHomogeneous(const Mat4 &matrix, Vec4 value) noexcept;
[[nodiscard]] Vec3 TransformAffinePoint(const Mat4 &matrix, Vec3 point) noexcept;
[[nodiscard]] Vec3 TransformDirection(const Mat4 &matrix, Vec3 direction) noexcept;
[[nodiscard]] Vec3 TransformPoint(const Mat4 &matrix, Vec3 point) noexcept;
/**
 * @brief Applies a homogeneous point transform and perspective divide.
 * @param matrix Finite general matrix.
 * @param point Finite input point.
 * @return Transformed point, or a typed invalid-homogeneous-point error.
 */
[[nodiscard]] Result<Vec3> TryTransformPoint(const Mat4 &matrix, Vec3 point) noexcept;
/**
 * @brief Projects a world point to normalized device coordinates.
 * @param viewProjection Validated view-projection matrix.
 * @param worldPoint Finite point in front of the view origin.
 * @return NDC point, or a typed invalid-projection error.
 */
[[nodiscard]] Result<Vec3> TryProject(const Mat4 &viewProjection, Vec3 worldPoint) noexcept;
/**
 * @brief Unprojects normalized device coordinates through an inverse view-projection.
 * @param inverseViewProjection Validated inverse view-projection matrix.
 * @param ndcPoint Finite NDC point using the matrix's clip-depth convention.
 * @return World point, or a typed homogeneous-transform error.
 */
[[nodiscard]] Result<Vec3> TryUnproject(const Mat4 &inverseViewProjection, Vec3 ndcPoint) noexcept;
/**
 * @brief Creates a finite normalized ray segment.
 * @param origin Finite ray origin.
 * @param direction Finite, non-zero direction.
 * @param minimumDistance Non-negative segment start.
 * @param maximumDistance Segment end not less than the start.
 * @return Normalized ray, or a typed invalid-ray error.
 */
[[nodiscard]] Result<Ray> TryMakeRay(Vec3 origin, Vec3 direction, float minimumDistance,
                                     float maximumDistance) noexcept;
/**
 * @brief Creates a normalized plane from a point and normal.
 * @param point Finite point on the plane.
 * @param normal Finite, non-zero plane normal.
 * @return Plane, or a typed invalid-plane error.
 */
[[nodiscard]] Result<Plane> TryMakePlane(Vec3 point, Vec3 normal) noexcept;
/**
 * @brief Computes conservative world AABB by transforming all eight corners.
 * @param bounds Valid local bounds.
 * @param localToWorld Finite affine transform.
 * @return Transformed AABB, or a typed bounds/affine error.
 */
[[nodiscard]] Result<Aabb> TransformAabb(const Aabb &bounds, const Mat4 &localToWorld) noexcept;
/**
 * @brief Creates the conservative sphere enclosing an AABB.
 * @param bounds Valid bounds.
 * @return Enclosing sphere, or a typed bounds error.
 */
[[nodiscard]] Result<BoundingSphere> SphereFromAabb(const Aabb &bounds) noexcept;
/**
 * @brief Intersects a finite ray segment with a plane.
 * @param ray Valid normalized ray segment.
 * @param plane Valid normalized plane.
 * @return Nearest hit, successful empty optional for a miss, or typed invalid-input error.
 */
[[nodiscard]] Result<std::optional<RayHit>> IntersectRayPlane(const Ray &ray, const Plane &plane) noexcept;
/**
 * @brief Intersects a finite ray segment with an AABB.
 * @param ray Valid normalized ray segment.
 * @param bounds Valid axis-aligned bounds.
 * @return Nearest surface hit, successful empty optional for a miss, or typed invalid-input error.
 */
[[nodiscard]] Result<std::optional<RayHit>> IntersectRayAabb(const Ray &ray, const Aabb &bounds) noexcept;
} // namespace Horo::Math
