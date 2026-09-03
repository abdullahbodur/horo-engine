#include "Horo/Extensions/ExtensionDiscovery.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>

namespace Horo::Extensions::Discovery::Tests {
    struct DiscoveryPlanFixture {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                          ("horo-plan-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        RootRequest root{.id = "user",
                         .path = directory,
                         .kind = RootKind::User,
                         .approval = RootApproval::Approved,
                         .configuration = ConfigurationOrigin::UserLocal};

        DiscoveryPlanFixture() {
            REQUIRE(std::filesystem::create_directory(directory));
            std::filesystem::create_directory(directory / "alpha");
            std::filesystem::create_directory(directory / "beta");
            std::filesystem::create_directory(directory / "not-declared");
            std::ofstream(directory / "not-a-directory") << "{}";
        }

        ~DiscoveryPlanFixture() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

        Result<DiscoveryPlan> Discover(std::span<const PackageLocation> locations, RootPolicy policy = {}) const {
            return DiscoverDeclaredPackages(std::span(&root, 1), locations, policy);
        }
    };

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Only declared packages are discovered in stable identity order", "[Extensions][Discovery]") {
        std::array locations{PackageLocation{"com.example.beta", "user", "beta"}, PackageLocation{"com.example.alpha", "user", "alpha"}};
        for (int permutation = 0; permutation < 2; ++permutation) {
            const auto plan = Discover(locations);
            REQUIRE(plan.HasValue());
            REQUIRE(plan.Value().packages.size() == 2);
            CHECK(plan.Value().packages[0].packageId == "com.example.alpha");
            CHECK(plan.Value().packages[1].packageId == "com.example.beta");
            CHECK(plan.Value().packages[0].canonicalPath == std::filesystem::canonical(directory / "alpha"));
            CHECK(plan.Value().packages[0].rootId == "user");
            CHECK(plan.Value().packages[0].kind == RootKind::User);
            CHECK_FALSE(plan.Value().nonPortable);
            std::ranges::reverse(locations);
        }
        REQUIRE(Discover({}).HasValue());
        CHECK(Discover({}).Value().packages.empty());
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Duplicate packages fail instead of winning by input order", "[Extensions][Discovery]") {
        std::array locations{PackageLocation{"com.example.same", "user", "beta"}, PackageLocation{"com.example.same", "user", "alpha"}};
        const auto first = Discover(locations);
        REQUIRE(first.HasError());
        std::ranges::reverse(locations);
        const auto second = Discover(locations);
        REQUIRE(second.HasError());
        CHECK(first.ErrorValue().message == second.ErrorValue().message);
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Ignored roots never probe declared content", "[Extensions][Discovery]") {
        const std::array locations{PackageLocation{"com.example.alpha", "user", "../missing"}};
        root.approval = RootApproval::Pending;
        root.path.clear();
        const auto plan = Discover(locations);
        REQUIRE(plan.HasValue());
        CHECK(plan.Value().packages.empty());
        REQUIRE(plan.Value().rootDiagnostics.size() == 1);
        CHECK(plan.Value().rootDiagnostics[0].disposition == RootDisposition::ApprovalRequired);
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Ignored root IDs do not alias the next approved root", "[Extensions][Discovery]") {
        auto ignored = root;
        ignored.id = "aaa";
        ignored.approval = RootApproval::Pending;
        ignored.path.clear();
        const std::array roots{root, ignored};
        const std::array locations{PackageLocation{"ignored", "aaa", "../missing"}, PackageLocation{"accepted", "user", "alpha"}};
        const auto plan = DiscoverDeclaredPackages(roots, locations, {});
        REQUIRE(plan.HasValue());
        REQUIRE(plan.Value().packages.size() == 1);
        CHECK(plan.Value().packages[0].packageId == "accepted");
        CHECK(plan.Value().packages[0].rootId == "user");
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Non-directory packages report a meaningful failure", "[Extensions][Discovery]") {
        const std::array locations{PackageLocation{"com.example.file", "user", "not-a-directory"}};
        const auto plan = Discover(locations);
        REQUIRE(plan.HasError());
        CHECK(plan.ErrorValue().message.find("com.example.file") != std::string::npos);
        CHECK(plan.ErrorValue().message.find("not a directory") != std::string::npos);
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Development provenance remains visible and opt-in", "[Extensions][Discovery]") {
        root.kind = RootKind::Development;
        const std::array locations{PackageLocation{"com.example.alpha", "user", "alpha"}};
        CHECK(Discover(locations).Value().packages.empty());
        const auto plan = Discover(locations, {.profile = DiscoveryProfile::Development, .enableDevelopmentOverrides = true});
        REQUIRE(plan.HasValue());
        REQUIRE(plan.Value().packages.size() == 1);
        CHECK(plan.Value().nonPortable);
        CHECK(plan.Value().packages[0].kind == RootKind::Development);
    }

    TEST_CASE_METHOD(DiscoveryPlanFixture, "Invalid graph references and package paths reject the whole plan", "[Extensions][Discovery]") {
        const std::array invalid{PackageLocation{"", "user", "alpha"}, PackageLocation{"com.example.alpha", "unknown", "alpha"},
                                 PackageLocation{"com.example.alpha", "user", "missing"},
                                 PackageLocation{"com.example.alpha", "user", "not-a-directory"},
                                 PackageLocation{"com.example.alpha", "user", "../alpha"}};
        for (const auto &location : invalid)
            CHECK(Discover(std::span(&location, 1)).HasError());
        std::vector<PackageLocation> oversized(4097);
        CHECK(Discover(oversized).HasError());
        root.path.clear();
        CHECK(Discover({}).HasError());
    }
}  // namespace Horo::Extensions::Discovery::Tests
