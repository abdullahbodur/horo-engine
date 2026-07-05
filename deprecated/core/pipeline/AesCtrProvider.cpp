/** @file AesCtrProvider.cpp
 *  @brief AES-128-CTR encryption/decryption using Horo::Crypto::AESContext.
 *
 *  Delegates all block cipher operations to AESContext, which uses
 *  hardware acceleration on macOS (CommonCrypto) and Windows (BCrypt),
 *  with a software fallback on other platforms.
 */
#include "core/pipeline/AesCtrProvider.h"
#include "core/crypto/AESContext.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>

namespace Horo::Build {

using Crypto::kAes128KeySize;
using Crypto::kAesBlockSize;

// ── AesCtrProvider ───────────────────────────────────────────────────────

/** @copydoc ICryptoProvider::~ICryptoProvider */
AesCtrProvider::~AesCtrProvider() {
    // Clear key material from memory
    std::memset(m_key, 0, sizeof(m_key));
    m_hasKey = false;
}

void AesCtrProvider::GenerateRandomBytes(uint8_t *buf, uint32_t count) const {
    // std::random_device provides CSPRNG-quality entropy backed by OS
    // sources (RDRAND, /dev/urandom, etc.) on all supported platforms.
    std::random_device rd;
    for (uint32_t i = 0; i < count; ++i)
        buf[i] = static_cast<uint8_t>(rd() & 0xFFu);
}

/** @copydoc ICryptoProvider::SetKey */
bool AesCtrProvider::SetKey(const uint8_t *key, uint32_t keySize) {
    if (keySize < kAes128KeySize)
        return false;
    std::memcpy(m_key, key, kAes128KeySize);
    m_aesCtx.Init(m_key);
    m_hasKey = true;
    return true;
}

/** @copydoc ICryptoProvider::HasKey */
bool AesCtrProvider::HasKey() const {
    return m_hasKey;
}

/** @copydoc ICryptoProvider::GenerateKcv */
bool AesCtrProvider::GenerateKcv(uint8_t *kcvOut) {
    if (!m_hasKey)
        return false;
    m_aesCtx.GenerateKcv(kcvOut);
    return true;
}

/** @copydoc ICryptoProvider::Encrypt */
bool AesCtrProvider::Encrypt(std::span<uint8_t> data, uint32_t &outSize) {
    if (!m_hasKey || data.size() < kAesBlockSize)
        return false;

    // Generate random nonce (first 8 bytes) + zero counter (last 8 bytes)
    uint8_t iv[kAesBlockSize]{};
    GenerateRandomBytes(iv, 8);
    // iv[8..15] stays zero as initial counter value

    std::memcpy(data.data(), iv, kAesBlockSize);

    uint8_t counterBlock[kAesBlockSize];
    std::memcpy(counterBlock, iv, kAesBlockSize);
    const uint32_t plaintextSize =
        static_cast<uint32_t>(data.size()) - kAesBlockSize;

    uint8_t *ciphertext = data.data() + kAesBlockSize;
    for (uint32_t i = 0; i < plaintextSize; i += kAesBlockSize) {
        const uint32_t chunkSize =
            std::min<uint32_t>(plaintextSize - i, kAesBlockSize);
        Crypto::AesCtrProcess(m_aesCtx, counterBlock,
                              ciphertext + i, chunkSize);
    }

    outSize = kAesBlockSize + plaintextSize;
    return true;
}

/** @copydoc ICryptoProvider::Decrypt */
bool AesCtrProvider::Decrypt(std::span<uint8_t> data, uint32_t &outSize) {
    if (!m_hasKey || data.size() < kAesBlockSize)
        return false;

    uint8_t counterBlock[kAesBlockSize];
    std::memcpy(counterBlock, data.data(), kAesBlockSize); // IV
    const uint32_t ciphertextSize =
        static_cast<uint32_t>(data.size()) - kAesBlockSize;

    uint8_t *plaintext = data.data() + kAesBlockSize;
    for (uint32_t i = 0; i < ciphertextSize; i += kAesBlockSize) {
        const uint32_t chunkSize =
            std::min<uint32_t>(ciphertextSize - i, kAesBlockSize);
        Crypto::AesCtrProcess(m_aesCtx, counterBlock,
                              plaintext + i, chunkSize);
    }

    outSize = ciphertextSize;
    return true;
}

/** @copydoc ICryptoProvider::Name */
const char *AesCtrProvider::Name() const {
#if defined(__APPLE__)
    return "AES-128-CTR (CommonCrypto)";
#elif defined(_WIN32)
    return "AES-128-CTR (BCrypt CNG)";
#else
    return "AES-128-CTR (software)";
#endif
}

// ── Factory ──────────────────────────────────────────────────────────────

/** @copydoc CreateDefaultCryptoProvider */
std::unique_ptr<ICryptoProvider> CreateDefaultCryptoProvider() {
    return std::make_unique<AesCtrProvider>();
}

} // namespace Horo::Build
