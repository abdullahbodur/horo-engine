#include "editor/project_model/EditorViewportModel.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
[[nodiscard]] bool NearlyEqual(const float lhs, const float rhs) noexcept
{
    return std::fabs(lhs - rhs) < 0.0001F;
}

void NavigationMovesTheAuthoritativeCameraAndPublishesOnce()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    std::vector<ViewportChangedEvent> published;
    auto subscription = events.Subscribe<ViewportChangedEvent>(
        [&published](const ViewportChangedEvent &event) { published.push_back(event); });

    const auto moved = viewport.Navigate(EditorViewportNavigationDelta{.moveForward = 1.0F});
    assert(moved.HasValue());
    assert(viewport.Current().revision == ViewportRevision{1});
    assert(NearlyEqual(viewport.Current().camera.position.z, 3.0F));
    assert(NearlyEqual(viewport.Current().camera.target.z, -1.0F));
    assert(published.size() == 1);
    assert(published.front().kind == ViewportChangeKind::CameraMoved);
    static_cast<void>(subscription);
}

void OrbitKeepsTheTargetFixedAndProducesAValidCamera()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    const Horo::Math::Vec3 target = viewport.Current().camera.target;

    const auto orbited =
        viewport.Navigate(EditorViewportNavigationDelta{.yawRadians = 0.4F, .pitchRadians = -0.2F, .orbit = true});
    assert(orbited.HasValue());
    assert(viewport.Current().camera.target == target);
    assert(viewport.Current().camera.IsValid());
    assert(!NearlyEqual(viewport.Current().camera.position.x, 0.0F));
}

void EmptyAndInvalidNavigationDoNotPublishOrMutate()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    std::vector<ViewportChangedEvent> published;
    auto subscription = events.Subscribe<ViewportChangedEvent>(
        [&published](const ViewportChangedEvent &event) { published.push_back(event); });

    assert(viewport.Navigate({}).HasValue());
    EditorViewportNavigationDelta invalid;
    invalid.moveRight = std::numeric_limits<float>::quiet_NaN();
    assert(viewport.Navigate(invalid).HasError());
    assert(viewport.Current().revision == ViewportRevision{});
    assert(published.empty());
    static_cast<void>(subscription);
}

void ProjectionChangesPreserveTargetPlaneScale()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    const float initialDistance =
        Horo::Math::Length(viewport.Current().camera.target - viewport.Current().camera.position);
    const float expectedHeight = 2.0F * initialDistance * std::tan(viewport.Current().camera.verticalFovRadians * 0.5F);
    assert(viewport.SetProjection(Horo::Runtime::CameraProjection::Orthographic).HasValue());
    assert(NearlyEqual(viewport.Current().camera.orthographicHeight, expectedHeight));
    assert(viewport.SetProjection(Horo::Runtime::CameraProjection::Perspective).HasValue());
    assert(NearlyEqual(Horo::Math::Length(viewport.Current().camera.target - viewport.Current().camera.position),
                       initialDistance));
    assert(viewport.Current().revision == ViewportRevision{2});
}

void FocusAndDollyAreProjectionAware()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    const Horo::Math::Aabb bounds{{-1.0F, -2.0F, -1.0F}, {1.0F, 2.0F, 1.0F}};
    assert(viewport.Focus(bounds, 0.5F).HasValue());
    assert(viewport.Current().camera.target == bounds.Center());
    const float perspectiveDistance =
        Horo::Math::Length(viewport.Current().camera.target - viewport.Current().camera.position);
    assert(viewport.Navigate(EditorViewportNavigationDelta{.dollyScale = 0.5F}).HasValue());
    assert(Horo::Math::Length(viewport.Current().camera.target - viewport.Current().camera.position) <
           perspectiveDistance);

    assert(viewport.SetProjection(Horo::Runtime::CameraProjection::Orthographic).HasValue());
    assert(viewport.Focus(bounds, 0.5F).HasValue());
    const float orthographicHeight = viewport.Current().camera.orthographicHeight;
    assert(orthographicHeight >= 2.0F * 2.0F * 1.2F / 0.5F);
    assert(viewport.Navigate(EditorViewportNavigationDelta{.dollyScale = 0.5F}).HasValue());
    assert(NearlyEqual(viewport.Current().camera.orthographicHeight, orthographicHeight * 0.5F));
    assert(viewport.Focus(bounds, 0.0F).HasError());
}

void TransformPreviewIsAuthoritativeWorkspaceState()
{
    using namespace Horo::Editor;
    EditorDataBus events;
    EditorViewportModel viewport{events};
    std::vector<ViewportChangedEvent> published;
    auto subscription = events.Subscribe<ViewportChangedEvent>(
        [&published](const ViewportChangedEvent &event) { published.push_back(event); });

    const SceneObjectTransformPreview preview{
        .object = SceneObjectId{42},
        .localTransform = Horo::Math::Transform{.translation = {1.0F, 2.0F, 3.0F}},
    };
    assert(viewport.SetTransformPreview(preview).HasValue());
    assert(viewport.Current().transformPreview == preview);
    assert(viewport.Current().revision == ViewportRevision{1});
    assert(published.size() == 1 && published.back().kind == ViewportChangeKind::ScenePreviewChanged);

    assert(viewport.SetTransformPreview(preview).HasValue());
    assert(published.size() == 1);
    assert(viewport.ClearTransformPreview());
    assert(!viewport.Current().transformPreview.has_value());
    assert(viewport.Current().revision == ViewportRevision{2});
    assert(published.size() == 2 && published.back().kind == ViewportChangeKind::ScenePreviewChanged);
    assert(!viewport.ClearTransformPreview());

    SceneObjectTransformPreview invalid = preview;
    invalid.localTransform.translation.x = std::numeric_limits<float>::quiet_NaN();
    assert(viewport.SetTransformPreview(invalid).HasError());
    assert(viewport.Current().revision == ViewportRevision{2});
    assert(published.size() == 2);
    static_cast<void>(subscription);
}
} // namespace

int main()
{
    NavigationMovesTheAuthoritativeCameraAndPublishesOnce();
    OrbitKeepsTheTargetFixedAndProducesAValidCamera();
    EmptyAndInvalidNavigationDoNotPublishOrMutate();
    ProjectionChangesPreserveTargetPlaneScale();
    FocusAndDollyAreProjectionAware();
    TransformPreviewIsAuthoritativeWorkspaceState();
    return 0;
}
