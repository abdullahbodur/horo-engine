#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationOutcomes.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Navigation {
    namespace {
        template <typename IdentityT> IdentityT Identity(const std::uint64_t value) {
            auto identity = IdentityT::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        NavigationOutcomeProvenance Provenance() {
            return {
                .snapshot = Identity<NavigationSnapshotToken>(11),
                .world = Identity<NavigationWorldId>(12),
                .topology = Identity<NavigationGeneration>(13),
                .obstacleRevision = 14,
                .filterRevision = 15,
                .profileRevision = 16,
                .originRevision = 17,
                .completionTick = 0,
            };
        }

        NavigationCoverageDependency Dependency(const std::uint64_t region, const std::uint64_t generation) {
            return {.region = region, .generation = Identity<NavigationGeneration>(generation)};
        }

        NavigationCoverageEvidence PartialCoverage() {
            const std::array covered{Dependency(21, 31)};
            const std::array missing{Dependency(22, 32)};
            auto evidence = NavigationCoverageEvidence::PartialExact(covered, missing);
            REQUIRE(evidence.HasValue());
            return std::move(evidence).Value();
        }

        template <typename ResultT> void ExpectOutcomeDescriptorError(const ResultT &result) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().domain.Value() == NavigationErrors::OutcomeDescriptorInvalid.domain.Value());
            REQUIRE(result.ErrorValue().code.Value() == NavigationErrors::OutcomeDescriptorInvalid.code.Value());
        }

        TEST_CASE("Navigation outcomes preserve every terminal state through explicit type mapping", "[navigation][outcome]") {
            using Value = std::vector<std::uint32_t>;
            using Outcome = NavigationOutcome<Value>;

            auto succeeded = NavigationSucceeded<Value>::Create({1, 2}, Provenance(), NavigationCoverageEvidence::CompleteTopology());
            auto partial = NavigationPartial<Value>::Create({1}, Provenance(), PartialCoverage());
            auto noPath = NavigationNoPath::Create(Provenance(), NavigationCoverageEvidence::CompleteTopology());
            auto stale = NavigationStale::Create(Identity<NavigationGeneration>(8), Identity<NavigationGeneration>(7));
            auto unavailable = NavigationUnavailable::Create(NavigationUnavailableReason::NoNavigationData);
            auto failed = MakeNavigationProviderFailure(NavigationProviderFailureCategory::PermanentFailure);
            REQUIRE(succeeded.HasValue());
            REQUIRE(partial.HasValue());
            REQUIRE(noPath.HasValue());
            REQUIRE(stale.HasValue());
            REQUIRE(unavailable.HasValue());
            REQUIRE(failed.HasValue());

            const std::array<Outcome, static_cast<std::size_t>(NavigationOutcomeKind::Count)> outcomes{
                std::move(succeeded).Value(), std::move(partial).Value(),     std::move(noPath).Value(), NavigationCancelled{},
                std::move(stale).Value(),     std::move(unavailable).Value(), NavigationInvalidWorld{},  NavigationInvalidHandle{},
                NavigationCapacityExceeded{}, std::move(failed).Value(),
            };
            for (std::size_t index = 0; index < outcomes.size(); ++index)
                REQUIRE(GetNavigationOutcomeKind(outcomes[index]) == static_cast<NavigationOutcomeKind>(index));

            REQUIRE(std::get<NavigationSucceeded<Value>>(outcomes[0]).Value() == Value{1, 2});
            REQUIRE(std::get<NavigationSucceeded<Value>>(outcomes[0]).Provenance().snapshot == Provenance().snapshot);
            REQUIRE(std::get<NavigationSucceeded<Value>>(outcomes[0]).Coverage().IsComplete());
            REQUIRE(std::get<NavigationPartial<Value>>(outcomes[1]).Value() == Value{1});
            REQUIRE(std::get<NavigationPartial<Value>>(outcomes[1]).Provenance().topology == Provenance().topology);
            REQUIRE(std::get<NavigationPartial<Value>>(outcomes[1]).Coverage().Missing().size() == 1);
            REQUIRE(std::get<NavigationNoPath>(outcomes[2]).Provenance().world == Provenance().world);
            REQUIRE(std::get<NavigationNoPath>(outcomes[2]).Coverage().IsComplete());
            REQUIRE(std::get<NavigationStale>(outcomes[4]).ExpectedTopology() != std::get<NavigationStale>(outcomes[4]).ObservedTopology());
            static_assert(!std::is_same_v<NavigationPartial<Value>, NavigationStale>);
        }

        TEST_CASE("Path-like navigation outcomes require valid provenance and matching coverage proof", "[navigation][outcome][coverage]") {
            REQUIRE(ValidateNavigationOutcomeProvenance(Provenance()));
            using Mutation = void (*)(NavigationOutcomeProvenance &);
            const std::array<Mutation, 7> mutations{
                [](auto &value) {
                value.snapshot = {};
            },
                [](auto &value) {
                value.world = {};
            },
                [](auto &value) {
                value.topology = {};
            },
                [](auto &value) {
                value.obstacleRevision = 0;
            },
                [](auto &value) {
                value.filterRevision = 0;
            },
                [](auto &value) {
                value.profileRevision = 0;
            },
                [](auto &value) {
                value.originRevision = 0;
            },
            };
            for (const auto mutate : mutations) {
                auto invalid = Provenance();
                mutate(invalid);
                REQUIRE_FALSE(ValidateNavigationOutcomeProvenance(invalid));
                ExpectOutcomeDescriptorError(NavigationSucceeded<int>::Create(1, invalid, NavigationCoverageEvidence::CompleteTopology()));
            }

            const auto complete = NavigationCoverageEvidence::CompleteTopology();
            const auto partial = PartialCoverage();
            REQUIRE(complete.IsComplete());
            REQUIRE(complete.Scope() == NavigationCoverageScope::WholeTopology);
            REQUIRE_FALSE(partial.IsComplete());
            REQUIRE(partial.Scope() == NavigationCoverageScope::ExactRegions);
            ExpectOutcomeDescriptorError(NavigationSucceeded<int>::Create(1, Provenance(), partial));
            ExpectOutcomeDescriptorError(NavigationPartial<int>::Create(1, Provenance(), complete));
            ExpectOutcomeDescriptorError(NavigationNoPath::Create(Provenance(), partial));
        }

        TEST_CASE("Exact navigation coverage rejects invalid duplicate and over-capacity evidence", "[navigation][outcome][coverage]") {
            const std::array valid{Dependency(1, 2), Dependency(2, 3)};
            auto complete = NavigationCoverageEvidence::CompleteExact(valid);
            REQUIRE(complete.HasValue());
            REQUIRE(complete.Value().Covered().size() == valid.size());
            REQUIRE(complete.Value().Missing().empty());

            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::CompleteExact({}));
            const std::array invalidRegion{NavigationCoverageDependency{.region = 0, .generation = Identity<NavigationGeneration>(1)}};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::CompleteExact(invalidRegion));
            const std::array invalidGeneration{NavigationCoverageDependency{.region = 1}};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::CompleteExact(invalidGeneration));
            const std::array duplicate{Dependency(1, 2), Dependency(1, 3)};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::CompleteExact(duplicate));
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::PartialExact(valid, {}));
            const std::array overlapping{Dependency(2, 4)};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::PartialExact(valid, overlapping));
            const std::array duplicateMissing{Dependency(3, 4), Dependency(3, 5)};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::PartialExact({}, duplicateMissing));
            const std::array invalidMissing{NavigationCoverageDependency{.region = 0, .generation = Identity<NavigationGeneration>(1)}};
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::PartialExact({}, invalidMissing));

            std::vector<NavigationCoverageDependency> maximum;
            maximum.reserve(MaximumNavigationOutcomeCoverageDependencies + 1);
            for (std::size_t index = 0; index <= MaximumNavigationOutcomeCoverageDependencies; ++index)
                maximum.push_back(Dependency(index + 1, index + 2));
            REQUIRE(NavigationCoverageEvidence::CompleteExact(
                        std::span<const NavigationCoverageDependency>{maximum}.first(MaximumNavigationOutcomeCoverageDependencies))
                        .HasValue());
            ExpectOutcomeDescriptorError(NavigationCoverageEvidence::CompleteExact(maximum));
        }

        TEST_CASE("Closed navigation outcome factories reject enum sentinels and impossible generations", "[navigation][outcome]") {
            for (const auto reason : {NavigationUnavailableReason::NoNavigationData, NavigationUnavailableReason::WorldUnavailable,
                                      NavigationUnavailableReason::ProviderUnavailable}) {
                auto unavailable = NavigationUnavailable::Create(reason);
                REQUIRE(unavailable.HasValue());
                REQUIRE(unavailable.Value().Reason() == reason);
            }
            for (const auto reason : {NavigationUnavailableReason::Count, static_cast<NavigationUnavailableReason>(255)})
                ExpectOutcomeDescriptorError(NavigationUnavailable::Create(reason));

            for (const auto category :
                 {NavigationProviderFailureCategory::InvalidProviderData, NavigationProviderFailureCategory::ResourceExhausted,
                  NavigationProviderFailureCategory::TransientFailure, NavigationProviderFailureCategory::PermanentFailure}) {
                auto failed = MakeNavigationProviderFailure(category);
                REQUIRE(failed.HasValue());
                REQUIRE(failed.Value().Category() == category);
            }
            for (const auto category : {NavigationProviderFailureCategory::Count, static_cast<NavigationProviderFailureCategory>(255)})
                ExpectOutcomeDescriptorError(MakeNavigationProviderFailure(category));

            const auto generation = Identity<NavigationGeneration>(1);
            ExpectOutcomeDescriptorError(NavigationStale::Create({}, generation));
            ExpectOutcomeDescriptorError(NavigationStale::Create(generation, {}));
            ExpectOutcomeDescriptorError(NavigationStale::Create(generation, generation));
            static_assert(!std::is_aggregate_v<NavigationFailed>);
            static_assert(!std::is_default_constructible_v<NavigationFailed>);
        }

        TEST_CASE("Provider failure detail remains ordered Horo diagnostic evidence", "[navigation][outcome][diagnostic]") {
            std::vector<Diagnostic> diagnostics{
                {
                    .code = DiagnosticCode{"navigation.provider.capacity"},
                    .severity = DiagnosticSeverity::Error,
                    .message = "Provider scratch capacity was exhausted.",
                    .location = {.source = "query", .line = 4, .column = 2},
                },
                {
                    .code = DiagnosticCode{"navigation.provider.retry"},
                    .severity = DiagnosticSeverity::Note,
                    .message = "Retry after reducing the admitted node limit.",
                },
            };
            auto failure = MakeNavigationProviderFailure(NavigationProviderFailureCategory::ResourceExhausted, diagnostics);
            REQUIRE(failure.HasValue());
            REQUIRE(failure.Value().Category() == NavigationProviderFailureCategory::ResourceExhausted);
            const auto &error = failure.Value().ErrorValue();
            REQUIRE(error.domain.Value() == "horo.navigation");
            REQUIRE(error.code.Value() == "navigation.provider.failed");
            REQUIRE(error.message == NavigationErrors::ProviderFailed.summary);
            REQUIRE(error.diagnostics.size() == diagnostics.size());
            for (std::size_t index = 0; index < diagnostics.size(); ++index) {
                REQUIRE(error.diagnostics[index].code.Value() == diagnostics[index].code.Value());
                REQUIRE(error.diagnostics[index].severity == diagnostics[index].severity);
                REQUIRE(error.diagnostics[index].message == diagnostics[index].message);
                REQUIRE(error.diagnostics[index].location.source == diagnostics[index].location.source);
                REQUIRE(error.diagnostics[index].location.line == diagnostics[index].location.line);
                REQUIRE(error.diagnostics[index].location.column == diagnostics[index].location.column);
            }
        }

        TEST_CASE("Navigation error descriptors keep admission and terminal identities distinct", "[navigation][error]") {
            const std::array descriptors{
                &NavigationErrors::IdentityInvalid,       &NavigationErrors::InvalidHandle,
                &NavigationErrors::GenerationExhausted,   &NavigationErrors::CapabilityDescriptorInvalid,
                &NavigationErrors::CapabilityStale,       &NavigationErrors::OperationUnsupported,
                &NavigationErrors::CapabilityUnavailable, &NavigationErrors::QueryLimitExceeded,
                &NavigationErrors::AdmissionRejected,     &NavigationErrors::QueryCancelled,
                &NavigationErrors::StaleSnapshot,         &NavigationErrors::NoNavigationData,
                &NavigationErrors::ProviderFailed,        &NavigationErrors::OutcomeDescriptorInvalid,
                &NavigationErrors::InvalidWorld,          &NavigationErrors::CapacityExceeded,
            };
            for (std::size_t first = 0; first < descriptors.size(); ++first) {
                REQUIRE(descriptors[first]->domain.Value() == "horo.navigation");
                REQUIRE_FALSE(descriptors[first]->code.Value().empty());
                REQUIRE_FALSE(descriptors[first]->summary.empty());
                REQUIRE_FALSE(descriptors[first]->remediationHint.empty());
                for (std::size_t second = first + 1; second < descriptors.size(); ++second)
                    REQUIRE(descriptors[first]->code.Value() != descriptors[second]->code.Value());
            }
            REQUIRE(NavigationErrors::OperationUnsupported.code.Value() == "navigation.operation.unsupported");
            REQUIRE(NavigationErrors::QueryLimitExceeded.code.Value() == "navigation.query.limit_exceeded");
            REQUIRE(NavigationErrors::AdmissionRejected.code.Value() == "navigation.query.admission_rejected");
            REQUIRE(NavigationErrors::CapacityExceeded.code.Value() == "navigation.query.capacity_exceeded");
        }
    }  // namespace
}  // namespace Horo::Navigation
