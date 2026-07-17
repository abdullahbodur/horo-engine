#include "Horo/Runtime/Render/NullBackendModule.h"
#include "Horo/Runtime/Render/RenderBackend.h"
#include "Horo/Runtime/Render/RenderBackendRegistry.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
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

enum class ProviderBehavior
{
    ReturnNull,
    ReturnFailure,
    Throw,
};

struct ProviderProbe
{
    int createCount{0};
    int destroyCount{0};
};

class TestBackendProvider final : public IRenderBackendProvider
{
  public:
    explicit TestBackendProvider(const ProviderBehavior behavior, ProviderProbe *probe = nullptr) noexcept
        : behavior_(behavior), probe_(probe)
    {
    }

    ~TestBackendProvider() override
    {
        if (probe_ != nullptr)
        {
            ++probe_->destroyCount;
        }
    }

    Result<std::unique_ptr<IRenderBackend>> Create() const override
    {
        if (probe_ != nullptr)
        {
            ++probe_->createCount;
        }
        switch (behavior_)
        {
        case ProviderBehavior::ReturnNull:
            return Result<std::unique_ptr<IRenderBackend>>::Success(nullptr);
        case ProviderBehavior::ReturnFailure:
            return Result<std::unique_ptr<IRenderBackend>>::Failure(Error{
                .code = ErrorCode{"render.test.provider_failure"},
                .domain = ErrorDomainId{"horo.render.test"},
                .severity = ErrorSeverity::Critical,
                .message = "Expected provider failure",
                .diagnostics = {{
                    .code = DiagnosticCode{"render.test.provider_diagnostic"},
                    .severity = DiagnosticSeverity::Warning,
                    .message = "Expected provider diagnostic",
                    .location = {.source = "provider", .line = 7, .column = 3},
                }},
            });
        case ProviderBehavior::Throw:
            throw std::runtime_error("Expected provider exception");
        }
        std::abort();
    }

  private:
    ProviderBehavior behavior_;
    ProviderProbe *probe_;
};

[[nodiscard]] std::unique_ptr<IRenderBackendProvider> MakeTestProvider(const ProviderBehavior behavior,
                                                                       ProviderProbe *probe = nullptr)
{
    return std::make_unique<TestBackendProvider>(behavior, probe);
}

void RegistryOwnsProviderAndDefersInvocationUntilCreate()
{
    ProviderProbe probe;
    {
        RenderBackendRegistry registry;
        Check(registry
                  .Register(RenderBackendDescriptor{
                      .id = RenderBackendId{"probed"},
                      .displayName = "Probed",
                      .provider = MakeTestProvider(ProviderBehavior::ReturnNull, &probe),
                  })
                  .HasValue());
        Check(probe.createCount == 0);
        Check(probe.destroyCount == 0);
        Check(registry.Seal().HasValue());
        Check(probe.createCount == 0);

        const auto created = registry.Create(RenderBackendId{"probed"});
        Check(created.HasError());
        Check(probe.createCount == 1);
        Check(probe.destroyCount == 0);
    }
    Check(probe.createCount == 1);
    Check(probe.destroyCount == 1);
}

void RegistryDestroysAProviderRejectedDuringRegistration()
{
    ProviderProbe probe;
    RenderBackendRegistry registry;
    const Result<void> rejected = registry.Register(RenderBackendDescriptor{
        .id = RenderBackendId{"Invalid"},
        .displayName = "Rejected",
        .provider = MakeTestProvider(ProviderBehavior::ReturnNull, &probe),
    });
    Check(rejected.HasError());
    Check(probe.createCount == 0);
    Check(probe.destroyCount == 1);
}

void RegistryRejectsInvalidAndDuplicateDescriptors()
{
    RenderBackendRegistry registry;

    const Result<void> invalid = registry.Register(
        RenderBackendDescriptor{.id = RenderBackendId{"broken"}, .displayName = "Broken", .provider = nullptr});
    Check(invalid.HasError());
    Check(invalid.ErrorValue().code.Value() == "render.registry.invalid_descriptor");

    const std::array<std::string, 6> invalidIds{
        "", "Invalid", "invalid backend", "1invalid", "invalid-", std::string(65, 'a'),
    };
    for (const std::string &id : invalidIds)
    {
        Check(registry
                  .Register(RenderBackendDescriptor{
                      .id = RenderBackendId{id},
                      .displayName = "Invalid ID",
                      .provider = MakeTestProvider(ProviderBehavior::ReturnNull),
                  })
                  .HasError());
    }

    Check(RegisterNullRenderBackend(registry).HasValue());
    const Result<void> duplicate = RegisterNullRenderBackend(registry);
    Check(duplicate.HasError());
    Check(duplicate.ErrorValue().code.Value() == "render.registry.duplicate_backend");
}

void RegistrySealsDeterministicallyAndRejectsLateMutation()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"secondary"},
                  .displayName = "Secondary",
                  .provider = MakeTestProvider(ProviderBehavior::ReturnNull),
              })
              .HasValue());
    Check(registry.Seal().HasValue());
    Check(registry.Seal().HasValue());

    const auto descriptors = registry.Descriptors();
    Check(descriptors.size() == 2);
    Check(descriptors.front().id == RenderBackendId{"null"});
    Check(descriptors.front().displayName == "Null");
    Check(descriptors.back().id == RenderBackendId{"secondary"});

    const Result<void> lateRegistration = RegisterNullRenderBackend(registry);
    Check(lateRegistration.HasError());
    Check(lateRegistration.ErrorValue().code.Value() == "render.registry.sealed");
}

void RegistryRequiresSealAndReturnsTypedUnknownBackendErrors()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());

    auto beforeSeal = registry.Create(RenderBackendId{"null"});
    Check(beforeSeal.HasError());
    Check(beforeSeal.ErrorValue().code.Value() == "render.registry.not_sealed");

    Check(registry.Seal().HasValue());
    auto missing = registry.Create(RenderBackendId{"missing"});
    Check(missing.HasError());
    Check(missing.ErrorValue().code.Value() == "render.registry.backend_not_found");
}

void RegistryRejectsAProviderThatReturnsNoBackendInstance()
{
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"null-pointer"},
                  .displayName = "Null Pointer",
                  .provider = MakeTestProvider(ProviderBehavior::ReturnNull),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto created = registry.Create(RenderBackendId{"null-pointer"});
    Check(created.HasError());
    Check(created.ErrorValue().code.Value() == "render.registry.provider_returned_null");
}

void RegistryPreservesProviderFailuresAndTranslatesExceptions()
{
    RenderBackendRegistry registry;
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"failing"},
                  .displayName = "Failing",
                  .provider = MakeTestProvider(ProviderBehavior::ReturnFailure),
              })
              .HasValue());
    Check(registry
              .Register(RenderBackendDescriptor{
                  .id = RenderBackendId{"throwing"},
                  .displayName = "Throwing",
                  .provider = MakeTestProvider(ProviderBehavior::Throw),
              })
              .HasValue());
    Check(registry.Seal().HasValue());

    auto failed = registry.Create(RenderBackendId{"failing"});
    Check(failed.HasError());
    const Error &failure = failed.ErrorValue();
    Check(failure.code.Value() == "render.test.provider_failure");
    Check(failure.domain.Value() == "horo.render.test");
    Check(failure.severity == ErrorSeverity::Critical);
    Check(failure.message == "Expected provider failure");
    Check(failure.diagnostics.size() == 1);
    Check(failure.diagnostics.front().code.Value() == "render.test.provider_diagnostic");
    Check(failure.diagnostics.front().severity == DiagnosticSeverity::Warning);
    Check(failure.diagnostics.front().message == "Expected provider diagnostic");
    Check(failure.diagnostics.front().location.source == "provider");
    Check(failure.diagnostics.front().location.line == 7);
    Check(failure.diagnostics.front().location.column == 3);

    auto threw = registry.Create(RenderBackendId{"throwing"});
    Check(threw.HasError());
    Check(threw.ErrorValue().code.Value() == "render.registry.provider_exception");
}

void NullProviderIsInertUntilExplicitInitialization()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());

    auto created = registry.Create(RenderBackendId{"null"});
    Check(created.HasValue());
    std::unique_ptr<IRenderBackend> backend = std::move(created).Value();

    const FrameDescriptor frame{.frameNumber = 1, .outputExtent = {1280, 720}};
    auto beforeInitialize = backend->BeginFrame(frame);
    Check(beforeInitialize.HasError());
    Check(beforeInitialize.ErrorValue().code.Value() == "render.backend.not_initialized");

    const RenderBackendConfig config{.requirePresentation = false, .enableValidation = true, .maxFramesInFlight = 2};
    Check(backend->Initialize(config).HasValue());
    Check(backend->Capabilities().backend == RenderBackendId{"null"});
    Check(!backend->Capabilities().presentsToWindow);
}

void NullBackendValidatesFrameLifecycleWithoutGpuWork()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());
    auto created = registry.Create(RenderBackendId{"null"});
    Check(created.HasValue());
    std::unique_ptr<IRenderBackend> backend = std::move(created).Value();
    Check(backend->Initialize(RenderBackendConfig{}).HasValue());

    const RenderExecutionPlan inactivePlan{};
    Check(backend->Execute(inactivePlan).ErrorValue().code.Value() == "render.backend.no_active_frame");
    Check(backend->Present(FrameToken{1}).ErrorValue().code.Value() == "render.backend.no_active_frame");

    auto begun = backend->BeginFrame(FrameDescriptor{.frameNumber = 7, .outputExtent = {640, 360}});
    Check(begun.HasValue());
    const FrameToken token = begun.Value();

    auto nested = backend->BeginFrame(FrameDescriptor{.frameNumber = 8, .outputExtent = {640, 360}});
    Check(nested.HasError());
    Check(nested.ErrorValue().code.Value() == "render.backend.frame_already_active");

    const std::array passes{
        RenderPassDescriptor{.id = RenderPassId{1}, .kind = RenderPassKind::Graphics},
        RenderPassDescriptor{.id = RenderPassId{2}, .kind = RenderPassKind::Copy},
    };
    const std::array computePass{
        RenderPassDescriptor{.id = RenderPassId{3}, .kind = RenderPassKind::Compute},
    };
    const auto unsupportedCompute = backend->Execute(RenderExecutionPlan{.frame = token, .orderedPasses = computePass});
    Check(unsupportedCompute.HasError());
    Check(unsupportedCompute.ErrorValue().code.Value() == "render.backend.unsupported_pass_kind");

    const std::array invalidPassKind{
        RenderPassDescriptor{.id = RenderPassId{3}, .kind = static_cast<RenderPassKind>(255)},
    };
    Check(backend->Execute(RenderExecutionPlan{.frame = token, .orderedPasses = invalidPassKind})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_execution_plan");

    const std::array duplicatePasses{
        RenderPassDescriptor{.id = RenderPassId{4}, .kind = RenderPassKind::Graphics},
        RenderPassDescriptor{.id = RenderPassId{4}, .kind = RenderPassKind::Copy},
    };
    Check(backend->Execute(RenderExecutionPlan{.frame = token, .orderedPasses = duplicatePasses})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_execution_plan");
    Check(backend->Resize(FramebufferExtent{800, 600}).ErrorValue().code.Value() == "render.backend.frame_active");
    Check(backend->Execute(RenderExecutionPlan{.frame = token, .orderedPasses = passes}).HasValue());

    const Result<void> wrongPresent = backend->Present(FrameToken{token.value + 1});
    Check(wrongPresent.HasError());
    Check(wrongPresent.ErrorValue().code.Value() == "render.backend.frame_token_mismatch");

    Check(backend->Present(token).HasValue());
    Check(backend->Resize(FramebufferExtent{1920, 1080}).HasValue());
    backend->Shutdown();
    backend->Shutdown();
}

void NullBackendRejectsPresentationRequirements()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());
    auto created = registry.Create(RenderBackendId{"null"});
    Check(created.HasValue());
    std::unique_ptr<IRenderBackend> backend = std::move(created).Value();

    const Result<void> initialized = backend->Initialize(RenderBackendConfig{.requirePresentation = true});
    Check(initialized.HasError());
    Check(initialized.ErrorValue().code.Value() == "render.null.presentation_unsupported");
}

void NullBackendRejectsInvalidConfigurationAndFrameExtent()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());

    auto invalidConfigurationBackend = registry.Create(RenderBackendId{"null"});
    Check(invalidConfigurationBackend.HasValue());
    Check(std::move(invalidConfigurationBackend)
              .Value()
              ->Initialize(RenderBackendConfig{.maxFramesInFlight = 0})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_config");

    auto invalidPresentModeBackend = registry.Create(RenderBackendId{"null"});
    Check(invalidPresentModeBackend.HasValue());
    Check(std::move(invalidPresentModeBackend)
              .Value()
              ->Initialize(RenderBackendConfig{.presentMode = static_cast<PresentMode>(0xFF)})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_config");

    auto invalidFrameBackend = registry.Create(RenderBackendId{"null"});
    Check(invalidFrameBackend.HasValue());
    auto backend = std::move(invalidFrameBackend).Value();
    Check(backend->Initialize(RenderBackendConfig{}).HasValue());
    Check(backend->BeginFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {0, 720}}).ErrorValue().code.Value() ==
          "render.backend.invalid_frame_descriptor");
}

void NullBackendValidatesPrimaryOutputAttachments()
{
    RenderBackendRegistry registry;
    Check(RegisterNullRenderBackend(registry).HasValue());
    Check(registry.Seal().HasValue());
    auto created = registry.Create(RenderBackendId{"null"});
    Check(created.HasValue());
    std::unique_ptr<IRenderBackend> backend = std::move(created).Value();
    Check(backend->Initialize(RenderBackendConfig{}).HasValue());

    auto begun = backend->BeginFrame(FrameDescriptor{.frameNumber = 9, .outputExtent = {640, 360}});
    Check(begun.HasValue());
    const FrameToken frame = begun.Value();
    const std::array validPasses{
        RenderPassDescriptor{
            .id = RenderPassId{10},
            .kind = RenderPassKind::Graphics,
            .primaryOutput =
                PrimaryOutputAttachment{
                    .loadOperation = AttachmentLoadOperation::Clear,
                    .storeOperation = AttachmentStoreOperation::Store,
                    .clearColor = ClearColor{0.05F, 0.1F, 0.2F, 1.0F},
                },
        },
        RenderPassDescriptor{
            .id = RenderPassId{11},
            .kind = RenderPassKind::Graphics,
            .primaryOutput =
                PrimaryOutputAttachment{
                    .loadOperation = AttachmentLoadOperation::Load,
                    .storeOperation = AttachmentStoreOperation::Store,
                },
        },
    };
    Check(backend->Execute(RenderExecutionPlan{.frame = frame, .orderedPasses = validPasses}).HasValue());

    const std::array invalidCopyPass{
        RenderPassDescriptor{
            .id = RenderPassId{12},
            .kind = RenderPassKind::Copy,
            .primaryOutput = PrimaryOutputAttachment{},
        },
    };
    Check(backend->Execute(RenderExecutionPlan{.frame = frame, .orderedPasses = invalidCopyPass})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_execution_plan");

    const std::array invalidClearPass{
        RenderPassDescriptor{
            .id = RenderPassId{13},
            .kind = RenderPassKind::Graphics,
            .primaryOutput =
                PrimaryOutputAttachment{
                    .loadOperation = AttachmentLoadOperation::Clear,
                    .clearColor = ClearColor{0.0F, 0.0F, std::numeric_limits<float>::infinity(), 1.0F},
                },
        },
    };
    Check(backend->Execute(RenderExecutionPlan{.frame = frame, .orderedPasses = invalidClearPass})
              .ErrorValue()
              .code.Value() == "render.backend.invalid_execution_plan");
    backend->AbortFrame(frame);
}
} // namespace

int main()
{
    RegistryOwnsProviderAndDefersInvocationUntilCreate();
    RegistryDestroysAProviderRejectedDuringRegistration();
    RegistryRejectsInvalidAndDuplicateDescriptors();
    RegistrySealsDeterministicallyAndRejectsLateMutation();
    RegistryRequiresSealAndReturnsTypedUnknownBackendErrors();
    RegistryRejectsAProviderThatReturnsNoBackendInstance();
    RegistryPreservesProviderFailuresAndTranslatesExceptions();
    NullProviderIsInertUntilExplicitInitialization();
    NullBackendValidatesFrameLifecycleWithoutGpuWork();
    NullBackendRejectsPresentationRequirements();
    NullBackendRejectsInvalidConfigurationAndFrameExtent();
    NullBackendValidatesPrimaryOutputAttachments();
    return 0;
}
