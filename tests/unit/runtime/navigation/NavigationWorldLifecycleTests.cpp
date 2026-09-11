#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationWorldLifecycle.h"
#include "navigation/NavigationTestAssertions.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace Horo::Navigation {
    namespace {
        using TestSupport::RequireError;

        class ObservedBackend final : public INavigationQueryBackend {
        public:
            explicit ObservedBackend(std::shared_ptr<std::atomic<std::uint32_t>> destructions) noexcept
                : destructions_(std::move(destructions)) {}

            ~ObservedBackend() override {
                destructions_->fetch_add(1);
            }

            [[nodiscard]] NavigationProviderCapabilities Capabilities() const noexcept override {
                constexpr NavigationQueryLimits limits{
                    .maximumNodeExpansions = 64,
                    .maximumResultPoints = 16,
                    .maximumSearchDistanceMeters = 1'000.0F,
                };
                return MakeAvailablePathQueryCapabilities(1, limits, 2);
            }

            [[nodiscard]] Result<NavigationPath> FindPath(const NavigationPathRequest &, const CancellationToken &) const override {
                return Result<NavigationPath>::Failure(MakeError(NavigationErrors::NoNavigationData));
            }

        private:
            std::shared_ptr<std::atomic<std::uint32_t>> destructions_;
        };

        [[nodiscard]] NavigationWorldActivationDescriptor Descriptor(const std::uint64_t scene, const std::uint64_t sceneGeneration,
                                                                     const std::uint64_t world, const std::uint64_t topology) {
            return {
                .scene = NavigationSceneRuntimeId::Create(scene).Value(),
                .sceneGeneration = NavigationSceneGeneration::Create(sceneGeneration).Value(),
                .world = NavigationWorldId::Create(world).Value(),
                .topology = NavigationGeneration::Create(topology).Value(),
            };
        }

        [[nodiscard]] std::unique_ptr<INavigationQueryBackend> Backend(const std::shared_ptr<std::atomic<std::uint32_t>> &destructions) {
            return std::make_unique<ObservedBackend>(destructions);
        }
    }  // namespace

    TEST_CASE("Navigation world activation validates bounded owner storage", "[unit][navigation][headless][lifecycle]") {
        RequireError(NavigationWorldLifecycle::Create(0), NavigationErrors::CapabilityDescriptorInvalid);
        RequireError(NavigationWorldLifecycle::Create(NavigationWorldLifecycle::MaximumRetiredWorlds + 1U),
                     NavigationErrors::CapabilityDescriptorInvalid);

        auto created = NavigationWorldLifecycle::Create(1);
        REQUIRE(created.HasValue());
        auto lifecycle = std::move(created).Value();
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Empty);
        REQUIRE(lifecycle.RetiredCount() == 0);
        REQUIRE_FALSE(lifecycle.HasStagedCandidate());

        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        RequireError(lifecycle.Stage({}, Backend(destructions)), NavigationErrors::CapabilityDescriptorInvalid);
        REQUIRE(destructions->load() == 1);
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Empty);
        REQUIRE_FALSE(lifecycle.HasStagedCandidate());
    }

    TEST_CASE("Navigation world publication is transactional and generation exact", "[unit][navigation][headless][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(2)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        const auto first = Descriptor(11, 1, 101, 1);
        const auto replacement = Descriptor(11, 2, 102, 2);

        REQUIRE(lifecycle.Stage(first, Backend(destructions)).HasValue());
        RequireError(lifecycle.CommitAtSafePoint(first.scene, NavigationSceneGeneration::Create(2).Value()),
                     NavigationErrors::StaleSnapshot);
        REQUIRE(lifecycle.HasStagedCandidate());
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Empty);
        REQUIRE(lifecycle.CommitAtSafePoint(first.scene, first.sceneGeneration).HasValue());
        REQUIRE(lifecycle.ActiveDescriptor().Value() == first);

        REQUIRE(lifecycle.Stage(replacement, Backend(destructions)).HasValue());
        RequireError(lifecycle.CommitAtSafePoint(first.scene, first.sceneGeneration), NavigationErrors::StaleSnapshot);
        REQUIRE(lifecycle.ActiveDescriptor().Value() == first);
        REQUIRE(lifecycle.CommitAtSafePoint(replacement.scene, replacement.sceneGeneration).HasValue());
        REQUIRE(lifecycle.ActiveDescriptor().Value() == replacement);
        REQUIRE(destructions->load() == 1);
    }

    TEST_CASE("Revoked navigation worlds remain pinned until worker leases drain", "[unit][navigation][headless][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(1)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        const auto first = Descriptor(21, 1, 201, 1);
        const auto replacement = Descriptor(21, 2, 202, 2);
        REQUIRE(lifecycle.Stage(first, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(first.scene, first.sceneGeneration).HasValue());
        auto oldLease = std::move(lifecycle.Acquire(first.world)).Value();
        const CancellationToken oldCancellation = oldLease.Cancellation();

        REQUIRE(lifecycle.Stage(replacement, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(replacement.scene, replacement.sceneGeneration).HasValue());
        REQUIRE(oldLease.IsRevoked());
        REQUIRE(oldCancellation.IsCancellationRequested());
        REQUIRE(lifecycle.RetiredCount() == 1);
        REQUIRE(destructions->load() == 0);

        NavigationWorldReadLease workerLease = oldLease;
        std::atomic<bool> workerObservedRevocation{false};
        std::thread worker{[lease = std::move(workerLease), &workerObservedRevocation]() mutable {
            workerObservedRevocation.store(lease.IsValid() && lease.IsRevoked() && lease.Cancellation().IsCancellationRequested());
        }};
        worker.join();
        REQUIRE(workerObservedRevocation.load());
        oldLease = std::move(lifecycle.Acquire(replacement.world)).Value();
        REQUIRE(lifecycle.CollectRetired() == NavigationWorldLifecycleState::Active);
        REQUIRE(lifecycle.RetiredCount() == 0);
        REQUIRE(destructions->load() == 1);
    }

    TEST_CASE("Retirement saturation preserves the published navigation world", "[unit][navigation][headless][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(1)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        const auto first = Descriptor(31, 1, 301, 1);
        const auto second = Descriptor(31, 2, 302, 2);
        const auto third = Descriptor(31, 3, 303, 3);
        REQUIRE(lifecycle.Stage(first, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(first.scene, first.sceneGeneration).HasValue());
        auto firstLease = std::move(lifecycle.Acquire(first.world)).Value();
        REQUIRE(lifecycle.Stage(second, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(second.scene, second.sceneGeneration).HasValue());
        auto secondLease = std::move(lifecycle.Acquire(second.world)).Value();
        REQUIRE(lifecycle.Stage(third, Backend(destructions)).HasValue());

        RequireError(lifecycle.CommitAtSafePoint(third.scene, third.sceneGeneration), NavigationErrors::CapacityExceeded);
        REQUIRE(lifecycle.ActiveDescriptor().Value() == second);
        REQUIRE(lifecycle.HasStagedCandidate());
        firstLease = std::move(secondLease);
        REQUIRE(lifecycle.CollectRetired() == NavigationWorldLifecycleState::Active);
        REQUIRE(lifecycle.CommitAtSafePoint(third.scene, third.sceneGeneration).HasValue());
        REQUIRE(lifecycle.ActiveDescriptor().Value() == third);
    }

    TEST_CASE("Pause resume unload and shutdown define admission and release order", "[unit][navigation][headless][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(2)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        const auto active = Descriptor(41, 1, 401, 1);
        const auto staged = Descriptor(41, 2, 402, 2);
        REQUIRE(lifecycle.Stage(active, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(active.scene, active.sceneGeneration).HasValue());
        auto lease = std::move(lifecycle.Acquire(active.world)).Value();

        REQUIRE(lifecycle.Pause(active.world).HasValue());
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Paused);
        RequireError(lifecycle.Acquire(active.world), NavigationErrors::CapabilityUnavailable);
        REQUIRE_FALSE(lease.Cancellation().IsCancellationRequested());
        REQUIRE(lifecycle.Resume(active.world).HasValue());
        REQUIRE(lifecycle.Stage(staged, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.Unload(active.world).HasValue());
        REQUIRE_FALSE(lifecycle.HasStagedCandidate());
        REQUIRE(lease.IsRevoked());
        REQUIRE(lease.Cancellation().IsCancellationRequested());
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Empty);

        lifecycle.BeginShutdown();
        lifecycle.BeginShutdown();
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::Closed);
        RequireError(lifecycle.Stage(staged, Backend(destructions)), NavigationErrors::CapabilityUnavailable);
    }

    TEST_CASE("Shutdown cancellation is nonblocking and closes after retained leases drain", "[unit][navigation][headless][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(1)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        const auto active = Descriptor(51, 1, 501, 1);
        REQUIRE(lifecycle.Stage(active, Backend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(active.scene, active.sceneGeneration).HasValue());
        auto lease = std::move(lifecycle.Acquire(active.world)).Value();

        lifecycle.BeginShutdown();
        REQUIRE(lifecycle.State() == NavigationWorldLifecycleState::ShuttingDown);
        REQUIRE(lease.IsRevoked());
        REQUIRE(lease.Cancellation().IsCancellationRequested());
        RequireError(lifecycle.Acquire(active.world), NavigationErrors::CapabilityUnavailable);
        REQUIRE(destructions->load() == 0);

        {
            auto moved = std::move(lease);
            REQUIRE_FALSE(lease.IsValid());
            REQUIRE(moved.IsValid());
        }
        REQUIRE(lifecycle.CollectRetired() == NavigationWorldLifecycleState::Closed);
        REQUIRE(destructions->load() == 1);
    }
}  // namespace Horo::Navigation
