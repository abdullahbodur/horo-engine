#pragma once

/**
 * @file SecureMemory.h
 * @brief Move-only secret byte ownership and secure entropy contracts.
 */

#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Horo::Security {
    /** @brief Overwrites a writable byte range through a non-optimizable platform primitive. @param bytes Bytes to clear. */
    void SecureZero(std::span<std::byte> bytes) noexcept;

    /** @brief Move-only owner that clears its storage before release. */
    class SecureBytes final {
    public:
        SecureBytes() = default;

        /**
         * @brief Copies caller-owned secret bytes into secure lifetime-managed storage.
         * @param bytes Secret bytes to own.
         */
        explicit SecureBytes(std::span<const std::byte> bytes);
        ~SecureBytes();
        SecureBytes(const SecureBytes &) = delete;
        SecureBytes &operator=(const SecureBytes &) = delete;
        SecureBytes(SecureBytes &&other) noexcept;
        SecureBytes &operator=(SecureBytes &&other) noexcept;

        /** @brief Returns a borrowed read-only view valid only for this object's lifetime. @return Borrowed secret bytes. */
        [[nodiscard]] std::span<const std::byte> View() const noexcept;

        /** @brief Reports whether this owner contains no secret bytes. @return True when empty. */
        [[nodiscard]] bool Empty() const noexcept;

        /** @brief Clears the owned bytes immediately while retaining no readable value. */
        void Clear() noexcept;

    private:
        std::vector<std::byte> bytes_;
    };

    /** @brief Injected cryptographically secure random byte provider. */
    class SecureRandomSource {
    public:
        virtual ~SecureRandomSource() = default;

        /**
         * @brief Fills the complete destination with cryptographically secure OS entropy.
         * @param destination Writable bytes that must all be initialized on success.
         * @return Success, or a typed failure after clearing the complete destination.
         */
        [[nodiscard]] virtual Result<void> Fill(std::span<std::byte> destination) = 0;
    };
}  // namespace Horo::Security
