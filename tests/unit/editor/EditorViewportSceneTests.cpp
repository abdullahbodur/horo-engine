#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "editor/renderer/EditorViewportRenderer.h"
#include "editor/renderer/EditorViewportScene.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace
{
[[nodiscard]] bool NearlyEqual(const float lhs, const float rhs) noexcept
{
    return std::fabs(lhs - rhs) < 0.0001F;
}

void TransformUsesTranslationRotationScaleOrder()
{
    using namespace Horo::Math;
    const Transform transform{
        .translation = {2.0F, 3.0F, 4.0F},
        .rotation = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, 1.57079632679489661923F),
        .scale = {2.0F, 2.0F, 2.0F},
    };
    const Vec3 result = TransformPoint(transform.ToMatrix(), {1.0F, 0.0F, 0.0F});
    assert(NearlyEqual(result.x, 2.0F));
    assert(NearlyEqual(result.y, 3.0F));
    assert(NearlyEqual(result.z, 2.0F));
}

void LookAtUsesRightHandedNegativeZViewSpace()
{
    using namespace Horo::Math;
    const Mat4 view = LookAt({0.0F, 0.0F, 4.0F}, {}, {0.0F, 1.0F, 0.0F});
    const Vec3 originInView = TransformPoint(view, {});
    assert(NearlyEqual(originInView.x, 0.0F));
    assert(NearlyEqual(originInView.y, 0.0F));
    assert(NearlyEqual(originInView.z, -4.0F));
}

void PerspectiveMakesClipDepthExplicit()
{
    using namespace Horo::Math;
    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;
    constexpr float fov = 0.9599310885968813F;
    const Mat4 openGl = Perspective(fov, 1.0F, nearPlane, farPlane, ClipDepthRange::NegativeOneToOne);
    const Mat4 zeroToOne = Perspective(fov, 1.0F, nearPlane, farPlane, ClipDepthRange::ZeroToOne);
    assert(NearlyEqual(TransformPoint(openGl, {0.0F, 0.0F, -nearPlane}).z, -1.0F));
    assert(NearlyEqual(TransformPoint(openGl, {0.0F, 0.0F, -farPlane}).z, 1.0F));
    assert(NearlyEqual(TransformPoint(zeroToOne, {0.0F, 0.0F, -nearPlane}).z, 0.0F));
    assert(NearlyEqual(TransformPoint(zeroToOne, {0.0F, 0.0F, -farPlane}).z, 1.0F));
}

void ViewportSceneValidatesTypedInputsAndSharedGeometry()
{
    using namespace Horo;
    using namespace Horo::Editor;
    Runtime::PrimitiveMeshCache cache;
    auto acquired = cache.Acquire(Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box));
    assert(acquired.HasValue());
    Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
    const Render::MeshData &mesh = lease.Data();
    const Render::RenderMeshHandle meshHandle{lease.Id(), 1};
    const std::array resources{
        EditorViewportMeshResourceView{meshHandle, mesh.vertices, mesh.indices, mesh.localBounds}};
    const std::array instances{EditorViewportInstance{meshHandle, Math::Mat4::Identity(), mesh.localBounds,
                                                       Render::CoreDefaultMaterial, {}}};
    const EditorViewportSceneView valid{.camera = {}, .meshResources = resources, .instances = instances};
    assert(valid.IsValid());
    assert(mesh.vertices.size() == 24);
    assert(mesh.indices.size() == 36);

    EditorViewportCamera invalidCamera;
    invalidCamera.nearPlane = std::numeric_limits<float>::quiet_NaN();
    const EditorViewportSceneView invalid{.camera = invalidCamera, .meshResources = resources, .instances = instances};
    assert(!invalid.IsValid());
}

void ViewportProjectionAndRaysShareTheCameraContract()
{
    using namespace Horo;
    using namespace Horo::Editor;
    EditorViewportCamera perspective;
    const Result<Math::Ray> perspectiveRay = BuildEditorViewportRay(perspective, 0.5F, 0.5F, 1.0F);
    assert(perspectiveRay.HasValue());
    assert(Math::NearlyEqual(perspectiveRay.Value().origin, perspective.position));
    assert(Math::NearlyEqual(perspectiveRay.Value().direction,
                             Math::Normalize(perspective.target - perspective.position)));

    EditorViewportCamera orthographic = perspective;
    orthographic.projection = Runtime::CameraProjection::Orthographic;
    orthographic.orthographicHeight = 4.0F;
    const Result<Math::Ray> centerRay = BuildEditorViewportRay(orthographic, 0.5F, 0.5F, 1.0F);
    const Result<Math::Ray> rightRay = BuildEditorViewportRay(orthographic, 0.75F, 0.5F, 1.0F);
    assert(centerRay.HasValue() && rightRay.HasValue());
    assert(Math::NearlyEqual(centerRay.Value().direction, rightRay.Value().direction));
    assert(rightRay.Value().origin.x > centerRay.Value().origin.x);

    const Result<Math::Mat4> openGl =
        BuildEditorViewportViewProjection(orthographic, 1.0F, Math::ClipDepthRange::NegativeOneToOne);
    const Result<Math::Mat4> metal =
        BuildEditorViewportViewProjection(orthographic, 1.0F, Math::ClipDepthRange::ZeroToOne);
    assert(openGl.HasValue() && metal.HasValue());
    assert(!Math::NearlyEqual(openGl.Value().values[10], metal.Value().values[10]));
}
} // namespace

int main()
{
    TransformUsesTranslationRotationScaleOrder();
    LookAtUsesRightHandedNegativeZViewSpace();
    PerspectiveMakesClipDepthExplicit();
    ViewportSceneValidatesTypedInputsAndSharedGeometry();
    ViewportProjectionAndRaysShareTheCameraContract();
    return 0;
}
