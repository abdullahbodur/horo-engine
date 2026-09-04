#pragma once

#include "Horo/Audio/AudioMemory.h"

#include <new>

namespace Horo::Audio::Detail {
    /** @brief Paired aligned deallocation; only the quiescent control owner destroys backing storage. */
    struct AlignedAudioDelete {
        void operator()(std::byte *bytes) const noexcept {
            ::operator delete[](bytes, std::align_val_t{AudioMemoryAlignment});
        }
    };

    using AlignedAudioStorage = std::unique_ptr<std::byte, AlignedAudioDelete>;

    /** @brief Allocate a previously validated, bounded byte reservation off-callback. */
    inline AlignedAudioStorage AllocateAudioStorage(const std::size_t bytes) {
        return AlignedAudioStorage{static_cast<std::byte *>(::operator new[](bytes, std::align_val_t{AudioMemoryAlignment}))};
    }

    /** @brief Round a size already bounded below MaximumAudioMemoryBytes to the common alignment. */
    inline std::size_t AudioMemoryStride(const std::size_t bytes) noexcept {
        return (bytes + AudioMemoryAlignment - 1) & ~(AudioMemoryAlignment - 1);
    }
}  // namespace Horo::Audio::Detail
