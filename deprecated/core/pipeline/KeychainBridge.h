/** @file KeychainBridge.h
 *  @brief Platform-guarded keychain credential retrieval for signing pipelines.
 *
 *  Provides an abstract interface for reading generic passwords from the
 *  system keychain.  On macOS this delegates to Security.framework; on other
 *  platforms the default implementation always returns false.  The interface
 *  is injectable, so tests can supply a mock backend.
 */

#pragma once

#include <memory>
#include <string>

namespace Horo::Pipeline {

/** @brief Abstract provider for retrieving secrets from the system keychain.
 *
 *  Concrete implementations are platform-specific.  The default
 *  implementation (non-macOS) always returns false.  Tests supply a
 *  MockKeychainProvider that maps (service, account) pairs to passwords
 *  stored in memory.
 *
 *  Security contract: implementations MUST NOT write retrieved secrets to
 *  any log output.  Callers are responsible for clearing password buffers
 *  after use. */
class IKeychainProvider {
public:
    virtual ~IKeychainProvider() = default;

    /** @brief Retrieves a generic password from the keychain.
     *  @param service  Service name (e.g. "com.horo.signing").
     *  @param account  Account identifier (e.g. "codesign-cert").
     *  @param password Output parameter; unchanged if the lookup fails.
     *  @return true when a password was found and written to @p password. */
    virtual bool GetPassword(const std::string &service,
                             const std::string &account,
                             std::string &password) = 0;
};

/** @brief Creates the platform-appropriate keychain provider.
 *
 *  On macOS this returns a provider backed by Security.framework
 *  (SecItemCopyMatching).  On all other platforms it returns a provider
 *  whose GetPassword() always returns false.
 *
 *  @return Non-null unique_ptr to a heap-allocated IKeychainProvider. */
std::unique_ptr<IKeychainProvider> CreateKeychainProvider();

} // namespace Horo::Pipeline
