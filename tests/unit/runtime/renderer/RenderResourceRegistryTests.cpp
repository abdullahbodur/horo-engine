#include "RenderResourceRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace {
    using namespace Horo;
    using namespace Horo::Render;
    using namespace Horo::Render::Detail;

    static_assert(!std::is_same_v<RenderBufferHandle, RenderTextureHandle>);
    static_assert(!std::is_convertible_v<RenderBufferHandle, RenderTextureHandle>);

    [[nodiscard]] RenderResourceIdentity Identity(const ResourceReservation &reservation) {
        return reservation.identity;
    }

    struct ReleasedBackendResources {
        std::array<std::uint64_t, 4> instances{};
        std::size_t count{0};
    };

    void RecordBackendRelease(void *const context, const RenderResourceClass, const std::uint64_t backendInstance) noexcept {
        auto &released = *static_cast<ReleasedBackendResources *>(context);
        released.instances[released.count++] = backendInstance;
    }

    TEST_CASE("Resource registry publishes a pending generation and records completion", "[unit][runtime][renderer][resource]") {
        const auto owner = AcquireRenderResourceOwnerId();
        REQUIRE(owner.HasValue());
        RenderResourceRegistry registry{owner.Value(), {}};

        auto reserved = registry.Reserve(RenderResourceClass::Texture);
        REQUIRE(reserved.HasValue());
        REQUIRE(reserved.Value().identity.owner == owner.Value());
        REQUIRE(reserved.Value().identity.slot != 0);
        REQUIRE(reserved.Value().identity.generation != 0);
        REQUIRE(reserved.Value().operation.IsValid());
        REQUIRE(registry.State(RenderResourceClass::Texture, Identity(reserved.Value())).Value() == RenderResourceState::Pending);

        const Result<void> pending = registry.OperationResult(reserved.Value().operation);
        REQUIRE(pending.HasError());
        REQUIRE(pending.ErrorValue().code.Value() == "render.frontend.resource.operation_pending");

        REQUIRE(registry.Publish(RenderResourceClass::Texture, Identity(reserved.Value()), 17).HasValue());
        REQUIRE(registry.State(RenderResourceClass::Texture, Identity(reserved.Value())).Value() == RenderResourceState::Ready);
        REQUIRE(registry.BackendInstance(RenderResourceClass::Texture, Identity(reserved.Value())).Value() == 17);
        REQUIRE(registry.OperationResult(reserved.Value().operation).HasValue());
    }

    TEST_CASE("Resource registry rejects foreign wrong-type and stale identities", "[unit][runtime][renderer][resource]") {
        const auto firstOwner = AcquireRenderResourceOwnerId();
        const auto secondOwner = AcquireRenderResourceOwnerId();
        REQUIRE(firstOwner.HasValue());
        REQUIRE(secondOwner.HasValue());
        REQUIRE(firstOwner.Value() != secondOwner.Value());
        RenderResourceRegistry registry{firstOwner.Value(), {}};
        auto reserved = registry.Reserve(RenderResourceClass::Buffer);
        REQUIRE(reserved.HasValue());
        REQUIRE(registry.Publish(RenderResourceClass::Buffer, Identity(reserved.Value()), 1).HasValue());

        RenderResourceIdentity foreign = Identity(reserved.Value());
        foreign.owner = secondOwner.Value();
        const auto wrongOwner = registry.State(RenderResourceClass::Buffer, foreign);
        REQUIRE(wrongOwner.HasError());
        REQUIRE(wrongOwner.ErrorValue().code.Value() == "render.frontend.resource.wrong_owner");

        const auto wrongType = registry.State(RenderResourceClass::Texture, Identity(reserved.Value()));
        REQUIRE(wrongType.HasError());
        REQUIRE(wrongType.ErrorValue().code.Value() == "render.frontend.resource.wrong_type");

        REQUIRE(registry.Release(RenderResourceClass::Buffer, Identity(reserved.Value())).HasValue());
        REQUIRE(registry.DrainRetirements() == 1);
        const auto stale = registry.State(RenderResourceClass::Buffer, Identity(reserved.Value()));
        REQUIRE(stale.HasError());
        REQUIRE(stale.ErrorValue().code.Value() == "render.frontend.resource.stale");

        auto replacement = registry.Reserve(RenderResourceClass::Buffer);
        REQUIRE(replacement.HasValue());
        REQUIRE(replacement.Value().identity.slot == reserved.Value().identity.slot);
        REQUIRE(replacement.Value().identity.generation == reserved.Value().identity.generation + 1);
    }

    TEST_CASE("Resource registry enforces pending queue and slot bounds", "[unit][runtime][renderer][resource]") {
        const auto owner = AcquireRenderResourceOwnerId();
        REQUIRE(owner.HasValue());
        RenderResourceRegistry registry{owner.Value(),
                                        {.maximumSlots = 2,
                                         .maximumPendingRequests = 1,
                                         .retirementDrainBudget = 1,
                                         .maximumOperationResults = 2}};

        auto first = registry.Reserve(RenderResourceClass::Buffer);
        REQUIRE(first.HasValue());
        const auto queueFull = registry.Reserve(RenderResourceClass::Texture);
        REQUIRE(queueFull.HasError());
        REQUIRE(queueFull.ErrorValue().code.Value() == "render.frontend.resource.queue_full");
        REQUIRE(registry.Publish(RenderResourceClass::Buffer, Identity(first.Value()), 1).HasValue());

        auto second = registry.Reserve(RenderResourceClass::Texture);
        REQUIRE(second.HasValue());
        REQUIRE(registry.Publish(RenderResourceClass::Texture, Identity(second.Value()), 2).HasValue());
        const auto capacity = registry.Reserve(RenderResourceClass::Sampler);
        REQUIRE(capacity.HasError());
        REQUIRE(capacity.ErrorValue().code.Value() == "render.frontend.resource.capacity_exhausted");
    }

    TEST_CASE("Resource registry bounds retained operation results", "[unit][runtime][renderer][resource]") {
        const auto owner = AcquireRenderResourceOwnerId();
        REQUIRE(owner.HasValue());
        RenderResourceRegistry registry{owner.Value(),
                                        {.maximumSlots = 1,
                                         .maximumPendingRequests = 1,
                                         .retirementDrainBudget = 1,
                                         .maximumOperationResults = 1}};

        auto first = registry.Reserve(RenderResourceClass::Sampler);
        REQUIRE(first.HasValue());
        REQUIRE(registry.Publish(RenderResourceClass::Sampler, Identity(first.Value()), 1).HasValue());
        REQUIRE(registry.Release(RenderResourceClass::Sampler, Identity(first.Value())).HasValue());
        REQUIRE(registry.DrainRetirements() == 1);

        auto second = registry.Reserve(RenderResourceClass::Sampler);
        REQUIRE(second.HasValue());
        const auto evicted = registry.OperationResult(first.Value().operation);
        REQUIRE(evicted.HasError());
        REQUIRE(evicted.ErrorValue().code.Value() == "render.frontend.resource.operation_unknown");
    }

    TEST_CASE("Pinned dependencies and submissions defer retirement", "[unit][runtime][renderer][resource]") {
        const auto owner = AcquireRenderResourceOwnerId();
        REQUIRE(owner.HasValue());
        ReleasedBackendResources released;
        RenderResourceRegistry registry{owner.Value(),
                                        {.maximumSlots = 8,
                                         .maximumPendingRequests = 8,
                                         .retirementDrainBudget = 1,
                                         .maximumOperationResults = 8},
                                        &released,
                                        &RecordBackendRelease};

        auto buffer = registry.Reserve(RenderResourceClass::Buffer);
        REQUIRE(buffer.HasValue());
        REQUIRE(registry.Publish(RenderResourceClass::Buffer, Identity(buffer.Value()), 1).HasValue());
        const std::array dependencies{Identity(buffer.Value())};
        auto mesh = registry.Reserve(RenderResourceClass::Mesh, dependencies);
        REQUIRE(mesh.HasValue());
        REQUIRE(registry.Publish(RenderResourceClass::Mesh, Identity(mesh.Value()), 2).HasValue());

        REQUIRE(registry.AddSubmissionPin(RenderResourceClass::Mesh, Identity(mesh.Value())).HasValue());
        REQUIRE(registry.Release(RenderResourceClass::Buffer, Identity(buffer.Value())).HasValue());
        REQUIRE(registry.Release(RenderResourceClass::Mesh, Identity(mesh.Value())).HasValue());
        REQUIRE(registry.DrainRetirements() == 0);
        REQUIRE(released.count == 0);
        REQUIRE(registry.ReleaseSubmissionPin(RenderResourceClass::Mesh, Identity(mesh.Value())).HasValue());
        REQUIRE(registry.DrainRetirements() == 1);
        REQUIRE(released.count == 1);
        REQUIRE(released.instances[0] == 2);
        REQUIRE(registry.DrainRetirements() == 1);
        REQUIRE(released.count == 2);
        REQUIRE(released.instances[1] == 1);
    }

    TEST_CASE("Failure and shutdown preserve terminal operation results", "[unit][runtime][renderer][resource]") {
        const auto owner = AcquireRenderResourceOwnerId();
        REQUIRE(owner.HasValue());
        RenderResourceRegistry registry{owner.Value(), {}};

        auto failed = registry.Reserve(RenderResourceClass::ShaderModule);
        REQUIRE(failed.HasValue());
        const Error backendError{ErrorCode{"render.test.create_failed"},
                                 ErrorDomainId{"render.test"},
                                 ErrorSeverity::Error,
                                 "Injected backend creation failure.",
                                 {}};
        REQUIRE(registry.Fail(RenderResourceClass::ShaderModule, Identity(failed.Value()), backendError).HasValue());
        const auto failure = registry.OperationResult(failed.Value().operation);
        REQUIRE(failure.HasError());
        REQUIRE(failure.ErrorValue().code.Value() == "render.test.create_failed");
        REQUIRE(registry.DrainRetirements() == 1);

        auto cancelled = registry.Reserve(RenderResourceClass::Pipeline);
        REQUIRE(cancelled.HasValue());
        registry.Shutdown();
        const auto cancellation = registry.OperationResult(cancelled.Value().operation);
        REQUIRE(cancellation.HasError());
        REQUIRE(cancellation.ErrorValue().code.Value() == "render.frontend.resource.registry_stopped");
        const auto stopped = registry.Reserve(RenderResourceClass::Buffer);
        REQUIRE(stopped.HasError());
        REQUIRE(stopped.ErrorValue().code.Value() == "render.frontend.resource.registry_stopped");
        registry.Shutdown();
    }
}  // namespace
