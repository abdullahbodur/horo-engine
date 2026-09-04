#include "CanonicalPhysicsRuntime.h"

#include "CanonicalSolver.h"
#include "CanonicalWorldSettings.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <Jolt/Jolt.h>

// Jolt subsidiary headers require its root definitions first.
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace Horo::Physics::Detail {
    namespace {
        /** @brief Native callback targets are installed by the sole composition owner and cleared after all worlds retire. */
        struct AllocatorFunctions final {
            JPH::AllocateFunction allocate{};
            JPH::ReallocateFunction reallocate{};
            JPH::AlignedAllocateFunction alignedAllocate{};
        };

        AllocatorFunctions allocatorFunctions;

        /** @brief Native allocation cannot unwind through no-exception Jolt frames; fail closed instead of dereferencing null. */
        void *RequireNativeAllocation(void *memory) noexcept {
            if (memory == nullptr)
                std::abort();
            return memory;
        }

        /** @brief Preserves native allocation semantics with a defined process-fatal exhaustion path. */
        void *CheckedAllocate(const std::size_t size) {
            return RequireNativeAllocation(allocatorFunctions.allocate(std::max(size, std::size_t{1})));
        }

        /** @brief Delegates reallocation without allowing null to escape into no-exception native code. */
        void *CheckedReallocate(void *memory, const std::size_t oldSize, const std::size_t newSize) {
            return RequireNativeAllocation(allocatorFunctions.reallocate(memory, oldSize, std::max(newSize, std::size_t{1})));
        }

        /** @brief Retains platform alignment and fails closed on exhaustion. */
        void *CheckedAlignedAllocate(const std::size_t size, const std::size_t alignment) {
            return RequireNativeAllocation(allocatorFunctions.alignedAllocate(std::max(size, std::size_t{1}), alignment));
        }

        /** @brief Captures platform allocation functions and installs bounded allocation-failure behavior explicitly. */
        void InstallAllocators() {
            JPH::RegisterDefaultAllocator();
            allocatorFunctions = {JPH::Allocate, JPH::Reallocate, JPH::AlignedAllocate};
            JPH::Allocate = CheckedAllocate;
            JPH::Reallocate = CheckedReallocate;
            JPH::AlignedAllocate = CheckedAlignedAllocate;
        }

        /** @brief Process-owned Jolt registration; destruction occurs only after every native world retires. */
        struct CanonicalRuntime final {
            ~CanonicalRuntime() {
                if (typesRegistered)
                    JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
                JPH::Allocate = nullptr;
                JPH::Reallocate = nullptr;
                JPH::Free = nullptr;
                JPH::AlignedAllocate = nullptr;
                JPH::AlignedFree = nullptr;
                allocatorFunctions = {};
            }

            bool typesRegistered{};
            CanonicalResourceCounts resources;
        };

        /** @brief Temporary closed filter until the collision-profile ticket installs a validated table. */
        class ClosedBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
                return 1;
            }

            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
                return JPH::BroadPhaseLayer{0};
            }
        };

        /** @brief Rejects all pairs while body/filter capability remains unpublished. */
        class ClosedObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
                return false;
            }
        };

        /** @brief Rejects all object pairs while body/filter capability remains unpublished. */
        class ClosedObjectPairs final : public JPH::ObjectLayerPairFilter {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override {
                return false;
            }
        };

        /** @brief Per-world native ownership in dependency order; reverse member destruction releases the system first. */
        struct CanonicalWorld final {
            explicit CanonicalWorld(CanonicalRuntime &runtime) : owner(runtime) {
                ++owner.resources.worlds;
            }

            ~CanonicalWorld() {
                const auto hadSystem = static_cast<std::uint32_t>(system != nullptr);
                const auto hadJobs = static_cast<std::uint32_t>(jobs != nullptr);
                const auto hadScratch = static_cast<std::uint32_t>(scratch != nullptr);
                system.reset();
                jobs.reset();
                scratch.reset();
                owner.resources.physicsSystems -= hadSystem;
                owner.resources.jobSystems -= hadJobs;
                owner.resources.scratchAllocators -= hadScratch;
                --owner.resources.worlds;
            }

            CanonicalRuntime &owner;
            ClosedBroadPhaseLayers broadPhaseLayers;
            ClosedObjectVsBroadPhase objectVsBroadPhase;
            ClosedObjectPairs objectPairs;
            std::unique_ptr<JPH::TempAllocatorImpl> scratch;
            std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
            std::unique_ptr<JPH::PhysicsSystem> system;
        };

        /** @brief Rejects foreign native ownership rather than replacing global hooks or factories. */
        bool NativeGlobalsAreUnowned() noexcept {
            return JPH::Factory::sInstance == nullptr && JPH::Allocate == nullptr && JPH::Reallocate == nullptr && JPH::Free == nullptr &&
                   JPH::AlignedAllocate == nullptr && JPH::AlignedFree == nullptr;
        }
    }  // namespace

    /** @copydoc CreateCanonicalRuntime */
    Result<CanonicalRuntimeHandle> CreateCanonicalRuntime(const CanonicalFailurePoint failurePoint) {
        if (!IsCanonicalSolverBuildCompatible())
            return Result<CanonicalRuntimeHandle>::Failure(
                MakeError(PhysicsErrors::ProfileUnsupported, "Canonical solver ABI is incompatible."));
        if (!NativeGlobalsAreUnowned())
            return Result<CanonicalRuntimeHandle>::Failure(
                MakeError(PhysicsErrors::InvalidState, "Jolt process globals are already owned by another runtime."));

        auto runtime = std::make_unique<CanonicalRuntime>();
        InstallAllocators();
        if (failurePoint == CanonicalFailurePoint::AllocatorRegistered)
            return Result<CanonicalRuntimeHandle>::Failure(MakeError(PhysicsErrors::InitializationFailed, "Allocator registration stage."));
        JPH::Factory::sInstance = new JPH::Factory();
        if (failurePoint == CanonicalFailurePoint::FactoryCreated)
            return Result<CanonicalRuntimeHandle>::Failure(MakeError(PhysicsErrors::InitializationFailed, "Factory creation stage."));
        JPH::RegisterTypes();
        runtime->typesRegistered = true;
        if (failurePoint == CanonicalFailurePoint::TypesRegistered)
            return Result<CanonicalRuntimeHandle>::Failure(MakeError(PhysicsErrors::InitializationFailed, "Type registration stage."));
        return Result<CanonicalRuntimeHandle>::Success({runtime.release()});
    }

    /** @copydoc DestroyCanonicalRuntime */
    void DestroyCanonicalRuntime(const CanonicalRuntimeHandle runtime) noexcept {
        delete static_cast<CanonicalRuntime *>(runtime.value);
    }

    /** @copydoc CreateCanonicalWorld */
    Result<CanonicalWorldHandle> CreateCanonicalWorld(const CanonicalRuntimeHandle runtime, const PhysicsWorldSettings &settings,
                                                      const CanonicalFailurePoint failurePoint) {
        if (runtime.value == nullptr || JPH::Factory::sInstance == nullptr)
            return Result<CanonicalWorldHandle>::Failure(MakeError(PhysicsErrors::InvalidState));
        if (settings.Values().world.capacity.maximumBodies == 0)
            return Result<CanonicalWorldHandle>::Failure(
                MakeError(PhysicsErrors::OperationUnsupported, "Canonical broad-phase initialization requires non-zero body capacity."));
        if (settings.Values().nonFinitePolicy != PhysicsNonFinitePolicy::FailWorld)
            return Result<CanonicalWorldHandle>::Failure(
                MakeError(PhysicsErrors::OperationUnsupported, "Body quarantine requires a qualified safe-point retirement path."));
        const auto translated = TranslateCanonicalWorldSettings(settings);
        if (translated.HasError())
            return Result<CanonicalWorldHandle>::Failure(translated.ErrorValue());

        const auto &values = translated.Value();
        auto world = std::make_unique<CanonicalWorld>(*static_cast<CanonicalRuntime *>(runtime.value));
        world->scratch = std::make_unique<JPH::TempAllocatorImpl>(static_cast<std::size_t>(values.scratchBytes));
        ++world->owner.resources.scratchAllocators;
        if (failurePoint == CanonicalFailurePoint::ScratchCreated)
            return Result<CanonicalWorldHandle>::Failure(MakeError(PhysicsErrors::InitializationFailed, "Scratch creation stage."));
        world->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        ++world->owner.resources.jobSystems;
        if (failurePoint == CanonicalFailurePoint::JobsCreated)
            return Result<CanonicalWorldHandle>::Failure(
                MakeError(PhysicsErrors::InitializationFailed, "Serial job-dispatch creation stage."));
        world->system = std::make_unique<JPH::PhysicsSystem>();
        ++world->owner.resources.physicsSystems;
        world->system->Init(values.maximumBodies, 1, values.maximumBodyPairs, values.maximumContactConstraints, world->broadPhaseLayers,
                            world->objectVsBroadPhase, world->objectPairs);
        world->system->SetPhysicsSettings(values.solver);
        world->system->SetGravity(values.gravity);
        if (failurePoint == CanonicalFailurePoint::SystemInitialized)
            return Result<CanonicalWorldHandle>::Failure(
                MakeError(PhysicsErrors::InitializationFailed, "Native world initialization stage."));
        return Result<CanonicalWorldHandle>::Success({world.release()});
    }

    /** @copydoc DestroyCanonicalWorld */
    void DestroyCanonicalWorld(const CanonicalWorldHandle world) noexcept {
        delete static_cast<CanonicalWorld *>(world.value);
    }

    /** @copydoc InspectCanonicalResources */
    CanonicalResourceCounts InspectCanonicalResources(const CanonicalRuntimeHandle runtime) noexcept {
        return runtime.value == nullptr ? CanonicalResourceCounts{} : static_cast<const CanonicalRuntime *>(runtime.value)->resources;
    }
}  // namespace Horo::Physics::Detail
