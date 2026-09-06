#include "Horo/Navigation/NavigationOutcomes.h"

#include <algorithm>

namespace Horo::Navigation {
    namespace {
        /** @brief Validates one Horo-owned region-generation dependency. */
        bool IsValidDependency(const NavigationCoverageDependency &dependency) noexcept {
            return dependency.region != 0 && dependency.generation.IsValid();
        }

        /** @brief Rejects duplicate regions within or across exact coverage sets. */
        bool HasUniqueRegions(const std::span<const NavigationCoverageDependency> covered,
                              const std::span<const NavigationCoverageDependency> missing) noexcept {
            for (std::size_t index = 0; index < covered.size(); ++index) {
                if (std::ranges::any_of(covered.subspan(index + 1),
                                        [&covered, index](const auto &candidate) {
                    return candidate.region == covered[index].region;
                }) ||
                    std::ranges::any_of(missing, [&covered, index](const auto &candidate) {
                    return candidate.region == covered[index].region;
                }))
                    return false;
            }
            for (std::size_t index = 0; index < missing.size(); ++index) {
                if (std::ranges::any_of(missing.subspan(index + 1), [&missing, index](const auto &candidate) {
                    return candidate.region == missing[index].region;
                }))
                    return false;
            }
            return true;
        }

        /** @brief Validates bounded exact coverage inputs shared by complete and partial factories. */
        bool ValidateExactCoverage(const std::span<const NavigationCoverageDependency> covered,
                                   const std::span<const NavigationCoverageDependency> missing) noexcept {
            if (covered.size() > MaximumNavigationOutcomeCoverageDependencies ||
                missing.size() > MaximumNavigationOutcomeCoverageDependencies - covered.size())
                return false;
            if (!std::ranges::all_of(covered, IsValidDependency) || !std::ranges::all_of(missing, IsValidDependency))
                return false;
            return HasUniqueRegions(covered, missing);
        }

        /** @brief Copies a bounded dependency view into fixed outcome-owned storage. */
        void CopyDependencies(const std::span<const NavigationCoverageDependency> source,
                              std::array<NavigationCoverageDependency, MaximumNavigationOutcomeCoverageDependencies> &destination) {
            std::ranges::copy(source, destination.begin());
        }
    }  // namespace

    /** @copydoc ValidateNavigationOutcomeProvenance */
    bool ValidateNavigationOutcomeProvenance(const NavigationOutcomeProvenance &provenance) noexcept {
        return provenance.snapshot.IsValid() && provenance.world.IsValid() && provenance.topology.IsValid() &&
               provenance.obstacleRevision != 0 && provenance.filterRevision != 0 && provenance.profileRevision != 0 &&
               provenance.originRevision != 0;
    }

    /** @copydoc NavigationCoverageEvidence::CompleteTopology */
    NavigationCoverageEvidence NavigationCoverageEvidence::CompleteTopology() noexcept {
        return {};
    }

    /** @copydoc NavigationCoverageEvidence::CompleteExact */
    Result<NavigationCoverageEvidence> NavigationCoverageEvidence::CompleteExact(
        const std::span<const NavigationCoverageDependency> covered) {
        if (covered.empty() || !ValidateExactCoverage(covered, {}))
            return Result<NavigationCoverageEvidence>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        NavigationCoverageEvidence evidence;
        evidence.scope_ = NavigationCoverageScope::ExactRegions;
        evidence.coveredCount_ = covered.size();
        CopyDependencies(covered, evidence.covered_);
        return Result<NavigationCoverageEvidence>::Success(std::move(evidence));
    }

    /** @copydoc NavigationCoverageEvidence::PartialExact */
    Result<NavigationCoverageEvidence> NavigationCoverageEvidence::PartialExact(
        const std::span<const NavigationCoverageDependency> covered, const std::span<const NavigationCoverageDependency> missing) {
        if (missing.empty() || !ValidateExactCoverage(covered, missing))
            return Result<NavigationCoverageEvidence>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        NavigationCoverageEvidence evidence;
        evidence.scope_ = NavigationCoverageScope::ExactRegions;
        evidence.complete_ = false;
        evidence.coveredCount_ = covered.size();
        evidence.missingCount_ = missing.size();
        CopyDependencies(covered, evidence.covered_);
        CopyDependencies(missing, evidence.missing_);
        return Result<NavigationCoverageEvidence>::Success(std::move(evidence));
    }

    /** @copydoc NavigationCoverageEvidence::Scope */
    NavigationCoverageScope NavigationCoverageEvidence::Scope() const noexcept {
        return scope_;
    }

    /** @copydoc NavigationCoverageEvidence::IsComplete */
    bool NavigationCoverageEvidence::IsComplete() const noexcept {
        return complete_;
    }

    /** @copydoc NavigationCoverageEvidence::Covered */
    std::span<const NavigationCoverageDependency> NavigationCoverageEvidence::Covered() const noexcept {
        return std::span{covered_}.first(coveredCount_);
    }

    /** @copydoc NavigationCoverageEvidence::Missing */
    std::span<const NavigationCoverageDependency> NavigationCoverageEvidence::Missing() const noexcept {
        return std::span{missing_}.first(missingCount_);
    }

    /** @copydoc NavigationNoPath::Create */
    Result<NavigationNoPath> NavigationNoPath::Create(NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage) {
        if (!ValidateNavigationOutcomeProvenance(provenance) || !coverage.IsComplete())
            return Result<NavigationNoPath>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        return Result<NavigationNoPath>::Success(NavigationNoPath{std::move(provenance), std::move(coverage)});
    }

    NavigationNoPath::NavigationNoPath(NavigationOutcomeProvenance provenance, NavigationCoverageEvidence coverage)
        : provenance_(std::move(provenance)), coverage_(std::move(coverage)) {}

    /** @copydoc NavigationNoPath::Provenance */
    const NavigationOutcomeProvenance &NavigationNoPath::Provenance() const noexcept {
        return provenance_;
    }

    /** @copydoc NavigationNoPath::Coverage */
    const NavigationCoverageEvidence &NavigationNoPath::Coverage() const noexcept {
        return coverage_;
    }

    /** @copydoc NavigationStale::Create */
    Result<NavigationStale> NavigationStale::Create(const NavigationGeneration expectedTopology,
                                                    const NavigationGeneration observedTopology) {
        if (!expectedTopology.IsValid() || !observedTopology.IsValid() || expectedTopology == observedTopology)
            return Result<NavigationStale>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        return Result<NavigationStale>::Success(NavigationStale{expectedTopology, observedTopology});
    }

    NavigationStale::NavigationStale(const NavigationGeneration expectedTopology, const NavigationGeneration observedTopology) noexcept
        : expectedTopology_(expectedTopology), observedTopology_(observedTopology) {}

    /** @copydoc NavigationStale::ExpectedTopology */
    NavigationGeneration NavigationStale::ExpectedTopology() const noexcept {
        return expectedTopology_;
    }

    /** @copydoc NavigationStale::ObservedTopology */
    NavigationGeneration NavigationStale::ObservedTopology() const noexcept {
        return observedTopology_;
    }

    /** @copydoc NavigationUnavailable::Create */
    Result<NavigationUnavailable> NavigationUnavailable::Create(const NavigationUnavailableReason reason) {
        if (reason >= NavigationUnavailableReason::Count)
            return Result<NavigationUnavailable>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        return Result<NavigationUnavailable>::Success(NavigationUnavailable{reason});
    }

    NavigationUnavailable::NavigationUnavailable(const NavigationUnavailableReason reason) noexcept : reason_(reason) {}

    /** @copydoc NavigationUnavailable::Reason */
    NavigationUnavailableReason NavigationUnavailable::Reason() const noexcept {
        return reason_;
    }

    NavigationFailed::NavigationFailed(const NavigationProviderFailureCategory category, Error error)
        : category_(category), error_(std::move(error)) {}

    /** @copydoc NavigationFailed::Category */
    NavigationProviderFailureCategory NavigationFailed::Category() const noexcept {
        return category_;
    }

    /** @copydoc NavigationFailed::ErrorValue */
    const Error &NavigationFailed::ErrorValue() const noexcept {
        return error_;
    }

    /** @copydoc MakeNavigationProviderFailure */
    Result<NavigationFailed> MakeNavigationProviderFailure(const NavigationProviderFailureCategory category,
                                                           std::vector<Diagnostic> diagnostics) {
        if (category >= NavigationProviderFailureCategory::Count)
            return Result<NavigationFailed>::Failure(MakeError(NavigationErrors::OutcomeDescriptorInvalid));
        Error error = MakeError(NavigationErrors::ProviderFailed);
        error.diagnostics = std::move(diagnostics);
        return Result<NavigationFailed>::Success(NavigationFailed{category, std::move(error)});
    }
}  // namespace Horo::Navigation
