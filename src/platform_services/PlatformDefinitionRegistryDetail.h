#pragma once

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/PlatformServices/PlatformStableIdRegistry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::PlatformServices::DefinitionRegistryDetail {
    [[nodiscard]] inline ErrorCodeDescriptor MakeDescriptor(const ErrorDomainId &domain, const std::string_view code,
                                                            const std::string_view summary, const std::string_view remediation,
                                                            const bool userActionable) {
        return {domain, ErrorCode{std::string{code}}, ErrorSeverity::Error, summary, remediation, false, userActionable};
    }

    [[nodiscard]] inline bool IsZero(const Sha256Digest &digest) noexcept {
        return std::ranges::all_of(digest.bytes, [](const std::uint8_t byte) {
            return byte == 0;
        });
    }

    [[nodiscard]] inline bool IsLocalizationCharacter(const char value) noexcept {
        const bool lowerCaseLetter = value >= 'a' && value <= 'z';
        const bool digit = value >= '0' && value <= '9';
        return lowerCaseLetter || digit || value == '_' || value == '.' || value == '-';
    }

    [[nodiscard]] inline bool IsLocalizationKey(const std::string_view key, const std::uint32_t maximumBytes) noexcept {
        return !key.empty() && key.size() <= maximumBytes && key.front() >= 'a' && key.front() <= 'z' &&
               std::ranges::all_of(key.substr(1), IsLocalizationCharacter);
    }

    [[nodiscard]] inline Error MakeDiagnostic(const ErrorCodeDescriptor &descriptor, std::string source, const std::string_view message) {
        Error error = MakeError(descriptor);
        error.diagnostics.push_back({.code = DiagnosticCode{descriptor.code.Value()},
                                     .severity = DiagnosticSeverity::Error,
                                     .message = std::string{message},
                                     .location = {.source = std::move(source)}});
        return error;
    }

    [[nodiscard]] inline Error FieldError(const ErrorCodeDescriptor &descriptor, const std::size_t index, const std::string_view field,
                                          const std::string_view message) {
        return MakeDiagnostic(descriptor, std::format("definitions[{}].{}", index, field), message);
    }

    [[nodiscard]] inline Error DocumentError(const ErrorCodeDescriptor &descriptor, const std::string_view field,
                                             const std::string_view message) {
        return MakeDiagnostic(descriptor, std::string{field}, message);
    }

    class FingerprintWriter final {
    public:
        explicit FingerprintWriter(const std::size_t capacity) {
            bytes_.reserve(capacity);
        }

        void AddByte(const std::uint8_t value) {
            bytes_.push_back(static_cast<std::byte>(value));
        }

        void AddU32(const std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                AddByte(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
        }

        void AddU64(const std::uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8)
                AddByte(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
        }

        void AddText(const std::string_view value) {
            AddU32(static_cast<std::uint32_t>(value.size()));
            for (const char character : value)
                AddByte(static_cast<std::uint8_t>(character));
        }

        void AddDigest(const Sha256Digest &digest) {
            for (const std::uint8_t byte : digest.bytes)
                AddByte(byte);
        }

        [[nodiscard]] Sha256Digest Finish() const {
            return ComputeSha256(bytes_);
        }

    private:
        std::vector<std::byte> bytes_;
    };
}  // namespace Horo::PlatformServices::DefinitionRegistryDetail
