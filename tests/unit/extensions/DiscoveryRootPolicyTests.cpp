#include "DiscoveryRootPolicy.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>

namespace Horo::Extensions::Discovery::Tests {
    namespace {
        RootRequest ApprovedRequest(RootKind kind, ConfigurationOrigin configuration) {
            return {.id = "test-root",
                    .path = std::filesystem::temp_directory_path(),
                    .kind = kind,
                    .approval = RootApproval::Approved,
                    .configuration = configuration};
        }

        RootSelection SelectOne(const RootRequest &request, const RootPolicy policy = {}) {
            auto selected = SelectApprovedRoots(std::span(&request, 1), policy);
            REQUIRE(selected.HasValue());
            REQUIRE(selected.Value().diagnostics.size() == 1);
            return std::move(selected).Value();
        }
    }  // namespace

    TEST_CASE("Discovery root approval is explicit for every source kind", "[Extensions][Discovery]") {
        const std::array requests{ApprovedRequest(RootKind::System, ConfigurationOrigin::ProductPolicy),
                                  ApprovedRequest(RootKind::User, ConfigurationOrigin::UserLocal),
                                  ApprovedRequest(RootKind::Project, ConfigurationOrigin::ProjectPortable),
                                  ApprovedRequest(RootKind::Development, ConfigurationOrigin::UserLocal)};
        for (auto request : requests) {
            request.approval = RootApproval::Pending;
            request.path.clear();
            const auto selection = SelectOne(request, {.profile = DiscoveryProfile::Development, .enableDevelopmentOverrides = true});
            CHECK(selection.roots.empty());
            CHECK_FALSE(selection.nonPortable);
            CHECK(selection.diagnostics.front().disposition == RootDisposition::ApprovalRequired);
        }
    }

    TEST_CASE("Only the expected configuration authority can supply a discovery root", "[Extensions][Discovery]") {
        const std::array requests{ApprovedRequest(RootKind::System, ConfigurationOrigin::ProjectPortable),
                                  ApprovedRequest(RootKind::User, ConfigurationOrigin::ProjectPortable),
                                  ApprovedRequest(RootKind::Project, ConfigurationOrigin::UserLocal),
                                  ApprovedRequest(RootKind::Development, ConfigurationOrigin::ProjectPortable),
                                  ApprovedRequest(static_cast<RootKind>(-1), ConfigurationOrigin::UserLocal),
                                  ApprovedRequest(RootKind::User, static_cast<ConfigurationOrigin>(-1))};
        for (auto request : requests) {
            request.path.clear();
            const auto selection = SelectOne(request, {.profile = DiscoveryProfile::Development, .enableDevelopmentOverrides = true});
            CHECK(selection.roots.empty());
            CHECK(selection.diagnostics.front().disposition == RootDisposition::InvalidAuthority);
        }
    }

    TEST_CASE("Approved portable roots are canonical and independent of input ordering", "[Extensions][Discovery]") {
        auto user = ApprovedRequest(RootKind::User, ConfigurationOrigin::UserLocal);
        auto system = ApprovedRequest(RootKind::System, ConfigurationOrigin::ProductPolicy);
        auto project = ApprovedRequest(RootKind::Project, ConfigurationOrigin::ProjectPortable);
        user.id = "c-user";
        system.id = "b-system";
        project.id = "a-project";
        std::array requests{user, system, project};
        for (int permutation = 0; permutation < 3; ++permutation) {
            auto selected = SelectApprovedRoots(requests, {});
            REQUIRE(selected.HasValue());
            REQUIRE(selected.Value().roots.size() == 3);
            CHECK(selected.Value().roots[0].id == project.id);
            CHECK(selected.Value().roots[1].id == system.id);
            CHECK(selected.Value().roots[2].id == user.id);
            CHECK(selected.Value().roots[0].canonicalPath == std::filesystem::canonical(project.path));
            CHECK(selected.Value().diagnostics[0].disposition == RootDisposition::Accepted);
            CHECK_FALSE(selected.Value().nonPortable);
            std::ranges::rotate(requests, requests.begin() + 1);
        }
    }

    TEST_CASE("Development roots require explicit opt-in and cannot enter reproducible profiles", "[Extensions][Discovery]") {
        auto request = ApprovedRequest(RootKind::Development, ConfigurationOrigin::UserLocal);
        const std::array profiles{DiscoveryProfile::Normal, DiscoveryProfile::ContinuousIntegration, DiscoveryProfile::Release,
                                  DiscoveryProfile::OfflineReproducible};
        for (const auto profile : profiles) {
            const auto selection = SelectOne(request, {.profile = profile, .enableDevelopmentOverrides = true});
            CHECK(selection.roots.empty());
            CHECK_FALSE(selection.nonPortable);
            CHECK(selection.diagnostics.front().disposition == RootDisposition::DevelopmentDisabled);
        }
        CHECK(SelectOne(request, {.profile = DiscoveryProfile::Development}).roots.empty());
        const auto enabled = SelectOne(request, {.profile = DiscoveryProfile::Development, .enableDevelopmentOverrides = true});
        REQUIRE(enabled.roots.size() == 1);
        CHECK(enabled.nonPortable);
        CHECK(enabled.diagnostics.front().disposition == RootDisposition::DevelopmentNonPortable);
        request.path.clear();
        CHECK(SelectOne(request).roots.empty());
    }

    TEST_CASE("Invalid root selection fails atomically", "[Extensions][Discovery]") {
        auto request = ApprovedRequest(RootKind::User, ConfigurationOrigin::UserLocal);
        std::vector requests{request, request};
        CHECK(SelectApprovedRoots(requests, {}).HasError());
        requests = {request};
        requests.front().id.clear();
        CHECK(SelectApprovedRoots(requests, {}).HasError());
        requests.assign(65, request);
        CHECK(SelectApprovedRoots(requests, {}).HasError());
        request.path.clear();
        CHECK(SelectApprovedRoots(std::span(&request, 1), {}).HasError());
        CHECK(SelectApprovedRoots({}, {}).Value().roots.empty());
    }
}  // namespace Horo::Extensions::Discovery::Tests
