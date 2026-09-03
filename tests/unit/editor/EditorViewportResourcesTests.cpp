#include "Horo/Runtime/Render/NullBackendModule.h"
#include "Horo/Runtime/Render/RenderFrontend.h"
#include "editor/renderer/EditorRendererErrors.h"
#include "editor/renderer/EditorViewportResources.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace {
    using namespace Horo;
    using namespace Horo::Editor;
    using namespace Horo::Render;

    struct ImageResolverProbe {
        RenderTextureViewHandle lastView;
        bool reject{false};
    };

    [[nodiscard]] ImageResolverProbe &ResolverProbe() noexcept {
        static ImageResolverProbe probe;
        return probe;
    }

    [[nodiscard]] Result<std::uintptr_t> ResolveImage(const RenderFrontend &, const RenderTextureViewHandle view) {
        ImageResolverProbe &probe = ResolverProbe();
        probe.lastView = view;
        if (probe.reject) {
            return Result<std::uintptr_t>::Failure(
                MakeError(RendererErrors::ViewportGeometryCreationFailed, "Injected viewport image resolution failure."));
        }
        return Result<std::uintptr_t>::Success(view.slot);
    }

    [[nodiscard]] std::unique_ptr<RenderFrontend> CreateFrontend() {
        RenderBackendRegistry registry;
        REQUIRE(RegisterNullRenderBackend(registry).HasValue());
        REQUIRE(registry.Seal().HasValue());
        auto created = RenderFrontend::Create(registry, RenderBackendId{"null"}, RenderBackendConfig{});
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    [[nodiscard]] EditorViewportResources CreateResources(RenderFrontend &frontend) {
        return EditorViewportResources{
            frontend,
            {.depthFormat = RenderTextureFormat::Depth32Float, .depthAspect = RenderTextureAspect::Depth, .resolveImage = &ResolveImage},
        };
    }

    void SettleResources(EditorViewportResources &resources, RenderFrontend &frontend, const RenderSceneView &scene,
                         const EditorViewportExtent extent) {
        for (int step = 0; step < 4; ++step) {
            REQUIRE(resources.Prepare(scene, extent).HasValue());
            REQUIRE(frontend.ProcessResourceRequests().HasValue());
        }
    }

    struct MeshSceneFixture {
        std::array<MeshVertex, 3> vertices{{
            {.position = {-1.0F, -1.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = {0.0F, 0.0F}},
            {.position = {1.0F, -1.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = {1.0F, 0.0F}},
            {.position = {0.0F, 1.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = {0.5F, 1.0F}},
        }};
        std::array<std::uint32_t, 3> indices{0, 1, 2};
        std::array<RenderMeshResourceView, 1> resources;

        [[nodiscard]] RenderSceneView Scene(const std::uint32_t generation) {
            resources[0] = {
                .handle = {.id = MeshResourceId{41}, .generation = generation},
                .vertices = vertices,
                .indices = indices,
                .localBounds = {{-1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}},
            };
            return {.meshResources = resources};
        }
    };
}  // namespace

TEST_CASE("Headless viewport resources replace resize generations atomically", "[unit][headless][renderer][resource]") {
    ResolverProbe() = {};
    std::unique_ptr<RenderFrontend> frontend = CreateFrontend();
    EditorViewportResources resources = CreateResources(*frontend);
    const RenderSceneView scene{};

    SettleResources(resources, *frontend, scene, {320, 180});
    REQUIRE(resources.IsReady());
    REQUIRE(resources.AllocatedExtent() == EditorViewportExtent{320, 180});
    const RenderTargetHandle original = resources.Target();
    const std::uintptr_t originalImage = resources.ImageIdentity();

    const auto pendingResize = resources.Prepare(scene, {640, 360});
    REQUIRE(pendingResize.HasValue());
    REQUIRE(pendingResize.Value() == std::optional<RenderTargetHandle>{original});
    REQUIRE(resources.Target() == original);
    REQUIRE(resources.ImageIdentity() == originalImage);
    REQUIRE(frontend->ProcessResourceRequests().HasValue());

    SettleResources(resources, *frontend, scene, {640, 360});
    REQUIRE(resources.IsReady());
    REQUIRE(resources.AllocatedExtent() == EditorViewportExtent{640, 360});
    REQUIRE(resources.Target() != original);
    const auto retired = frontend->ResourceState(original);
    REQUIRE(retired.HasError());
    REQUIRE(retired.ErrorValue().code.Value() == "render.frontend.resource.stale");

    const RenderTargetHandle replacement = resources.Target();
    const auto steady = resources.Prepare(scene, {640, 360});
    REQUIRE(steady.HasValue());
    REQUIRE(steady.Value() == std::optional<RenderTargetHandle>{replacement});
    const auto processed = frontend->ProcessResourceRequests();
    REQUIRE(processed.HasValue());
    REQUIRE(processed.Value() == 0);
    REQUIRE(resources.Target() == replacement);
}

TEST_CASE("Headless viewport resources preserve active meshes until replacements are ready", "[unit][headless][renderer][resource]") {
    ResolverProbe() = {};
    std::unique_ptr<RenderFrontend> frontend = CreateFrontend();
    EditorViewportResources resources = CreateResources(*frontend);
    MeshSceneFixture fixture;

    RenderSceneView scene = fixture.Scene(1);
    SettleResources(resources, *frontend, scene, {320, 180});
    const auto original = resources.FindMesh(41);
    REQUIRE(original.has_value());

    scene = fixture.Scene(2);
    REQUIRE(resources.Prepare(scene, {320, 180}).HasValue());
    const auto firstProcessed = frontend->ProcessResourceRequests();
    REQUIRE(firstProcessed.HasValue());
    REQUIRE(firstProcessed.Value() == 2);
    const auto firstPending = resources.FindMesh(41);
    REQUIRE(firstPending.has_value());
    REQUIRE(firstPending->mesh == original->mesh);
    REQUIRE(resources.Prepare(scene, {320, 180}).HasValue());
    const auto secondProcessed = frontend->ProcessResourceRequests();
    REQUIRE(secondProcessed.HasValue());
    REQUIRE(secondProcessed.Value() == 1);
    const auto secondPending = resources.FindMesh(41);
    REQUIRE(secondPending.has_value());
    REQUIRE(secondPending->mesh == original->mesh);
    REQUIRE(resources.Prepare(scene, {320, 180}).HasValue());

    const auto replacement = resources.FindMesh(41);
    REQUIRE(replacement.has_value());
    REQUIRE(replacement->mesh != original->mesh);
    REQUIRE(frontend->ResourceState(original->mesh).HasError());

    REQUIRE(resources.Prepare(RenderSceneView{}, {320, 180}).HasValue());
    REQUIRE_FALSE(resources.FindMesh(41).has_value());
    REQUIRE(frontend->ResourceState(replacement->mesh).HasError());
}

TEST_CASE("Headless viewport resources recover image resolution and shut down repeatedly", "[unit][headless][renderer][resource]") {
    ResolverProbe() = {.reject = true};
    std::unique_ptr<RenderFrontend> frontend = CreateFrontend();
    EditorViewportResources resources = CreateResources(*frontend);
    const RenderSceneView scene{};

    for (int step = 0; step < 3; ++step) {
        REQUIRE(resources.Prepare(scene, {320, 180}).HasValue());
        REQUIRE(frontend->ProcessResourceRequests().HasValue());
    }
    const auto rejected = resources.Prepare(scene, {320, 180});
    REQUIRE(rejected.HasError());
    REQUIRE(rejected.ErrorValue().code.Value() == RendererErrors::ViewportGeometryCreationFailed.code.Value());
    REQUIRE(ResolverProbe().lastView.IsValid());
    REQUIRE_FALSE(resources.Target().IsValid());

    ResolverProbe().reject = false;
    REQUIRE(resources.Prepare(scene, {320, 180}).HasValue());
    REQUIRE(resources.IsReady());
    const RenderTargetHandle target = resources.Target();
    const RenderTargetHandle shadowTarget = resources.ShadowTarget();

    resources.Shutdown();
    resources.Shutdown();
    REQUIRE(frontend->ResourceState(target).HasError());
    REQUIRE(frontend->ResourceState(shadowTarget).HasError());
    REQUIRE_FALSE(resources.IsReady());
}
