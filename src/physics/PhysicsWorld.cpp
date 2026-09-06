#include "Horo/Physics/PhysicsWorld.h"

#include "CanonicalPhysicsRuntime.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Physics {
    /** @brief Shared only by the process wrapper and its worlds; identity pointers are owner-thread, stable-address registrations. */
    struct PhysicsRuntime::Impl final {
        explicit Impl(const PhysicsRuntimeMode selectedMode) : mode(selectedMode) {}

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;

        ~Impl() {
            Detail::DestroyCanonicalRuntime(native);
        }

        void ReleaseNativeWhenIdle() noexcept {
            if (const std::array releaseConditions{state == PhysicsRuntimeState::Stopped, identities.empty()};
                !std::ranges::all_of(releaseConditions, std::identity{}))
                return;
            Detail::DestroyCanonicalRuntime(native);
            native = {};
        }

        PhysicsRuntimeMode mode;
        PhysicsRuntimeState state{PhysicsRuntimeState::Ready};
        std::thread::id ownerThread{std::this_thread::get_id()};
        Detail::CanonicalRuntimeHandle native;
        std::vector<const PhysicsWorldId *> identities;
    };

    /** @brief Owns one candidate's settings/native state and unregisters its identity before releasing the runtime lease. */
    struct PhysicsWorld::Impl final {
        Impl(std::shared_ptr<PhysicsRuntime::Impl> runtimeOwner, const PhysicsWorldSettings &worldSettings)
            : runtime(std::move(runtimeOwner)), settings(worldSettings), commands(worldSettings.Values().budgets.maximumCommands) {
            runtime->identities.push_back(&identity);
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;

        ~Impl() {
            Shutdown();
        }

        void Shutdown() noexcept {
            if (state == PhysicsWorldState::Destroyed)
                return;
            Detail::DestroyCanonicalWorld(native);
            native = {};
            state = PhysicsWorldState::Destroyed;
            std::erase(runtime->identities, &identity);
            runtime->ReleaseNativeWhenIdle();
        }

        [[nodiscard]] const PhysicsStructuralCommand &CommandAt(const std::uint32_t offset) const noexcept {
            return commands[(commandHead + offset) % commands.size()];
        }

        void DiscardCommands(const std::uint32_t discarded) noexcept {
            if (discarded == 0)
                return;
            commandHead = (commandHead + discarded) % commands.size();
            commandCount -= discarded;
            statistics.pendingCommands = commandCount;
        }

        std::shared_ptr<PhysicsRuntime::Impl> runtime;
        PhysicsWorldSettings settings;
        PhysicsWorldState state{PhysicsWorldState::Preparing};
        PhysicsWorldId identity;
        Detail::CanonicalWorldHandle native;
        std::vector<PhysicsStructuralCommand> commands;
        std::uint32_t commandHead{};
        std::uint32_t commandCount{};
        std::uint64_t lastAdmittedCommandSequence{};
        bool stepping{};
        // The owner thread alone writes publication state; any live-world thread may take a coherent snapshot.
        // The flag protects only the bounded copy below and owns no worker or shutdown lifetime.
        mutable std::atomic_flag publicationLock = ATOMIC_FLAG_INIT;
        PhysicsPublishedTick published;
        PhysicsTickStatistics statistics;
    };

    namespace {
        /** @brief Provides non-throwing mutual exclusion for the bounded publication snapshot copy. */
        class PublicationGuard final {
        public:
            explicit PublicationGuard(std::atomic_flag &lock) noexcept : lock_(lock) {
                while (lock_.test_and_set(std::memory_order_acquire))
                    std::this_thread::yield();
            }

            ~PublicationGuard() {
                lock_.clear(std::memory_order_release);
            }

        private:
            std::atomic_flag &lock_;
        };

        /** @brief Records bounded-buffer rejection metrics and preserves explicit destruction retry ownership. */
        [[nodiscard]] PhysicsCommandAdmission RejectFullCommand(auto &impl, const bool destruction) noexcept {
            ++impl.statistics.rejectedCommands;
            if (!destruction)
                return {PhysicsCommandAdmissionStatus::RejectedFull, impl.commandCount};
            ++impl.statistics.destructionRetryCount;
            return {PhysicsCommandAdmissionStatus::DestructionRetryRequired, impl.commandCount};
        }

        /** @brief Replaces every externally visible publication domain under one synchronization boundary. */
        void PublishCompletedTick(auto &impl, const std::uint64_t tick, const std::uint32_t appliedCommands) noexcept {
            PublicationGuard publicationGuard{impl.publicationLock};
            const std::uint64_t revision = impl.published.publicationRevision + 1;
            impl.published = {.completedTick = tick,
                              .publicationRevision = revision,
                              .transformTick = tick,
                              .queryTick = tick,
                              .eventTick = tick,
                              .appliedCommands = appliedCommands};
        }

        /** @brief Validates the opaque structural envelope before it enters retained world storage. */
        [[nodiscard]] bool IsValidCommand(const PhysicsStructuralCommand &command) noexcept {
            return command.sequence != 0 && command.sceneGeneration != 0 && command.subject != 0 &&
                   command.kind <= PhysicsStructuralCommandKind::Destroy;
        }

        /** @brief Emits one optional synchronous phase observation on the owner thread. */
        void ObservePhase(const PhysicsFixedTickInput &input, const PhysicsTickPhase phase) noexcept {
            if (input.observer.phase)
                input.observer.phase(input.observer.context, phase, input.simulationTick);
        }

        /** @brief Emits eligible commands selected for one semantic safe point without mutating the queue. */
        void ObserveCommands(const auto &impl, const PhysicsFixedTickInput &input, const std::uint32_t eligible,
                             const PhysicsStructuralCommandKind selectedKind, const PhysicsCommandSafePoint safePoint,
                             std::uint32_t &applied) noexcept {
            for (std::uint32_t offset = 0; offset < eligible; ++offset) {
                const PhysicsStructuralCommand &command = impl.CommandAt(offset);
                const bool selected = selectedKind == PhysicsStructuralCommandKind::Destroy
                                          ? command.kind == PhysicsStructuralCommandKind::Destroy
                                          : command.kind != PhysicsStructuralCommandKind::Destroy;
                if (!selected)
                    continue;
                if (input.observer.command)
                    input.observer.command(input.observer.context, command, safePoint, input.simulationTick);
                ++applied;
            }
        }
    }  // namespace

    /** @copydoc PhysicsRuntime::Create */
    Result<std::unique_ptr<PhysicsRuntime>> PhysicsRuntime::Create(const PhysicsRuntimeMode mode) {
        if (mode > PhysicsRuntimeMode::Null)
            return Result<std::unique_ptr<PhysicsRuntime>>::Failure(
                MakeError(PhysicsErrors::OperationUnsupported, "Unknown Physics runtime composition."));
        try {
            auto impl = std::make_shared<Impl>(mode);
            if (mode == PhysicsRuntimeMode::Canonical) {
                const auto native = Detail::CreateCanonicalRuntime();
                if (native.HasError()) {
                    impl->state = PhysicsRuntimeState::Failed;
                    return Result<std::unique_ptr<PhysicsRuntime>>::Failure(native.ErrorValue());
                }
                impl->native = native.Value();
            }
            return Result<std::unique_ptr<PhysicsRuntime>>::Success(std::unique_ptr<PhysicsRuntime>{new PhysicsRuntime(std::move(impl))});
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<PhysicsRuntime>>::Failure(
                MakeError(PhysicsErrors::CapacityExceeded, "Unable to allocate Physics runtime ownership state."));
        }
    }

    /** @copydoc PhysicsRuntime::~PhysicsRuntime */
    PhysicsRuntime::~PhysicsRuntime() {
        Shutdown();
    }

    /** @copydoc PhysicsRuntime::PrepareWorld */
    Result<std::unique_ptr<PhysicsWorld>> PhysicsRuntime::PrepareWorld(const PhysicsWorldSettings &settings) {
        if (const std::array admissionConditions{
                impl_->state == PhysicsRuntimeState::Ready,
                impl_->ownerThread == std::this_thread::get_id(),
            };
            !std::ranges::all_of(admissionConditions, std::identity{}))
            return Result<std::unique_ptr<PhysicsWorld>>::Failure(MakeError(PhysicsErrors::InvalidState));
        try {
            using enum PhysicsWorldState;
            auto worldImpl = std::make_unique<PhysicsWorld::Impl>(impl_, settings);
            if (impl_->mode == PhysicsRuntimeMode::Canonical) {
                const auto created = Detail::CreateCanonicalWorld(impl_->native, settings);
                if (created.HasError()) {
                    worldImpl->state = Failed;
                    return Result<std::unique_ptr<PhysicsWorld>>::Failure(created.ErrorValue());
                }
                worldImpl->native = created.Value();
                worldImpl->state = PreparedSolver;
            } else {
                worldImpl->state = PreparedNull;
            }
            return Result<std::unique_ptr<PhysicsWorld>>::Success(std::unique_ptr<PhysicsWorld>{new PhysicsWorld(std::move(worldImpl))});
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<PhysicsWorld>>::Failure(
                MakeError(PhysicsErrors::CapacityExceeded, "Unable to allocate Physics world ownership state."));
        }
    }

    /** @copydoc PhysicsRuntime::Shutdown */
    void PhysicsRuntime::Shutdown() noexcept {
        impl_->state = PhysicsRuntimeState::Stopped;
        impl_->ReleaseNativeWhenIdle();
    }

    /** @copydoc PhysicsRuntime::Mode */
    PhysicsRuntimeMode PhysicsRuntime::Mode() const noexcept {
        return impl_->mode;
    }

    /** @copydoc PhysicsRuntime::State */
    PhysicsRuntimeState PhysicsRuntime::State() const noexcept {
        return impl_->state;
    }

    /** @copydoc PhysicsRuntime::Availability */
    PhysicsAvailability PhysicsRuntime::Availability() const noexcept {
        const auto canonical = static_cast<std::uint8_t>(impl_->mode == PhysicsRuntimeMode::Canonical);
        const auto ready = static_cast<std::uint8_t>(impl_->state == PhysicsRuntimeState::Ready);
        return static_cast<PhysicsAvailability>(canonical * (static_cast<std::uint8_t>(PhysicsAvailability::Unavailable) + ready));
    }

    /** @copydoc PhysicsRuntime::Capability */
    PhysicsCapabilitySupport PhysicsRuntime::Capability(const PhysicsCapability capability) const noexcept {
        if (capability >= PhysicsCapability::Count)
            return PhysicsCapabilitySupport::Unknown;
        const auto canonicalWorld = static_cast<std::uint8_t>(impl_->mode == PhysicsRuntimeMode::Canonical) *
                                    static_cast<std::uint8_t>(capability == PhysicsCapability::WorldCreation);
        const auto ready = static_cast<std::uint8_t>(impl_->state == PhysicsRuntimeState::Ready);
        return static_cast<PhysicsCapabilitySupport>(static_cast<std::uint8_t>(PhysicsCapabilitySupport::Unsupported) +
                                                     canonicalWorld * (1U + ready));
    }

    /** @copydoc PhysicsWorld::PhysicsWorld */
    PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    /** @copydoc PhysicsWorld::~PhysicsWorld */
    PhysicsWorld::~PhysicsWorld() = default;

    /** @copydoc PhysicsWorld::Activate */
    Result<void> PhysicsWorld::Activate(const PhysicsWorldId identity) {
        const auto state = static_cast<std::uint8_t>(impl_->state);
        const auto firstPrepared = static_cast<std::uint8_t>(PhysicsWorldState::PreparedSolver);
        const auto prepared =
            static_cast<std::uint8_t>(state - firstPrepared) <= static_cast<std::uint8_t>(PhysicsWorldState::PreparedNull) - firstPrepared;
        if (const std::array admissionConditions{
                prepared,
                impl_->runtime->state == PhysicsRuntimeState::Ready,
                impl_->runtime->ownerThread == std::this_thread::get_id(),
            };
            !std::ranges::all_of(admissionConditions, std::identity{}))
            return Result<void>::Failure(MakeError(PhysicsErrors::InvalidState));
        if (!identity.IsValid())
            return Result<void>::Failure(MakeError(PhysicsErrors::WorldInvalid));
        if (std::ranges::any_of(impl_->runtime->identities, [identity](const auto *existing) {
            return *existing == identity;
        }))
            return Result<void>::Failure(
                MakeError(PhysicsErrors::WorldInvalid, "The world generation is already active in this Physics runtime."));
        impl_->identity = identity;
        impl_->state = static_cast<PhysicsWorldState>(state + 2U);
        return Result<void>::Success();
    }

    /** @copydoc PhysicsWorld::Shutdown */
    void PhysicsWorld::Shutdown() noexcept {
        impl_->Shutdown();
    }

    /** @copydoc PhysicsWorld::State */
    PhysicsWorldState PhysicsWorld::State() const noexcept {
        return impl_->state;
    }

    /** @copydoc PhysicsWorld::Identity */
    PhysicsWorldId PhysicsWorld::Identity() const noexcept {
        return impl_->identity;
    }

    /** @copydoc PhysicsWorld::Settings */
    const PhysicsWorldSettings &PhysicsWorld::Settings() const noexcept {
        return impl_->settings;
    }

    /** @copydoc PhysicsWorld::QueueStructuralCommand */
    Result<PhysicsCommandAdmission> PhysicsWorld::QueueStructuralCommand(const PhysicsStructuralCommand &command) {
        if (impl_->state == PhysicsWorldState::ActiveNull)
            return Result<PhysicsCommandAdmission>::Failure(MakeError(PhysicsErrors::CapabilityUnavailable));
        if (impl_->state != PhysicsWorldState::ActiveSolver || impl_->runtime->ownerThread != std::this_thread::get_id())
            return Result<PhysicsCommandAdmission>::Failure(MakeError(PhysicsErrors::InvalidState));
        if (!IsValidCommand(command) || command.sequence <= impl_->lastAdmittedCommandSequence)
            return Result<PhysicsCommandAdmission>::Failure(
                MakeError(PhysicsErrors::DescriptorInvalid,
                          "Physics structural commands require non-zero identities, a known kind and increasing sequence order."));

        const std::uint32_t capacity = static_cast<std::uint32_t>(impl_->commands.size());
        if (capacity == 0)
            return Result<PhysicsCommandAdmission>::Failure(
                MakeError(PhysicsErrors::InvalidState, "Validated Physics command storage is unexpectedly unavailable."));
        const bool destruction = command.kind == PhysicsStructuralCommandKind::Destroy;
        const std::uint32_t ordinaryLimit = capacity - 1;
        if (impl_->commandCount >= (destruction ? capacity : ordinaryLimit))
            return Result<PhysicsCommandAdmission>::Success(RejectFullCommand(*impl_, destruction));

        const std::uint32_t tail = (impl_->commandHead + impl_->commandCount) % capacity;
        impl_->commands[tail] = command;
        ++impl_->commandCount;
        impl_->lastAdmittedCommandSequence = command.sequence;
        ++impl_->statistics.admittedCommands;
        impl_->statistics.pendingCommands = impl_->commandCount;
        impl_->statistics.maximumCommandDepth = std::max(impl_->statistics.maximumCommandDepth, impl_->commandCount);
        return Result<PhysicsCommandAdmission>::Success({PhysicsCommandAdmissionStatus::Deferred, impl_->commandCount});
    }

    /** @copydoc PhysicsWorld::AdvanceFixedTick */
    Result<void> PhysicsWorld::AdvanceFixedTick(const PhysicsFixedTickInput &input) {
        if (impl_->state == PhysicsWorldState::ActiveNull)
            return Result<void>::Failure(MakeError(PhysicsErrors::CapabilityUnavailable));
        if (impl_->state != PhysicsWorldState::ActiveSolver || impl_->runtime->ownerThread != std::this_thread::get_id() || impl_->stepping)
            return Result<void>::Failure(MakeError(PhysicsErrors::InvalidState));
        const auto configuredNanoseconds =
            static_cast<std::int64_t>(std::llround(impl_->settings.Values().world.fixedDeltaSeconds * 1'000'000'000.0));
        if (input.simulationTick == 0 || input.simulationTick != impl_->published.completedTick + 1 ||
            input.fixedDelta.ToNanoseconds() != configuredNanoseconds)
            return Result<void>::Failure(
                MakeError(PhysicsErrors::DescriptorInvalid, "Physics requires the next one-based tick and the world's exact fixed delta."));

        struct StepGuard final {
            bool &stepping;

            explicit StepGuard(bool &value) noexcept : stepping(value) {
                stepping = true;
            }

            ~StepGuard() {
                stepping = false;
            }
        } guard{impl_->stepping};

        const std::uint32_t eligible = std::min(impl_->commandCount, impl_->settings.Values().budgets.maximumCommandsPerTick);
        std::uint32_t applied{};
        ObservePhase(input, PhysicsTickPhase::ApplyDeferredPreStep);
        ObserveCommands(*impl_, input, eligible, PhysicsStructuralCommandKind::Create, PhysicsCommandSafePoint::PreStep, applied);
        ObservePhase(input, PhysicsTickPhase::CopyKinematicTargets);
        ObservePhase(input, PhysicsTickPhase::ApplyDynamicInputs);
        ObservePhase(input, PhysicsTickPhase::BroadPhase);
        ObservePhase(input, PhysicsTickPhase::ContactGeneration);
        ObservePhase(input, PhysicsTickPhase::ConstraintSolve);

        const Result<void> stepped =
            Detail::StepCanonicalWorld(impl_->native, static_cast<float>(impl_->settings.Values().world.fixedDeltaSeconds));
        if (stepped.HasError())
            return stepped;

        ObservePhase(input, PhysicsTickPhase::IntegrateBodies);
        ObservePhase(input, PhysicsTickPhase::WriteRuntimeTransforms);
        ObservePhase(input, PhysicsTickPhase::ProduceEvents);
        ObservePhase(input, PhysicsTickPhase::ApplyDeferredPostStep);
        ObserveCommands(*impl_, input, eligible, PhysicsStructuralCommandKind::Destroy, PhysicsCommandSafePoint::PostStep, applied);

        impl_->DiscardCommands(eligible);
        PublishCompletedTick(*impl_, input.simulationTick, applied);
        impl_->statistics.completedTicks = input.simulationTick;
        ObservePhase(input, PhysicsTickPhase::PublishCompletedTick);
        return Result<void>::Success();
    }

    /** @copydoc PhysicsWorld::PublishedTick */
    PhysicsPublishedTick PhysicsWorld::PublishedTick() const noexcept {
        PublicationGuard publicationGuard{impl_->publicationLock};
        return impl_->published;
    }

    /** @copydoc PhysicsWorld::TickStatistics */
    PhysicsTickStatistics PhysicsWorld::TickStatistics() const noexcept {
        return impl_->statistics;
    }
}  // namespace Horo::Physics
