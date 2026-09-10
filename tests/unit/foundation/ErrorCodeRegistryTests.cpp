#include "Horo/Foundation/ErrorCodeRegistry.h"
#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Foundation/ModuleHost.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

namespace {
    using namespace Horo;

    const ErrorCodeDescriptor kAssetMissing{.domain = ErrorDomainId{"horo.asset"},
                                            .code = ErrorCode{"asset.missing"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = "Asset is missing.",
                                            .remediationHint = "Restore the asset."};
    const ErrorCodeDescriptor kAssetUnreadable{.domain = ErrorDomainId{"horo.asset"},
                                               .code = ErrorCode{"asset.unreadable"},
                                               .defaultSeverity = ErrorSeverity::Error,
                                               .summary = "Asset is unreadable.",
                                               .remediationHint = "Check permissions."};
    const ErrorCodeDescriptor kAssetLegacy{.domain = ErrorDomainId{"horo.asset"},
                                           .code = ErrorCode{"asset.legacy_missing"},
                                           .defaultSeverity = ErrorSeverity::Warning,
                                           .summary = "Legacy missing asset identity.",
                                           .remediationHint = "Use the replacement identity.",
                                           .deprecatedBy = ErrorCode{"asset.missing"}};
    const ErrorCodeDescriptor kNestedImport{.domain = ErrorDomainId{"horo.asset.import"},
                                            .code = ErrorCode{"asset.import.failed"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = "Import failed."};

    [[nodiscard]] ModuleDescriptor ModuleWithDomain(std::string moduleId, std::string domain,
                                                    std::initializer_list<const ErrorCodeDescriptor *> descriptors) {
        ModuleDescriptor module{.id = ModuleId{std::move(moduleId)}, .version = {1, 0, 0}};
        module.errorDomains.push_back(ModuleErrorDomainDescriptor{.id = ErrorDomainId{std::move(domain)}, .descriptors = descriptors});
        return module;
    }

    [[nodiscard]] std::string ErrorIdentity(const Error &error) {
        return error.domain.Value() + "/" + error.code.Value();
    }

    int g_activationCount = 0;

    Result<void> CountActivation(ModuleActivationContext &) noexcept {
        ++g_activationCount;
        return Result<void>::Success();
    }

    void IgnoreDeactivation(ModuleActivationContext &) noexcept {}
}  // namespace

TEST_CASE("Error code registry owns and resolves stable textual descriptors", "[unit][foundation][errors][registry]") {
    const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetUnreadable, &kAssetMissing})};
    const Result<ErrorCodeRegistry> built = BuildErrorCodeRegistry(modules);

    REQUIRE(built.HasValue());
    const ErrorCodeRegistry registry = built.Value();
    REQUIRE(registry.Size() == 2);
    REQUIRE(registry.DomainCount() == 1);
    REQUIRE_FALSE(registry.Empty());

    const ErrorCodeDescriptor *missing = registry.Resolve(ErrorDomainId{"horo.asset"}, ErrorCode{"asset.missing"});
    REQUIRE(missing != nullptr);
    REQUIRE(missing->domain.Value() == "horo.asset");
    REQUIRE(missing->code.Value() == "asset.missing");
    REQUIRE(missing->summary == "Asset is missing.");
    REQUIRE(registry.OwnerOf(missing->domain, missing->code)->value == "horo.assets");

    const Error error = MakeError(*missing, "Tree mesh is missing.");
    REQUIRE(registry.Resolve(error) == missing);
    REQUIRE(ErrorIdentity(error) == "horo.asset/asset.missing");
    REQUIRE(registry.Resolve(ErrorDomainId{"horo.asset"}, ErrorCode{"asset.unknown"}) == nullptr);
}

TEST_CASE("Registry copies borrowed descriptor text into its immutable snapshot", "[unit][foundation][errors][registry]") {
    ErrorCodeRegistry registry;
    {
        std::string summary = "Temporary summary storage";
        std::string remediation = "Temporary remediation storage";
        const ErrorCodeDescriptor temporary{.domain = ErrorDomainId{"horo.temporary"},
                                            .code = ErrorCode{"temporary.failed"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = summary,
                                            .remediationHint = remediation};
        const std::array modules{ModuleWithDomain("horo.temporary_module", "horo.temporary", {&temporary})};
        Result<ErrorCodeRegistry> built = BuildErrorCodeRegistry(modules);
        REQUIRE(built.HasValue());
        registry = std::move(built).Value();
    }

    const ErrorCodeDescriptor *resolved = registry.Resolve(ErrorDomainId{"horo.temporary"}, ErrorCode{"temporary.failed"});
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->summary == "Temporary summary storage");
    REQUIRE(resolved->remediationHint == "Temporary remediation storage");

    const ErrorCodeRegistry shared = registry;
    REQUIRE(shared.Resolve(ErrorDomainId{"horo.temporary"}, ErrorCode{"temporary.failed"}) == resolved);
}

TEST_CASE("Registry rejects duplicate pairs and conflicting domain owners", "[unit][foundation][errors][registry]") {
    SECTION("duplicate pair within one claimed domain") {
        const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetMissing, &kAssetMissing})};
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(modules);
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == "foundation.error_registry.duplicate_code");
    }

    SECTION("same domain claimed by two modules") {
        const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetMissing}),
                                 ModuleWithDomain("horo.importer", "horo.asset", {&kAssetUnreadable})};
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(modules);
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == "foundation.error_registry.domain_ownership_conflict");
    }

    SECTION("nested namespace claimed by a different module") {
        const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetMissing}),
                                 ModuleWithDomain("horo.importer", "horo.asset.import", {&kNestedImport})};
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(modules);
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == "foundation.error_registry.domain_ownership_conflict");
    }
}

TEST_CASE("Registry rejects malformed and escaped namespaces", "[unit][foundation][errors][registry]") {
    const ErrorCodeDescriptor foreignDomain{.domain = ErrorDomainId{"horo.platform"},
                                            .code = ErrorCode{"platform.failed"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = "Platform failed."};
    const ErrorCodeDescriptor invalidCode{.domain = ErrorDomainId{"horo.asset"},
                                          .code = ErrorCode{"Not Namespaced"},
                                          .defaultSeverity = ErrorSeverity::Error,
                                          .summary = "Invalid code."};
    const ErrorCodeDescriptor missingSummary{.domain = ErrorDomainId{"horo.asset"},
                                             .code = ErrorCode{"asset.missing_summary"},
                                             .defaultSeverity = ErrorSeverity::Error};
    const ErrorCodeDescriptor invalidSeverity{.domain = ErrorDomainId{"horo.asset"},
                                              .code = ErrorCode{"asset.invalid_severity"},
                                              .defaultSeverity = static_cast<ErrorSeverity>(255),
                                              .summary = "Invalid severity."};

    const std::array invalidModules{
        ModuleWithDomain("extension.mesh_optimizer", "horo.asset", {&kAssetMissing}),
        ModuleWithDomain("extension.mesh_optimizer", "extension.other", {}),
        ModuleWithDomain("horo.assets", "Horo.Asset", {&kAssetMissing}),
        ModuleWithDomain("horo.assets", "horo.asset", {&foreignDomain}),
        ModuleWithDomain("horo.assets", "horo.asset", {&invalidCode}),
        ModuleWithDomain("horo.assets", "horo.asset", {&missingSummary}),
        ModuleWithDomain("horo.assets", "horo.asset", {&invalidSeverity}),
        ModuleWithDomain("horo.assets", "horo.asset", {nullptr}),
    };
    for (const ModuleDescriptor &module : invalidModules) {
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(std::span{&module, 1});
        REQUIRE(result.HasError());
        REQUIRE((result.ErrorValue().code.Value() == "foundation.error_registry.invalid_namespace" ||
                 result.ErrorValue().code.Value() == "foundation.error_registry.invalid_descriptor"));
    }
}

TEST_CASE("Registry validates deprecation replacements within the same domain", "[unit][foundation][errors][registry]") {
    SECTION("registered replacement") {
        const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetLegacy, &kAssetMissing})};
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(modules);
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().Resolve(kAssetLegacy.domain, kAssetLegacy.code)->deprecatedBy->Value() == "asset.missing");
    }

    SECTION("missing replacement") {
        const std::array modules{ModuleWithDomain("horo.assets", "horo.asset", {&kAssetLegacy})};
        const Result<ErrorCodeRegistry> result = BuildErrorCodeRegistry(modules);
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == "foundation.error_registry.invalid_deprecation");
    }
}

TEST_CASE("Module graph and host reject invalid registries before activation", "[unit][foundation][errors][registry][composition]") {
    SECTION("duplicate code") {
        g_activationCount = 0;
        ModuleDescriptor module = ModuleWithDomain("horo.assets", "horo.asset", {&kAssetMissing, &kAssetMissing});
        module.lifecycle = {.activate = &CountActivation, .deactivate = &IgnoreDeactivation};

        const Result<ValidatedModuleGraph> graph = ValidateModuleGraph(std::span{&module, 1});
        REQUIRE(graph.HasError());
        REQUIRE(graph.ErrorValue().code.Value() == "foundation.error_registry.duplicate_code");

        ModuleHost host;
        REQUIRE(host.Register(module).HasValue());
        const Result<std::size_t> activated = host.ActivateRegistered(nullptr);
        REQUIRE(activated.HasError());
        REQUIRE(activated.ErrorValue().code.Value() == "foundation.error_registry.duplicate_code");
        REQUIRE(g_activationCount == 0);
        REQUIRE(host.StateOf(module.id) == ModuleLifecycleState::Registered);
        REQUIRE(host.ErrorCodes() == nullptr);
    }

    SECTION("module namespace escape") {
        g_activationCount = 0;
        ModuleDescriptor module = ModuleWithDomain("extension.mesh_optimizer", "extension.other", {});
        module.lifecycle = {.activate = &CountActivation, .deactivate = &IgnoreDeactivation};

        ModuleHost host;
        REQUIRE(host.Register(module).HasValue());
        const Result<std::size_t> activated = host.ActivateRegistered(nullptr);
        REQUIRE(activated.HasError());
        REQUIRE(activated.ErrorValue().code.Value() == "foundation.error_registry.invalid_namespace");
        REQUIRE(g_activationCount == 0);
        REQUIRE(host.StateOf(module.id) == ModuleLifecycleState::Registered);
        REQUIRE(host.ErrorCodes() == nullptr);
    }
}

TEST_CASE("Module host publishes registry snapshots transactionally", "[unit][foundation][errors][registry][composition]") {
    ModuleHost host;
    const ModuleDescriptor assets = ModuleWithDomain("horo.assets", "horo.asset", {&kAssetMissing});
    REQUIRE(host.Register(assets).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasValue());

    const std::shared_ptr<const ErrorCodeRegistry> first = host.ErrorCodes();
    REQUIRE(first != nullptr);
    REQUIRE(first->Size() == 1);

    const ErrorCodeDescriptor platformTimeout{.domain = ErrorDomainId{"horo.platform"},
                                              .code = ErrorCode{"platform.timeout"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "Platform operation timed out."};
    const ModuleDescriptor platform = ModuleWithDomain("horo.platform", "horo.platform", {&platformTimeout});
    REQUIRE(host.Register(platform).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasValue());

    const std::shared_ptr<const ErrorCodeRegistry> second = host.ErrorCodes();
    REQUIRE(second != nullptr);
    REQUIRE(second != first);
    REQUIRE(second->Size() == 2);
    REQUIRE(first->Size() == 1);
    REQUIRE(first->Resolve(platformTimeout.domain, platformTimeout.code) == nullptr);
    REQUIRE(second->Resolve(platformTimeout.domain, platformTimeout.code) != nullptr);

    ModuleDescriptor conflicting = ModuleWithDomain("horo.importer", "horo.asset", {&kAssetUnreadable});
    REQUIRE(host.Register(conflicting).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasError());
    REQUIRE(host.ErrorCodes() == second);
    REQUIRE(second->Size() == 2);
}
