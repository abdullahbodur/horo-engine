#include "runtime/renderer/modules/metal/MetalBackendInternal.h"
#include "Horo/Runtime/Render/RenderFrontend.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
using namespace Horo;
using namespace Horo::Render;

void Check(const bool condition)
{
    if (!condition)
    {
        std::abort();
    }
}

[[nodiscard]] Error MakePortError(const char *code, const char *message)
{
    return Error{ErrorCode{code}, ErrorDomainId{"horo.render.test"}, ErrorSeverity::Critical, message, {}};
}

enum class PortFailure
{
    None,
    Create,
    Begin,
    Execute,
    Present,
};

struct PortState
{
    int createCount{0};
    int beginCount{0};
    int executeCount{0};
    int presentCount{0};
    int abortCount{0};
    int resizeCount{0};
    int destroyCount{0};
    MetalPresentationDescriptor descriptor{};
    FramebufferExtent frameExtent{};
    FramebufferExtent resizedExtent{};
    PrimaryOutputAttachment attachment{};
    PortFailure failure{PortFailure::None};
    bool presentationCreated{false};
    bool frameActive{false};
};

class FakePresentationPort final : public IMetalPresentationPort
{
  public:
    explicit FakePresentationPort(PortState &state) noexcept : state_(&state)
    {
    }

    Result<void> CreateSurface() override
    {
        ++state_->createCount;
        if (state_->failure == PortFailure::Create)
        {
            return Result<void>::Failure(
                MakePortError("render.test.create_failed", "Injected Metal creation failure."));
        }
        state_->presentationCreated = true;
        return Result<void>::Success();
    }

    void *Layer() const noexcept override
    {
        return state_->presentationCreated ? reinterpret_cast<void *>(1) : nullptr;
    }

    void DestroySurface() noexcept override
    {
        if (state_->presentationCreated)
        {
            ++state_->destroyCount;
            state_->presentationCreated = false;
        }
    }

  private:
    PortState *state_{nullptr};
};

class FakeMetalRuntime final : public Detail::IMetalRuntime
{
  public:
    FakeMetalRuntime(IMetalPresentationPort &presentationPort, PortState &state) noexcept
        : presentationPort_(&presentationPort), state_(&state)
    {
    }

    Result<void> Initialize(const MetalPresentationDescriptor &descriptor) override
    {
        state_->descriptor = descriptor;
        const Result<void> created = presentationPort_->CreateSurface();
        initialized_ = created.HasValue();
        return created;
    }

    Result<void> BeginFrame(const FramebufferExtent extent) override
    {
        ++state_->beginCount;
        state_->frameExtent = extent;
        if (state_->failure == PortFailure::Begin)
        {
            return Result<void>::Failure(MakePortError("render.test.begin_failed", "Injected Metal begin failure."));
        }
        state_->frameActive = true;
        return Result<void>::Success();
    }

    Result<void> ExecutePrimaryOutput(const PrimaryOutputAttachment &attachment) override
    {
        ++state_->executeCount;
        state_->attachment = attachment;
        if (state_->failure == PortFailure::Execute)
        {
            return Result<void>::Failure(
                MakePortError("render.test.execute_failed", "Injected Metal execute failure."));
        }
        return Result<void>::Success();
    }

    Result<void> Present() override
    {
        ++state_->presentCount;
        if (state_->failure == PortFailure::Present)
        {
            return Result<void>::Failure(
                MakePortError("render.test.present_failed", "Injected Metal present failure."));
        }
        state_->frameActive = false;
        return Result<void>::Success();
    }

    void AbortFrame() noexcept override
    {
        if (state_->frameActive)
        {
            ++state_->abortCount;
            state_->frameActive = false;
        }
    }

    Result<void> Resize(const FramebufferExtent extent) override
    {
        ++state_->resizeCount;
        state_->resizedExtent = extent;
        return Result<void>::Success();
    }

    void Shutdown() noexcept override
    {
        AbortFrame();
        if (initialized_)
        {
            presentationPort_->DestroySurface();
            initialized_ = false;
        }
    }

  private:
    IMetalPresentationPort *presentationPort_{nullptr};
    PortState *state_{nullptr};
    bool initialized_{false};
};

class FakeMetalRuntimeFactory final : public Detail::IMetalRuntimeFactory
{
  public:
    explicit FakeMetalRuntimeFactory(PortState &state) noexcept : state_(&state)
    {
    }

    Result<std::unique_ptr<Detail::IMetalRuntime>> Create(
        IMetalPresentationPort &presentationPort, MetalEditorGraphicsBridge &) const override
    {
        return Result<std::unique_ptr<Detail::IMetalRuntime>>::Success(
            std::make_unique<FakeMetalRuntime>(presentationPort, *state_));
    }

  private:
    PortState *state_{nullptr};
};

[[nodiscard]] std::unique_ptr<IRenderBackend> CreateBackend(FakePresentationPort &port, PortState &state,
                                                            MetalEditorGraphicsBridge &bridge)
{
    FakeMetalRuntimeFactory runtimeFactory{state};
    RenderBackendRegistry registry;
    Check(Detail::RegisterMetalRenderBackendWithRuntimeFactory(registry, port, bridge, runtimeFactory).HasValue());
    Check(registry.Seal().HasValue());
    auto created = registry.Create(RenderBackendId{"metal"});
    Check(created.HasValue());
    return std::move(created).Value();
}

void ProviderIsInertAndBackendOwnsPresentationLifecycle()
{
    PortState state;
    FakePresentationPort port{state};
    MetalEditorGraphicsBridge bridge;
    FakeMetalRuntimeFactory runtimeFactory{state};
    RenderBackendRegistry registry;
    Check(Detail::RegisterMetalRenderBackendWithRuntimeFactory(registry, port, bridge, runtimeFactory).HasValue());
    Check(state.createCount == 0);
    Check(registry.Seal().HasValue());
    auto created = registry.Create(RenderBackendId{"metal"});
    Check(created.HasValue());
    Check(state.createCount == 0);
    std::unique_ptr<IRenderBackend> backend = std::move(created).Value();

    Check(backend
              ->Initialize(RenderBackendConfig{
                  .requirePresentation = true,
                  .enableValidation = true,
                  .maxFramesInFlight = 3,
                  .presentMode = PresentMode::Immediate,
              })
              .HasValue());
    Check(state.createCount == 1);
    Check(state.descriptor.enableValidation);
    Check(state.descriptor.maxFramesInFlight == 3);
    Check(state.descriptor.presentMode == PresentMode::Immediate);
    Check(backend->Capabilities().backend == RenderBackendId{"metal"});
    Check(backend->Capabilities().presentsToWindow);

    Check(backend->Resize(FramebufferExtent{1920, 1080}).HasValue());
    Check(state.resizeCount == 1);
    Check(state.resizedExtent.width == 1920 && state.resizedExtent.height == 1080);

    auto begun = backend->BeginFrame(FrameDescriptor{.frameNumber = 7, .outputExtent = {1280, 720}});
    Check(begun.HasValue());
    const FrameToken frame = begun.Value();
    Check(state.beginCount == 1);
    Check(state.frameExtent.width == 1280 && state.frameExtent.height == 720);

    const std::array passes{RenderPassDescriptor{
        .id = RenderPassId{1},
        .kind = RenderPassKind::Graphics,
        .primaryOutput =
            PrimaryOutputAttachment{
                .loadOperation = AttachmentLoadOperation::Clear,
                .storeOperation = AttachmentStoreOperation::Store,
                .clearColor = ClearColor{0.1F, 0.2F, 0.3F, 1.0F},
            },
    }};
    Check(backend->Execute(RenderExecutionPlan{.frame = frame, .orderedPasses = passes}).HasValue());
    Check(state.executeCount == 1);
    Check(state.attachment.clearColor.red == 0.1F);
    Check(state.attachment.clearColor.green == 0.2F);
    Check(state.attachment.clearColor.blue == 0.3F);
    Check(backend->Present(frame).HasValue());
    Check(state.presentCount == 1);

    backend->Shutdown();
    backend->Shutdown();
    Check(state.destroyCount == 1);
}

void FailuresPreserveTypedErrorsAndFrameRecovery()
{
    PortState state{.failure = PortFailure::Create};
    FakePresentationPort port{state};
    MetalEditorGraphicsBridge bridge;
    std::unique_ptr<IRenderBackend> backend = CreateBackend(port, state, bridge);
    const Result<void> createFailure = backend->Initialize(RenderBackendConfig{});
    Check(createFailure.HasError());
    Check(createFailure.ErrorValue().code.Value() == "render.test.create_failed");
    Check(state.destroyCount == 0);

    state.failure = PortFailure::None;
    Check(backend->Initialize(RenderBackendConfig{}).HasValue());
    state.failure = PortFailure::Begin;
    const Result<FrameToken> beginFailure =
        backend->BeginFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {800, 600}});
    Check(beginFailure.HasError());
    Check(beginFailure.ErrorValue().code.Value() == "render.test.begin_failed");

    state.failure = PortFailure::None;
    auto begun = backend->BeginFrame(FrameDescriptor{.frameNumber = 2, .outputExtent = {800, 600}});
    Check(begun.HasValue());
    const FrameToken frame = begun.Value();
    const std::array pass{RenderPassDescriptor{
        .id = RenderPassId{2},
        .kind = RenderPassKind::Graphics,
        .primaryOutput = PrimaryOutputAttachment{},
    }};
    Check(backend->Execute(RenderExecutionPlan{.frame = frame, .orderedPasses = pass}).HasValue());

    state.failure = PortFailure::Present;
    const Result<void> presentFailure = backend->Present(frame);
    Check(presentFailure.HasError());
    Check(presentFailure.ErrorValue().code.Value() == "render.test.present_failed");
    backend->AbortFrame(frame);
    Check(state.abortCount == 1);

    state.failure = PortFailure::None;
    Check(backend->BeginFrame(FrameDescriptor{.frameNumber = 3, .outputExtent = {800, 600}}).HasValue());
    backend->AbortActiveFrame();
    Check(state.abortCount == 2);
    backend->Shutdown();
}

void SharedPresentationPortRejectsOverlappingBackends()
{
    PortState state;
    FakePresentationPort port{state};
    MetalEditorGraphicsBridge bridge;
    FakeMetalRuntimeFactory runtimeFactory{state};
    RenderBackendRegistry registry;
    Check(Detail::RegisterMetalRenderBackendWithRuntimeFactory(registry, port, bridge, runtimeFactory).HasValue());
    Check(registry.Seal().HasValue());
    auto firstResult = registry.Create(RenderBackendId{"metal"});
    auto secondResult = registry.Create(RenderBackendId{"metal"});
    Check(firstResult.HasValue() && secondResult.HasValue());
    std::unique_ptr<IRenderBackend> first = std::move(firstResult).Value();
    std::unique_ptr<IRenderBackend> second = std::move(secondResult).Value();

    Check(first->Initialize(RenderBackendConfig{}).HasValue());
    const Result<void> overlapping = second->Initialize(RenderBackendConfig{});
    Check(overlapping.HasError());
    Check(overlapping.ErrorValue().code.Value() == "render.metal.presentation_in_use");
    first->Shutdown();
    Check(second->Initialize(RenderBackendConfig{}).HasValue());
    second->Shutdown();
    Check(state.destroyCount == 2);
}

void InvalidPlansDoNotReachThePresentationPort()
{
    PortState state;
    FakePresentationPort port{state};
    MetalEditorGraphicsBridge bridge;
    std::unique_ptr<IRenderBackend> backend = CreateBackend(port, state, bridge);
    Check(backend->Initialize(RenderBackendConfig{}).HasValue());
    auto begun = backend->BeginFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {640, 480}});
    Check(begun.HasValue());

    const std::array invalidPasses{
        RenderPassDescriptor{
            .id = RenderPassId{3}, .kind = RenderPassKind::Graphics, .primaryOutput = PrimaryOutputAttachment{}},
        RenderPassDescriptor{.id = RenderPassId{4}, .kind = RenderPassKind::Compute},
    };
    const Result<void> invalid =
        backend->Execute(RenderExecutionPlan{.frame = begun.Value(), .orderedPasses = invalidPasses});
    Check(invalid.HasError());
    Check(invalid.ErrorValue().code.Value() == "render.metal.unsupported_pass_kind");
    Check(state.executeCount == 0);
    backend->AbortActiveFrame();
    backend->Shutdown();
}
void ModuleInfoDescribesMetalWindowBeforeBackendCreation()
{
    const RenderBackendModuleInfo &info = GetMetalRenderBackendModuleInfo();
    Check(info.id == RenderBackendId{"metal"});
    Check(info.windowRequirements.presentation == RenderPresentationKind::Metal);
    Check(info.windowRequirements.resizable && info.windowRequirements.highPixelDensity);
    Check(info.supportsInteractivePresentation);
}

void UnsupportedDrawableBufferCountFailsBeforePresentationCreation()
{
    PortState state;
    FakePresentationPort port{state};
    MetalEditorGraphicsBridge bridge;
    std::unique_ptr<IRenderBackend> backend = CreateBackend(port, state, bridge);
    const Result<void> initialized = backend->Initialize(RenderBackendConfig{.maxFramesInFlight = 4});
    Check(initialized.HasError());
    Check(initialized.ErrorValue().code.Value() == "render.metal.unsupported_frames_in_flight");
    Check(state.createCount == 0);
}
} // namespace

int main()
{
    ModuleInfoDescribesMetalWindowBeforeBackendCreation();
    ProviderIsInertAndBackendOwnsPresentationLifecycle();
    FailuresPreserveTypedErrorsAndFrameRecovery();
    SharedPresentationPortRejectsOverlappingBackends();
    InvalidPlansDoNotReachThePresentationPort();
    UnsupportedDrawableBufferCountFailsBeforePresentationCreation();
    return 0;
}
