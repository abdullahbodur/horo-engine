#include "Horo/Platform/SecureRandom.h"
#include "Horo/Security/SecurityErrors.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#include <limits>

namespace Horo::Platform {
    namespace {
        [[nodiscard]] Result<void> FailEntropyRequest(const std::span<std::byte> destination) {
            Security::SecureZero(destination);
            return Result<void>::Failure(MakeError(SecurityErrors::EntropyUnavailable));
        }

        class WindowsSecureRandomSource final : public Security::SecureRandomSource {
        public:
            [[nodiscard]] Result<void> Fill(const std::span<std::byte> destination) override {
                if (destination.size() > std::numeric_limits<ULONG>::max())
                    return FailEntropyRequest(destination);
                const NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(destination.data()),
                                                        static_cast<ULONG>(destination.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
                if (BCRYPT_SUCCESS(status))
                    return Result<void>::Success();
                return FailEntropyRequest(destination);
            }
        };
    }  // namespace

    /** @copydoc CreateNativeSecureRandomSource */
    std::shared_ptr<Security::SecureRandomSource> CreateNativeSecureRandomSource() {
        return std::make_shared<WindowsSecureRandomSource>();
    }
}  // namespace Horo::Platform
