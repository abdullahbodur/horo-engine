#include "Horo/Packages/PackageDependencyResolver.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <random>

namespace {
    using namespace Horo::Packages;

    HoroPackageId Id(const std::string_view text) {
        auto parsed = HoroPackageId::Parse(text);
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    PackageVersion Version(const std::string_view text) {
        auto parsed = PackageVersion::Parse(text);
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    PackageVersionRange Exact(const std::string_view text) {
        return {PackageVersionRange::Kind::Exact, Version(text)};
    }

    PackageVersionRange Caret(const std::string_view text) {
        return {PackageVersionRange::Kind::Caret, Version(text)};
    }

    PackageDependencyRequest Dependency(const std::string_view package, PackageVersionRange range,
                                        const PackageDependencyRequirement requirement = PackageDependencyRequirement::Required,
                                        std::vector<std::string> features = {}) {
        return {Id(package), std::move(range), requirement, std::move(features)};
    }

    PackageResolutionCandidate Candidate(const std::string_view package, const std::string_view version, const std::string_view source,
                                         const std::uint32_t rank, const std::string_view digest,
                                         std::vector<PackageDependencyRequest> dependencies = {}, std::vector<std::string> features = {},
                                         std::vector<PackagePlatform> platforms = {}) {
        return {Id(package),         Version(version),        std::string{source},  rank, std::string{digest},
                std::move(features), std::move(dependencies), std::move(platforms), false};
    }

    std::vector<std::string> PlanIdentity(const PackageResolutionPlan &plan) {
        std::vector<std::string> result;
        for (const ResolvedPackage &package : plan.packages)
            result.push_back(package.package.Value() + "@" + package.version.ToString() + "#" + package.sourceId);
        return result;
    }

    void CheckFailure(const Horo::Result<PackageResolutionPlan> &result, const std::string_view code, const std::string_view message = {}) {
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().code.Value() == code);
        if (!message.empty())
            CHECK(result.ErrorValue().message == message);
    }
}  // namespace

TEST_CASE("Package resolver produces the same transitive plan for every candidate order", "[packages][resolver]") {
    PackageResolutionRequest request{
        .roots = {Dependency("com.game.root", Caret("1.0.0"))},
        .candidates = {Candidate("com.game.root", "1.1.0", "vendor.index", 10, "sha256:root-new",
                                 {Dependency("com.horo.runtime", Caret("2.0.0")),
                                  Dependency("com.horo.editor", Exact("1.0.0"), PackageDependencyRequirement::Optional)}),
                       Candidate("com.game.root", "1.0.0", "vendor.index", 10, "sha256:root-old"),
                       Candidate("com.horo.runtime", "2.2.0", "public", 20, "sha256:runtime-new", {}, {"physics"},
                                 {{"linux", "x64", "horo-sdk-2"}}),
                       Candidate("com.horo.runtime", "2.1.0", "vendor.index", 10, "sha256:runtime-vendor", {}, {"physics"},
                                 {{"linux", "x64", "horo-sdk-2"}}),
                       Candidate("com.horo.editor", "1.0.0", "public", 20, "sha256:editor")},
        .host = {"linux", "x64", "horo-sdk-2"},
    };

    const auto expected = PackageDependencyResolver::Resolve(request);
    REQUIRE(expected.HasValue());
    REQUIRE((PlanIdentity(expected.Value()) ==
             std::vector<std::string>{"com.game.root@1.1.0#vendor.index", "com.horo.runtime@2.1.0#vendor.index"}));

    std::mt19937 random{42};
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::ranges::shuffle(request.candidates, random);
        const auto repeated = PackageDependencyResolver::Resolve(request);
        REQUIRE(repeated.HasValue());
        CHECK(PlanIdentity(repeated.Value()) == PlanIdentity(expected.Value()));
    }

    request.includeOptionalDependencies = true;
    const auto withOptional = PackageDependencyResolver::Resolve(request);
    REQUIRE(withOptional.HasValue());
    CHECK(
        (PlanIdentity(withOptional.Value()) == std::vector<std::string>{"com.game.root@1.1.0#vendor.index", "com.horo.editor@1.0.0#public",
                                                                        "com.horo.runtime@2.1.0#vendor.index"}));
}

TEST_CASE("Package resolver filters platform and required features", "[packages][resolver]") {
    PackageResolutionRequest request{
        .roots = {Dependency("com.horo.runtime", Caret("2.0.0"), PackageDependencyRequirement::Required, {"physics"})},
        .candidates = {Candidate("com.horo.runtime", "2.3.0", "public", 10, "sha256:windows", {}, {"physics"},
                                 {{"windows", "x64", "horo-sdk-2"}}),
                       Candidate("com.horo.runtime", "2.2.0", "public", 10, "sha256:no-feature", {}, {}, {{"linux", "x64", "horo-sdk-2"}}),
                       Candidate("com.horo.runtime", "2.1.0", "public", 10, "sha256:linux", {}, {"physics"},
                                 {{"linux", "x64", "horo-sdk-2"}})},
        .host = {"linux", "x64", "horo-sdk-2"},
    };
    const auto result = PackageDependencyResolver::Resolve(request);
    REQUIRE(result.HasValue());
    REQUIRE((PlanIdentity(result.Value()) == std::vector<std::string>{"com.horo.runtime@2.1.0#public"}));
}

TEST_CASE("Package resolver reports stable constraint conflicts", "[packages][resolver]") {
    SECTION("conflicting transitive constraints") {
        PackageResolutionRequest request{
            .roots = {Dependency("com.game.a", Exact("1.0.0")), Dependency("com.game.b", Exact("1.0.0"))},
            .candidates = {Candidate("com.game.a", "1.0.0", "public", 10, "sha256:a", {Dependency("com.shared", Caret("1.0.0"))}),
                           Candidate("com.game.b", "1.0.0", "public", 10, "sha256:b", {Dependency("com.shared", Caret("2.0.0"))}),
                           Candidate("com.shared", "1.5.0", "public", 10, "sha256:shared-1"),
                           Candidate("com.shared", "2.5.0", "public", 10, "sha256:shared-2")},
        };
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.conflict", "Conflicting constraints for com.shared");
    }

    SECTION("a later edge can conflict with an already selected package") {
        PackageResolutionRequest request{
            .roots = {Dependency("com.a.shared", Caret("1.0.0")), Dependency("com.z.root", Exact("1.0.0"))},
            .candidates = {Candidate("com.a.shared", "1.5.0", "public", 10, "sha256:shared"),
                           Candidate("com.z.root", "1.0.0", "public", 10, "sha256:root", {Dependency("com.a.shared", Caret("2.0.0"))})},
        };
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.conflict", "Conflicting constraints for com.a.shared");
    }
}

TEST_CASE("Package resolver reports stable cycles and source ambiguity", "[packages][resolver]") {
    SECTION("dependency cycle") {
        PackageResolutionRequest request{
            .roots = {Dependency("com.cycle.a", Exact("1.0.0"))},
            .candidates = {Candidate("com.cycle.a", "1.0.0", "public", 10, "sha256:a", {Dependency("com.cycle.b", Exact("1.0.0"))}),
                           Candidate("com.cycle.b", "1.0.0", "public", 10, "sha256:b", {Dependency("com.cycle.a", Exact("1.0.0"))})},
        };
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.cycle", "Dependency cycle: com.cycle.a -> com.cycle.b -> com.cycle.a");
    }

    SECTION("same package version has conflicting source digests") {
        PackageResolutionRequest request{
            .roots = {Dependency("com.ambiguous", Exact("1.0.0"))},
            .candidates = {Candidate("com.ambiguous", "1.0.0", "public", 20, "sha256:one"),
                           Candidate("com.ambiguous", "1.0.0", "vendor", 10, "sha256:two")},
        };
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.source_ambiguity", "Source ambiguity for com.ambiguous@1.0.0");
    }
}

TEST_CASE("Package resolver backtracks and enforces input limits", "[packages][resolver]") {
    SECTION("an older root version can satisfy the complete graph") {
        PackageResolutionRequest request{
            .roots = {Dependency("com.root", Caret("1.0.0")), Dependency("com.shared", Caret("1.0.0"))},
            .candidates = {Candidate("com.root", "1.1.0", "public", 10, "sha256:new", {Dependency("com.shared", Caret("2.0.0"))}),
                           Candidate("com.root", "1.0.0", "public", 10, "sha256:old", {Dependency("com.shared", Caret("1.0.0"))}),
                           Candidate("com.shared", "1.2.0", "public", 10, "sha256:shared")},
        };
        const auto result = PackageDependencyResolver::Resolve(request);
        REQUIRE(result.HasValue());
        CHECK((PlanIdentity(result.Value()) == std::vector<std::string>{"com.root@1.0.0#public", "com.shared@1.2.0#public"}));
    }

    SECTION("candidate limit is checked before search") {
        PackageResolutionRequest request{.roots = {Dependency("com.root", Exact("1.0.0"))},
                                         .candidates = {Candidate("com.root", "1.0.0", "public", 10, "sha256:root")},
                                         .limits = {.candidates = 0}};
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.limit");
    }

    SECTION("search exploration is bounded independently of graph depth") {
        PackageResolutionRequest request{.roots = {Dependency("com.root", Exact("1.0.0"))},
                                         .candidates = {Candidate("com.root", "1.0.0", "public", 10, "sha256:root")},
                                         .limits = {.searchSteps = 0}};
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.limit", "Dependency search exceeds its exploration-step limit.");
    }
}

TEST_CASE("Package semantic versions follow prerelease precedence", "[packages][resolver][semver]") {
    const std::vector ordered{Version("1.0.0-alpha"),  Version("1.0.0-alpha.1"), Version("1.0.0-alpha.beta"), Version("1.0.0-beta"),
                              Version("1.0.0-beta.2"), Version("1.0.0-beta.11"), Version("1.0.0-rc.1"),       Version("1.0.0")};
    CHECK(std::ranges::is_sorted(ordered));
    CHECK(PackageVersion::Parse("1.0.0-alpha.01").HasError());
    CHECK(PackageVersion::Parse("1.0").HasError());
    CHECK(PackageVersion::Parse("01.0.0").HasError());
    CHECK(HoroPackageId::Parse("com..horo").HasError());
}

TEST_CASE("Package resolver distinguishes unsatisfied and malformed input", "[packages][resolver]") {
    SECTION("a valid request without a compatible candidate is unsatisfied") {
        PackageResolutionRequest request{.roots = {Dependency("com.missing", Exact("1.0.0"))}};
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.unsatisfied");
    }

    SECTION("aggregate inputs cannot bypass canonical range validation") {
        PackageResolutionRequest request{.roots = {Dependency("com.root", Exact("1.0.0"))},
                                         .candidates = {Candidate("com.root", "1.0.0", "public", 10, "sha256:root")}};
        request.roots.front().versions.kind = static_cast<PackageVersionRange::Kind>(255);
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.invalid_input");
    }

    SECTION("candidate platform tuples are canonical") {
        PackageResolutionRequest request{.roots = {Dependency("com.root", Exact("1.0.0"))},
                                         .candidates = {Candidate("com.root", "1.0.0", "public", 10, "sha256:root", {}, {},
                                                                  {{"Linux", "x64", "horo-sdk-2"}})},
                                         .host = {"linux", "x64", "horo-sdk-2"}};
        const auto result = PackageDependencyResolver::Resolve(request);
        CheckFailure(result, "packages.resolver.invalid_input");
    }
}
