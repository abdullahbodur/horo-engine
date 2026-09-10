#include "Horo/Platform/SecureRandom.h"
#include "Horo/Security/SecurityErrors.h"

#include <cerrno>

#if defined(__APPLE__)
#include <Security/SecRandom.h>
#else
#include <sys/random.h>
#endif

namespace Horo::Platform {
    namespace {
        class NativeSecureRandomSource final : public Security::SecureRandomSource {
        public:
            [[nodiscard]] Result<void> Fill(const std::span<std::byte> destination) override {
#if defined(__APPLE__)
                if (SecRandomCopyBytes(kSecRandomDefault, destination.size(), reinterpret_cast<std::uint8_t *>(destination.data())) ==
                    errSecSuccess)
                    return Result<void>::Success();
#else
                std::size_t offset = 0;
                while (offset < destination.size()) {
                    const ssize_t read = getrandom(destination.data() + offset, destination.size() - offset, 0);
                    if (read > 0) {
                        offset += static_cast<std::size_t>(read);
                        continue;
                    }
                    if (read < 0 && errno == EINTR)
                        continue;
                    break;
                }
                if (offset == destination.size())
                    return Result<void>::Success();
#endif
                Security::SecureZero(destination);
                return Result<void>::Failure(MakeError(SecurityErrors::EntropyUnavailable));
            }
        };
    }  // namespace

    /** @copydoc CreateNativeSecureRandomSource */
    std::shared_ptr<Security::SecureRandomSource> CreateNativeSecureRandomSource() {
        return std::make_shared<NativeSecureRandomSource>();
    }
}  // namespace Horo::Platform
