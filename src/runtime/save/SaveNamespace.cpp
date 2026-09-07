#include "Horo/Runtime/Save/SaveNamespace.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace Horo::Runtime {
    namespace {
        constexpr std::size_t kVersionOffset = 0;
        constexpr std::size_t kProductOffset = 1;
        constexpr std::size_t kEnvironmentOffset = kProductOffset + SaveIdentityDetail::Bytes{}.size();
        constexpr std::size_t kOwnerKindOffset = kEnvironmentOffset + SaveIdentityDetail::Bytes{}.size();
        constexpr std::size_t kOwnerPrimaryOffset = kOwnerKindOffset + 1;
        constexpr std::size_t kOwnerSecondaryOffset = kOwnerPrimaryOffset + SaveIdentityDetail::Bytes{}.size();
        constexpr std::byte kUserProfileKind{1};
        constexpr std::byte kServerWorldKind{2};
        static_assert(CanonicalSaveNamespaceKeyBytes == kOwnerSecondaryOffset + SaveIdentityDetail::Bytes{}.size());

        template <typename> inline constexpr bool kUnsupportedNamespaceOwner = false;

        void CopyIdentity(const SaveIdentityDetail::Bytes &identity, std::array<std::byte, CanonicalSaveNamespaceKeyBytes> &destination,
                          const std::size_t offset) {
            std::ranges::transform(identity, destination.begin() + static_cast<std::ptrdiff_t>(offset), [](const std::uint8_t value) {
                return static_cast<std::byte>(value);
            });
        }
    }  // namespace

    /** @copydoc UserProfileOwner::IsValid */
    bool UserProfileOwner::IsValid() const noexcept {
        return user.IsValid() && profile.IsValid();
    }

    /** @copydoc ServerWorldOwner::IsValid */
    bool ServerWorldOwner::IsValid() const noexcept {
        return owner.IsValid();
    }

    /** @copydoc SaveNamespaceId::IsValid */
    bool SaveNamespaceId::IsValid() const noexcept {
        return product.IsValid() && environment.IsValid() && std::visit([](const auto &value) {
            return value.IsValid();
        }, owner);
    }

    /** @copydoc CanonicalSaveNamespaceKey::CanonicalSaveNamespaceKey */
    CanonicalSaveNamespaceKey::CanonicalSaveNamespaceKey(std::array<std::byte, CanonicalSaveNamespaceKeyBytes> bytes) noexcept
        : bytes_(bytes) {}

    /** @copydoc CanonicalSaveNamespaceKey::Bytes */
    const std::array<std::byte, CanonicalSaveNamespaceKeyBytes> &CanonicalSaveNamespaceKey::Bytes() const noexcept {
        return bytes_;
    }

    /** @copydoc ClassifySaveNamespaceTransition */
    Result<SaveNamespaceTransitionKind> ClassifySaveNamespaceTransition(const SaveNamespaceId &current, const SaveNamespaceId &requested) {
        if (!current.IsValid() || !requested.IsValid())
            return Result<SaveNamespaceTransitionKind>::Failure(MakeError(SaveErrors::NamespaceInvalid));
        if (current == requested)
            return Result<SaveNamespaceTransitionKind>::Success(SaveNamespaceTransitionKind::Unchanged);
        if (current.product != requested.product || current.environment != requested.environment)
            return Result<SaveNamespaceTransitionKind>::Success(SaveNamespaceTransitionKind::ExplicitMigrationRequired);
        return Result<SaveNamespaceTransitionKind>::Success(SaveNamespaceTransitionKind::RebindRequired);
    }

    /** @copydoc ValidateSaveNamespaceAccess */
    Result<void> ValidateSaveNamespaceAccess(const SaveNamespaceAccessRequest &request, const SaveNamespaceBindingSnapshot &binding) {
        if (!request.expected.IsValid() || request.expectedRevision == 0)
            return Result<void>::Failure(MakeError(SaveErrors::NamespaceInvalid));
        if (binding.state != SaveNamespaceBindingState::Available || !binding.active || !binding.active->IsValid() || binding.revision == 0)
            return Result<void>::Failure(MakeError(SaveErrors::NamespaceUnavailable));
        if (request.expectedRevision != binding.revision || request.expected != *binding.active)
            return Result<void>::Failure(MakeError(SaveErrors::NamespaceStale));
        return Result<void>::Success();
    }

    /** @copydoc EncodeSaveNamespaceKey */
    Result<CanonicalSaveNamespaceKey> EncodeSaveNamespaceKey(const SaveNamespaceId &namespaceId) {
        if (!namespaceId.IsValid())
            return Result<CanonicalSaveNamespaceKey>::Failure(MakeError(SaveErrors::NamespaceInvalid));

        std::array<std::byte, CanonicalSaveNamespaceKeyBytes> bytes{};
        bytes[kVersionOffset] = static_cast<std::byte>(SaveNamespaceEncodingVersion);
        CopyIdentity(namespaceId.product.Bytes(), bytes, kProductOffset);
        CopyIdentity(namespaceId.environment.Bytes(), bytes, kEnvironmentOffset);
        std::visit([&bytes](const auto &owner) {
            using Owner = std::remove_cvref_t<decltype(owner)>;
            if constexpr (std::same_as<Owner, UserProfileOwner>) {
                bytes[kOwnerKindOffset] = kUserProfileKind;
                CopyIdentity(owner.user.Bytes(), bytes, kOwnerPrimaryOffset);
                CopyIdentity(owner.profile.Bytes(), bytes, kOwnerSecondaryOffset);
            } else if constexpr (std::same_as<Owner, ServerWorldOwner>) {
                bytes[kOwnerKindOffset] = kServerWorldKind;
                CopyIdentity(owner.owner.Bytes(), bytes, kOwnerPrimaryOffset);
            } else {
                static_assert(kUnsupportedNamespaceOwner<Owner>, "SaveNamespaceOwner visitor is not exhaustive");
            }
        }, namespaceId.owner);
        return Result<CanonicalSaveNamespaceKey>::Success(CanonicalSaveNamespaceKey{bytes});
    }
}  // namespace Horo::Runtime
