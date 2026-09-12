#include "Horo/Foundation/Utf8.h"
#include "Horo/Runtime/Save/SaveCanonicalCodec.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveCanonicalCodecInternal.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <ranges>
#include <type_traits>

namespace Horo::Runtime {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
                  "Canonical binary32 requires a 32-bit IEC 559 float.");
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
                  "Canonical binary64 requires a 64-bit IEC 559 double.");

    template <typename Unsigned> Result<void> CanonicalValueWriter::WriteUnsigned(const Unsigned value) {
        return Append(CanonicalCodecDetail::ToLittleEndian(value));
    }

    template <typename Signed> Result<void> CanonicalValueWriter::WriteSigned(const Signed value) {
        return WriteUnsigned(std::bit_cast<std::make_unsigned_t<Signed>>(value));
    }

    template <typename Float, typename Unsigned> Result<void> CanonicalValueWriter::WriteFloating(Float value) {
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (!std::isfinite(value))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecNonFinite));
        if (value == 0)
            value = 0;
        return WriteUnsigned(std::bit_cast<Unsigned>(value));
    }

    /** @copydoc CanonicalValueWriter::WriteBool */
    Result<void> CanonicalValueWriter::WriteBool(const bool value) {
        return WriteUInt8(value ? 1 : 0);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt8 */
    Result<void> CanonicalValueWriter::WriteUInt8(const std::uint8_t value) {
        return Append(std::as_bytes(std::span{&value, 1}));
    }

    /** @copydoc CanonicalValueWriter::WriteUInt16 */
    Result<void> CanonicalValueWriter::WriteUInt16(const std::uint16_t value) {
        return WriteUnsigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt32 */
    Result<void> CanonicalValueWriter::WriteUInt32(const std::uint32_t value) {
        return WriteUnsigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt64 */
    Result<void> CanonicalValueWriter::WriteUInt64(const std::uint64_t value) {
        return WriteUnsigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteInt8 */
    Result<void> CanonicalValueWriter::WriteInt8(const std::int8_t value) {
        return WriteSigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteInt16 */
    Result<void> CanonicalValueWriter::WriteInt16(const std::int16_t value) {
        return WriteSigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteInt32 */
    Result<void> CanonicalValueWriter::WriteInt32(const std::int32_t value) {
        return WriteSigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteInt64 */
    Result<void> CanonicalValueWriter::WriteInt64(const std::int64_t value) {
        return WriteSigned(value);
    }

    /** @copydoc CanonicalValueWriter::WriteFloat32 */
    Result<void> CanonicalValueWriter::WriteFloat32(float value) {
        return WriteFloating<float, std::uint32_t>(value);
    }

    /** @copydoc CanonicalValueWriter::WriteFloat64 */
    Result<void> CanonicalValueWriter::WriteFloat64(double value) {
        return WriteFloating<double, std::uint64_t>(value);
    }

    /** @copydoc CanonicalValueWriter::WriteUtf8 */
    Result<void> CanonicalValueWriter::WriteUtf8(const std::string_view value) {
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (value.size() > limits_.maximumStringBytes)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        if (!IsValidUtf8ScalarSequence(value))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecUtf8Invalid));
        return AppendLengthDelimited(std::as_bytes(std::span{value.data(), value.size()}));
    }

    /** @copydoc CanonicalValueWriter::WriteBytes */
    Result<void> CanonicalValueWriter::WriteBytes(const std::span<const std::byte> value) {
        return AppendLengthDelimited(value);
    }

    Result<void> CanonicalValueWriter::WriteFloatComponents(const std::span<const float> components) {
        for (const float component : components) {
            auto written = WriteFloat32(component);
            if (written.HasError())
                return written;
        }
        return Result<void>::Success();
    }

    /** @copydoc CanonicalValueWriter::WriteVec2 */
    Result<void> CanonicalValueWriter::WriteVec2(const Math::Vec2 value) {
        const std::array components{value.x, value.y};
        return WriteFloatComponents(components);
    }

    /** @copydoc CanonicalValueWriter::WriteVec3 */
    Result<void> CanonicalValueWriter::WriteVec3(const Math::Vec3 value) {
        const std::array components{value.x, value.y, value.z};
        return WriteFloatComponents(components);
    }

    /** @copydoc CanonicalValueWriter::WriteVec4 */
    Result<void> CanonicalValueWriter::WriteVec4(const Math::Vec4 value) {
        const std::array components{value.x, value.y, value.z, value.w};
        return WriteFloatComponents(components);
    }

    /** @copydoc CanonicalValueWriter::WriteQuaternion */
    Result<void> CanonicalValueWriter::WriteQuaternion(const Math::Quaternion value) {
        const std::array components{value.x, value.y, value.z, value.w};
        return WriteFloatComponents(components);
    }

    template <typename Unsigned> Result<Unsigned> CanonicalValueReader::ReadUnsigned() {
        auto encoded = ReadExactBytes(sizeof(Unsigned));
        if (encoded.HasError())
            return Result<Unsigned>::Failure(encoded.ErrorValue());
        std::array<std::byte, sizeof(Unsigned)> bytes{};
        std::ranges::copy(encoded.Value(), bytes.begin());
        return Result<Unsigned>::Success(CanonicalCodecDetail::FromLittleEndian<Unsigned>(bytes));
    }

    template <typename Signed> Result<Signed> CanonicalValueReader::ReadSigned() {
        auto decoded = ReadUnsigned<std::make_unsigned_t<Signed>>();
        return decoded.HasError() ? Result<Signed>::Failure(decoded.ErrorValue())
                                  : Result<Signed>::Success(std::bit_cast<Signed>(decoded.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt8 */
    Result<std::uint8_t> CanonicalValueReader::ReadUInt8() {
        auto encoded = ReadExactBytes(1);
        return encoded.HasError() ? Result<std::uint8_t>::Failure(encoded.ErrorValue())
                                  : Result<std::uint8_t>::Success(std::to_integer<std::uint8_t>(encoded.Value().front()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt16 */
    Result<std::uint16_t> CanonicalValueReader::ReadUInt16() {
        return ReadUnsigned<std::uint16_t>();
    }

    /** @copydoc CanonicalValueReader::ReadUInt32 */
    Result<std::uint32_t> CanonicalValueReader::ReadUInt32() {
        return ReadUnsigned<std::uint32_t>();
    }

    /** @copydoc CanonicalValueReader::ReadUInt64 */
    Result<std::uint64_t> CanonicalValueReader::ReadUInt64() {
        return ReadUnsigned<std::uint64_t>();
    }

    /** @copydoc CanonicalValueReader::ReadInt8 */
    Result<std::int8_t> CanonicalValueReader::ReadInt8() {
        return ReadSigned<std::int8_t>();
    }

    /** @copydoc CanonicalValueReader::ReadInt16 */
    Result<std::int16_t> CanonicalValueReader::ReadInt16() {
        return ReadSigned<std::int16_t>();
    }

    /** @copydoc CanonicalValueReader::ReadInt32 */
    Result<std::int32_t> CanonicalValueReader::ReadInt32() {
        return ReadSigned<std::int32_t>();
    }

    /** @copydoc CanonicalValueReader::ReadInt64 */
    Result<std::int64_t> CanonicalValueReader::ReadInt64() {
        return ReadSigned<std::int64_t>();
    }

    /** @copydoc CanonicalValueReader::ReadBool */
    Result<bool> CanonicalValueReader::ReadBool() {
        auto value = ReadUInt8();
        if (value.HasError())
            return Result<bool>::Failure(value.ErrorValue());
        return value.Value() < 2 ? Result<bool>::Success(value.Value() == 1)
                                 : Result<bool>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }

    /** @copydoc CanonicalValueReader::ReadFloat32 */
    Result<float> CanonicalValueReader::ReadFloat32() {
        auto bits = ReadUInt32();
        if (bits.HasError())
            return Result<float>::Failure(bits.ErrorValue());
        const auto value = std::bit_cast<float>(bits.Value());
        return std::isfinite(value) && bits.Value() != 0x80000000U ? Result<float>::Success(value)
                                                                   : Result<float>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }

    /** @copydoc CanonicalValueReader::ReadFloat64 */
    Result<double> CanonicalValueReader::ReadFloat64() {
        auto bits = ReadUInt64();
        if (bits.HasError())
            return Result<double>::Failure(bits.ErrorValue());
        const auto value = std::bit_cast<double>(bits.Value());
        return std::isfinite(value) && bits.Value() != 0x8000000000000000ULL
                   ? Result<double>::Success(value)
                   : Result<double>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }

    /** @copydoc CanonicalValueReader::ReadBytes */
    Result<std::vector<std::byte>> CanonicalValueReader::ReadBytes(const std::size_t maximumBytes) {
        auto size = ReadLength(maximumBytes);
        if (size.HasError())
            return Result<std::vector<std::byte>>::Failure(size.ErrorValue());
        auto encoded = ReadExactBytes(size.Value());
        if (encoded.HasError())
            return Result<std::vector<std::byte>>::Failure(encoded.ErrorValue());
        if (auto charged = Charge(size.Value()); charged.HasError())
            return Result<std::vector<std::byte>>::Failure(charged.ErrorValue());
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            return Result<std::vector<std::byte>>::Success({encoded.Value().begin(), encoded.Value().end()});
        } catch (const std::bad_alloc &) {
            return Result<std::vector<std::byte>>::Failure(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueReader::ReadUtf8 */
    Result<std::string> CanonicalValueReader::ReadUtf8() {
        auto bytes = ReadBytes(limits_.maximumStringBytes);
        if (bytes.HasError())
            return Result<std::string>::Failure(std::move(bytes).ErrorValue());
        if (auto charged = Charge(bytes.Value().size()); charged.HasError())
            return Result<std::string>::Failure(charged.ErrorValue());
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            std::string value{reinterpret_cast<const char *>(bytes.Value().data()),  // NOSONAR: text construction requires a char view.
                              bytes.Value().size()};
            return IsValidUtf8ScalarSequence(value) ? Result<std::string>::Success(std::move(value))
                                                    : Result<std::string>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        } catch (const std::bad_alloc &) {
            return Result<std::string>::Failure(std::move(allocationFailure));
        }
    }

    Result<void> CanonicalValueReader::ReadFloatComponents(const std::span<float> components) {
        for (float &component : components) {
            auto decoded = ReadFloat32();
            if (decoded.HasError())
                return Result<void>::Failure(decoded.ErrorValue());
            component = decoded.Value();
        }
        return Result<void>::Success();
    }

    /** @copydoc CanonicalValueReader::ReadVec2 */
    Result<Math::Vec2> CanonicalValueReader::ReadVec2() {
        std::array<float, 2> values{};
        const auto decoded = ReadFloatComponents(values);
        return decoded.HasError() ? Result<Math::Vec2>::Failure(decoded.ErrorValue()) : Result<Math::Vec2>::Success({values[0], values[1]});
    }

    /** @copydoc CanonicalValueReader::ReadVec3 */
    Result<Math::Vec3> CanonicalValueReader::ReadVec3() {
        std::array<float, 3> values{};
        const auto decoded = ReadFloatComponents(values);
        return decoded.HasError() ? Result<Math::Vec3>::Failure(decoded.ErrorValue())
                                  : Result<Math::Vec3>::Success({values[0], values[1], values[2]});
    }

    /** @copydoc CanonicalValueReader::ReadVec4 */
    Result<Math::Vec4> CanonicalValueReader::ReadVec4() {
        std::array<float, 4> values{};
        const auto decoded = ReadFloatComponents(values);
        return decoded.HasError() ? Result<Math::Vec4>::Failure(decoded.ErrorValue())
                                  : Result<Math::Vec4>::Success({values[0], values[1], values[2], values[3]});
    }

    /** @copydoc CanonicalValueReader::ReadQuaternion */
    Result<Math::Quaternion> CanonicalValueReader::ReadQuaternion() {
        std::array<float, 4> values{};
        const auto decoded = ReadFloatComponents(values);
        return decoded.HasError() ? Result<Math::Quaternion>::Failure(decoded.ErrorValue())
                                  : Result<Math::Quaternion>::Success({values[0], values[1], values[2], values[3]});
    }
}  // namespace Horo::Runtime
