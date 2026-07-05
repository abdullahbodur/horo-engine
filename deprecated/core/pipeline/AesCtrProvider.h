/** @file AesCtrProvider.h
 *  @brief AES-128-CTR crypto provider for archive encryption.
 *
 *  Implements ICryptoProvider using the platform-optimized Horo::Crypto::AESContext.
 *  On macOS this uses CommonCrypto (hardware AES-NI / ARM CE); on Windows it
 *  uses BCrypt CNG; on other platforms a software fallback is used.
 *
 *  Reference: FIPS PUB 197, NIST SP 800-38A.
 */
#pragma once

#include "core/crypto/AESContext.h"
#include "core/pipeline/CryptoProvider.h"

namespace Horo::Build {

/**
 * @brief AES-128-CTR crypto provider.
 *
 * Implements ICryptoProvider using CTR mode.  The IV (nonce + counter)
 * is 16 bytes: 8 bytes random nonce + 8 bytes counter (big-endian).
 *
 * Backed by Horo::Crypto::AESContext for hardware-accelerated block
 * encryption on macOS (CommonCrypto) and Windows (BCrypt).
 */
class AesCtrProvider final : public ICryptoProvider {
public:
    /** @copydoc ICryptoProvider::~ICryptoProvider */
    ~AesCtrProvider() override;

    /** @copydoc ICryptoProvider::SetKey */
    bool SetKey(const uint8_t *key, uint32_t keySize) override;
    /** @copydoc ICryptoProvider::HasKey */
    bool HasKey() const override;
    /** @copydoc ICryptoProvider::GenerateKcv */
    bool GenerateKcv(uint8_t *kcvOut) override;
    /** @copydoc ICryptoProvider::Encrypt */
    bool Encrypt(std::span<uint8_t> data, uint32_t &outSize) override;
    /** @copydoc ICryptoProvider::Decrypt */
    bool Decrypt(std::span<uint8_t> data, uint32_t &outSize) override;
    /** @copydoc ICryptoProvider::Name */
    const char *Name() const override;

private:
    bool m_hasKey = false;
    uint8_t m_key[Crypto::kAes128KeySize]{};
    Crypto::AESContext m_aesCtx;

    /** @brief Writes cryptographically secure random bytes to the given buffer. */
    void GenerateRandomBytes(uint8_t *buf, uint32_t count) const;
};

} // namespace Horo::Build
