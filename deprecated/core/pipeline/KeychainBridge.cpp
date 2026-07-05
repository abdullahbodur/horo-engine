/** @file KeychainBridge.cpp
 *  @brief Non-macOS keychain provider stub — always returns false.
 *
 *  The macOS implementation lives in KeychainBridge_mac.mm and is guarded
 *  behind __APPLE__.  Both files are compiled on all platforms; the
 *  preprocessor selects the active factory at compile time.
 */

#include "core/pipeline/KeychainBridge.h"

#if !defined(__APPLE__)

#include <memory>

namespace Horo::Pipeline {
namespace {

/** @brief Stub keychain provider for non-macOS platforms.
 *
 *  GetPassword() always returns false.  This is the default when no
 *  platform keychain API is available. */
class StubKeychainProvider final : public IKeychainProvider {
public:
    /** @copydoc IKeychainProvider::GetPassword */
    bool GetPassword(const std::string & /*service*/,
                     const std::string & /*account*/,
                     std::string & /*password*/) override {
        return false;
    }
};

} // namespace

/** @copydoc CreateKeychainProvider */
std::unique_ptr<IKeychainProvider> CreateKeychainProvider() {
    return std::make_unique<StubKeychainProvider>();
}

} // namespace Horo::Pipeline

#endif // !defined(__APPLE__)
