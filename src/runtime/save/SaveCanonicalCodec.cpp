#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utf8proc.h>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] bool HasValidLimits(const CanonicalCodecLimits &limits) noexcept {
            return limits.maximumBytes != 0 && limits.maximumStringBytes != 0 && limits.maximumCollectionElements != 0 &&
                   limits.maximumFields != 0 && limits.maximumNestingDepth != 0 && limits.maximumStringBytes <= limits.maximumBytes;
        }

        [[nodiscard]] bool IsValidUtf8(const std::string_view value) noexcept {
            if (value.size() > static_cast<std::size_t>(std::numeric_limits<utf8proc_ssize_t>::max()))
                return false;
            const auto *cursor = reinterpret_cast<const utf8proc_uint8_t *>(value.data());
            auto remaining = static_cast<utf8proc_ssize_t>(value.size());
            while (remaining > 0) {
                utf8proc_int32_t codepoint{};
                const utf8proc_ssize_t decoded = utf8proc_iterate(cursor, remaining, &codepoint);
                if (decoded <= 0)
                    return false;
                cursor += decoded;
                remaining -= decoded;
            }
            return true;
        }

        template <typename Unsigned>
        [[nodiscard]] std::array<std::byte, sizeof(Unsigned)> EncodeLittleEndian(const Unsigned value) noexcept {
            static_assert(std::is_unsigned_v<Unsigned>);
            std::array<std::byte, sizeof(Unsigned)> bytes{};
            for (std::size_t index = 0; index < bytes.size(); ++index)
                bytes[index] = static_cast<std::byte>(value >> (index * 8U));
            return bytes;
        }

        template <typename Unsigned> [[nodiscard]] Unsigned DecodeLittleEndian(const std::span<const std::byte> bytes) noexcept {
            static_assert(std::is_unsigned_v<Unsigned>);
            Unsigned value{};
            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                value |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
            return value;
        }

        [[nodiscard]] bool ByteLess(const std::vector<std::byte> &left, const std::vector<std::byte> &right) noexcept {
            return std::ranges::lexicographical_compare(left, right);
        }

        [[nodiscard]] bool SameBytes(const std::vector<std::byte> &left, const std::vector<std::byte> &right) noexcept {
            return std::ranges::equal(left, right);
        }
    }  // namespace

    /** @copydoc CanonicalFieldId::Create */
    Result<CanonicalFieldId> CanonicalFieldId::Create(const ValueType value) {
        if (value == 0)
            return Result<CanonicalFieldId>::Failure(MakeError(SaveErrors::CanonicalCodecInvalid));
        return Result<CanonicalFieldId>::Success(CanonicalFieldId{value});
    }

    /** @copydoc CanonicalValueWriter::CanonicalValueWriter(CanonicalCodecLimits) */
    CanonicalValueWriter::CanonicalValueWriter(const CanonicalCodecLimits limits) : limits_(limits) {}

    CanonicalValueWriter::CanonicalValueWriter(const CanonicalCodecLimits limits, std::vector<CanonicalFieldId> path)
        : limits_(limits), path_(std::move(path)) {}

    /** @copydoc CanonicalValueWriter::ForField */
    CanonicalValueWriter CanonicalValueWriter::ForField(const CanonicalFieldId field) const {
        auto path = path_;
        path.push_back(field);
        return CanonicalValueWriter{limits_, std::move(path)};
    }

    CanonicalCodecFailure CanonicalValueWriter::Failure(const ErrorCodeDescriptor &descriptor) const {
        return {.error = MakeError(descriptor), .context = {.fieldPath = path_, .byteOffset = bytes_.size()}};
    }

    CanonicalCodecResult<void> CanonicalValueWriter::Append(const std::span<const std::byte> value) {
        if (!HasValidLimits(limits_) || path_.size() > limits_.maximumNestingDepth || value.size() > limits_.maximumBytes ||
            bytes_.size() > limits_.maximumBytes - value.size())
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return CanonicalCodecResult<void>::Success();
    }

    CanonicalCodecResult<void> CanonicalValueWriter::WriteLength(const std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max())
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        return WriteUInt32(static_cast<std::uint32_t>(value));
    }

    CanonicalCodecResult<void> CanonicalValueWriter::ValidateCollectionSize(const std::size_t count) const {
        if (!HasValidLimits(limits_) || count > limits_.maximumCollectionElements || count > std::numeric_limits<std::uint32_t>::max())
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        return CanonicalCodecResult<void>::Success();
    }

    /** @copydoc CanonicalValueWriter::WriteBool */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteBool(const bool value) {
        return WriteUInt8(value ? 1U : 0U);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt8 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteUInt8(const std::uint8_t value) {
        return Append(std::span{reinterpret_cast<const std::byte *>(&value), 1});
    }

    /** @copydoc CanonicalValueWriter::WriteUInt16 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteUInt16(const std::uint16_t value) {
        const auto bytes = EncodeLittleEndian(value);
        return Append(bytes);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt32 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteUInt32(const std::uint32_t value) {
        const auto bytes = EncodeLittleEndian(value);
        return Append(bytes);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt64 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteUInt64(const std::uint64_t value) {
        const auto bytes = EncodeLittleEndian(value);
        return Append(bytes);
    }

    /** @copydoc CanonicalValueWriter::WriteInt8 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteInt8(const std::int8_t value) {
        return WriteUInt8(std::bit_cast<std::uint8_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteInt16 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteInt16(const std::int16_t value) {
        return WriteUInt16(std::bit_cast<std::uint16_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteInt32 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteInt32(const std::int32_t value) {
        return WriteUInt32(std::bit_cast<std::uint32_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteInt64 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteInt64(const std::int64_t value) {
        return WriteUInt64(std::bit_cast<std::uint64_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteFloat32 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteFloat32(float value) {
        static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(std::uint32_t));
        if (!std::isfinite(value))
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecNonFinite));
        if (value == 0.0F)
            value = 0.0F;
        return WriteUInt32(std::bit_cast<std::uint32_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteFloat64 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteFloat64(double value) {
        static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == sizeof(std::uint64_t));
        if (!std::isfinite(value))
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecNonFinite));
        if (value == 0.0)
            value = 0.0;
        return WriteUInt64(std::bit_cast<std::uint64_t>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteUtf8 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteUtf8(const std::string_view value) {
        if (value.size() > limits_.maximumStringBytes)
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        if (!IsValidUtf8(value))
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecUtf8Invalid));
        auto length = WriteLength(value.size());
        if (length.HasError())
            return length;
        return Append(std::as_bytes(std::span{value.data(), value.size()}));
    }

    /** @copydoc CanonicalValueWriter::WriteBytes */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteBytes(const std::span<const std::byte> value) {
        auto length = WriteLength(value.size());
        if (length.HasError())
            return length;
        return Append(value);
    }

    /** @copydoc CanonicalValueWriter::WriteVec2 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteVec2(const Math::Vec2 value) {
        auto result = WriteFloat32(value.x);
        return result.HasError() ? result : WriteFloat32(value.y);
    }

    /** @copydoc CanonicalValueWriter::WriteVec3 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteVec3(const Math::Vec3 value) {
        auto result = WriteVec2({value.x, value.y});
        return result.HasError() ? result : WriteFloat32(value.z);
    }

    /** @copydoc CanonicalValueWriter::WriteVec4 */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteVec4(const Math::Vec4 value) {
        auto result = WriteVec3({value.x, value.y, value.z});
        return result.HasError() ? result : WriteFloat32(value.w);
    }

    /** @copydoc CanonicalValueWriter::WriteQuaternion */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteQuaternion(const Math::Quaternion value) {
        return WriteVec4({value.x, value.y, value.z, value.w});
    }

    /** @copydoc CanonicalValueWriter::WriteSequenceSize */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteSequenceSize(const std::size_t count) {
        auto admitted = ValidateCollectionSize(count);
        return admitted.HasError() ? admitted : WriteLength(count);
    }

    /** @copydoc CanonicalValueWriter::WriteOptionalPresence */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteOptionalPresence(const bool present) {
        return WriteBool(present);
    }

    /** @copydoc CanonicalValueWriter::WriteVariantIndex */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteVariantIndex(const std::uint32_t index) {
        return WriteUInt32(index);
    }

    /** @copydoc CanonicalValueWriter::WriteMap */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteMap(const std::span<const CanonicalMapEntry> entries) {
        auto admitted = ValidateCollectionSize(entries.size());
        if (admitted.HasError())
            return admitted;
        std::vector<CanonicalMapEntry> ordered{entries.begin(), entries.end()};
        std::ranges::sort(ordered, [](const auto &left, const auto &right) {
            return ByteLess(left.key, right.key);
        });
        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (SameBytes(ordered[index - 1].key, ordered[index].key))
                return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecDuplicate));
        }
        auto result = WriteLength(ordered.size());
        for (const CanonicalMapEntry &entry : ordered) {
            if (result.HasError())
                return result;
            result = WriteBytes(entry.key);
            if (result.HasError())
                return result;
            result = WriteBytes(entry.value);
        }
        return result;
    }

    /** @copydoc CanonicalValueWriter::WriteSet */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteSet(const std::span<const std::vector<std::byte>> elements) {
        auto admitted = ValidateCollectionSize(elements.size());
        if (admitted.HasError())
            return admitted;
        std::vector<std::vector<std::byte>> ordered{elements.begin(), elements.end()};
        std::ranges::sort(ordered, ByteLess);
        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (SameBytes(ordered[index - 1], ordered[index]))
                return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecDuplicate));
        }
        auto result = WriteLength(ordered.size());
        for (const auto &element : ordered) {
            if (result.HasError())
                return result;
            result = WriteBytes(element);
        }
        return result;
    }

    /** @copydoc CanonicalValueWriter::WriteRecord */
    CanonicalCodecResult<void> CanonicalValueWriter::WriteRecord(const std::span<const CanonicalRecordField> fields) {
        if (fields.size() > limits_.maximumFields || fields.size() > std::numeric_limits<std::uint32_t>::max())
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        std::vector<CanonicalRecordField> ordered{fields.begin(), fields.end()};
        std::ranges::sort(ordered, {}, &CanonicalRecordField::id);
        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (ordered[index - 1].id == ordered[index].id)
                return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecDuplicate));
        }
        auto result = WriteLength(ordered.size());
        for (const CanonicalRecordField &field : ordered) {
            if (result.HasError())
                return result;
            result = WriteUInt32(field.id.Value());
            if (result.HasError())
                return result;
            result = WriteBytes(field.value);
        }
        return result;
    }

    /** @copydoc CanonicalValueReader::CanonicalValueReader(std::span<const std::byte>, CanonicalCodecLimits) */
    CanonicalValueReader::CanonicalValueReader(const std::span<const std::byte> bytes, const CanonicalCodecLimits limits)
        : bytes_(bytes), limits_(limits) {}

    CanonicalValueReader::CanonicalValueReader(const std::span<const std::byte> bytes, const CanonicalCodecLimits limits,
                                               std::vector<CanonicalFieldId> path)
        : bytes_(bytes), limits_(limits), path_(std::move(path)) {}

    /** @copydoc CanonicalValueReader::ForField */
    CanonicalValueReader CanonicalValueReader::ForField(const CanonicalRecordField &field) const {
        auto path = path_;
        path.push_back(field.id);
        return CanonicalValueReader{field.value, limits_, std::move(path)};
    }

    CanonicalCodecFailure CanonicalValueReader::Failure(const ErrorCodeDescriptor &descriptor) const {
        return {.error = MakeError(descriptor), .context = {.fieldPath = path_, .byteOffset = offset_}};
    }

    CanonicalCodecResult<std::span<const std::byte>> CanonicalValueReader::Read(const std::size_t count) {
        if (!HasValidLimits(limits_) || path_.size() > limits_.maximumNestingDepth || bytes_.size() > limits_.maximumBytes ||
            count > bytes_.size() - std::min(offset_, bytes_.size()))
            return CanonicalCodecResult<std::span<const std::byte>>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        const auto value = bytes_.subspan(offset_, count);
        offset_ += count;
        return CanonicalCodecResult<std::span<const std::byte>>::Success(value);
    }

    CanonicalCodecResult<std::size_t> CanonicalValueReader::ReadLength(const std::size_t maximum) {
        auto value = ReadUInt32();
        if (value.HasError())
            return CanonicalCodecResult<std::size_t>::Failure(value.ErrorValue());
        if (value.Value() > maximum)
            return CanonicalCodecResult<std::size_t>::Failure(Failure(SaveErrors::CanonicalCodecLimitExceeded));
        return CanonicalCodecResult<std::size_t>::Success(value.Value());
    }

    /** @copydoc CanonicalValueReader::ReadUInt8 */
    CanonicalCodecResult<std::uint8_t> CanonicalValueReader::ReadUInt8() {
        auto bytes = Read(1);
        if (bytes.HasError())
            return CanonicalCodecResult<std::uint8_t>::Failure(bytes.ErrorValue());
        return CanonicalCodecResult<std::uint8_t>::Success(std::to_integer<std::uint8_t>(bytes.Value().front()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt16 */
    CanonicalCodecResult<std::uint16_t> CanonicalValueReader::ReadUInt16() {
        auto bytes = Read(2);
        return bytes.HasError() ? CanonicalCodecResult<std::uint16_t>::Failure(bytes.ErrorValue())
                                : CanonicalCodecResult<std::uint16_t>::Success(DecodeLittleEndian<std::uint16_t>(bytes.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt32 */
    CanonicalCodecResult<std::uint32_t> CanonicalValueReader::ReadUInt32() {
        auto bytes = Read(4);
        return bytes.HasError() ? CanonicalCodecResult<std::uint32_t>::Failure(bytes.ErrorValue())
                                : CanonicalCodecResult<std::uint32_t>::Success(DecodeLittleEndian<std::uint32_t>(bytes.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt64 */
    CanonicalCodecResult<std::uint64_t> CanonicalValueReader::ReadUInt64() {
        auto bytes = Read(8);
        return bytes.HasError() ? CanonicalCodecResult<std::uint64_t>::Failure(bytes.ErrorValue())
                                : CanonicalCodecResult<std::uint64_t>::Success(DecodeLittleEndian<std::uint64_t>(bytes.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadBool */
    CanonicalCodecResult<bool> CanonicalValueReader::ReadBool() {
        auto value = ReadUInt8();
        if (value.HasError())
            return CanonicalCodecResult<bool>::Failure(value.ErrorValue());
        if (value.Value() > 1)
            return CanonicalCodecResult<bool>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        return CanonicalCodecResult<bool>::Success(value.Value() == 1);
    }

    /** @copydoc CanonicalValueReader::ReadInt8 */
    CanonicalCodecResult<std::int8_t> CanonicalValueReader::ReadInt8() {
        auto value = ReadUInt8();
        return value.HasError() ? CanonicalCodecResult<std::int8_t>::Failure(value.ErrorValue())
                                : CanonicalCodecResult<std::int8_t>::Success(std::bit_cast<std::int8_t>(value.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadInt16 */
    CanonicalCodecResult<std::int16_t> CanonicalValueReader::ReadInt16() {
        auto value = ReadUInt16();
        return value.HasError() ? CanonicalCodecResult<std::int16_t>::Failure(value.ErrorValue())
                                : CanonicalCodecResult<std::int16_t>::Success(std::bit_cast<std::int16_t>(value.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadInt32 */
    CanonicalCodecResult<std::int32_t> CanonicalValueReader::ReadInt32() {
        auto value = ReadUInt32();
        return value.HasError() ? CanonicalCodecResult<std::int32_t>::Failure(value.ErrorValue())
                                : CanonicalCodecResult<std::int32_t>::Success(std::bit_cast<std::int32_t>(value.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadInt64 */
    CanonicalCodecResult<std::int64_t> CanonicalValueReader::ReadInt64() {
        auto value = ReadUInt64();
        return value.HasError() ? CanonicalCodecResult<std::int64_t>::Failure(value.ErrorValue())
                                : CanonicalCodecResult<std::int64_t>::Success(std::bit_cast<std::int64_t>(value.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadFloat32 */
    CanonicalCodecResult<float> CanonicalValueReader::ReadFloat32() {
        auto bits = ReadUInt32();
        if (bits.HasError())
            return CanonicalCodecResult<float>::Failure(bits.ErrorValue());
        const float value = std::bit_cast<float>(bits.Value());
        if (!std::isfinite(value))
            return CanonicalCodecResult<float>::Failure(Failure(SaveErrors::CanonicalCodecNonFinite));
        if (bits.Value() == 0x80000000U)
            return CanonicalCodecResult<float>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        return CanonicalCodecResult<float>::Success(value);
    }

    /** @copydoc CanonicalValueReader::ReadFloat64 */
    CanonicalCodecResult<double> CanonicalValueReader::ReadFloat64() {
        auto bits = ReadUInt64();
        if (bits.HasError())
            return CanonicalCodecResult<double>::Failure(bits.ErrorValue());
        const double value = std::bit_cast<double>(bits.Value());
        if (!std::isfinite(value))
            return CanonicalCodecResult<double>::Failure(Failure(SaveErrors::CanonicalCodecNonFinite));
        if (bits.Value() == 0x8000000000000000ULL)
            return CanonicalCodecResult<double>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        return CanonicalCodecResult<double>::Success(value);
    }

    /** @copydoc CanonicalValueReader::ReadBytes */
    CanonicalCodecResult<std::vector<std::byte>> CanonicalValueReader::ReadBytes() {
        auto length = ReadLength(limits_.maximumBytes);
        if (length.HasError())
            return CanonicalCodecResult<std::vector<std::byte>>::Failure(length.ErrorValue());
        auto bytes = Read(length.Value());
        if (bytes.HasError())
            return CanonicalCodecResult<std::vector<std::byte>>::Failure(bytes.ErrorValue());
        return CanonicalCodecResult<std::vector<std::byte>>::Success({bytes.Value().begin(), bytes.Value().end()});
    }

    /** @copydoc CanonicalValueReader::ReadUtf8 */
    CanonicalCodecResult<std::string> CanonicalValueReader::ReadUtf8() {
        auto length = ReadLength(limits_.maximumStringBytes);
        if (length.HasError())
            return CanonicalCodecResult<std::string>::Failure(length.ErrorValue());
        auto bytes = Read(length.Value());
        if (bytes.HasError())
            return CanonicalCodecResult<std::string>::Failure(bytes.ErrorValue());
        std::string value{reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size()};
        if (!IsValidUtf8(value))
            return CanonicalCodecResult<std::string>::Failure(Failure(SaveErrors::CanonicalCodecUtf8Invalid));
        return CanonicalCodecResult<std::string>::Success(std::move(value));
    }

    /** @copydoc CanonicalValueReader::ReadVec2 */
    CanonicalCodecResult<Math::Vec2> CanonicalValueReader::ReadVec2() {
        auto x = ReadFloat32();
        if (x.HasError())
            return CanonicalCodecResult<Math::Vec2>::Failure(x.ErrorValue());
        auto y = ReadFloat32();
        return y.HasError() ? CanonicalCodecResult<Math::Vec2>::Failure(y.ErrorValue())
                            : CanonicalCodecResult<Math::Vec2>::Success({x.Value(), y.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadVec3 */
    CanonicalCodecResult<Math::Vec3> CanonicalValueReader::ReadVec3() {
        auto xy = ReadVec2();
        if (xy.HasError())
            return CanonicalCodecResult<Math::Vec3>::Failure(xy.ErrorValue());
        auto z = ReadFloat32();
        return z.HasError() ? CanonicalCodecResult<Math::Vec3>::Failure(z.ErrorValue())
                            : CanonicalCodecResult<Math::Vec3>::Success({xy.Value().x, xy.Value().y, z.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadVec4 */
    CanonicalCodecResult<Math::Vec4> CanonicalValueReader::ReadVec4() {
        auto xyz = ReadVec3();
        if (xyz.HasError())
            return CanonicalCodecResult<Math::Vec4>::Failure(xyz.ErrorValue());
        auto w = ReadFloat32();
        return w.HasError() ? CanonicalCodecResult<Math::Vec4>::Failure(w.ErrorValue())
                            : CanonicalCodecResult<Math::Vec4>::Success({xyz.Value().x, xyz.Value().y, xyz.Value().z, w.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadQuaternion */
    CanonicalCodecResult<Math::Quaternion> CanonicalValueReader::ReadQuaternion() {
        auto value = ReadVec4();
        return value.HasError()
                   ? CanonicalCodecResult<Math::Quaternion>::Failure(value.ErrorValue())
                   : CanonicalCodecResult<Math::Quaternion>::Success({value.Value().x, value.Value().y, value.Value().z, value.Value().w});
    }

    /** @copydoc CanonicalValueReader::ReadSequenceSize */
    CanonicalCodecResult<std::size_t> CanonicalValueReader::ReadSequenceSize() {
        return ReadLength(limits_.maximumCollectionElements);
    }

    /** @copydoc CanonicalValueReader::ReadOptionalPresence */
    CanonicalCodecResult<bool> CanonicalValueReader::ReadOptionalPresence() {
        return ReadBool();
    }

    /** @copydoc CanonicalValueReader::ReadVariantIndex */
    CanonicalCodecResult<std::uint32_t> CanonicalValueReader::ReadVariantIndex(const std::uint32_t alternativeCount) {
        if (alternativeCount == 0)
            return CanonicalCodecResult<std::uint32_t>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        auto index = ReadUInt32();
        if (index.HasError())
            return index;
        if (index.Value() >= alternativeCount)
            return CanonicalCodecResult<std::uint32_t>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        return index;
    }

    /** @copydoc CanonicalValueReader::ReadMap */
    CanonicalCodecResult<std::vector<CanonicalMapEntry>> CanonicalValueReader::ReadMap() {
        auto count = ReadSequenceSize();
        if (count.HasError())
            return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Failure(count.ErrorValue());
        constexpr std::size_t kMinimumEncodedEntryBytes = 8;
        if (count.Value() > (bytes_.size() - offset_) / kMinimumEncodedEntryBytes)
            return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        std::vector<CanonicalMapEntry> entries;
        entries.reserve(count.Value());
        for (std::size_t index = 0; index < count.Value(); ++index) {
            auto key = ReadBytes();
            if (key.HasError())
                return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Failure(key.ErrorValue());
            auto value = ReadBytes();
            if (value.HasError())
                return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Failure(value.ErrorValue());
            if (!entries.empty() && !ByteLess(entries.back().key, key.Value()))
                return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Failure(Failure(
                    SameBytes(entries.back().key, key.Value()) ? SaveErrors::CanonicalCodecDuplicate : SaveErrors::CanonicalCodecInvalid));
            entries.push_back({std::move(key).Value(), std::move(value).Value()});
        }
        return CanonicalCodecResult<std::vector<CanonicalMapEntry>>::Success(std::move(entries));
    }

    /** @copydoc CanonicalValueReader::ReadSet */
    CanonicalCodecResult<std::vector<std::vector<std::byte>>> CanonicalValueReader::ReadSet() {
        auto count = ReadSequenceSize();
        if (count.HasError())
            return CanonicalCodecResult<std::vector<std::vector<std::byte>>>::Failure(count.ErrorValue());
        constexpr std::size_t kMinimumEncodedElementBytes = 4;
        if (count.Value() > (bytes_.size() - offset_) / kMinimumEncodedElementBytes)
            return CanonicalCodecResult<std::vector<std::vector<std::byte>>>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        std::vector<std::vector<std::byte>> elements;
        elements.reserve(count.Value());
        for (std::size_t index = 0; index < count.Value(); ++index) {
            auto element = ReadBytes();
            if (element.HasError())
                return CanonicalCodecResult<std::vector<std::vector<std::byte>>>::Failure(element.ErrorValue());
            if (!elements.empty() && !ByteLess(elements.back(), element.Value()))
                return CanonicalCodecResult<std::vector<std::vector<std::byte>>>::Failure(Failure(
                    SameBytes(elements.back(), element.Value()) ? SaveErrors::CanonicalCodecDuplicate : SaveErrors::CanonicalCodecInvalid));
            elements.push_back(std::move(element).Value());
        }
        return CanonicalCodecResult<std::vector<std::vector<std::byte>>>::Success(std::move(elements));
    }

    /** @copydoc CanonicalValueReader::ReadRecord */
    CanonicalCodecResult<std::vector<CanonicalRecordField>> CanonicalValueReader::ReadRecord() {
        auto count = ReadLength(limits_.maximumFields);
        if (count.HasError())
            return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(count.ErrorValue());
        constexpr std::size_t kMinimumEncodedFieldBytes = 8;
        if (count.Value() > (bytes_.size() - offset_) / kMinimumEncodedFieldBytes)
            return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        std::vector<CanonicalRecordField> fields;
        fields.reserve(count.Value());
        for (std::size_t index = 0; index < count.Value(); ++index) {
            auto idValue = ReadUInt32();
            if (idValue.HasError())
                return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(idValue.ErrorValue());
            auto id = CanonicalFieldId::Create(idValue.Value());
            if (id.HasError())
                return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
            auto value = ReadBytes();
            if (value.HasError())
                return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(value.ErrorValue());
            if (!fields.empty() && fields.back().id >= id.Value())
                return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Failure(
                    Failure(fields.back().id == id.Value() ? SaveErrors::CanonicalCodecDuplicate : SaveErrors::CanonicalCodecInvalid));
            fields.push_back({std::move(id).Value(), std::move(value).Value()});
        }
        return CanonicalCodecResult<std::vector<CanonicalRecordField>>::Success(std::move(fields));
    }

    /** @copydoc CanonicalValueReader::RequireFinished */
    CanonicalCodecResult<void> CanonicalValueReader::RequireFinished() const {
        if (!HasValidLimits(limits_) || bytes_.size() > limits_.maximumBytes || offset_ != bytes_.size())
            return CanonicalCodecResult<void>::Failure(Failure(SaveErrors::CanonicalCodecInvalid));
        return CanonicalCodecResult<void>::Success();
    }
}  // namespace Horo::Runtime
