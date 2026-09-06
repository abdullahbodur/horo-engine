#include "Horo/Navigation/NavigationIdentity.h"

namespace Horo::Navigation {
    /** @copydoc DeserializeSurfaceId */
    Result<SurfaceId> DeserializeSurfaceId(const SerializedSurfaceId &bytes) {
        std::uint64_t value{};
        for (const std::uint8_t byte : bytes)
            value = (value << 8U) | byte;
        return SurfaceId::Create(value);
    }

    /** @copydoc SerializeSurfaceId */
    SerializedSurfaceId SerializeSurfaceId(const SurfaceId surface) noexcept {
        SerializedSurfaceId bytes{};
        std::uint64_t remaining = surface.Value();
        for (auto iterator = bytes.rbegin(); iterator != bytes.rend(); ++iterator) {
            *iterator = static_cast<std::uint8_t>(remaining & 0xffU);
            remaining >>= 8U;
        }
        return bytes;
    }
}  // namespace Horo::Navigation
