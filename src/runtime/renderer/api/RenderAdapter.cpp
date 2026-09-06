#include "Horo/Runtime/Render/RenderAdapter.h"

#include "Horo/Runtime/Render/RenderAdapterErrors.h"

#include <algorithm>
#include <string_view>

namespace Horo::Render {
    namespace {
        constexpr std::size_t MaxIdentityLength = 128;
        constexpr std::size_t MaxDisplayNameLength = 256;
        constexpr std::size_t MaxDiagnosticLength = 1024;

        /** @brief Reports whether a byte belongs to the stable adapter identity alphabet. */
        [[nodiscard]] bool IsIdentityCharacter(const unsigned char value) noexcept {
            const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
            const bool digit = value >= '0' && value <= '9';
            return alpha || digit || value == '.' || value == '_' || value == ':' || value == '-';
        }

        /** @brief Reports whether a device kind belongs to the public contract. */
        [[nodiscard]] constexpr bool IsKnown(const RenderAdapterKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderAdapterKind::Unknown);
        }

        /** @brief Reports whether an availability value belongs to the public contract. */
        [[nodiscard]] constexpr bool IsKnown(const RenderAdapterAvailability availability) noexcept {
            return static_cast<std::uint8_t>(availability) <= static_cast<std::uint8_t>(RenderAdapterAvailability::Unavailable);
        }

        /** @brief Reports whether a creation failure kind belongs to the public contract. */
        [[nodiscard]] constexpr bool IsKnown(const RenderDeviceCreationFailureKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderDeviceCreationFailureKind::Unknown);
        }

        /** @brief Reports whether one adapter satisfies all exact selection constraints. */
        [[nodiscard]] bool Matches(const RenderAdapterProperties &adapter, const RenderAdapterSelectionRequest &request) noexcept {
            if (adapter.availability != RenderAdapterAvailability::Available) {
                return false;
            }
            if (request.requiredKind && adapter.kind != *request.requiredKind) {
                return false;
            }
            if (request.requirePresentation && !adapter.supportsPresentation) {
                return false;
            }
            return request.allowSoftware || adapter.kind != RenderAdapterKind::Software;
        }
    }  // namespace

    /** @copydoc RenderAdapterId::IsValid */
    bool RenderAdapterId::IsValid() const noexcept {
        return !value_.empty() && value_.size() <= MaxIdentityLength && std::ranges::all_of(value_, [](const char value) {
            return IsIdentityCharacter(static_cast<unsigned char>(value));
        });
    }

    /** @copydoc RenderAdapterProperties::IsValid */
    bool RenderAdapterProperties::IsValid() const noexcept {
        return id.IsValid() && !displayName.empty() && displayName.size() <= MaxDisplayNameLength &&
               displayName.find('\0') == std::string::npos && IsKnown(kind) && IsKnown(availability);
    }

    /** @copydoc RenderAdapterSnapshot::IsValid */
    bool RenderAdapterSnapshot::IsValid() const noexcept {
        if (revision == 0 || adapters.size() > 64) {
            return false;
        }
        for (std::size_t index = 0; index < adapters.size(); ++index) {
            if (!adapters[index].IsValid()) {
                return false;
            }
            if (index > 0 && adapters[index - 1].id.Value() >= adapters[index].id.Value()) {
                return false;
            }
        }
        return true;
    }

    /** @copydoc RenderAdapterSelectionRequest::IsValid */
    bool RenderAdapterSelectionRequest::IsValid() const noexcept {
        return (!requiredAdapter || requiredAdapter->IsValid()) && (!requiredKind || IsKnown(*requiredKind));
    }

    /** @copydoc RenderDeviceCreationFailure::IsValid */
    bool RenderDeviceCreationFailure::IsValid() const noexcept {
        return adapter.IsValid() && IsKnown(kind) && !message.empty() && message.size() <= MaxDiagnosticLength;
    }

    /** @copydoc SelectRenderAdapter */
    Result<RenderAdapterSelection> SelectRenderAdapter(const RenderAdapterSnapshot &snapshot,
                                                       const RenderAdapterSelectionRequest &request) {
        if (!snapshot.IsValid()) {
            return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::InvalidSnapshot));
        }
        if (!request.IsValid()) {
            return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::InvalidSelectionRequest));
        }

        if (request.requiredAdapter) {
            const auto found =
                std::ranges::lower_bound(snapshot.adapters, std::string_view{request.requiredAdapter->Value()}, std::ranges::less{},
                                         [](const RenderAdapterProperties &adapter) -> std::string_view {
                return adapter.id.Value();
            });
            if (found == snapshot.adapters.end() || found->id != *request.requiredAdapter) {
                return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::RequiredAdapterNotFound));
            }
            if (found->availability != RenderAdapterAvailability::Available) {
                return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::AdapterUnavailable));
            }
            if (!Matches(*found, request)) {
                return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::NoCompatibleAdapter));
            }
            return Result<RenderAdapterSelection>::Success(RenderAdapterSelection{*found, snapshot.revision});
        }

        const auto found = std::ranges::find_if(snapshot.adapters, [&](const RenderAdapterProperties &adapter) {
            return Matches(adapter, request);
        });
        if (found == snapshot.adapters.end()) {
            return Result<RenderAdapterSelection>::Failure(MakeError(RenderAdapterErrors::NoCompatibleAdapter));
        }
        return Result<RenderAdapterSelection>::Success(RenderAdapterSelection{*found, snapshot.revision});
    }
}  // namespace Horo::Render
