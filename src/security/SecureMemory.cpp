#include "Horo/Security/SecureMemory.h"

#include <atomic>
#include <utility>

namespace Horo::Security {
    /** @copydoc SecureZero */
    void SecureZero(const std::span<std::byte> bytes) noexcept {
        volatile std::byte *cursor = bytes.data();
        for (std::size_t index = 0; index < bytes.size(); ++index)
            cursor[index] = std::byte{0};
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    /** @copydoc SecureBytes::SecureBytes */
    SecureBytes::SecureBytes(const std::span<const std::byte> bytes) : bytes_(bytes.begin(), bytes.end()) {}

    SecureBytes::~SecureBytes() {
        Clear();
    }

    SecureBytes::SecureBytes(SecureBytes &&other) noexcept : bytes_(std::move(other.bytes_)) {
        other.bytes_.clear();
    }

    SecureBytes &SecureBytes::operator=(SecureBytes &&other) noexcept {
        if (this != &other) {
            Clear();
            bytes_ = std::move(other.bytes_);
            other.bytes_.clear();
        }
        return *this;
    }

    /** @copydoc SecureBytes::View */
    std::span<const std::byte> SecureBytes::View() const noexcept {
        return bytes_;
    }

    /** @copydoc SecureBytes::Empty */
    bool SecureBytes::Empty() const noexcept {
        return bytes_.empty();
    }

    /** @copydoc SecureBytes::Clear */
    void SecureBytes::Clear() noexcept {
        SecureZero(bytes_);
        std::vector<std::byte>{}.swap(bytes_);
    }
}  // namespace Horo::Security
