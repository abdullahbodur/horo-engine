#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationOutcomes.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace Horo::Navigation {
    namespace {
        NavigationGeneration Generation(const std::uint64_t value) {
            auto generation = NavigationGeneration::Create(value);
            REQUIRE(generation.HasValue());
            return generation.Value();
        }

        TEST_CASE("Navigation outcomes preserve every terminal state as a distinct variant", "[navigation][outcome]") {
            using Outcome = NavigationOutcome<std::vector<std::uint32_t>>;
            const std::array<Outcome, static_cast<std::size_t>(NavigationOutcomeKind::Count)> outcomes{
                NavigationSucceeded<std::vector<std::uint32_t>>{{1, 2}},
                NavigationPartial<std::vector<std::uint32_t>>{{1}},
                NavigationNoPath{},
                NavigationCancelled{},
                NavigationStale{.expectedTopology = Generation(8), .observedTopology = Generation(7)},
                NavigationUnavailable{.reason = NavigationUnavailableReason::NoNavigationData},
                MakeNavigationProviderFailure(NavigationProviderFailureCategory::PermanentFailure),
            };
            for (std::size_t index = 0; index < outcomes.size(); ++index)
                REQUIRE(GetNavigationOutcomeKind(outcomes[index]) == static_cast<NavigationOutcomeKind>(index));

            REQUIRE(std::get<NavigationSucceeded<std::vector<std::uint32_t>>>(outcomes[0]).value == std::vector<std::uint32_t>{1, 2});
            REQUIRE(std::get<NavigationPartial<std::vector<std::uint32_t>>>(outcomes[1]).value == std::vector<std::uint32_t>{1});
            REQUIRE(std::get<NavigationStale>(outcomes[4]).expectedTopology != std::get<NavigationStale>(outcomes[4]).observedTopology);
            static_assert(!std::is_same_v<NavigationPartial<std::vector<std::uint32_t>>, NavigationStale>);
        }

        TEST_CASE("Navigation unavailability and provider failures expose closed typed categories", "[navigation][outcome]") {
            for (const auto reason : {NavigationUnavailableReason::NoNavigationData, NavigationUnavailableReason::WorldUnavailable,
                                      NavigationUnavailableReason::ProviderUnavailable}) {
                const NavigationOutcome<int> outcome = NavigationUnavailable{.reason = reason};
                REQUIRE(GetNavigationOutcomeKind(outcome) == NavigationOutcomeKind::Unavailable);
                REQUIRE(std::get<NavigationUnavailable>(outcome).reason == reason);
            }
            for (const auto category :
                 {NavigationProviderFailureCategory::InvalidProviderData, NavigationProviderFailureCategory::ResourceExhausted,
                  NavigationProviderFailureCategory::TransientFailure, NavigationProviderFailureCategory::PermanentFailure}) {
                const NavigationOutcome<int> outcome = MakeNavigationProviderFailure(category);
                REQUIRE(GetNavigationOutcomeKind(outcome) == NavigationOutcomeKind::Failed);
                REQUIRE(std::get<NavigationFailed>(outcome).category == category);
            }
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
            const auto failure = MakeNavigationProviderFailure(NavigationProviderFailureCategory::ResourceExhausted, diagnostics);
            REQUIRE(failure.category == NavigationProviderFailureCategory::ResourceExhausted);
            REQUIRE(failure.error.domain.Value() == "horo.navigation");
            REQUIRE(failure.error.code.Value() == "navigation.provider.failed");
            REQUIRE(failure.error.message == NavigationErrors::ProviderFailed.summary);
            REQUIRE(failure.error.diagnostics.size() == diagnostics.size());
            for (std::size_t index = 0; index < diagnostics.size(); ++index) {
                REQUIRE(failure.error.diagnostics[index].code.Value() == diagnostics[index].code.Value());
                REQUIRE(failure.error.diagnostics[index].severity == diagnostics[index].severity);
                REQUIRE(failure.error.diagnostics[index].message == diagnostics[index].message);
                REQUIRE(failure.error.diagnostics[index].location.source == diagnostics[index].location.source);
                REQUIRE(failure.error.diagnostics[index].location.line == diagnostics[index].location.line);
                REQUIRE(failure.error.diagnostics[index].location.column == diagnostics[index].location.column);
            }
        }

        TEST_CASE("Navigation error descriptors keep admission and terminal identities distinct", "[navigation][error]") {
            const std::array descriptors{
                &NavigationErrors::IdentityInvalid,       &NavigationErrors::InvalidHandle,
                &NavigationErrors::GenerationExhausted,   &NavigationErrors::CapabilityDescriptorInvalid,
                &NavigationErrors::CapabilityStale,       &NavigationErrors::OperationUnsupported,
                &NavigationErrors::CapabilityUnavailable, &NavigationErrors::QueryLimitExceeded,
                &NavigationErrors::QueryCancelled,        &NavigationErrors::StaleSnapshot,
                &NavigationErrors::NoNavigationData,      &NavigationErrors::ProviderFailed,
            };
            for (std::size_t first = 0; first < descriptors.size(); ++first) {
                REQUIRE(descriptors[first]->domain.Value() == "horo.navigation");
                REQUIRE_FALSE(descriptors[first]->code.Value().empty());
                REQUIRE_FALSE(descriptors[first]->summary.empty());
                REQUIRE_FALSE(descriptors[first]->remediationHint.empty());
                for (std::size_t second = first + 1; second < descriptors.size(); ++second)
                    REQUIRE(descriptors[first]->code.Value() != descriptors[second]->code.Value());
            }
        }
    }  // namespace
}  // namespace Horo::Navigation
