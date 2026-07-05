/** @file CryptoProvider.h
 *  @brief Abstraction for symmetric encryption used by the asset archive system.
 *
 *  The CryptoProvider interface decouples archive encryption from any specific
 *  cipher implementation.  A default AES-128-CTR provider is included.
 *  Consumers (CLI tools, editor) create a provider, set a key, and pass it to
 *  ArchiveBuilder / ArchiveReader.
 *
 *  Thread safety: Encrypt/Decrypt are re-entrant but NOT thread-safe on the
 *  same provider instance (CTR mode carries mutable counter state).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace Horo::Build {

/** @brief Minimum key size in bytes for any provider. */
inline constexpr uint32_t kMinCryptoKeySize = 16;

/**
 * @brief Abstract interface for symmetric encryption.
 *
 * Each method accepts non-owning byte spans.  The caller must ensure the
 * output buffer is at least as large as the input.
 */
class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;

    /**
     * @brief Sets the encryption key.
     * @param key Key material; must be at least kMinCryptoKeySize bytes.
     * @param keySize Size of key in bytes.
     * @return True if the key was accepted.
     */
    virtual bool SetKey(const uint8_t *key, uint32_t keySize) = 0;

    /**
     * @brief Returns true when a valid key has been set.
     */
    virtual bool HasKey() const = 0;

    /**
     * @brief Generates a Key Check Value for password verification.
     *
     * The KCV is the first 8 bytes of encrypting a 16-byte zero block.
     * Callers compare KCVs to verify correct key/password without
     * decrypting real data.
     *
     * @param kcvOut Buffer of at least 8 bytes.
     * @return True on success.
     */
    virtual bool GenerateKcv(uint8_t *kcvOut) = 0;

    /**
     * @brief Encrypts data in-place using the configured key and a new IV.
     *
     * This implementation auto-generates a random IV, prepends it to the
     * output, and encrypts the remainder with AES-128-CTR.
     *
     * @param data Mutable span; must have at least 16 extra bytes at the end
     *             for the IV. After the call, the first 16 bytes are the IV
     *             and the rest is ciphertext.
     * @param outSize Set to the total encrypted size (input + 16 bytes IV).
     * @return True on success.
     */
    virtual bool Encrypt(std::span<uint8_t> data, uint32_t &outSize) = 0;

    /**
     * @brief Decrypts data in-place using the configured key.
     *
     * Reads the IV from the first 16 bytes of the input, then decrypts the
     * remainder with AES-128-CTR.
     *
     * @param data Mutable span containing [IV(16B) | ciphertext].
     * @param outSize Set to the plaintext size (input size - 16).
     * @return True on success.
     */
    virtual bool Decrypt(std::span<uint8_t> data, uint32_t &outSize) = 0;

    /**
     * @brief Returns a human-readable name for this provider.
     */
    virtual const char *Name() const = 0;
};

/**
 * @brief Creates the default crypto provider (AES-128-CTR).
 *
 * On platforms with hardware AES acceleration, the implementation
 * uses platform-native APIs; otherwise a software fallback is used.
 *
 * @return Non-null owning pointer to a new crypto provider.
 */
std::unique_ptr<ICryptoProvider> CreateDefaultCryptoProvider();

} // namespace Horo::Build
