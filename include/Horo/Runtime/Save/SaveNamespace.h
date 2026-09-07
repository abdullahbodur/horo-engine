#pragma once

/**
 * @file SaveNamespace.h
 * @brief Typed product, environment, user, profile, and server save namespaces.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Save/SaveIdentity.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace Horo::Runtime {
    /** @brief Client save owner formed only from opaque local-user and product-profile identities. */
    struct UserProfileOwner {
        LocalUserStorageId user; /**< Authenticated, installation-local, or guest partition identity. */
        GameProfileId profile;   /**< Product-owned profile identity within the local-user partition. */

        /** @brief Reports whether both owner identities are usable. @return True when neither identity is reserved. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const UserProfileOwner &) const noexcept = default;
    };

    /** @brief Dedicated/headless world or tenant save owner. */
    struct ServerWorldOwner {
        ServerStorageOwnerId owner; /**< Stable opaque server world/tenant identity. */

        /** @brief Reports whether the server owner identity is usable. @return True when the identity is non-zero. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const ServerWorldOwner &) const noexcept = default;
    };

    /** @brief Disjoint client-profile or server-world namespace authority. */
    using SaveNamespaceOwner = std::variant<UserProfileOwner, ServerWorldOwner>;

    /** @brief Stable logical namespace identity, independent of paths and display/provider metadata. */
    struct SaveNamespaceId {
        ProductStorageId product;         /**< Stable product configuration identity. */
        EnvironmentStorageId environment; /**< Production, development, test, or unique PIE partition. */
        SaveNamespaceOwner owner;         /**< Client profile or server world authority. */

        /** @brief Validates every typed identity in the namespace. @return True for a complete namespace. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const SaveNamespaceId &) const noexcept = default;
    };

    /** @brief Explicit change category used by profile/session transaction owners. */
    enum class SaveNamespaceTransitionKind : std::uint8_t {
        Unchanged,
        RebindRequired,
        ExplicitMigrationRequired
    };

    /** @brief Lifecycle state of one active namespace binding. */
    enum class SaveNamespaceBindingState : std::uint8_t {
        Available,
        NoActiveUser,
        NoActiveProfile,
        ProfileDeleting
    };

    /** @brief Immutable active-binding projection used for pure access validation. */
    struct SaveNamespaceBindingSnapshot {
        std::optional<SaveNamespaceId> active; /**< Exact active namespace, absent while unavailable. */
        SaveNamespaceBindingState state{SaveNamespaceBindingState::NoActiveProfile}; /**< Current admission state. */
        std::uint64_t revision{};                                                    /**< Non-zero revision changed by every bind/close. */
    };

    /** @brief Captured namespace and revision expected by one operation. */
    struct SaveNamespaceAccessRequest {
        SaveNamespaceId expected;         /**< Namespace captured when the operation was admitted. */
        std::uint64_t expectedRevision{}; /**< Binding revision captured with the namespace. */
    };

    /** @brief Current version of the fixed-width canonical namespace-key encoding. */
    inline constexpr std::uint8_t SaveNamespaceEncodingVersion = 1;
    /** @brief Byte count of the fixed-width canonical namespace key. */
    inline constexpr std::size_t CanonicalSaveNamespaceKeyBytes = 66;

    /** @brief Backend-neutral fixed-width namespace key for private storage-adapter mapping. */
    class CanonicalSaveNamespaceKey final {
    public:
        /** @brief Returns the versioned fixed-width bytes. @return Borrowed immutable canonical bytes. */
        [[nodiscard]] const std::array<std::byte, CanonicalSaveNamespaceKeyBytes> &Bytes() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const CanonicalSaveNamespaceKey &) const noexcept = default;

    private:
        friend Result<CanonicalSaveNamespaceKey> EncodeSaveNamespaceKey(const SaveNamespaceId &);
        /** @brief Constructs a key after typed namespace validation. */
        explicit CanonicalSaveNamespaceKey(std::array<std::byte, CanonicalSaveNamespaceKeyBytes> bytes) noexcept;

        std::array<std::byte, CanonicalSaveNamespaceKeyBytes> bytes_{};
    };

    /**
     * @brief Classifies a namespace change without mutating bindings or storage.
     * @param current Existing valid namespace.
     * @param requested Requested valid namespace.
     * @return Unchanged, transactional rebind for an owner change, or explicit migration for product/environment changes;
     * SaveErrors::NamespaceInvalid when either namespace is malformed.
     */
    [[nodiscard]] Result<SaveNamespaceTransitionKind> ClassifySaveNamespaceTransition(const SaveNamespaceId &current,
                                                                                      const SaveNamespaceId &requested);

    /**
     * @brief Validates captured access against an immutable active-binding snapshot.
     * @param request Namespace and revision captured at operation admission.
     * @param binding Current binding state published by the profile/session owner.
     * @return Success only for the exact available namespace and revision; otherwise a stable invalid, unavailable, or stale error.
     */
    [[nodiscard]] Result<void> ValidateSaveNamespaceAccess(const SaveNamespaceAccessRequest &request,
                                                           const SaveNamespaceBindingSnapshot &binding);

    /**
     * @brief Encodes a namespace into a collision-resistant versioned fixed-width typed key.
     * @param namespaceId Complete typed namespace identity.
     * @return Canonical binary key or SaveErrors::NamespaceInvalid. No path, label, provider handle, or account text is accepted.
     */
    [[nodiscard]] Result<CanonicalSaveNamespaceKey> EncodeSaveNamespaceKey(const SaveNamespaceId &namespaceId);
}  // namespace Horo::Runtime
