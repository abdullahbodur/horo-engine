#pragma once

/**
 * @file CpuParticleSpawnPipeline.h
 * @brief Deterministic allocation-free CPU particle spawn, initialization, and expiry stages.
 */

#include "Horo/Vfx/CpuParticleBuffer.h"
#include "Horo/Vfx/ParticleSystemDescriptor.h"

#include <compare>
#include <cstdint>
#include <memory>

namespace Horo::Vfx {
    namespace Detail {
        struct CpuParticleSpawnPipelineState;
    }

    /** @brief Fixed semantic random channels used by the version-one CPU initialization contract. */
    enum class ParticleRandomChannel : std::uint8_t {
        SpawnX = 1,
        SpawnY = 2,
        SpawnZ = 3,
        SpawnRate = 4,
        Speed = 5,
        Size = 6,
        Opacity = 7,
        Lifetime = 8,
        Rotation = 9
    };

    /** @brief Compile-time ceilings for one bounded CPU lifecycle step. */
    struct CpuParticleSpawnHardLimits final {
        static constexpr std::uint32_t BurstParticles = 1'000'000; /**< Absolute one-step authored burst ceiling. */
        static constexpr float DeltaSeconds = 60.0F;               /**< Absolute one-step simulation delta ceiling. */
        static constexpr std::uint32_t RandomAlgorithmVersion = 1; /**< Counter-hash algorithm compatibility version. */
    };

    /** @brief Immutable preparation inputs captured before frame-hot stepping. */
    struct CpuParticleSpawnPipelineCreateInfo final {
        ParticleBufferId buffer{};                                           /**< Exact identity for the owned SoA buffer. */
        EffectSystemId activation{};                                         /**< Stable effect activation identity. */
        std::uint64_t effectSeed{};                                          /**< Cooked/replay seed; zero is a valid deterministic seed. */
        std::uint32_t maximumBurstParticles{};                               /**< Product-lowered one-step burst ceiling. */
        float maximumDeltaSeconds{CpuParticleSpawnHardLimits::DeltaSeconds}; /**< Product-lowered delta ceiling. */
        std::size_t maximumBufferBytes{CpuParticleBufferHardLimits::Bytes};  /**< Admitted storage byte ceiling. */
    };

    /** @brief One owner-thread step request; requests are never retained after the call. */
    struct CpuParticleSpawnStep final {
        float deltaSeconds{};       /**< Finite non-negative selected-clock delta. */
        std::uint32_t burstCount{}; /**< Additional births requested at this step cutoff. */
        bool cancelled{};           /**< Cancellation fence checked before any mutation. */
    };

    /** @brief Exact observable result of one committed spawn/initialize/expiry step. */
    struct CpuParticleSpawnStepResult final {
        std::uint64_t requestedBirths{}; /**< Continuous plus burst births before capacity admission. */
        std::uint32_t spawned{};         /**< Successfully initialized births. */
        std::uint64_t dropped{};         /**< Cosmetic births rejected by fixed capacity. */
        std::uint32_t killed{};          /**< Expired or explicitly signalled particles reclaimed. */
        std::uint32_t active{};          /**< Packed live count after Kill. */
        constexpr auto operator<=>(const CpuParticleSpawnStepResult &) const noexcept = default;
    };

    /** @brief Lifetime allocation-free diagnostics for the CPU spawn pipeline. */
    struct CpuParticleSpawnStatistics final {
        std::uint64_t steps{};            /**< Successfully committed steps. */
        std::uint64_t spawned{};          /**< Total initialized births. */
        std::uint64_t dropped{};          /**< Total capacity-rejected births. */
        std::uint64_t killed{};           /**< Total reclaimed particles. */
        std::uint64_t nextSpawnOrdinal{}; /**< Next stable ordinal, independent of recyclable slots. */
    };

    /**
     * @brief Owner-thread CPU Spawn, Initialize, age integration, and Kill composition.
     *
     * Create performs every allocation. Advance, SignalKill, View, Statistics, and Shutdown
     * allocate no memory and never block. Continuous spawn carry, stable ordinals, counter-hash
     * random samples, handles, and the SoA buffer are owned together and transfer only by move.
     * Point, sphere, box, and cone descriptors use canonical unit emitter-local geometry; host
     * transforms remain outside this backend-neutral contract. Shutdown is idempotent.
     */
    class CpuParticleSpawnPipeline final {
    public:
        CpuParticleSpawnPipeline(const CpuParticleSpawnPipeline &) = delete;
        CpuParticleSpawnPipeline &operator=(const CpuParticleSpawnPipeline &) = delete;
        /** @brief Transfers sole pipeline and buffer ownership. @param other Source pipeline. */
        CpuParticleSpawnPipeline(CpuParticleSpawnPipeline &&other) noexcept;
        /** @brief Transfers sole ownership, retiring prior state. @param other Source pipeline. @return This pipeline. */
        CpuParticleSpawnPipeline &operator=(CpuParticleSpawnPipeline &&other) noexcept;
        /** @brief Releases prepared owner state after dependent readers have quiesced. */
        ~CpuParticleSpawnPipeline();

        /**
         * @brief Prepares one pipeline from a previously validated immutable descriptor.
         * @param descriptor Admitted particle descriptor copied into pipeline-owned state.
         * @param info Exact identity, seed, burst, delta, and byte limits.
         * @return Prepared pipeline or typed malformed, limit, or allocation failure.
         */
        [[nodiscard]] static Result<CpuParticleSpawnPipeline> Create(const ParticleSystemDescriptor &descriptor,
                                                                     const CpuParticleSpawnPipelineCreateInfo &info);

        /**
         * @brief Executes bounded Spawn, Initialize, age integration, and Kill stages in order.
         * @param step Frozen delta, burst, and cancellation input.
         * @return Committed counts or a typed zero-mutation cancellation, limit, thread, or lifecycle failure.
         */
        [[nodiscard]] Result<CpuParticleSpawnStepResult> Advance(const CpuParticleSpawnStep &step);

        /**
         * @brief Marks one exact live particle for reclamation by the next Kill stage.
         * @param handle Generation-safe handle issued by this pipeline.
         * @return Success or a typed thread, foreign, stale, or lifecycle failure.
         */
        [[nodiscard]] Result<void> SignalKill(const CpuParticleHandle &handle);

        /**
         * @brief Returns the stable handle currently stored at one packed dense index.
         * @param dense Index inside the current live prefix.
         * @return Handle or a typed bounds, lifecycle, or thread failure.
         */
        [[nodiscard]] Result<CpuParticleHandle> HandleAtDenseIndex(std::uint32_t dense);

        /** @brief Returns the owner-thread packed SoA view. @return Mutable view or typed lifecycle/thread failure. */
        [[nodiscard]] Result<CpuParticleSoAView> View();
        /** @brief Returns allocation-free lifetime counters. @return Current counters; zeroed after move. */
        [[nodiscard]] CpuParticleSpawnStatistics Statistics() const noexcept;
        /** @brief Idempotently closes admission and invalidates live handles. @return Success or thread failure. */
        [[nodiscard]] Result<void> Shutdown();

    private:
        /** @brief Adopts one fully prepared owner state. @param state Sole state owner. */
        explicit CpuParticleSpawnPipeline(std::unique_ptr<Detail::CpuParticleSpawnPipelineState> state) noexcept;
        std::unique_ptr<Detail::CpuParticleSpawnPipelineState> state_;
    };
}  // namespace Horo::Vfx
