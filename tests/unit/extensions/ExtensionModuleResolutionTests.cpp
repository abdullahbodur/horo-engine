#include "Horo/Extensions/ExtensionModuleResolution.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <utility>

namespace Horo::Extensions::Tests {
    namespace {
        [[nodiscard]] ExtensionModuleManifest Module(std::string id, const ExtensionModuleRole role) {
            return ExtensionModuleManifest{
                .id = std::move(id),
                .version = "1.0.0",
                .kind = "native",
                .roles = {role},
            };
        }

        void RequireResolutionError(const Result<ExtensionModulePlan> &result, const std::string_view expected) {
            REQUIRE(result.HasError());
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(std::string{expected}));
        }
    }  // namespace

    TEST_CASE("Extension module resolution is deterministic and binds service ownership", "[Extensions][Resolution]") {
        ExtensionModuleManifest backend = Module("com.example.backend", ExtensionModuleRole::BackendCapability);
        backend.exports.push_back({.id = "com.example.service", .contract = "com.horo.example", .version = "2.1.0"});
        ExtensionModuleManifest scripting = Module("com.example.scripting", ExtensionModuleRole::ScriptProvider);
        scripting.imports.push_back(
            {.id = "com.example.import", .service = "com.example.service", .contract = "com.horo.example", .minimumVersion = "2.0.0"});
        ExtensionModuleManifest editor = Module("com.example.editor", ExtensionModuleRole::EditorPresentation);
        editor.dependencies.push_back("com.example.backend");

        ExtensionManifest manifest;
        manifest.modules = {editor, scripting, backend};
        manifest.contributions = {{.type = "editor.panel", .id = "com.example.panel", .owningModule = editor.id},
                                  {.type = "application.service", .id = "com.example.backend-service", .owningModule = backend.id}};

        const auto interactive = ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive);
        REQUIRE(interactive.HasValue());
        CHECK(interactive.Value().moduleIds ==
              std::vector<std::string>{"com.example.backend", "com.example.editor", "com.example.scripting"});
        CHECK(interactive.Value().contributions.size() == 2);

        std::ranges::reverse(manifest.modules);
        const auto reordered = ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive);
        REQUIRE(reordered.HasValue());
        CHECK(reordered.Value().moduleIds == interactive.Value().moduleIds);

        const auto headless = ResolveExtensionModules(manifest, ExtensionHostProfile::Headless);
        REQUIRE(headless.HasValue());
        CHECK(headless.Value().moduleIds == std::vector<std::string>{"com.example.backend", "com.example.scripting"});
        REQUIRE(headless.Value().contributions.size() == 1);
        CHECK(headless.Value().contributions.front().owningModule == backend.id);
    }

    TEST_CASE("Extension module resolution reports complete graph failures", "[Extensions][Resolution]") {
        SECTION("duplicate service exports identify both owners") {
            ExtensionModuleManifest first = Module("com.example.first", ExtensionModuleRole::BackendCapability);
            ExtensionModuleManifest second = Module("com.example.second", ExtensionModuleRole::BackendCapability);
            first.exports.push_back({.id = "com.example.service", .contract = "com.example.contract", .version = "1.0.0"});
            second.exports = first.exports;
            ExtensionManifest manifest;
            manifest.modules = {second, first};
            const auto result = ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive);
            RequireResolutionError(result, "Duplicate service export");
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(first.id));
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(second.id));
        }

        SECTION("missing and incompatible imports identify consumers and providers") {
            ExtensionModuleManifest provider = Module("com.example.provider", ExtensionModuleRole::BackendCapability);
            provider.exports.push_back({.id = "com.example.service", .contract = "com.example.contract", .version = "1.4.0"});
            ExtensionModuleManifest consumer = Module("com.example.consumer", ExtensionModuleRole::ScriptProvider);
            consumer.imports.push_back({.id = "com.example.import",
                                        .service = "com.example.missing",
                                        .contract = "com.example.contract",
                                        .minimumVersion = "1.0.0"});
            ExtensionManifest manifest;
            manifest.modules = {provider, consumer};
            RequireResolutionError(ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive), "Missing service export");

            consumer.imports.front().service = "com.example.service";
            consumer.imports.front().minimumVersion = "2.0.0";
            manifest.modules = {provider, consumer};
            const auto result = ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive);
            RequireResolutionError(result, "Incompatible service export");
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(provider.id));
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(consumer.id));
        }

        SECTION("cycles identify every unresolved participant") {
            ExtensionModuleManifest first = Module("com.example.first", ExtensionModuleRole::BackendCapability);
            ExtensionModuleManifest second = Module("com.example.second", ExtensionModuleRole::BackendCapability);
            ExtensionModuleManifest third = Module("com.example.third", ExtensionModuleRole::BackendCapability);
            first.dependencies = {second.id};
            second.dependencies = {third.id};
            third.dependencies = {first.id};
            ExtensionManifest manifest;
            manifest.modules = {third, first, second};
            const auto result = ResolveExtensionModules(manifest, ExtensionHostProfile::Interactive);
            RequireResolutionError(result, "contains a cycle");
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(first.id));
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(second.id));
            CHECK_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring(third.id));
        }

        SECTION("headless backends cannot depend on omitted presentation modules") {
            ExtensionModuleManifest backend = Module("com.example.backend", ExtensionModuleRole::BackendCapability);
            ExtensionModuleManifest editor = Module("com.example.editor", ExtensionModuleRole::EditorPresentation);
            backend.dependencies = {editor.id};
            ExtensionManifest manifest;
            manifest.modules = {backend, editor};
            RequireResolutionError(ResolveExtensionModules(manifest, ExtensionHostProfile::Headless), "excluded presentation-only");
        }
    }
}  // namespace Horo::Extensions::Tests
