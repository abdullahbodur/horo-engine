#pragma once

/**
 * @file PhysicsMetrics.h
 * @brief Bounded backend-neutral Physics measurements and observability binding.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Telemetry/Telemetry.h"
#include "Horo/Physics/PhysicsIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace Horo::Physics {
    /** @brief Host policy for the availability of Physics metric publication. */
    enum class PhysicsMetricAvailability : std::uint8_t {
        Off,
        Unavailable,
        Available,
    };

    /** @brief Whether a world may run without an available Physics metric binding. */
    enum class PhysicsMetricRequirement : std::uint8_t {
        Optional,
        Required,
    };

    /** @brief Observable result of a validated measurement publication attempt. */
    enum class PhysicsMetricPublishDisposition : std::uint8_t {
        Submitted,
        SuppressedByPolicy,
        SuppressedUnavailable,
    };

    /** @brief Qualified per-world maxima used to reject impossible measurement snapshots. */
    struct PhysicsMetricBounds final {
        std::uint64_t maximumBodies{};          /**< Maximum current bodies. */
        std::uint64_t maximumShapes{};          /**< Maximum resident shapes. */
        std::uint64_t maximumConstraints{};     /**< Maximum current constraints. */
        std::uint64_t maximumBroadphasePairs{}; /**< Maximum broadphase pairs in one tick. */
        std::uint64_t maximumContacts{};        /**< Maximum contacts in one tick. */
        std::uint64_t maximumQueriesPerTick{};  /**< Maximum queries executed in one tick. */
        std::uint64_t maximumCommandDepth{};    /**< Maximum deferred command depth. */
        std::uint64_t maximumEventDepth{};      /**< Maximum event depth and per-tick drop count. */
    };

    /** @brief One immutable owner-published Physics tick measurement snapshot. */
    struct PhysicsMetricSnapshot final {
        std::uint32_t schemaVersion{1};      /**< Exact measurement schema version. */
        PhysicsWorldId world;                /**< Exact published world generation. */
        std::uint64_t publicationRevision{}; /**< Current atomic tick publication revision. */
        std::uint64_t simulationTick{};      /**< One-based committed simulation tick. */
        double fixedStepSeconds{};           /**< Host-measured complete fixed-step duration. */
        double broadphaseSeconds{};          /**< Adapter-supplied broadphase duration. */
        double narrowphaseSeconds{};         /**< Adapter-supplied narrowphase duration. */
        double solverSeconds{};              /**< Adapter-supplied solver duration. */
        std::uint64_t bodyCount{};           /**< Current total body count. */
        std::uint64_t sleepingBodyCount{};   /**< Current sleeping subset of bodyCount. */
        std::uint64_t shapeCount{};          /**< Current resident shape count. */
        std::uint64_t constraintCount{};     /**< Current constraint count. */
        std::uint64_t broadphasePairCount{}; /**< Pairs observed in this tick. */
        std::uint64_t contactCount{};        /**< Contacts observed in this tick. */
        std::uint64_t queryCount{};          /**< Queries executed in this tick. */
        std::uint64_t commandDepth{};        /**< Deferred command depth at publication. */
        std::uint64_t eventDepth{};          /**< Event depth at publication. */
        std::uint64_t droppedEventCount{};   /**< Events dropped during this tick. */
        std::uint64_t overflowCount{};       /**< Command/event domains that overflowed, at most two. */
    };

    /** @brief Pre-bound process Telemetry handles; construction belongs to host composition. */
    struct PhysicsMetricHandles final {
        Telemetry::Histogram fixedStepDuration;             /**< Core fixed-step seconds series. */
        std::array<Telemetry::Histogram, 3> stageDurations; /**< Detailed broad/narrow/solver seconds series. */
        std::array<Telemetry::Gauge, 7> counts;             /**< Closed body/shape/constraint/pair/contact/query series. */
        std::array<Telemetry::Gauge, 2> depths;             /**< Command and event queue depth series. */
        std::array<Telemetry::Counter, 2> events;           /**< Dropped-event and overflow series. */
    };

    /**
     * @brief Registers and pre-binds the closed Physics metric vocabulary.
     * @param level Host-selected process metric collection level.
     * @return Handles bound outside the tick path; empty handles truthfully represent unavailable collection.
     * @pre Process composition after Telemetry runtime initialization, never a Physics tick or solver callback.
     */
    [[nodiscard]] PhysicsMetricHandles RegisterPhysicsMetricHandles(Telemetry::MetricCollectionLevel level);

    /** @brief Validated owner-thread lifecycle binding from one Physics world to process observability handles. */
    class PhysicsMetricBinding final {
    public:
        PhysicsMetricBinding(const PhysicsMetricBinding &) = delete;
        PhysicsMetricBinding &operator=(const PhysicsMetricBinding &) = delete;
        PhysicsMetricBinding(PhysicsMetricBinding &&) noexcept = default;
        PhysicsMetricBinding &operator=(PhysicsMetricBinding &&) noexcept = default;

        /**
         * @brief Creates one revision-scoped measurement publication binding.
         * @param world Exact active Physics world generation.
         * @param revision Non-zero binding revision issued by host composition.
         * @param bounds Qualified limits copied from the world admission plan.
         * @param availability Explicit available, unavailable, or policy-off state.
         * @param requirement Whether unavailable metrics reject world admission.
         * @param level Host-selected collection detail when available.
         * @param handles Pre-registered and pre-bound process Telemetry handles.
         * @return Binding or a typed Physics descriptor/capability error.
         * @pre Called on the Physics owner thread that will publish and close the binding; moving the value does not
         * transfer affinity.
         * @post Failure retains no handles and publishes no metric record.
         */
        [[nodiscard]] static Result<PhysicsMetricBinding> Create(PhysicsWorldId world, std::uint64_t revision, PhysicsMetricBounds bounds,
                                                                 PhysicsMetricAvailability availability,
                                                                 PhysicsMetricRequirement requirement,
                                                                 Telemetry::MetricCollectionLevel level, PhysicsMetricHandles handles);

        /**
         * @brief Validates and publishes one immutable owner-produced tick snapshot.
         * @param snapshot Complete current-world values measured by the owning host/adapter; this function owns no clock.
         * @param expectedBindingRevision Exact binding revision captured by the producer.
         * @param expectedPublicationRevision Exact current world revision captured at the same safe point.
         * @return Submitted/suppressed disposition or a typed invalid/stale error; invalid input invokes no handle.
         * @pre Successful tick publication boundary on the binding's owner thread.
         * @post Metric loss or saturation never changes simulation state, ordering, admission, or the supplied snapshot.
         */
        [[nodiscard]] Result<PhysicsMetricPublishDisposition> Publish(const PhysicsMetricSnapshot &snapshot,
                                                                      std::uint64_t expectedBindingRevision,
                                                                      std::uint64_t expectedPublicationRevision) const;

        /**
         * @brief Closes metric admission before the bound world retires.
         * @return Success, including repeated close, or PhysicsErrors::ThreadAffinityViolation.
         */
        [[nodiscard]] Result<void> Close();

        /** @brief Returns the bound world generation. @return Exact non-zero world identity. */
        [[nodiscard]] PhysicsWorldId World() const noexcept;
        /** @brief Returns the host-issued binding revision. @return Non-zero monotonic revision. */
        [[nodiscard]] std::uint64_t Revision() const noexcept;
        /** @brief Returns explicit collection availability. */
        [[nodiscard]] PhysicsMetricAvailability Availability() const noexcept;

    private:
        /**
         * @brief Retains a successfully validated binding without further registration.
         * @param world Exact world generation.
         * @param revision Host-issued binding revision.
         * @param bounds Qualified world measurement limits.
         * @param availability Explicit collection state.
         * @param level Selected collection detail.
         * @param handles Pre-bound process Telemetry handles.
         */
        PhysicsMetricBinding(PhysicsWorldId world, std::uint64_t revision, PhysicsMetricBounds bounds,
                             PhysicsMetricAvailability availability, Telemetry::MetricCollectionLevel level,
                             PhysicsMetricHandles handles) noexcept;

        PhysicsWorldId world_;
        std::uint64_t revision_{};
        PhysicsMetricBounds bounds_;
        PhysicsMetricAvailability availability_{PhysicsMetricAvailability::Unavailable};
        Telemetry::MetricCollectionLevel level_{Telemetry::MetricCollectionLevel::Off};
        PhysicsMetricHandles handles_;
        std::thread::id ownerThread_;
        bool closed_{};
    };
}  // namespace Horo::Physics
