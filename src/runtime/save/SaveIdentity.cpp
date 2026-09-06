#include "Horo/Runtime/Save/SaveIdentity.h"

#include <algorithm>
#include <charconv>
#include <optional>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] bool IsCanonicalParticipantCharacter(const unsigned char character) noexcept {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
        }

        /** @brief Parses exactly two lowercase hexadecimal characters into one byte. */
        [[nodiscard]] std::optional<std::uint8_t> ParseCanonicalByte(const std::string_view text) noexcept {
            if (text.size() != 2 || !std::ranges::all_of(text, [](const char character) {
                return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
            }))
                return std::nullopt;

            unsigned int parsedByte{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsedByte, 16);
            if (error != std::errc{} || end != text.data() + text.size())
                return std::nullopt;
            return static_cast<std::uint8_t>(parsedByte);
        }

        template <typename Range> [[nodiscard]] std::uint64_t StableHash64(const Range &bytes) noexcept {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const auto byte : bytes) {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 1099511628211ULL;
            }
            return hash;
        }
    }  // namespace

    namespace SaveIdentityDetail {
        /** @copydoc ParseUuid */
        Result<Bytes> ParseUuid(const std::string_view text) {
            if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
                return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityMalformed));

            Bytes bytes{};
            std::size_t byteIndex{};
            for (std::size_t index = 0; index < text.size();) {
                if (text[index] == '-') {
                    ++index;
                    continue;
                }
                if (index + 2 > text.size() || byteIndex >= bytes.size())
                    return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityMalformed));
                const auto parsedByte = ParseCanonicalByte(text.substr(index, 2));
                if (!parsedByte.has_value())
                    return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityMalformed));
                bytes[byteIndex++] = *parsedByte;
                index += 2;
            }
            if (byteIndex != bytes.size())
                return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityMalformed));
            return ValidateUuid(bytes);
        }

        /** @copydoc ValidateUuid */
        Result<Bytes> ValidateUuid(Bytes bytes) {
            if (std::ranges::none_of(bytes, [](const std::uint8_t value) {
                return value != 0;
            }))
                return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityInvalid));
            return Result<Bytes>::Success(bytes);
        }

        /** @copydoc FormatUuid */
        std::string FormatUuid(const Bytes &bytes) {
            constexpr char kHex[] = "0123456789abcdef";
            std::string result;
            result.reserve(36);
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                if (index == 4 || index == 6 || index == 8 || index == 10)
                    result.push_back('-');
                result.push_back(kHex[static_cast<std::size_t>(bytes[index]) >> 4U]);
                result.push_back(kHex[static_cast<std::size_t>(bytes[index]) & 0x0fU]);
            }
            return result;
        }

        /** @copydoc HashUuid */
        std::uint64_t HashUuid(const Bytes &bytes) noexcept {
            return StableHash64(bytes);
        }
    }  // namespace SaveIdentityDetail

    /** @copydoc SaveParticipantId::Parse */
    Result<SaveParticipantId> SaveParticipantId::Parse(const std::string_view value) {
        if (value.empty() || value.size() > MaximumSaveParticipantIdBytes || value.front() == '.' || value.back() == '.')
            return Result<SaveParticipantId>::Failure(MakeError(SaveErrors::ParticipantIdInvalid));
        bool segmentStart = true;
        bool hasDot = false;
        for (const unsigned char character : value) {
            if (character == '.') {
                if (segmentStart)
                    return Result<SaveParticipantId>::Failure(MakeError(SaveErrors::ParticipantIdInvalid));
                segmentStart = true;
                hasDot = true;
                continue;
            }
            if (!IsCanonicalParticipantCharacter(character) || (segmentStart && !(character >= 'a' && character <= 'z')))
                return Result<SaveParticipantId>::Failure(MakeError(SaveErrors::ParticipantIdInvalid));
            segmentStart = false;
        }
        if (segmentStart || !hasDot)
            return Result<SaveParticipantId>::Failure(MakeError(SaveErrors::ParticipantIdInvalid));
        return Result<SaveParticipantId>::Success(SaveParticipantId{std::string{value}});
    }

    /** @copydoc SaveParticipantId::Value */
    const std::string &SaveParticipantId::Value() const noexcept {
        return value_;
    }

    /** @copydoc SaveParticipantId::IsValid */
    bool SaveParticipantId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc SaveParticipantIdHash::operator() */
    std::size_t SaveParticipantIdHash::operator()(const SaveParticipantId &value) const noexcept {
        return static_cast<std::size_t>(StableHash64(value.Value()));
    }
}  // namespace Horo::Runtime
