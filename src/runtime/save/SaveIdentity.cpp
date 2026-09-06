#include "Horo/Runtime/Save/SaveIdentity.h"

#include <algorithm>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] int HexValue(const char value) noexcept {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            return -1;
        }

        [[nodiscard]] bool IsCanonicalParticipantCharacter(const unsigned char character) noexcept {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
        }

        template <typename Range> [[nodiscard]] std::size_t StableHash(const Range &bytes) noexcept {
            std::size_t hash = sizeof(std::size_t) == 8 ? 1469598103934665603ULL : 2166136261U;
            for (const auto byte : bytes) {
                hash ^= static_cast<unsigned char>(byte);
                hash *= sizeof(std::size_t) == 8 ? 1099511628211ULL : 16777619U;
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
                const int high = HexValue(text[index++]);
                const int low = index < text.size() ? HexValue(text[index++]) : -1;
                if (high < 0 || low < 0 || byteIndex >= bytes.size())
                    return Result<Bytes>::Failure(MakeError(SaveErrors::IdentityMalformed));
                bytes[byteIndex++] = static_cast<std::uint8_t>((high << 4) | low);
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
        std::size_t HashUuid(const Bytes &bytes) noexcept {
            return StableHash(bytes);
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
        return StableHash(value.Value());
    }
}  // namespace Horo::Runtime
