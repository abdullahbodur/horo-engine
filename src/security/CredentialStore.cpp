#include "Horo/Security/CredentialStore.h"

#include "Horo/Security/SecurityErrors.h"

#include <array>
#include <utility>

namespace Horo::Security {
    namespace {
        constexpr std::size_t ReferenceBytes = 24;
        constexpr std::size_t MaximumReferenceAttempts = 4;
        constexpr char Hex[] = "0123456789abcdef";
    }  // namespace

    CredentialReference::CredentialReference(std::string value) : value_(std::move(value)) {}

    /** @copydoc CredentialReference::Value */
    std::string_view CredentialReference::Value() const noexcept {
        return value_;
    }

    /** @copydoc CredentialVault::CredentialVault */
    CredentialVault::CredentialVault(std::shared_ptr<CredentialBackend> backend, std::shared_ptr<SecureRandomSource> random)
        : backend_(std::move(backend)), random_(std::move(random)) {}

    CredentialVault::~CredentialVault() {
        Clear();
    }

    /** @copydoc CredentialVault::SetBackend */
    void CredentialVault::SetBackend(std::shared_ptr<CredentialBackend> backend) noexcept {
        backend_ = std::move(backend);
    }

    Result<CredentialReference> CredentialVault::NewReference() {
        if (!random_)
            return Result<CredentialReference>::Failure(MakeError(SecurityErrors::EntropyUnavailable));
        for (std::size_t attempt = 0; attempt < MaximumReferenceAttempts; ++attempt) {
            std::array<std::byte, ReferenceBytes> randomBytes{};
            if (auto filled = random_->Fill(randomBytes); filled.HasError())
                return Result<CredentialReference>::Failure(filled.ErrorValue());
            std::string value{"cred:v1:"};
            value.reserve(value.size() + randomBytes.size() * 2U);
            for (const std::byte byte : randomBytes) {
                const auto octet = std::to_integer<unsigned int>(byte);
                value.push_back(Hex[octet >> 4U]);
                value.push_back(Hex[octet & 0x0fU]);
            }
            if (!records_.contains(value))
                return Result<CredentialReference>::Success(CredentialReference{std::move(value)});
        }
        return Result<CredentialReference>::Failure(
            MakeError(SecurityErrors::EntropyUnavailable, "Secure entropy produced repeated credential references."));
    }

    /** @copydoc CredentialVault::Put */
    Result<CredentialReference> CredentialVault::Put(SecureBytes secret, const std::uint64_t expiresAtMillis) {
        if (secret.Empty())
            return Result<CredentialReference>::Failure(MakeError(SecurityErrors::InvalidInput, "Credential material must not be empty."));
        if (!backend_ || !backend_->Available())
            return Result<CredentialReference>::Failure(MakeError(SecurityErrors::CredentialBackendUnavailable));
        auto reference = NewReference();
        if (reference.HasError())
            return reference;
        if (auto stored = backend_->Put(reference.Value(), std::move(secret)); stored.HasError())
            return Result<CredentialReference>::Failure(stored.ErrorValue());
        records_.emplace(std::string{reference.Value().Value()}, Record{.expiresAtMillis = expiresAtMillis});
        return reference;
    }

    /** @copydoc CredentialVault::Resolve */
    Result<SecureBytes> CredentialVault::Resolve(const CredentialReference &reference, const std::uint64_t nowMillis) {
        const auto record = records_.find(std::string{reference.Value()});
        if (record == records_.end())
            return Result<SecureBytes>::Failure(MakeError(SecurityErrors::CredentialNotFound));
        if (record->second.revoked)
            return Result<SecureBytes>::Failure(MakeError(SecurityErrors::CredentialRevoked));
        if (record->second.expiresAtMillis != 0 && nowMillis >= record->second.expiresAtMillis) {
            static_cast<void>(backend_ ? backend_->Remove(reference) : Result<void>::Success());
            record->second.revoked = true;
            return Result<SecureBytes>::Failure(MakeError(SecurityErrors::CredentialExpired));
        }
        if (!backend_ || !backend_->Available())
            return Result<SecureBytes>::Failure(MakeError(SecurityErrors::CredentialBackendUnavailable));
        return backend_->Resolve(reference);
    }

    /** @copydoc CredentialVault::Revoke */
    Result<void> CredentialVault::Revoke(const CredentialReference &reference) {
        const auto record = records_.find(std::string{reference.Value()});
        if (record == records_.end())
            return Result<void>::Failure(MakeError(SecurityErrors::CredentialNotFound));
        if (!backend_ || !backend_->Available())
            return Result<void>::Failure(MakeError(SecurityErrors::CredentialBackendUnavailable));
        if (auto removed = backend_->Remove(reference); removed.HasError())
            return removed;
        record->second.revoked = true;
        return Result<void>::Success();
    }

    /** @copydoc CredentialVault::Rotate */
    Result<void> CredentialVault::Rotate(const CredentialReference &reference, SecureBytes replacement,
                                         const std::uint64_t expiresAtMillis) {
        const auto record = records_.find(std::string{reference.Value()});
        if (record == records_.end())
            return Result<void>::Failure(MakeError(SecurityErrors::CredentialNotFound));
        if (record->second.revoked)
            return Result<void>::Failure(MakeError(SecurityErrors::CredentialRevoked));
        if (replacement.Empty())
            return Result<void>::Failure(MakeError(SecurityErrors::InvalidInput, "Credential material must not be empty."));
        if (!backend_ || !backend_->Available())
            return Result<void>::Failure(MakeError(SecurityErrors::CredentialBackendUnavailable));
        if (auto stored = backend_->Put(reference, std::move(replacement)); stored.HasError())
            return stored;
        record->second.expiresAtMillis = expiresAtMillis;
        return Result<void>::Success();
    }

    /** @copydoc CredentialVault::Clear */
    void CredentialVault::Clear() noexcept {
        if (backend_ && backend_->Available()) {
            for (const auto &[value, record] : records_) {
                static_cast<void>(record);
                try {
                    static_cast<void>(backend_->Remove(CredentialReference{value}));
                } catch (...) {
                    // Destruction remains noexcept; the provider retains responsibility for material it could not remove.
                }
            }
        }
        records_.clear();
    }
}  // namespace Horo::Security
