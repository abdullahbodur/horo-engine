#include "Horo/Extensions/ApplicationCapabilityRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Extensions::Tests {
    namespace {
        [[nodiscard]] ExtensionCapabilityAdmission Admission() {
            ExtensionAdmissionPolicy policy{
                .revision = 4,
                .knownPermissions = {{"project.read"}},
                .approvedPermissions = {{"project.read"}},
                .availableCapabilities = {{"horo.project.validate"}},
            };
            ExtensionAdmissionRequest request{
                .extensionId = "com.example.validator",
                .moduleId = "com.example.validator.backend",
                .activationGeneration = 9,
                .capabilities = {{{"horo.project.validate"}, {{"project.read"}}}},
            };
            auto admission = ExtensionCapabilityAdmission::Evaluate(request, policy);
            REQUIRE(admission.HasValue());
            return std::move(admission).Value();
        }

        [[nodiscard]] ApplicationCapabilityProviderDescriptor Provider(const ApplicationCapabilityVersion version = {1, 0, 0},
                                                                       const std::uint64_t generation = 1) {
            return {.capability = {"horo.project.validate"},
                    .version = version,
                    .providerId = "horo.project-validator",
                    .providerGeneration = generation};
        }

        void RequireErrorCode(const auto &result, const std::string &code) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == code);
        }
    }  // namespace

    TEST_CASE("Application capability registry resolves the highest compatible explicit provider",
              "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        auto first = registry.Register(Provider({1, 0, 0}, 1));
        auto second = registry.Register(Provider({1, 2, 0}, 2));
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        auto admission = Admission();
        auto authority = admission.Grant({"horo.project.validate"});
        REQUIRE(authority.HasValue());

        auto resolved =
            registry.Resolve(authority.Value(), {{1, 0, 0}, {1, 9, 9}}, "com.example.validator", "com.example.validator.backend", 9);
        REQUIRE(resolved.HasValue());
        CHECK(resolved.Value().Descriptor().version == ApplicationCapabilityVersion{1, 2, 0});
        CHECK(resolved.Value().Descriptor().providerGeneration == 2);
        CHECK(resolved.Value().ConsumerExtensionId() == "com.example.validator");
        CHECK(resolved.Value().ConsumerModuleId() == "com.example.validator.backend");
        CHECK(resolved.Value().ConsumerActivationGeneration() == 9);
    }

    TEST_CASE("Application capability registry reports unavailable and incompatible contracts explicitly",
              "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        auto registration = registry.Register(Provider());
        REQUIRE(registration.HasValue());
        ApplicationCapabilityProviderRegistration ownedRegistration = std::move(registration).Value();
        auto admission = Admission();
        auto authority = admission.Grant({"horo.project.validate"});
        REQUIRE(authority.HasValue());

        RequireErrorCode(registry.Resolve(authority.Value(), {{2, 0, 0}, {2, 9, 9}}, "com.example.validator",
                                          "com.example.validator.backend", 9),
                         "capability_version_incompatible");
        ownedRegistration.Reset();
        RequireErrorCode(registry.Resolve(authority.Value(), {{1, 0, 0}, {1, 9, 9}}, "com.example.validator",
                                          "com.example.validator.backend", 9),
                         "capability_unavailable");
    }

    TEST_CASE("Application capability registry never bypasses exact caller admission", "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        auto registration = registry.Register(Provider());
        REQUIRE(registration.HasValue());
        auto admission = Admission();
        auto authority = admission.Grant({"horo.project.validate"});
        REQUIRE(authority.HasValue());

        RequireErrorCode(registry.Resolve(authority.Value(), {{1, 0, 0}, {1, 0, 0}}, "com.example.other", "com.example.validator.backend",
                                          9),
                         "permission_denied");
        admission.Revoke();
        RequireErrorCode(registry.Resolve(authority.Value(), {{1, 0, 0}, {1, 0, 0}}, "com.example.validator",
                                          "com.example.validator.backend", 9),
                         "capability_revoked");
    }

    TEST_CASE("Application capability provider registration owns discoverability", "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        auto first = registry.Register(Provider());
        REQUIRE(first.HasValue());
        ApplicationCapabilityProviderRegistration ownedFirst = std::move(first).Value();
        RequireErrorCode(registry.Register(Provider({1, 0, 0}, 2)), "capability_registry_duplicate");
        CHECK(ownedFirst.IsRegistered());
        ownedFirst.Reset();
        CHECK_FALSE(ownedFirst.IsRegistered());
        auto replacement = registry.Register(Provider({1, 0, 0}, 2));
        REQUIRE(replacement.HasValue());
    }

    TEST_CASE("Application capability registry validates boundaries and closes terminally", "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        auto malformed = Provider();
        malformed.version.major = 0;
        RequireErrorCode(registry.Register(std::move(malformed)), "capability_registry_invalid");

        auto registration = registry.Register(Provider());
        REQUIRE(registration.HasValue());
        ApplicationCapabilityProviderRegistration ownedRegistration = std::move(registration).Value();
        registry.BeginShutdown();
        CHECK(registry.IsShutdown());
        CHECK_FALSE(ownedRegistration.IsRegistered());
        RequireErrorCode(registry.Register(Provider({1, 1, 0}, 2)), "capability_registry_shutdown");
    }

    TEST_CASE("Application capability registry rejects malformed canonical identities", "[Extensions][ApplicationCapabilities]") {
        for (const std::string &invalid :
             std::vector<std::string>{"invalid.", "invalid.2segment", "invalid_under", std::string(257, 'a')}) {
            ApplicationCapabilityRegistry registry;
            auto provider = Provider();
            provider.providerId = invalid;
            RequireErrorCode(registry.Register(std::move(provider)), "capability_registry_invalid");
        }
    }

    TEST_CASE("Application capability registry enforces its provider bound", "[Extensions][ApplicationCapabilities]") {
        ApplicationCapabilityRegistry registry;
        std::vector<ApplicationCapabilityProviderRegistration> registrations;
        registrations.reserve(ApplicationCapabilityRegistry::MaximumProviders);
        for (std::size_t index = 0; index < ApplicationCapabilityRegistry::MaximumProviders; ++index) {
            auto registration =
                registry.Register(Provider({1, static_cast<std::uint16_t>(index), 0}, static_cast<std::uint64_t>(index + 1)));
            REQUIRE(registration.HasValue());
            registrations.push_back(std::move(registration).Value());
        }
        RequireErrorCode(registry.Register(Provider({2, 0, 0}, 257)), "capability_registry_capacity_exceeded");
    }
}  // namespace Horo::Extensions::Tests
