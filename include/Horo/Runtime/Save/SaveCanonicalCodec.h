#pragma once

/**
 * @file SaveCanonicalCodec.h
 * @brief Bounded deterministic codecs for canonical runtime-save values.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Horo::Runtime {
    /** @brief Stable nonzero numeric identity for one field in a versioned canonical record. */
    class CanonicalFieldId final {
    public:
        using ValueType = std::uint32_t;

        /** @brief Creates a field identity. @param value Stable nonzero schema value. @return Identity or a typed codec error. */
        [[nodiscard]] static Result<CanonicalFieldId> Create(ValueType value);

        /** @brief Returns the stable numeric value. @return Nonzero schema value. */
        [[nodiscard]] constexpr ValueType Value() const noexcept {
            return value_;
        }

        [[nodiscard]] constexpr auto operator<=>(const CanonicalFieldId &) const noexcept = default;

    private:
        explicit constexpr CanonicalFieldId(ValueType value) noexcept : value_(value) {}

        ValueType value_{};
    };

    /** @brief Qualified construction and hostile-input bounds for one canonical value tree. */
    struct CanonicalCodecLimits final {
        std::size_t maximumBytes{16 * 1024 * 1024};
        std::size_t maximumStringBytes{1024 * 1024};
        std::size_t maximumCollectionElements{1024 * 1024};
        std::size_t maximumFields{4096};
        std::size_t maximumNestingDepth{32};
    };

    /** @brief Exact failure location within a canonical value tree. */
    struct CanonicalCodecContext final {
        std::vector<CanonicalFieldId> fieldPath; /**< Outer-to-inner stable record field path. */
        std::size_t byteOffset{};                /**< Byte offset in the reader or writer value. */
    };

    /** @brief Typed codec error paired with stable field and byte context. */
    struct CanonicalCodecFailure final {
        Error error;
        CanonicalCodecContext context;
    };

    /** @brief Result carrying canonical codec field context on expected failure. */
    template <typename T> class CanonicalCodecResult final {
    public:
        [[nodiscard]] static CanonicalCodecResult Success(T value) {
            return CanonicalCodecResult{std::move(value)};
        }

        [[nodiscard]] static CanonicalCodecResult Failure(CanonicalCodecFailure failure) {
            return CanonicalCodecResult{std::move(failure)};
        }

        [[nodiscard]] bool HasValue() const noexcept {
            return std::holds_alternative<T>(value_);
        }

        [[nodiscard]] bool HasError() const noexcept {
            return !HasValue();
        }

        [[nodiscard]] const T &Value() const & {
            assert(HasValue());
            return std::get<T>(value_);
        }

        [[nodiscard]] T &&Value() && {
            assert(HasValue());
            return std::move(std::get<T>(value_));
        }

        [[nodiscard]] const CanonicalCodecFailure &ErrorValue() const {
            assert(HasError());
            return std::get<CanonicalCodecFailure>(value_);
        }

    private:
        explicit CanonicalCodecResult(T value) : value_(std::move(value)) {}

        explicit CanonicalCodecResult(CanonicalCodecFailure failure) : value_(std::move(failure)) {}

        std::variant<T, CanonicalCodecFailure> value_;
    };

    template <> class CanonicalCodecResult<void> final {
    public:
        [[nodiscard]] static CanonicalCodecResult Success() {
            return {};
        }

        [[nodiscard]] static CanonicalCodecResult Failure(CanonicalCodecFailure failure) {
            CanonicalCodecResult result;
            result.failure_ = std::move(failure);
            return result;
        }

        [[nodiscard]] bool HasValue() const noexcept {
            return !failure_;
        }

        [[nodiscard]] bool HasError() const noexcept {
            return failure_.has_value();
        }

        [[nodiscard]] const CanonicalCodecFailure &ErrorValue() const {
            assert(HasError());
            return *failure_;
        }

    private:
        std::optional<CanonicalCodecFailure> failure_;
    };

    /** @brief One pre-encoded field; record encoding sorts these values by stable field ID. */
    struct CanonicalRecordField final {
        CanonicalFieldId id;
        std::vector<std::byte> value;
    };

    /** @brief One pre-encoded canonical map entry. */
    struct CanonicalMapEntry final {
        std::vector<std::byte> key;
        std::vector<std::byte> value;
    };

    /** @brief Bounded writer for explicitly selected canonical value types; it never serializes object memory. */
    class CanonicalValueWriter final {
    public:
        /** @brief Creates an empty root writer. @param limits Trusted nonzero output bounds. */
        explicit CanonicalValueWriter(CanonicalCodecLimits limits = {});
        /** @brief Creates a child writer whose failures identify one nested record field. @param field Stable field identity. @return Child
         * writer. */
        [[nodiscard]] CanonicalValueWriter ForField(CanonicalFieldId field) const;

        /** @brief Writes a canonical zero-or-one boolean. @param value Value to append. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteBool(bool value);
        /** @brief Writes an unsigned fixed-width scalar. @param value Value to append. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteUInt8(std::uint8_t value);
        /** @copydoc WriteUInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteUInt16(std::uint16_t value);
        /** @copydoc WriteUInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteUInt32(std::uint32_t value);
        /** @copydoc WriteUInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteUInt64(std::uint64_t value);
        /** @brief Writes a two's-complement fixed-width scalar. @param value Value to append. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteInt8(std::int8_t value);
        /** @copydoc WriteInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteInt16(std::int16_t value);
        /** @copydoc WriteInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteInt32(std::int32_t value);
        /** @copydoc WriteInt8 */
        [[nodiscard]] CanonicalCodecResult<void> WriteInt64(std::int64_t value);
        /** @brief Writes finite IEEE 754 binary32, canonicalizing negative zero. @param value Value to append. @return Success or typed
         * failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteFloat32(float value);
        /** @brief Writes finite IEEE 754 binary64, canonicalizing negative zero. @param value Value to append. @return Success or typed
         * failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteFloat64(double value);
        /** @brief Writes validated UTF-8 without implicit normalization. @param value Text bytes to append. @return Success or typed
         * failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteUtf8(std::string_view value);
        /** @brief Writes a length-delimited opaque byte sequence. @param value Bytes to append. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteBytes(std::span<const std::byte> value);
        /** @brief Writes a finite scene vector component by component. @param value Vector to append. @return Success or typed failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteVec2(Math::Vec2 value);
        /** @copydoc WriteVec2 */
        [[nodiscard]] CanonicalCodecResult<void> WriteVec3(Math::Vec3 value);
        /** @copydoc WriteVec2 */
        [[nodiscard]] CanonicalCodecResult<void> WriteVec4(Math::Vec4 value);
        /** @brief Writes a finite quaternion component by component. @param value Quaternion to append. @return Success or typed failure.
         */
        [[nodiscard]] CanonicalCodecResult<void> WriteQuaternion(Math::Quaternion value);
        /** @brief Writes a sequence element count; adapters then write values in semantic order. @param count Element count. @return
         * Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteSequenceSize(std::size_t count);
        /** @brief Writes an optional-value presence marker. @param present Whether a value follows. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteOptionalPresence(bool present);
        /** @brief Writes a zero-based stable alternative index. @param index Schema-defined index. @return Success or bounded failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteVariantIndex(std::uint32_t index);
        /** @brief Sorts entries by encoded key bytes and rejects duplicate canonical keys. @param entries Pre-encoded entries. @return
         * Success or typed failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteMap(std::span<const CanonicalMapEntry> entries);
        /** @brief Sorts encoded elements and rejects duplicate canonical values. @param elements Pre-encoded values. @return Success or
         * typed failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteSet(std::span<const std::vector<std::byte>> elements);
        /** @brief Sorts fields by ID and rejects duplicate or zero identities. @param fields Pre-encoded fields. @return Success or typed
         * failure. */
        [[nodiscard]] CanonicalCodecResult<void> WriteRecord(std::span<const CanonicalRecordField> fields);

        /** @brief Returns currently encoded bytes. @return Borrowed bytes valid for this writer's lifetime. */
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept {
            return bytes_;
        }

        /** @brief Transfers encoded bytes. @return Owned canonical byte sequence. */
        [[nodiscard]] std::vector<std::byte> TakeBytes() && noexcept {
            return std::move(bytes_);
        }

    private:
        CanonicalValueWriter(CanonicalCodecLimits limits, std::vector<CanonicalFieldId> path);
        [[nodiscard]] CanonicalCodecResult<void> Append(std::span<const std::byte> value);
        [[nodiscard]] CanonicalCodecResult<void> WriteLength(std::size_t value);
        [[nodiscard]] CanonicalCodecResult<void> ValidateCollectionSize(std::size_t count) const;
        [[nodiscard]] CanonicalCodecFailure Failure(const ErrorCodeDescriptor &descriptor) const;

        CanonicalCodecLimits limits_;
        std::vector<CanonicalFieldId> path_;
        std::vector<std::byte> bytes_;
    };

    /** @brief Bounded reader for canonical values; every length is admitted before allocation. */
    class CanonicalValueReader final {
    public:
        /** @brief Creates a reader over one complete borrowed value. @param bytes Encoded bytes valid for this reader's lifetime. @param
         * limits Trusted nonzero input bounds. */
        explicit CanonicalValueReader(std::span<const std::byte> bytes, CanonicalCodecLimits limits = {});
        /** @brief Creates a child reader whose failures identify one nested record field. @param field Decoded field value. @return Child
         * reader. */
        [[nodiscard]] CanonicalValueReader ForField(const CanonicalRecordField &field) const;

        /** @brief Reads a canonical boolean. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<bool> ReadBool();
        /** @brief Reads an unsigned fixed-width scalar. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::uint8_t> ReadUInt8();
        /** @copydoc ReadUInt8 */
        [[nodiscard]] CanonicalCodecResult<std::uint16_t> ReadUInt16();
        /** @copydoc ReadUInt8 */
        [[nodiscard]] CanonicalCodecResult<std::uint32_t> ReadUInt32();
        /** @copydoc ReadUInt8 */
        [[nodiscard]] CanonicalCodecResult<std::uint64_t> ReadUInt64();
        /** @brief Reads a two's-complement fixed-width scalar. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::int8_t> ReadInt8();
        /** @copydoc ReadInt8 */
        [[nodiscard]] CanonicalCodecResult<std::int16_t> ReadInt16();
        /** @copydoc ReadInt8 */
        [[nodiscard]] CanonicalCodecResult<std::int32_t> ReadInt32();
        /** @copydoc ReadInt8 */
        [[nodiscard]] CanonicalCodecResult<std::int64_t> ReadInt64();
        /** @brief Reads finite canonical binary32 and rejects negative zero. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<float> ReadFloat32();
        /** @brief Reads finite canonical binary64 and rejects negative zero. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<double> ReadFloat64();
        /** @brief Reads length-delimited validated UTF-8. @return Owned text or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::string> ReadUtf8();
        /** @brief Reads a length-delimited byte sequence. @return Owned bytes or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::vector<std::byte>> ReadBytes();
        /** @brief Reads a finite scene vector component by component. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<Math::Vec2> ReadVec2();
        /** @copydoc ReadVec2 */
        [[nodiscard]] CanonicalCodecResult<Math::Vec3> ReadVec3();
        /** @copydoc ReadVec2 */
        [[nodiscard]] CanonicalCodecResult<Math::Vec4> ReadVec4();
        /** @brief Reads a finite quaternion component by component. @return Value or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<Math::Quaternion> ReadQuaternion();
        /** @brief Reads and admits a sequence element count. @return Count or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::size_t> ReadSequenceSize();
        /** @brief Reads an optional-value presence marker. @return Presence or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<bool> ReadOptionalPresence();
        /** @brief Reads and validates a variant index. @param alternativeCount Nonzero schema alternative count. @return Index or
         * field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::uint32_t> ReadVariantIndex(std::uint32_t alternativeCount);
        /** @brief Reads a strictly key-sorted unique map. @return Owned encoded entries or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::vector<CanonicalMapEntry>> ReadMap();
        /** @brief Reads a strictly sorted unique set. @return Owned encoded values or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::vector<std::vector<std::byte>>> ReadSet();
        /** @brief Reads a strictly ID-sorted unique structured record. @return Owned fields or field-aware failure. */
        [[nodiscard]] CanonicalCodecResult<std::vector<CanonicalRecordField>> ReadRecord();
        /** @brief Rejects unread trailing data. @return Success only when the input was consumed exactly. */
        [[nodiscard]] CanonicalCodecResult<void> RequireFinished() const;

    private:
        CanonicalValueReader(std::span<const std::byte> bytes, CanonicalCodecLimits limits, std::vector<CanonicalFieldId> path);
        [[nodiscard]] CanonicalCodecResult<std::span<const std::byte>> Read(std::size_t count);
        [[nodiscard]] CanonicalCodecResult<std::size_t> ReadLength(std::size_t maximum);
        [[nodiscard]] CanonicalCodecFailure Failure(const ErrorCodeDescriptor &descriptor) const;

        std::span<const std::byte> bytes_;
        CanonicalCodecLimits limits_;
        std::vector<CanonicalFieldId> path_;
        std::size_t offset_{};
    };
}  // namespace Horo::Runtime
