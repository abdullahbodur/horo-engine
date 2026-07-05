/** @file KeychainBridge_mac.mm
 *  @brief macOS keychain provider backed by Security.framework.
 *
 *  Uses the modern SecItemCopyMatching API (macOS 10.0+, iOS 2.0+) to
 *  retrieve generic passwords from the default keychain.  The legacy
 *  SecKeychain API is intentionally avoided.
 *
 *  Security invariants:
 *  - Retrieved passwords are never written to any log channel.
 *  - Intermediate CF/NS objects are explicitly cleared / released.
 *  - Lookup failures produce LogDebug diagnostics that reference
 *    only the service name and account, never the secret.
 */

#include "core/pipeline/KeychainBridge.h"

#if defined(__APPLE__)

#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <memory>

#include "core/Logger.h"

namespace Horo::Pipeline {
namespace {

/** @brief macOS keychain provider using SecItemCopyMatching.
 *
 *  Queries the default keychain for a generic password matching the
 *  given (service, account) pair.  The retrieved CFData is converted
 *  to a std::string and the sensitive bytes are cleared before the
 *  function returns. */
class MacKeychainProvider final : public IKeychainProvider {
public:
    /** @copydoc IKeychainProvider::GetPassword */
    bool GetPassword(const std::string &service,
                     const std::string &account,
                     std::string &password) override {
        if (service.empty() || account.empty()) {
            LogDebug("KeychainBridge: empty service or account — skipping lookup");
            return false;
        }

        @autoreleasepool {
            NSString *nsService =
                [NSString stringWithUTF8String:service.c_str()];
            NSString *nsAccount =
                [NSString stringWithUTF8String:account.c_str()];

            if (!nsService || !nsAccount) {
                LogDebug(
                    "KeychainBridge: failed to convert service/account to NSString");
                return false;
            }

            // Build the query dictionary.
            // kSecReturnData: return the raw password bytes.
            // kSecMatchLimitOne: only return one match.
            // kSecClass: generic password item.
            // kSecAttrService / kSecAttrAccount: the search keys.
            CFMutableDictionaryRef query = CFDictionaryCreateMutable(
                kCFAllocatorDefault, 5,
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);

            if (!query) {
                LogDebug(
                    "KeychainBridge: failed to create query dictionary");
                return false;
            }

            CFDictionarySetValue(
                query, kSecClass, kSecClassGenericPassword);
            CFDictionarySetValue(
                query, kSecAttrService, (__bridge CFStringRef)nsService);
            CFDictionarySetValue(
                query, kSecAttrAccount, (__bridge CFStringRef)nsAccount);
            CFDictionarySetValue(
                query, kSecReturnData, kCFBooleanTrue);
            CFDictionarySetValue(
                query, kSecMatchLimit, kSecMatchLimitOne);

            // Perform the lookup.
            CFTypeRef result = nullptr;
            OSStatus status = SecItemCopyMatching(query, &result);
            CFRelease(query);

            if (status != errSecSuccess || !result) {
                LogDebug(
                    "KeychainBridge: no password found for service='{}' account='{}' (status={})",
                    service, account, static_cast<int>(status));
                return false;
            }

            // result is a CFDataRef containing the raw password bytes.
            if (CFGetTypeID(result) != CFDataGetTypeID()) {
                LogDebug(
                    "KeychainBridge: unexpected result type from keychain");
                CFRelease(result);
                return false;
            }

            CFDataRef data = (CFDataRef)result;
            const CFIndex length = CFDataGetLength(data);
            const UInt8 *bytes = CFDataGetBytePtr(data);

            if (length > 0 && bytes) {
                password.assign(reinterpret_cast<const char *>(bytes),
                                static_cast<std::size_t>(length));
            } else {
                LogDebug(
                    "KeychainBridge: retrieved empty password for service='{}' account='{}'",
                    service, account);
            }

            // Wipe the CFData before releasing to minimise window where
            // sensitive bytes are recoverable from freed memory.
            if (length > 0) {
                CFMutableDataRef mutableCopy = CFDataCreateMutable(
                    kCFAllocatorDefault, 0);
                if (mutableCopy) {
                    CFDataAppendBytes(mutableCopy, bytes, length);
                    CFDataDeleteBytes(
                        mutableCopy,
                        CFRangeMake(0, CFDataGetLength(mutableCopy)));
                    CFRelease(mutableCopy);
                }
            }

            CFRelease(result);
        } // @autoreleasepool

        return !password.empty();
    }
};

} // namespace

/** @copydoc CreateKeychainProvider */
std::unique_ptr<IKeychainProvider> CreateKeychainProvider() {
    return std::make_unique<MacKeychainProvider>();
}

} // namespace Horo::Pipeline

#endif // defined(__APPLE__)
