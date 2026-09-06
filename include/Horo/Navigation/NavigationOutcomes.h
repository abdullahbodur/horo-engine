#pragma once

/**
 * @file NavigationOutcomes.h
 * @brief Provider-neutral terminal query outcomes with bounded snapshot and coverage evidence.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace Horo::Navigation {
    /** @brief Maximum exact covered plus missing region dependencies retained by one terminal outcome. */
    inline constexpr std::size_t MaximumNavigationOutcomeCoverageDependencies = 64;

    /** @brief Stable terminal outcome kinds; admission rejection never creates one of these outcomes. */
    enum class NavigationOutcomeKind : std::uint8_t {
        Succeeded,
        Partial,
        NoPath,
        Cancelled,
        Stale,
        Unavailable,
        InvalidWorld,
        InvalidHandle,
        CapacityExceeded,
        Failed,
        Count
    };

    /** @brief Normalized provider failure categories; native codes remain diagnostic evidence only. */
    enum class NavigationProviderFailureCategory : std::uint8_t {
        InvalidProviderData,
        ResourceExhausted,
        TransientFailure,
        PermanentFailure,
        Count
    };

    /** @brief Typed reasons that admitted work can become unavailable before publication. */
    enum class NavigationUnavailableReason : std::uint8_t {
        NoNavigationData,
        WorldUnavailable,
        ProviderUnavailable,
        Count
    };

    /** @brief Whether coverage is represented by exact regions or conservatively by the whole topology. */
    enum class NavigationCoverageScope : std::uint8_t {
        ExactRegions,
        WholeTopology,
        Count
    };

    /** @brief One provider-neutral region generation that contributed to an answer or remained missing. */
    struct NavigationCoverageDependency final {
        std::uint64_t region{};            /**< Non-zero Horo-owned logical region identity. */
        NavigationGeneration generation{}; /**< Exact non-zero generation observed for that region. */

        constexpr auto operator<=>(const NavigationCoverageDependency &) const noexcept = default;
    };

    /** @brief Exact immutable snapshot/revision evidence carried by every path-like terminal result. */
    struct NavigationOutcomeProvenance final {
        NavigationSnapshotToken snapshot;
        NavigationWorldId world;
        NavigationGeneration topology;
        std::uint64_t obstacleRevision{};
        std::uint64_t filterRevision{};
        std::uint64_t profileRevision{};
        std::uint64_t originRevision{};
        std::uint64_t completionTick{};
    };

    /**
     * @brief Validates non-zero snapshot, world, topology, and configuration revision identities.
     * @param provenance Complete Horo-owned result provenance.
     * @return True when every required identity and revision is non-zero; completion tick may be zero.
     */
    [[nodiscard]] bool ValidateNavigationOutcomeProvenance(const NavigationOutcomeProvenance &provenance) noexcept;

    /** @brief Bounded complete or partial coverage proof with no provider-native identifiers. */
    class NavigationCoverageEvidence final {
    public:
        /** @brief Creates conservative complete evidence for the entire captured topology. */
        [[nodiscard]] static NavigationCoverageEvidence CompleteTopology() noexcept;

        /**
         * @brief Creates complete evidence from a non-empty exact dependency set.
         * @param covered Unique valid region generations read while proving the answer.
         * @return Complete evidence or NavigationErrors::OutcomeDescriptorInvalid.
         */
        [[nodiscard]] static Result<NavigationCoverageEvidence> CompleteExact(std::span<const NavigationCoverageDependency> covered);

        /**
         * @brief Creates partial evidence with exact covered and missing dependency sets.
         * @param covered Valid unique region generations contributing to the partial answer; may be empty.
         * @param missing Non-empty valid unique region generations required to complete the answer.
         * @return Partial evidence or NavigationErrors::OutcomeDescriptorInvalid.
         */
        [[nodiscard]] static Result<NavigationCoverageEvidence> PartialExact(std::span<const NavigationCoverageDependency> covered,
                                                                             std::span<const NavigationCoverageDependency> missing);

        /** @brief Returns exact-region or whole-topology scope. */
        [[nodiscard]] NavigationCoverageScope Scope() const noexcept;
        /** @brief Returns true only when this evidence can support success or proven no-path. */
        [[nodiscard]] bool IsComplete() const noexcept;
        /** @brief Returns the bounded covered dependency view. */
        [[nodiscard]] std::span<const NavigationCoverageDependency> Covered() const noexcept;
        /** @brief Returns the bounded missing dependency view; non-empty only for partial evidence. */
        [[nodiscard]] std::span<const NavigationCoverageDependency> Missing() const noexcept;

    private:
        NavigationCoverageScope scope_{NavigationCoverageScope::WholeTopology};
        bool complete_{true};
        std::array<NavigationCoverageDependency, MaximumNavigationOutcomeCoverageDependencies> covered_{};
        std::array<NavigationCoverageDependency, MaximumNavigationOutcomeCoverageDependencies> missing_{};
        std::size_t coveredCount_{};
        std::size_t missingCount_{};
    };

    /** @brief Complete successful value proven against complete captured coverage. */
    template <typename ValueT> class NavigationSucceeded final {
    public:
        /** @brief Creates a complete outcome only from valid provenance and complete coverage evidence. */
        [[nodiscard]] static Result<NavigationSucceeded> Create(ValueT value, NavigationOutcomeProvenance provenance,
                                                                NavigationCoverageEvidence coverage) {
            if (!ValidateNavigationOutcomeProvenance(provenance) || !coverage.IsComplete())
                return Result<NavigationSucceeded>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
            return Result<NavigationSucceeded>::Success(NavigationSucceeded{std::move(value), std::move(provenance), std::move(coverage)});
        }

        /** @brief Returns the complete caller-owned value. */
        [[nodiscard]] const ValueT &Value() const noexcept {
            return value_;
        }

        /** @brief Returns immutable snapshot/revision provenance. */
        [[nodiscard]] const NavigationOutcomeProvenance &Provenance() const noexcept {
            return provenance_;
        }

        /** @brief Returns complete bounded coverage evidence. */
        [[nodiscard]] const NavigationCoverageEvidence &Coverage() const noexcept {
            return coverage_;
        }

    private:
        NavigationSucceeded(ValueT value, NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage)
            : value_(std::move(value)), provenance_(std::move(provenance)), coverage_(std::move(coverage)) {}

        ValueT value_;
        NavigationOutcomeProvenance provenance_;
        NavigationCoverageEvidence coverage_;
    };

    /** @brief Explicitly permitted partial value paired with exact missing coverage evidence. */
    template <typename ValueT> class NavigationPartial final {
    public:
        /** @brief Creates a partial outcome only from valid provenance and incomplete coverage evidence. */
        [[nodiscard]] static Result<NavigationPartial> Create(ValueT value, NavigationOutcomeProvenance provenance,
                                                              NavigationCoverageEvidence coverage) {
            if (!ValidateNavigationOutcomeProvenance(provenance) || coverage.IsComplete() || coverage.Missing().empty())
                return Result<NavigationPartial>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
            return Result<NavigationPartial>::Success(NavigationPartial{std::move(value), std::move(provenance), std::move(coverage)});
        }

        /** @brief Returns the valid bounded prefix or frontier. */
        [[nodiscard]] const ValueT &Value() const noexcept {
            return value_;
        }

        /** @brief Returns immutable snapshot/revision provenance. */
        [[nodiscard]] const NavigationOutcomeProvenance &Provenance() const noexcept {
            return provenance_;
        }

        /** @brief Returns incomplete bounded coverage evidence with explicit missing regions. */
        [[nodiscard]] const NavigationCoverageEvidence &Coverage() const noexcept {
            return coverage_;
        }

    private:
        NavigationPartial(ValueT value, NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage)
            : value_(std::move(value)), provenance_(std::move(provenance)), coverage_(std::move(coverage)) {}

        ValueT value_;
        NavigationOutcomeProvenance provenance_;
        NavigationCoverageEvidence coverage_;
    };

    /** @brief Proven negative result that requires complete coverage and captured provenance. */
    class NavigationNoPath final {
    public:
        /** @brief Creates proven no-path only from valid provenance and complete coverage evidence. */
        [[nodiscard]] static Result<NavigationNoPath> Create(NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage);
        /** @brief Returns immutable snapshot/revision provenance. */
        [[nodiscard]] const NavigationOutcomeProvenance &Provenance() const noexcept;
        /** @brief Returns complete bounded coverage evidence. */
        [[nodiscard]] const NavigationCoverageEvidence &Coverage() const noexcept;

    private:
        NavigationNoPath(NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage);
        NavigationOutcomeProvenance provenance_;
        NavigationCoverageEvidence coverage_;
    };

    /** @brief Accepted work cancelled before owner-thread terminal publication. */
    struct NavigationCancelled final {};

    /** @brief Accepted result computed for an old topology generation and therefore not publishable. */
    class NavigationStale final {
    public:
        /** @brief Creates a stale result from two valid distinct topology generations. */
        [[nodiscard]] static Result<NavigationStale> Create(NavigationGeneration expectedTopology, NavigationGeneration observedTopology);
        /** @brief Returns the generation required at publication. */
        [[nodiscard]] NavigationGeneration ExpectedTopology() const noexcept;
        /** @brief Returns the generation actually observed by the result. */
        [[nodiscard]] NavigationGeneration ObservedTopology() const noexcept;

    private:
        NavigationStale(NavigationGeneration expectedTopology, NavigationGeneration observedTopology) noexcept;
        NavigationGeneration expectedTopology_;
        NavigationGeneration observedTopology_;
    };

    /** @brief Accepted work could not obtain required navigation data or provider availability. */
    class NavigationUnavailable final {
    public:
        /** @brief Creates unavailable only from a known closed reason. */
        [[nodiscard]] static Result<NavigationUnavailable> Create(NavigationUnavailableReason reason);
        /** @brief Returns the normalized reason. */
        [[nodiscard]] NavigationUnavailableReason Reason() const noexcept;

    private:
        explicit NavigationUnavailable(NavigationUnavailableReason reason) noexcept;
        NavigationUnavailableReason reason_;
    };

    /** @brief Accepted work references a revoked or replacement navigation-world incarnation. */
    struct NavigationInvalidWorld final {};

    /** @brief Accepted work references a revoked, foreign, or generation-stale request handle. */
    struct NavigationInvalidHandle final {};

    /** @brief A declared node, output, scratch, result, or retry bound prevented accepted work from completing. */
    struct NavigationCapacityExceeded final {};

    /** @brief Provider execution failed with a known normalized category and fixed Horo error identity. */
    class NavigationFailed final {
    public:
        /** @brief Returns the normalized provider failure category. */
        [[nodiscard]] NavigationProviderFailureCategory Category() const noexcept;
        /** @brief Returns the stable provider-failed error and ordered Horo diagnostics. */
        [[nodiscard]] const Error &ErrorValue() const noexcept;

    private:
        NavigationFailed(NavigationProviderFailureCategory category, Error error);
        NavigationProviderFailureCategory category_;
        Error error_;
        friend Result<NavigationFailed> MakeNavigationProviderFailure(NavigationProviderFailureCategory, std::vector<Diagnostic>);
    };

    /**
     * @brief Closed terminal outcome for one admitted query.
     *
     * Invalid descriptors, stale capability revisions, unsupported operations, unavailable providers at admission,
     * excessive requested limits, and queue/result admission capacity are Result errors and create no accepted handle.
     * These alternatives describe only accepted work at immediate return or owner-thread terminal publication.
     */
    template <typename ValueT>
    using NavigationOutcome =
        std::variant<NavigationSucceeded<ValueT>, NavigationPartial<ValueT>, NavigationNoPath, NavigationCancelled, NavigationStale,
                     NavigationUnavailable, NavigationInvalidWorld, NavigationInvalidHandle, NavigationCapacityExceeded, NavigationFailed>;

    /** @brief Explicit visitor mapping terminal variant types to stable outcome kinds without ordinal coupling. */
    template <typename ValueT> struct NavigationOutcomeKindVisitor final {
        NavigationOutcomeKind operator()(const NavigationSucceeded<ValueT> &) const noexcept {
            return NavigationOutcomeKind::Succeeded;
        }

        NavigationOutcomeKind operator()(const NavigationPartial<ValueT> &) const noexcept {
            return NavigationOutcomeKind::Partial;
        }

        NavigationOutcomeKind operator()(const NavigationNoPath &) const noexcept {
            return NavigationOutcomeKind::NoPath;
        }

        NavigationOutcomeKind operator()(const NavigationCancelled &) const noexcept {
            return NavigationOutcomeKind::Cancelled;
        }

        NavigationOutcomeKind operator()(const NavigationStale &) const noexcept {
            return NavigationOutcomeKind::Stale;
        }

        NavigationOutcomeKind operator()(const NavigationUnavailable &) const noexcept {
            return NavigationOutcomeKind::Unavailable;
        }

        NavigationOutcomeKind operator()(const NavigationInvalidWorld &) const noexcept {
            return NavigationOutcomeKind::InvalidWorld;
        }

        NavigationOutcomeKind operator()(const NavigationInvalidHandle &) const noexcept {
            return NavigationOutcomeKind::InvalidHandle;
        }

        NavigationOutcomeKind operator()(const NavigationCapacityExceeded &) const noexcept {
            return NavigationOutcomeKind::CapacityExceeded;
        }

        NavigationOutcomeKind operator()(const NavigationFailed &) const noexcept {
            return NavigationOutcomeKind::Failed;
        }
    };

    /**
     * @brief Returns the stable typed kind for a closed navigation outcome.
     * @param outcome Terminal outcome whose alternative remains unchanged.
     * @return Kind corresponding exactly to the active variant type.
     */
    template <typename ValueT>
    [[nodiscard]] NavigationOutcomeKind GetNavigationOutcomeKind(const NavigationOutcome<ValueT> &outcome) noexcept {
        return std::visit(NavigationOutcomeKindVisitor<ValueT>{}, outcome);
    }

    /**
     * @brief Wraps provider detail behind the fixed Navigation provider-failed identity.
     * @param category Known normalized failure category used for caller branching.
     * @param diagnostics Owned Horo diagnostics translated and redacted by the provider adapter.
     * @return Failed outcome preserving diagnostic order, or OutcomeDescriptorInvalid for an unknown category.
     */
    [[nodiscard]] Result<NavigationFailed> MakeNavigationProviderFailure(NavigationProviderFailureCategory category,
                                                                         std::vector<Diagnostic> diagnostics = {});
}  // namespace Horo::Navigation
