#include <catch2/catch_test_macros.hpp>

#include "Horo/Runtime/Render/NullBackendModule.h"
#include "Horo/Runtime/Render/RenderBackendRegistry.h"
#include "Horo/Runtime/Render/RenderFrontend.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
class TrackingStaticMeshExecutor final : public Horo::Render::IStaticMeshPassExecutor
{
  public:
    Horo::Result<void> ExecuteStaticMeshPass(const Horo::Render::StaticMeshPassDescriptor &) override
    {
        ++executeCount;
        return Horo::Result<void>::Success();
    }

    std::size_t executeCount{0};
};
using namespace Horo;
using namespace Horo::Render;

void Check(const bool condition)
{
    REQUIRE((condition));
}

enum class FrameThrowPoint
{
    None,
    Begin,
    Execute,
    Present,
};

struct BackendLifecycleState
{
    int initializeCount{0};
    int shutdownCount{0};
    int abortCount{0};
    int executeCount{0};
    int presentCount{0};
    int resizeCount{0};
    bool failPresentation{false};
    bool throwDuringInitialize{false};
    bool throwDuringResize{false};
    bool failResize{false};
    bool returnInvalidFrameToken{false};
    bool failBeginAfterActivation{false};
    FrameThrowPoint frameThrowPoint{FrameThrowPoint::None};
    bool frameActive{false};
    FrameToken activeFrame{};
};

BackendLifecycleState lifecycleState;

class TrackingBackend final : public IRenderBackend
{
  public:
    Result<void> Initialize(const RenderBackendConfig &) override
    {
        ++lifecycleState.initializeCount;
        initialized_ = true;
        if (lifecycleState.throwDuringInitialize)
        {
            throw std::runtime_error{"Injected initialization failure."};
        }
        return Result<void>::Success();
    }

    [[nodiscard]] const RenderBackendCapabilities &Capabilities() const noexcept override
    {
        return capabilities_;
    }

    Result<FrameToken> BeginFrame(const FrameDescriptor &descriptor) override
    {
        Check(!lifecycleState.frameActive);
        if (lifecycleState.frameThrowPoint == FrameThrowPoint::Begin)
        {
            lifecycleState.frameActive = true;
            lifecycleState.activeFrame = FrameToken{descriptor.frameNumber};
            throw std::runtime_error{"Injected begin failure."};
        }
        if (lifecycleState.failBeginAfterActivation)
        {
            lifecycleState.frameActive = true;
            lifecycleState.activeFrame = FrameToken{descriptor.frameNumber};
            return Result<FrameToken>::Failure({ErrorCode{"render.test.begin_failed"},
                                                ErrorDomainId{"render.test"},
                                                ErrorSeverity::Error,
                                                "Injected begin failure.",
                                                {}});
        }
        if (lifecycleState.returnInvalidFrameToken)
        {
            lifecycleState.frameActive = true;
            lifecycleState.activeFrame = {};
            return Result<FrameToken>::Success({});
        }
        lifecycleState.frameActive = true;
        lifecycleState.activeFrame = FrameToken{descriptor.frameNumber};
        return Result<FrameToken>::Success(lifecycleState.activeFrame);
    }

    Result<void> Execute(const RenderExecutionPlan &plan) override
    {
        ++lifecycleState.executeCount;
        Check(lifecycleState.frameActive);
        Check(plan.frame == lifecycleState.activeFrame);
        if (lifecycleState.frameThrowPoint == FrameThrowPoint::Execute)
        {
            throw std::runtime_error{"Injected execution failure."};
        }
        return Result<void>::Success();
    }

    Result<void> Present(const FrameToken frame) override
    {
        ++lifecycleState.presentCount;
        Check(lifecycleState.frameActive);
        Check(frame == lifecycleState.activeFrame);
        if (lifecycleState.frameThrowPoint == FrameThrowPoint::Present)
        {
            throw std::runtime_error{"Injected presentation exception."};
        }
        if (lifecycleState.failPresentation)
        {
            return Result<void>::Failure({ErrorCode{"render.test.present_failed"},
                                          ErrorDomainId{"render.test"},
                                          ErrorSeverity::Error,
                                          "Injected presentation failure.",
                                          {}});
        }
        lifecycleState.frameActive = false;
        lifecycleState.activeFrame = {};
        return Result<void>::Success();
    }

    void AbortFrame(const FrameToken frame) noexcept override
    {
        if (lifecycleState.frameActive && frame == lifecycleState.activeFrame)
        {
            ++lifecycleState.abortCount;
            lifecycleState.frameActive = false;
            lifecycleState.activeFrame = {};
        }
    }

    void AbortActiveFrame() noexcept override
    {
        if (lifecycleState.frameActive)
        {
            ++lifecycleState.abortCount;
            lifecycleState.frameActive = false;
            lifecycleState.activeFrame = {};
        }
    }

    Result<void> Resize(FramebufferExtent) override
    {
        ++lifecycleState.resizeCount;
        if (lifecycleState.throwDuringResize)
        {
            throw std::runtime_error{"Injected resize failure."};
        }
        if (lifecycleState.failResize)
        {
            return Result<void>::Failure({ErrorCode{"render.test.resize_failed"},
                                          ErrorDomainId{"render.test"},
                                          ErrorSeverity::Critical,
                                          "Injected typed resize failure.",
                                          {}});
        }
        return Result<void>::Success();
    }

    void Shutdown() noexcept override
    {
        if (initialized_)
        {
            ++lifecycleState.shutdownCount;
            initialized_ = false;
        }
    }

  private:
    RenderBackendCapabilities capabilities_{.backend = RenderBackendId{"tracking"}};
    bool initialized_{false};
};

class TrackingBackendProvider final : public IRenderBackendProvider
{
  public:
    Result<std::unique_ptr<IRenderBackend>> Create() const override
    {
        return Result<std::unique_ptr<IRenderBackend>>::Success(std::make_unique<TrackingBackend>());
    }
};

[[nodiscard]] std::unique_ptr<IRenderBackendProvider> MakeTrackingBackendProvider()
{
    return std::make_unique<TrackingBackendProvider>();
}

static_assert(!std::is_copy_constructible_v<RenderFrameScope>);
static_assert(!std::is_copy_assignable_v<RenderFrameScope>);
static_assert(std::is_nothrow_move_constructible_v<RenderFrameScope>);
static_assert(std::is_nothrow_move_assignable_v<RenderFrameScope>);

[[nodiscard]] std::unique_ptr<RenderFrontend> CreateTrackingFrontend()
{
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());
    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    return std::move(created).Value();
}

TEST_CASE("Frontend Stages Execution And Presentation", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 50, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();
    Check(lifecycleState.frameActive);
    Check(lifecycleState.executeCount == 0);
    Check(lifecycleState.presentCount == 0);

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
    };
    Check(frame.Execute(passes).HasValue());
    Check(lifecycleState.frameActive);
    Check(lifecycleState.executeCount == 1);
    Check(lifecycleState.presentCount == 0);
    Check(frame.Present().HasValue());
    Check(!lifecycleState.frameActive);
    Check(lifecycleState.presentCount == 1);
    Check(lifecycleState.abortCount == 0);
}

TEST_CASE("Frame Scope Move Transfers Abort Ownership", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    {
        auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 51, .outputExtent = {800, 600}});
        Check(begun.HasValue());
        RenderFrameScope original = std::move(begun).Value();
        RenderFrameScope moved = std::move(original);
        Check(lifecycleState.frameActive);
        (void)moved;
    }
    Check(!lifecycleState.frameActive);
    Check(lifecycleState.abortCount == 1);

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{2}, .kind = RenderPassKind::Graphics},
    };
    Check(frontend->SubmitFrame(FrameDescriptor{.frameNumber = 52, .outputExtent = {800, 600}}, passes).HasValue());
}

TEST_CASE("Frame Scope Rejects Invalid Stage Transitions Without Losing Recovery", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 53, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    const Result<void> earlyPresent = frame.Present();
    Check(earlyPresent.HasError());
    Check(earlyPresent.ErrorValue().code.Value() == "render.frontend.frame_not_executed");
    Check(lifecycleState.frameActive);
    Check(lifecycleState.abortCount == 0);

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{3}, .kind = RenderPassKind::Graphics},
    };
    Check(frame.Execute(passes).HasValue());
    const Result<void> duplicateExecute = frame.Execute(passes);
    Check(duplicateExecute.HasError());
    Check(duplicateExecute.ErrorValue().code.Value() == "render.frontend.frame_already_executed");
    Check(lifecycleState.executeCount == 1);
    Check(frame.Present().HasValue());
    Check(!lifecycleState.frameActive);
}

TEST_CASE("Frontend Rejects A Second Frame Without Aborting The Owned Scope", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 54, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    const auto duplicate = frontend->BeginFrame(FrameDescriptor{.frameNumber = 55, .outputExtent = {800, 600}});
    Check(duplicate.HasError());
    Check(duplicate.ErrorValue().code.Value() == "render.frontend.frame_already_active");
    Check(lifecycleState.frameActive);
    Check(lifecycleState.abortCount == 0);

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{4}, .kind = RenderPassKind::Graphics},
    };
    Check(frame.Execute(passes).HasValue());
    Check(frame.Present().HasValue());
}

TEST_CASE("Frontend Destruction Aborts And Invalidates An Outstanding Scope", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 56, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    frontend.reset();
    Check(lifecycleState.abortCount == 1);
    Check(lifecycleState.shutdownCount == 1);
    Check(!lifecycleState.frameActive);

    const Result<void> inactive = frame.Present();
    Check(inactive.HasError());
    Check(inactive.ErrorValue().code.Value() == "render.frontend.frame_not_active");
}

TEST_CASE("Frame Scope Move Assignment Aborts The Previous Frame And Transfers Ownership", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> trackingFrontend = CreateTrackingFrontend();
    auto trackingBegun = trackingFrontend->BeginFrame(FrameDescriptor{.frameNumber = 57, .outputExtent = {800, 600}});
    Check(trackingBegun.HasValue());
    RenderFrameScope destination = std::move(trackingBegun).Value();

    RenderBackendRegistry nullRegistry;
    Check(RegisterNullRenderBackend(nullRegistry).HasValue());
    Check(nullRegistry.Seal().HasValue());
    auto nullCreated = RenderFrontend::Create(nullRegistry, RenderBackendId{"null"}, RenderBackendConfig{});
    Check(nullCreated.HasValue());
    std::unique_ptr<RenderFrontend> nullFrontend = std::move(nullCreated).Value();
    auto nullBegun = nullFrontend->BeginFrame(FrameDescriptor{.frameNumber = 58, .outputExtent = {800, 600}});
    Check(nullBegun.HasValue());
    RenderFrameScope source = std::move(nullBegun).Value();

    destination = std::move(source);
    Check(lifecycleState.abortCount == 1);
    Check(!lifecycleState.frameActive);
    const Result<void> movedFrom = source.Present();
    Check(movedFrom.HasError());
    Check(movedFrom.ErrorValue().code.Value() == "render.frontend.frame_not_active");

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{5}, .kind = RenderPassKind::Graphics},
    };
    Check(destination.Execute(passes).HasValue());
    Check(destination.Present().HasValue());
}

TEST_CASE("Frame Scope Supports Self Move And Move After Execute", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 59, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    frame = std::move(frame);
    Check(lifecycleState.frameActive);
    Check(lifecycleState.abortCount == 0);

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{6}, .kind = RenderPassKind::Graphics},
    };
    Check(frame.Execute(passes).HasValue());
    RenderFrameScope moved = std::move(frame);
    Check(moved.Present().HasValue());
    Check(lifecycleState.executeCount == 1);
    Check(lifecycleState.presentCount == 1);
    Check(lifecycleState.abortCount == 0);
}

TEST_CASE("Frontend Rejects Resize During Active Frame And Supports Explicit Cancel", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    std::unique_ptr<RenderFrontend> frontend = CreateTrackingFrontend();
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 60, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    Check(frontend->Capabilities().backend == RenderBackendId{"tracking"});
    const Result<void> resize = frontend->Resize(FramebufferExtent{1024, 768});
    Check(resize.HasError());
    Check(resize.ErrorValue().code.Value() == "render.frontend.resize_during_frame");
    Check(lifecycleState.resizeCount == 0);
    Check(lifecycleState.frameActive);

    frame.Cancel();
    Check(lifecycleState.abortCount == 1);
    Check(!lifecycleState.frameActive);
    frame.Cancel();
    Check(lifecycleState.abortCount == 1);
    Check(frontend->Resize(FramebufferExtent{1024, 768}).HasValue());
    Check(lifecycleState.resizeCount == 1);
}

TEST_CASE("Frontend Initializes And Owns Selected Backend Lifetime", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    Check(lifecycleState.initializeCount == 1);

    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();
    Check(frontend->Capabilities().backend == RenderBackendId{"tracking"});
    frontend.reset();

    Check(lifecycleState.shutdownCount == 1);
}

TEST_CASE("Frontend Submits Frames And Recovers After Plan Failure", "[unit][runtime][renderer]")
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"null"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();

    const std::array validPasses{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
        RenderPassDescriptor{.id = RenderPassId{2}, .kind = RenderPassKind::Copy},
    };
    Check(
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {1280, 720}}, validPasses).HasValue());

    const std::array unsupportedPasses{
        RenderPassDescriptor{.id = RenderPassId{3}, .kind = RenderPassKind::Compute},
    };
    const Result<void> unsupported =
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 2, .outputExtent = {1280, 720}}, unsupportedPasses);
    Check(unsupported.HasError());
    Check(unsupported.ErrorValue().code.Value() == "render.backend.unsupported_pass_kind");

    Check(
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 3, .outputExtent = {1280, 720}}, validPasses).HasValue());
}

TEST_CASE("Frontend Recovers After Presentation Failure", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    lifecycleState.failPresentation = true;
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
    };
    const Result<void> failed =
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 10, .outputExtent = {640, 480}}, passes);
    Check(failed.HasError());
    Check(failed.ErrorValue().code.Value() == "render.test.present_failed");
    Check(lifecycleState.abortCount == 1);

    lifecycleState.failPresentation = false;
    Check(frontend->SubmitFrame(FrameDescriptor{.frameNumber = 11, .outputExtent = {640, 480}}, passes).HasValue());
}

TEST_CASE("Frontend Contains Initialization Exceptions And Cleans Up Partial State", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    lifecycleState.throwDuringInitialize = true;
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasError());
    Check(created.ErrorValue().code.Value() == "render.frontend.initialize_exception");
    Check(lifecycleState.initializeCount == 1);
    Check(lifecycleState.shutdownCount == 1);
}

TEST_CASE("Frontend Contains Frame Exceptions And Recovers Owned Frame State", "[unit][runtime][renderer]")
{
    constexpr std::array throwPoints{FrameThrowPoint::Begin, FrameThrowPoint::Execute, FrameThrowPoint::Present};
    for (const FrameThrowPoint throwPoint : throwPoints)
    {
        lifecycleState = {};
        lifecycleState.frameThrowPoint = throwPoint;
        RenderBackendRegistry registry;
        Check(registry
                  .Register(RenderBackendDescriptor{
                      .id = RenderBackendId{"tracking"},
                      .displayName = "Tracking",
                      .provider = MakeTrackingBackendProvider(),
                  })
                  .HasValue());
        Check(registry.Seal().HasValue());

        auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
        Check(created.HasValue());
        std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();
        const std::array passes{
            RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
        };

        const Result<void> failed =
            frontend->SubmitFrame(FrameDescriptor{.frameNumber = 20, .outputExtent = {640, 480}}, passes);
        Check(failed.HasError());
        Check(failed.ErrorValue().code.Value() == "render.frontend.frame_exception");
        Check(lifecycleState.abortCount == 1);

        lifecycleState.frameThrowPoint = FrameThrowPoint::None;
        Check(frontend->SubmitFrame(FrameDescriptor{.frameNumber = 21, .outputExtent = {640, 480}}, passes).HasValue());
    }
}

TEST_CASE("Frontend Owns Resize Boundary And Contains Backend Exceptions", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();
    Check(frontend->Resize(FramebufferExtent{1920, 1080}).HasValue());
    Check(lifecycleState.resizeCount == 1);

    lifecycleState.failResize = true;
    const Result<void> typedFailure = frontend->Resize(FramebufferExtent{1600, 900});
    Check(typedFailure.HasError());
    Check(typedFailure.ErrorValue().code.Value() == "render.test.resize_failed");
    Check(typedFailure.ErrorValue().domain.Value() == "render.test");
    Check(typedFailure.ErrorValue().severity == ErrorSeverity::Critical);
    Check(typedFailure.ErrorValue().message == "Injected typed resize failure.");

    lifecycleState.failResize = false;
    lifecycleState.throwDuringResize = true;
    const Result<void> failed = frontend->Resize(FramebufferExtent{1280, 720});
    Check(failed.HasError());
    Check(failed.ErrorValue().code.Value() == "render.frontend.resize_exception");

    lifecycleState.throwDuringResize = false;
    Check(frontend->Resize(FramebufferExtent{1280, 720}).HasValue());
}

TEST_CASE("Frontend Rejects Invalid Successful Frame Tokens And Recovers Backend State", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    lifecycleState.returnInvalidFrameToken = true;
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();
    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
    };

    const Result<void> failed =
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 30, .outputExtent = {640, 480}}, passes);
    Check(failed.HasError());
    Check(failed.ErrorValue().code.Value() == "render.frontend.invalid_frame_token");
    Check(lifecycleState.abortCount == 1);

    lifecycleState.returnInvalidFrameToken = false;
    Check(frontend->SubmitFrame(FrameDescriptor{.frameNumber = 31, .outputExtent = {640, 480}}, passes).HasValue());
}

TEST_CASE("Frontend Preserves Typed Begin Failures And Recovers Partial Backend State", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    lifecycleState.failBeginAfterActivation = true;
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"tracking"},
                  .displayName = "Tracking",
                  .provider = MakeTrackingBackendProvider(),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();
    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
    };

    const Result<void> failed =
        frontend->SubmitFrame(FrameDescriptor{.frameNumber = 40, .outputExtent = {640, 480}}, passes);
    Check(failed.HasError());
    Check(failed.ErrorValue().code.Value() == "render.test.begin_failed");
    Check(failed.ErrorValue().domain.Value() == "render.test");
    Check(failed.ErrorValue().severity == ErrorSeverity::Error);
    Check(lifecycleState.abortCount == 1);

    lifecycleState.failBeginAfterActivation = false;
    Check(frontend->SubmitFrame(FrameDescriptor{.frameNumber = 41, .outputExtent = {640, 480}}, passes).HasValue());
}

TEST_CASE("Frontend Owns Static Mesh Executor Attachment And Rejects Missing Execution", "[unit][runtime][renderer]")
{
    lifecycleState = {};
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{.id = RenderBackendId{"tracking"},
                                                .displayName = "Tracking",
                                                .provider = MakeTrackingBackendProvider()})
              .HasValue());
    Check(registry.Seal().HasValue());
    auto created = RenderFrontend::Create(registry, RenderBackendId{"tracking"}, RenderBackendConfig{});
    Check(created.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(created).Value();

    const std::array staticPass{RenderPassDescriptor{
        .id = RenderPassId{1}, .kind = RenderPassKind::Graphics, .staticMesh = StaticMeshPassDescriptor{}}};
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 50, .outputExtent = {64, 64}});
    Check(begun.HasValue());
    RenderFrameScope missingExecutorFrame = std::move(begun).Value();
    const Result<void> missing = missingExecutorFrame.Execute(staticPass);
    Check(missing.HasError());
    Check(missing.ErrorValue().code.Value() == "render.frontend.static_mesh_executor_missing");

    TrackingStaticMeshExecutor first;
    TrackingStaticMeshExecutor second;
    Check(frontend->AttachStaticMeshPassExecutor(first).HasValue());
    const Result<void> duplicate = frontend->AttachStaticMeshPassExecutor(second);
    Check(duplicate.HasError());
    Check(duplicate.ErrorValue().code.Value() == "render.frontend.static_mesh_executor_already_attached");
    frontend->DetachStaticMeshPassExecutor(first);
    Check(frontend->AttachStaticMeshPassExecutor(second).HasValue());
    frontend->DetachStaticMeshPassExecutor(second);

    const Result<RenderTargetHandle> invalidTarget = frontend->CreateOffscreenTarget({});
    Check(invalidTarget.HasError());
    auto target = frontend->CreateOffscreenTarget({32, 24});
    Check(target.HasValue());
    Check(frontend->ResizeOffscreenTarget(target.Value(), {64, 48}).HasValue());
    Check(frontend->ReleaseOffscreenTarget(target.Value()).HasValue());
    const Result<void> staleResize = frontend->ResizeOffscreenTarget(target.Value(), {16, 16});
    Check(staleResize.HasError());
    Check(staleResize.ErrorValue().code.Value() == "render.frontend.stale_render_target");
}
} // namespace
