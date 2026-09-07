#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveNamespace.h"
#include "SaveTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <variant>

namespace Horo::Runtime {
    namespace {
        using namespace Test;

        [[nodiscard]] SaveNamespaceId ClientNamespace(const std::uint8_t product = 1, const std::uint8_t environment = 2,
                                                      const std::uint8_t user = 3, const std::uint8_t profile = 4) {
            return {.product = Id<ProductStorageId>(product),
                    .environment = Id<EnvironmentStorageId>(environment),
                    .owner = UserProfileOwner{.user = Id<LocalUserStorageId>(user), .profile = Id<GameProfileId>(profile)}};
        }

        [[nodiscard]] SaveNamespaceId ServerNamespace(const std::uint8_t owner = 5) {
            return {.product = Id<ProductStorageId>(1),
                    .environment = Id<EnvironmentStorageId>(2),
                    .owner = ServerWorldOwner{.owner = Id<ServerStorageOwnerId>(owner)}};
        }

        TEST_CASE("Save namespace owners require every typed identity", "[unit][save][namespace]") {
            REQUIRE(ClientNamespace().IsValid());
            REQUIRE(ServerNamespace().IsValid());
            REQUIRE_FALSE(SaveNamespaceId{}.IsValid());

            auto missingUser = ClientNamespace();
            std::get<UserProfileOwner>(missingUser.owner).user = {};
            REQUIRE_FALSE(missingUser.IsValid());
            REQUIRE(EncodeSaveNamespaceKey(missingUser).ErrorValue().code.Value() == SaveErrors::NamespaceInvalid.code.Value());

            auto missingProfile = ClientNamespace();
            std::get<UserProfileOwner>(missingProfile.owner).profile = {};
            REQUIRE_FALSE(missingProfile.IsValid());

            auto missingServer = ServerNamespace();
            std::get<ServerWorldOwner>(missingServer.owner).owner = {};
            REQUIRE_FALSE(missingServer.IsValid());
        }

        TEST_CASE("Canonical namespace keys use versioned fixed-width typed identity encoding", "[unit][save][namespace]") {
            const auto client = EncodeSaveNamespaceKey(ClientNamespace());
            REQUIRE(client.HasValue());
            REQUIRE(client.Value().Bytes().size() == CanonicalSaveNamespaceKeyBytes);
            REQUIRE(client.Value().Bytes().front() == static_cast<std::byte>(SaveNamespaceEncodingVersion));

            const auto changedProduct = EncodeSaveNamespaceKey(ClientNamespace(9));
            const auto changedEnvironment = EncodeSaveNamespaceKey(ClientNamespace(1, 9));
            const auto changedUser = EncodeSaveNamespaceKey(ClientNamespace(1, 2, 9));
            const auto changedProfile = EncodeSaveNamespaceKey(ClientNamespace(1, 2, 3, 9));
            const auto server = EncodeSaveNamespaceKey(ServerNamespace(3));
            REQUIRE(changedProduct.Value() != client.Value());
            REQUIRE(changedEnvironment.Value() != client.Value());
            REQUIRE(changedUser.Value() != client.Value());
            REQUIRE(changedProfile.Value() != client.Value());
            REQUIRE(server.Value() != client.Value());
        }

        TEST_CASE("Opaque local identities isolate authenticated installation-local and guest partitions", "[unit][save][namespace]") {
            const auto authenticated = EncodeSaveNamespaceKey(ClientNamespace(1, 2, 10, 4));
            const auto installationLocal = EncodeSaveNamespaceKey(ClientNamespace(1, 2, 11, 4));
            const auto guest = EncodeSaveNamespaceKey(ClientNamespace(1, 2, 12, 4));

            REQUIRE(authenticated.Value() != installationLocal.Value());
            REQUIRE(authenticated.Value() != guest.Value());
            REQUIRE(installationLocal.Value() != guest.Value());
        }

        TEST_CASE("Namespace transition classification requires explicit owner rebinding or migration", "[unit][save][namespace]") {
            const auto current = ClientNamespace();
            REQUIRE(ClassifySaveNamespaceTransition(current, current).Value() == SaveNamespaceTransitionKind::Unchanged);
            REQUIRE(ClassifySaveNamespaceTransition(current, ClientNamespace(1, 2, 3, 9)).Value() ==
                    SaveNamespaceTransitionKind::RebindRequired);
            REQUIRE(ClassifySaveNamespaceTransition(current, ClientNamespace(1, 2, 9, 4)).Value() ==
                    SaveNamespaceTransitionKind::RebindRequired);
            REQUIRE(ClassifySaveNamespaceTransition(current, ClientNamespace(1, 9, 3, 4)).Value() ==
                    SaveNamespaceTransitionKind::ExplicitMigrationRequired);
            REQUIRE(ClassifySaveNamespaceTransition(current, ClientNamespace(9, 2, 3, 4)).Value() ==
                    SaveNamespaceTransitionKind::ExplicitMigrationRequired);
            REQUIRE(ClassifySaveNamespaceTransition(current, SaveNamespaceId{}).ErrorValue().code.Value() ==
                    SaveErrors::NamespaceInvalid.code.Value());
        }

        TEST_CASE("Profile switching invalidates captured namespace access", "[unit][save][namespace]") {
            const SaveNamespaceAccessRequest captured{.expected = ClientNamespace(), .expectedRevision = 7};
            SaveNamespaceBindingSnapshot binding{.active = ClientNamespace(), .state = SaveNamespaceBindingState::Available, .revision = 7};
            REQUIRE(ValidateSaveNamespaceAccess(captured, binding).HasValue());

            binding.active = ClientNamespace(1, 2, 3, 9);
            ++binding.revision;
            REQUIRE(ValidateSaveNamespaceAccess(captured, binding).ErrorValue().code.Value() == SaveErrors::NamespaceStale.code.Value());
        }

        TEST_CASE("Unavailable and deleting profiles reject namespace access", "[unit][save][namespace]") {
            const SaveNamespaceAccessRequest captured{.expected = ClientNamespace(), .expectedRevision = 7};
            for (const auto state : {SaveNamespaceBindingState::NoActiveUser, SaveNamespaceBindingState::NoActiveProfile,
                                     SaveNamespaceBindingState::ProfileDeleting}) {
                const SaveNamespaceBindingSnapshot binding{.active = std::nullopt, .state = state, .revision = 8};
                REQUIRE(ValidateSaveNamespaceAccess(captured, binding).ErrorValue().code.Value() ==
                        SaveErrors::NamespaceUnavailable.code.Value());
            }

            const SaveNamespaceBindingSnapshot malformed{.active = ClientNamespace(),
                                                         .state = SaveNamespaceBindingState::Available,
                                                         .revision = 0};
            REQUIRE(ValidateSaveNamespaceAccess(captured, malformed).ErrorValue().code.Value() ==
                    SaveErrors::NamespaceUnavailable.code.Value());
            REQUIRE(
                ValidateSaveNamespaceAccess({.expected = ClientNamespace(), .expectedRevision = 0}, malformed).ErrorValue().code.Value() ==
                SaveErrors::NamespaceInvalid.code.Value());
        }
    }  // namespace
}  // namespace Horo::Runtime
