#include "Horo/Math/SceneMath.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
using namespace Horo::Math;

bool Near(const float left, const float right, const float epsilon = 0.0001F)
{
    return std::fabs(left - right) <= epsilon;
}

void VectorsProvideValidatedAndHotPathOperations()
{
    assert(NearlyEqual(Vec2{1.0F, 2.0F} + Vec2{3.0F, 4.0F}, Vec2{4.0F, 6.0F}));
    assert(NearlyEqual(Cross({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), {0.0F, 0.0F, 1.0F}));
    assert(NearlyEqual(Lerp(Vec4{}, Vec4{2.0F, 4.0F, 6.0F, 8.0F}, 0.5F), Vec4{1.0F, 2.0F, 3.0F, 4.0F}));
    assert(NearlyEqual(Normalize(Vec3{0.0F, 3.0F, 4.0F}), Vec3{0.0F, 0.6F, 0.8F}));
    assert(TryNormalize(Vec3{}).HasError());
    assert(TryNormalize(Vec3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}).HasError());
    assert(Near(DegreesToRadians(180.0F), Pi));
    assert(Near(RadiansToDegrees(Pi * 0.5F), 90.0F));
}

void QuaternionsAreFiniteInvertibleAndInterpolateShortestPath()
{
    const Quaternion rotation = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, Pi * 0.5F);
    const Vec3 rotated = rotation.Rotate({1.0F, 0.0F, 0.0F});
    assert(NearlyEqual(rotated, Vec3{0.0F, 0.0F, -1.0F}, 0.0001F));
    assert(NearlyEqual(rotation.Inverse().Rotate(rotated), Vec3{1.0F, 0.0F, 0.0F}, 0.0001F));

    const Quaternion authored = Quaternion::FromEulerRadians({0.2F, -0.4F, 0.6F});
    const Quaternion negated{-authored.x, -authored.y, -authored.z, -authored.w};
    assert(NearlyEqual(Nlerp(authored, negated, 0.5F).Rotate({1.0F, 0.0F, 0.0F}), authored.Rotate({1.0F, 0.0F, 0.0F}),
                       0.0001F));
    assert(NearlyEqual(Slerp(Quaternion::Identity(), rotation, 0.5F).Rotate({1.0F, 0.0F, 0.0F}),
                       Vec3{0.7071067F, 0.0F, -0.7071067F}, 0.0002F));
    assert((Quaternion{0.0F, 0.0F, 0.0F, 0.0F}.TryNormalized().HasError()));
    assert(TrySlerp(Quaternion::Identity(), rotation, std::numeric_limits<float>::quiet_NaN()).HasError());
}

void MatricesComposeInvertAndDecomposeAffineTrs()
{
    const Transform authored{.translation = {2.0F, -3.0F, 4.0F},
                             .rotation = Quaternion::FromEulerRadians({0.2F, -0.4F, 0.6F}),
                             .scale = {-2.0F, 3.0F, 0.5F}};
    const Mat4 matrix = authored.ToMatrix();
    const Horo::Result<Transform> decomposed = TryDecomposeAffineTRS(matrix);
    assert(decomposed.HasValue());
    const Mat4 recovered = decomposed.Value().ToMatrix();
    for (std::size_t index = 0; index < matrix.values.size(); ++index)
        assert(Near(matrix.values[index], recovered.values[index], 0.001F));

    const Horo::Result<Mat4> affineInverse = TryInverseAffine(matrix);
    const Horo::Result<Mat4> generalInverse = TryInverse(matrix);
    assert(affineInverse.HasValue() && generalInverse.HasValue());
    const Vec3 point{0.7F, -1.2F, 2.4F};
    assert(
        NearlyEqual(TransformAffinePoint(affineInverse.Value(), TransformAffinePoint(matrix, point)), point, 0.0002F));
    assert(NearlyEqual(TransformPoint(generalInverse.Value(), TransformPoint(matrix, point)), point, 0.0002F));

    Mat4 sheared = matrix;
    sheared.values[4] += 0.35F;
    assert(TryDecomposeAffineTRS(sheared).HasValue());
    assert(TryInverseAffine(ScaleMatrix({0.0F, 1.0F, 1.0F})).HasError());
    Mat4 nonFinite = Mat4::Identity();
    nonFinite.values[5] = std::numeric_limits<float>::infinity();
    assert(TryDecomposeAffineTRS(nonFinite).HasError());
}

void ProjectionsRespectDepthConventionsAndRoundTrip()
{
    const Mat4 view = LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    for (const ClipDepthRange depth : {ClipDepthRange::NegativeOneToOne, ClipDepthRange::ZeroToOne})
    {
        const Mat4 perspective = Perspective(Pi * 0.5F, 2.0F, 1.0F, 11.0F, depth);
        const Vec3 nearNdc = TransformPoint(perspective, {0.0F, 0.0F, -1.0F});
        const Vec3 farNdc = TransformPoint(perspective, {0.0F, 0.0F, -11.0F});
        assert(Near(nearNdc.z, depth == ClipDepthRange::NegativeOneToOne ? -1.0F : 0.0F));
        assert(Near(farNdc.z, 1.0F));

        const Mat4 orthographic = Orthographic(4.0F, 2.0F, 1.0F, 11.0F, depth);
        assert(Near(TransformPoint(orthographic, {0.0F, 0.0F, -1.0F}).z,
                    depth == ClipDepthRange::NegativeOneToOne ? -1.0F : 0.0F));
        assert(Near(TransformPoint(orthographic, {0.0F, 0.0F, -11.0F}).z, 1.0F));

        const Mat4 viewProjection = Multiply(perspective, view);
        const Vec3 worldPoint{0.25F, -0.5F, 0.0F};
        const Horo::Result<Vec3> projected = TryProject(viewProjection, worldPoint);
        const Horo::Result<Mat4> inverse = TryInverse(viewProjection);
        assert(projected.HasValue() && inverse.HasValue());
        const Horo::Result<Vec3> unprojected = TryUnproject(inverse.Value(), projected.Value());
        assert(unprojected.HasValue());
        assert(NearlyEqual(unprojected.Value(), worldPoint, 0.0002F));
        assert(TryProject(viewProjection, {0.0F, 0.0F, 10.0F}).HasError());
    }
    assert(TryPerspective(0.0F, 1.0F, 0.1F, 10.0F, ClipDepthRange::NegativeOneToOne).HasError());
    assert(TryOrthographic(1.0F, -1.0F, 0.1F, 10.0F, ClipDepthRange::ZeroToOne).HasError());
}

void RaysBoundsAndPlanesDistinguishMissFromInvalidInput()
{
    const Horo::Result<Ray> ray = TryMakeRay({0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -2.0F}, 0.0F, 20.0F);
    const Horo::Result<Plane> plane = TryMakePlane({}, {0.0F, 0.0F, 1.0F});
    assert(ray.HasValue() && plane.HasValue());
    const auto planeHit = IntersectRayPlane(ray.Value(), plane.Value());
    assert(planeHit.HasValue() && planeHit.Value().has_value());
    assert(Near(planeHit.Value()->distance, 5.0F));

    const Aabb box{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const auto hit = IntersectRayAabb(ray.Value(), box);
    assert(hit.HasValue() && hit.Value().has_value() && Near(hit.Value()->distance, 4.0F));
    const auto inside = IntersectRayAabb(TryMakeRay({}, {1.0F, 0.0F, 0.0F}, 0.0F, 20.0F).Value(), box);
    assert(inside.HasValue() && inside.Value().has_value() && Near(inside.Value()->distance, 1.0F));
    const auto miss = IntersectRayAabb(TryMakeRay({5.0F, 5.0F, 5.0F}, {1.0F, 0.0F, 0.0F}, 0.0F, 20.0F).Value(), box);
    assert(miss.HasValue() && !miss.Value().has_value());
    assert(IntersectRayAabb(ray.Value(), Aabb{{1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}}).HasError());

    const Transform transform{.translation = {2.0F, 1.0F, -3.0F},
                              .rotation = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, Pi * 0.5F),
                              .scale = {-2.0F, 1.0F, 0.5F}};
    const Horo::Result<Aabb> transformed = TransformAabb(box, transform.ToMatrix());
    assert(transformed.HasValue());
    assert(NearlyEqual(transformed.Value().Center(), transform.translation, 0.0001F));
    assert(NearlyEqual(transformed.Value().Extents(), Vec3{0.5F, 1.0F, 2.0F}, 0.0001F));
}
} // namespace

int main()
{
    VectorsProvideValidatedAndHotPathOperations();
    QuaternionsAreFiniteInvertibleAndInterpolateShortestPath();
    MatricesComposeInvertAndDecomposeAffineTrs();
    ProjectionsRespectDepthConventionsAndRoundTrip();
    RaysBoundsAndPlanesDistinguishMissFromInvalidInput();
    return 0;
}
