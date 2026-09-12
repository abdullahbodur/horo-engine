#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include "Horo/Foundation/Utf8.h"
#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <ranges>
#include <type_traits>

namespace Horo::Runtime {
    struct CanonicalReadState final {
        std::size_t decodedBytes{};
    };

    namespace {
        bool ValidLimits(const CanonicalCodecLimits &v) noexcept {
            return v.maximumBytes && v.maximumDecodedBytes && v.maximumStringBytes && v.maximumCollectionElements && v.maximumFields &&
                   v.maximumNestingDepth && v.maximumStringBytes <= v.maximumBytes &&
                   v.maximumCollectionElements <= std::numeric_limits<std::uint32_t>::max() &&
                   v.maximumFields <= std::numeric_limits<std::uint32_t>::max();
        }

        template <typename U> std::array<std::byte, sizeof(U)> Little(const U value) noexcept {
            static_assert(std::is_unsigned_v<U>);
            std::array<std::byte, sizeof(U)> out{};
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = static_cast<std::byte>(value >> (i * 8U));
            return out;
        }

        template <typename U> U FromLittle(const std::array<std::byte, sizeof(U)> &bytes) noexcept {
            U out{};
            for (std::size_t i = 0; i < bytes.size(); ++i)
                out |= static_cast<U>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8U);
            return out;
        }

        bool Less(const std::span<const std::byte> a, const std::span<const std::byte> b) noexcept {
            return std::ranges::lexicographical_compare(a, b);
        }

        bool Same(const std::span<const std::byte> a, const std::span<const std::byte> b) noexcept {
            return std::ranges::equal(a, b);
        }

        std::size_t MaxDepth(const std::span<const CanonicalEncodedValue> values) noexcept {
            std::size_t depth{};
            for (const auto &value : values)
                depth = std::max(depth, value.StructuralDepth());
            return depth;
        }
    }  // namespace

    /** @copydoc CanonicalFieldId::Create */
    Result<CanonicalFieldId> CanonicalFieldId::Create(const ValueType value) {
        return value ? Result<CanonicalFieldId>::Success(CanonicalFieldId{value})
                     : Result<CanonicalFieldId>::Failure(MakeError(SaveErrors::CanonicalCodecInvalid));
    }

    /** @copydoc CanonicalValueWriter::CanonicalValueWriter */
    CanonicalValueWriter::CanonicalValueWriter(const CanonicalCodecLimits limits) : limits_(limits) {}

    CanonicalValueWriter::CanonicalValueWriter(const CanonicalCodecLimits limits, std::vector<CanonicalFieldId> path)
        : limits_(limits), path_(std::move(path)) {}

    /** @copydoc CanonicalValueWriter::ForField */
    CanonicalValueWriter CanonicalValueWriter::ForField(const CanonicalFieldId field) const {
        auto path = path_;
        path.push_back(field);
        return CanonicalValueWriter{limits_, std::move(path)};
    }

    Error CanonicalValueWriter::ErrorAt(const ErrorCodeDescriptor &descriptor) const {
        Error error = MakeError(descriptor);
        std::string source{"canonical"};
        for (const auto field : path_)
            source += "/field:" + std::to_string(field.Value());
        error.diagnostics.push_back(
            {DiagnosticCode{"save.canonical_codec.location"},
             DiagnosticSeverity::Error,
             std::string{descriptor.summary},
             {std::move(source), 0,
              static_cast<std::uint32_t>(std::min(bytes_.size(), static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())))}});
        return error;
    }

    Result<void> CanonicalValueWriter::Fail(Error error) {
        if (!failure_)
            failure_ = std::move(error);
        return Result<void>::Failure(*failure_);
    }

    Result<void> CanonicalValueWriter::Append(const std::span<const std::byte> value) {
        if (failure_)
            return Result<void>::Failure(*failure_);
        if (!ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (value.size() > limits_.maximumBytes || bytes_.size() > limits_.maximumBytes - value.size())
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        try {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
        return Result<void>::Success();
    }

    template <typename U> Result<void> CanonicalValueWriter::WriteUnsigned(const U value) {
        const auto bytes = Little(value);
        return Append(bytes);
    }

    template <typename S> Result<void> CanonicalValueWriter::WriteSigned(const S value) {
        return WriteUnsigned(std::bit_cast<std::make_unsigned_t<S>>(value));
    }

    Result<void> CanonicalValueWriter::AppendLengthDelimited(const std::span<const std::byte> value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max() || limits_.maximumBytes < sizeof(std::uint32_t) ||
            value.size() > limits_.maximumBytes - sizeof(std::uint32_t) ||
            bytes_.size() > limits_.maximumBytes - sizeof(std::uint32_t) - value.size())
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto result = WriteUInt32(static_cast<std::uint32_t>(value.size()));
        return result.HasError() ? result : Append(value);
    }

    Result<void> CanonicalValueWriter::AdmitComposite(const std::size_t childDepth) {
        if (childDepth >= limits_.maximumNestingDepth)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        structuralDepth_ = std::max(structuralDepth_, childDepth + 1);
        return Result<void>::Success();
    }

    /** @copydoc CanonicalValueWriter::WriteBool */
    Result<void> CanonicalValueWriter::WriteBool(bool v) {
        return WriteUInt8(v ? 1 : 0);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt8 */
    Result<void> CanonicalValueWriter::WriteUInt8(std::uint8_t v) {
        return Append(std::as_bytes(std::span{&v, 1}));
    }

    /** @copydoc CanonicalValueWriter::WriteUInt16 */
    Result<void> CanonicalValueWriter::WriteUInt16(std::uint16_t v) {
        return WriteUnsigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt32 */
    Result<void> CanonicalValueWriter::WriteUInt32(std::uint32_t v) {
        return WriteUnsigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteUInt64 */
    Result<void> CanonicalValueWriter::WriteUInt64(std::uint64_t v) {
        return WriteUnsigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteInt8 */
    Result<void> CanonicalValueWriter::WriteInt8(std::int8_t v) {
        return WriteSigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteInt16 */
    Result<void> CanonicalValueWriter::WriteInt16(std::int16_t v) {
        return WriteSigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteInt32 */
    Result<void> CanonicalValueWriter::WriteInt32(std::int32_t v) {
        return WriteSigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteInt64 */
    Result<void> CanonicalValueWriter::WriteInt64(std::int64_t v) {
        return WriteSigned(v);
    }

    /** @copydoc CanonicalValueWriter::WriteFloat32 */
    Result<void> CanonicalValueWriter::WriteFloat32(float v) {
        if (!std::isfinite(v))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecNonFinite));
        if (v == 0)
            v = 0;
        return WriteUInt32(std::bit_cast<std::uint32_t>(v));
    }

    /** @copydoc CanonicalValueWriter::WriteFloat64 */
    Result<void> CanonicalValueWriter::WriteFloat64(double v) {
        if (!std::isfinite(v))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecNonFinite));
        if (v == 0)
            v = 0;
        return WriteUInt64(std::bit_cast<std::uint64_t>(v));
    }

    /** @copydoc CanonicalValueWriter::WriteUtf8 */
    Result<void> CanonicalValueWriter::WriteUtf8(const std::string_view v) {
        if (v.size() > limits_.maximumStringBytes)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        if (!IsValidUtf8ScalarSequence(v))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecUtf8Invalid));
        return AppendLengthDelimited(std::as_bytes(std::span{v.data(), v.size()}));
    }

    /** @copydoc CanonicalValueWriter::WriteBytes */
    Result<void> CanonicalValueWriter::WriteBytes(const std::span<const std::byte> v) {
        return AppendLengthDelimited(v);
    }

    /** @copydoc CanonicalValueWriter::WriteVec2 */
    Result<void> CanonicalValueWriter::WriteVec2(Math::Vec2 v) {
        auto r = WriteFloat32(v.x);
        return r.HasError() ? r : WriteFloat32(v.y);
    }

    /** @copydoc CanonicalValueWriter::WriteVec3 */
    Result<void> CanonicalValueWriter::WriteVec3(Math::Vec3 v) {
        auto r = WriteVec2({v.x, v.y});
        return r.HasError() ? r : WriteFloat32(v.z);
    }

    /** @copydoc CanonicalValueWriter::WriteVec4 */
    Result<void> CanonicalValueWriter::WriteVec4(Math::Vec4 v) {
        auto r = WriteVec3({v.x, v.y, v.z});
        return r.HasError() ? r : WriteFloat32(v.w);
    }

    /** @copydoc CanonicalValueWriter::WriteQuaternion */
    Result<void> CanonicalValueWriter::WriteQuaternion(Math::Quaternion v) {
        return WriteVec4({v.x, v.y, v.z, v.w});
    }

    /** @copydoc CanonicalValueWriter::WriteSequence */
    Result<void> CanonicalValueWriter::WriteSequence(const std::span<const CanonicalEncodedValue> values) {
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto depth = AdmitComposite(MaxDepth(values));
        if (depth.HasError())
            return depth;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteUInt32(static_cast<std::uint32_t>(values.size()));
            for (const auto &v : values) {
                if (r.HasError())
                    break;
                r = staging.AppendLengthDelimited(v.Bytes());
            }
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto sealed = std::move(staging).Finalize();
            return sealed.HasError() ? Fail(sealed.ErrorValue()) : Append(sealed.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteOptional */
    Result<void> CanonicalValueWriter::WriteOptional(const std::optional<CanonicalEncodedValue> &value) {
        auto d = AdmitComposite(value ? value->StructuralDepth() : 0);
        if (d.HasError())
            return d;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteBool(value.has_value());
            if (r.HasValue() && value)
                r = staging.AppendLengthDelimited(value->Bytes());
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto s = std::move(staging).Finalize();
            return s.HasError() ? Fail(s.ErrorValue()) : Append(s.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteVariant */
    Result<void> CanonicalValueWriter::WriteVariant(std::uint32_t index, std::uint32_t count, const CanonicalEncodedValue &value) {
        if (!count || index >= count)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecInvalid));
        auto d = AdmitComposite(value.StructuralDepth());
        if (d.HasError())
            return d;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteUInt32(index);
            if (r.HasValue())
                r = staging.AppendLengthDelimited(value.Bytes());
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto s = std::move(staging).Finalize();
            return s.HasError() ? Fail(s.ErrorValue()) : Append(s.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteMap */
    Result<void> CanonicalValueWriter::WriteMap(const std::span<const CanonicalMapEntry> entries) {
        if (entries.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        std::size_t child{};
        for (const auto &e : entries)
            child = std::max({child, e.key.StructuralDepth(), e.value.StructuralDepth()});
        auto d = AdmitComposite(child);
        if (d.HasError())
            return d;
        try {
            std::vector<const CanonicalMapEntry *> order;
            order.reserve(entries.size());
            for (const auto &e : entries)
                order.push_back(&e);
            std::ranges::sort(order, [](const auto *left, const auto *right) {
                return Less(left->key.Bytes(), right->key.Bytes());
            });
            for (std::size_t i = 1; i < order.size(); ++i)
                if (Same(order[i - 1]->key.Bytes(), order[i]->key.Bytes()))
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteUInt32(static_cast<std::uint32_t>(order.size()));
            for (const auto *p : order) {
                if (r.HasError())
                    break;
                r = staging.AppendLengthDelimited(p->key.Bytes());
                if (r.HasValue())
                    r = staging.AppendLengthDelimited(p->value.Bytes());
            }
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto s = std::move(staging).Finalize();
            return s.HasError() ? Fail(s.ErrorValue()) : Append(s.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteSet */
    Result<void> CanonicalValueWriter::WriteSet(const std::span<const CanonicalEncodedValue> values) {
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto d = AdmitComposite(MaxDepth(values));
        if (d.HasError())
            return d;
        try {
            std::vector<const CanonicalEncodedValue *> order;
            order.reserve(values.size());
            for (const auto &v : values)
                order.push_back(&v);
            std::ranges::sort(order, [](const auto *left, const auto *right) {
                return Less(left->Bytes(), right->Bytes());
            });
            for (std::size_t i = 1; i < order.size(); ++i)
                if (Same(order[i - 1]->Bytes(), order[i]->Bytes()))
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteUInt32(static_cast<std::uint32_t>(order.size()));
            for (const auto *p : order) {
                if (r.HasError())
                    break;
                r = staging.AppendLengthDelimited(p->Bytes());
            }
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto s = std::move(staging).Finalize();
            return s.HasError() ? Fail(s.ErrorValue()) : Append(s.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteRecord */
    Result<void> CanonicalValueWriter::WriteRecord(const std::span<const CanonicalRecordField> fields) {
        if (fields.size() > limits_.maximumFields)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        std::size_t child{};
        for (const auto &f : fields)
            child = std::max(child, f.value.StructuralDepth());
        auto d = AdmitComposite(child);
        if (d.HasError())
            return d;
        try {
            std::vector<const CanonicalRecordField *> order;
            order.reserve(fields.size());
            for (const auto &f : fields)
                order.push_back(&f);
            std::ranges::sort(order, [](const auto *left, const auto *right) {
                return left->id < right->id;
            });
            for (std::size_t i = 1; i < order.size(); ++i)
                if (order[i - 1]->id == order[i]->id)
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto r = staging.WriteUInt32(static_cast<std::uint32_t>(order.size()));
            for (const auto *p : order) {
                if (r.HasError())
                    break;
                r = staging.WriteUInt32(p->id.Value());
                if (r.HasValue())
                    r = staging.AppendLengthDelimited(p->value.Bytes());
            }
            if (r.HasError())
                return Fail(r.ErrorValue());
            auto s = std::move(staging).Finalize();
            return s.HasError() ? Fail(s.ErrorValue()) : Append(s.Value().Bytes());
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::Finalize */
    Result<CanonicalEncodedValue> CanonicalValueWriter::Finalize() && {
        if (failure_)
            return Result<CanonicalEncodedValue>::Failure(*failure_);
        if (!ValidLimits(limits_))
            return Result<CanonicalEncodedValue>::Failure(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        return Result<CanonicalEncodedValue>::Success(CanonicalEncodedValue{std::move(bytes_), structuralDepth_});
    }

    CanonicalValueReader::CanonicalValueReader(std::span<const std::byte> b, CanonicalCodecLimits l, std::shared_ptr<CanonicalReadState> s,
                                               std::size_t d, std::vector<CanonicalFieldId> p)
        : bytes_(b), limits_(l), state_(std::move(s)), depth_(d), path_(std::move(p)) {}

    /** @copydoc CanonicalValueReader::Create */
    Result<CanonicalValueReader> CanonicalValueReader::Create(std::span<const std::byte> b, CanonicalCodecLimits l) {
        if (!ValidLimits(l))
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (b.size() > l.maximumBytes)
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecLimitExceeded));
        try {
            return Result<CanonicalValueReader>::Success(CanonicalValueReader{b, l, std::make_shared<CanonicalReadState>(), 0, {}});
        } catch (const std::bad_alloc &) {
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalDecodedValue::OpenReader */
    Result<CanonicalValueReader> CanonicalDecodedValue::OpenReader() const {
        try {
            return Result<CanonicalValueReader>::Success(CanonicalValueReader{bytes_, limits_, state_, depth_, path_});
        } catch (const std::bad_alloc &) {
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    Error CanonicalValueReader::ErrorAt(const ErrorCodeDescriptor &d) const {
        Error e = MakeError(d);
        std::string s{"canonical"};
        for (auto f : path_)
            s += "/field:" + std::to_string(f.Value());
        e.diagnostics.push_back(
            {DiagnosticCode{"save.canonical_codec.location"},
             DiagnosticSeverity::Error,
             std::string{d.summary},
             {std::move(s), 0,
              static_cast<std::uint32_t>(std::min(offset_, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())))}});
        return e;
    }

    Result<void> CanonicalValueReader::Charge(std::size_t n) {
        if (state_->decodedBytes > limits_.maximumDecodedBytes || n > limits_.maximumDecodedBytes - state_->decodedBytes)
            return Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        state_->decodedBytes += n;
        return Result<void>::Success();
    }

    Result<void> CanonicalValueReader::ChargeElements(const std::size_t count, const std::size_t elementSize) {
        if (elementSize != 0 && count > std::numeric_limits<std::size_t>::max() / elementSize)
            return Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        return Charge(count * elementSize);
    }

    Result<void> CanonicalValueReader::AdmitComposite() const {
        return depth_ < limits_.maximumNestingDepth ? Result<void>::Success()
                                                    : Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
    }

    /** @copydoc CanonicalValueReader::ReadExactBytes */
    Result<std::span<const std::byte>> CanonicalValueReader::ReadExactBytes(std::size_t n) {
        if (n > bytes_.size() - std::min(offset_, bytes_.size()))
            return Result<std::span<const std::byte>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        auto v = bytes_.subspan(offset_, n);
        offset_ += n;
        return Result<std::span<const std::byte>>::Success(v);
    }

    template <typename U> Result<U> CanonicalValueReader::ReadUnsigned() {
        auto v = ReadExactBytes(sizeof(U));
        if (v.HasError())
            return Result<U>::Failure(v.ErrorValue());
        std::array<std::byte, sizeof(U)> a{};
        std::ranges::copy(v.Value(), a.begin());
        return Result<U>::Success(FromLittle<U>(a));
    }

    template <typename S> Result<S> CanonicalValueReader::ReadSigned() {
        auto v = ReadUnsigned<std::make_unsigned_t<S>>();
        return v.HasError() ? Result<S>::Failure(v.ErrorValue()) : Result<S>::Success(std::bit_cast<S>(v.Value()));
    }

    /** @copydoc CanonicalValueReader::ReadUInt8 */
    Result<std::uint8_t> CanonicalValueReader::ReadUInt8() {
        auto v = ReadExactBytes(1);
        return v.HasError() ? Result<std::uint8_t>::Failure(v.ErrorValue())
                            : Result<std::uint8_t>::Success(std::to_integer<std::uint8_t>(v.Value()[0]));
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
        auto v = ReadUInt8();
        if (v.HasError())
            return Result<bool>::Failure(v.ErrorValue());
        return v.Value() < 2 ? Result<bool>::Success(v.Value() == 1) : Result<bool>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }

    /** @copydoc CanonicalValueReader::ReadFloat32 */
    Result<float> CanonicalValueReader::ReadFloat32() {
        auto b = ReadUInt32();
        if (b.HasError())
            return Result<float>::Failure(b.ErrorValue());
        float v = std::bit_cast<float>(b.Value());
        if (!std::isfinite(v) || b.Value() == 0x80000000U)
            return Result<float>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        return Result<float>::Success(v);
    }

    /** @copydoc CanonicalValueReader::ReadFloat64 */
    Result<double> CanonicalValueReader::ReadFloat64() {
        auto b = ReadUInt64();
        if (b.HasError())
            return Result<double>::Failure(b.ErrorValue());
        double v = std::bit_cast<double>(b.Value());
        if (!std::isfinite(v) || b.Value() == 0x8000000000000000ULL)
            return Result<double>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        return Result<double>::Success(v);
    }

    Result<std::size_t> CanonicalValueReader::ReadLength(std::size_t max) {
        auto n = ReadUInt32();
        if (n.HasError())
            return Result<std::size_t>::Failure(n.ErrorValue());
        return n.Value() <= max ? Result<std::size_t>::Success(n.Value())
                                : Result<std::size_t>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
    }

    /** @copydoc CanonicalValueReader::ReadBytes */
    Result<std::vector<std::byte>> CanonicalValueReader::ReadBytes(std::size_t max) {
        auto n = ReadLength(max);
        if (n.HasError())
            return Result<std::vector<std::byte>>::Failure(n.ErrorValue());
        auto v = ReadExactBytes(n.Value());
        if (v.HasError())
            return Result<std::vector<std::byte>>::Failure(v.ErrorValue());
        auto charged = Charge(n.Value());
        if (charged.HasError())
            return Result<std::vector<std::byte>>::Failure(charged.ErrorValue());
        try {
            return Result<std::vector<std::byte>>::Success({v.Value().begin(), v.Value().end()});
        } catch (const std::bad_alloc &) {
            return Result<std::vector<std::byte>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadUtf8 */
    Result<std::string> CanonicalValueReader::ReadUtf8() {
        auto b = ReadBytes(limits_.maximumStringBytes);
        if (b.HasError())
            return Result<std::string>::Failure(b.ErrorValue());
        auto charged = Charge(b.Value().size());
        if (charged.HasError())
            return Result<std::string>::Failure(charged.ErrorValue());
        try {
            std::string s{reinterpret_cast<const char *>(b.Value().data()), b.Value().size()};
            return IsValidUtf8ScalarSequence(s) ? Result<std::string>::Success(std::move(s))
                                                : Result<std::string>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        } catch (const std::bad_alloc &) {
            return Result<std::string>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadVec2 */
    Result<Math::Vec2> CanonicalValueReader::ReadVec2() {
        auto x = ReadFloat32();
        if (x.HasError())
            return Result<Math::Vec2>::Failure(x.ErrorValue());
        auto y = ReadFloat32();
        return y.HasError() ? Result<Math::Vec2>::Failure(y.ErrorValue()) : Result<Math::Vec2>::Success({x.Value(), y.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadVec3 */
    Result<Math::Vec3> CanonicalValueReader::ReadVec3() {
        auto xy = ReadVec2();
        if (xy.HasError())
            return Result<Math::Vec3>::Failure(xy.ErrorValue());
        auto z = ReadFloat32();
        return z.HasError() ? Result<Math::Vec3>::Failure(z.ErrorValue())
                            : Result<Math::Vec3>::Success({xy.Value().x, xy.Value().y, z.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadVec4 */
    Result<Math::Vec4> CanonicalValueReader::ReadVec4() {
        auto xyz = ReadVec3();
        if (xyz.HasError())
            return Result<Math::Vec4>::Failure(xyz.ErrorValue());
        auto w = ReadFloat32();
        return w.HasError() ? Result<Math::Vec4>::Failure(w.ErrorValue())
                            : Result<Math::Vec4>::Success({xyz.Value().x, xyz.Value().y, xyz.Value().z, w.Value()});
    }

    /** @copydoc CanonicalValueReader::ReadQuaternion */
    Result<Math::Quaternion> CanonicalValueReader::ReadQuaternion() {
        auto v = ReadVec4();
        return v.HasError() ? Result<Math::Quaternion>::Failure(v.ErrorValue())
                            : Result<Math::Quaternion>::Success({v.Value().x, v.Value().y, v.Value().z, v.Value().w});
    }

    Result<CanonicalDecodedValue> CanonicalValueReader::ReadChild(std::vector<CanonicalFieldId> path) {
        auto n = ReadLength(limits_.maximumBytes);
        if (n.HasError())
            return Result<CanonicalDecodedValue>::Failure(n.ErrorValue());
        auto b = ReadExactBytes(n.Value());
        if (b.HasError())
            return Result<CanonicalDecodedValue>::Failure(b.ErrorValue());
        return Result<CanonicalDecodedValue>::Success(CanonicalDecodedValue{b.Value(), limits_, state_, depth_ + 1, std::move(path)});
    }

    /** @copydoc CanonicalValueReader::ReadSequence */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadSequence() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto n = ReadLength(limits_.maximumCollectionElements);
        if (n.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(n.ErrorValue());
        if (auto c = ChargeElements(n.Value(), sizeof(CanonicalDecodedValue)); c.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(c.ErrorValue());
        try {
            std::vector<CanonicalDecodedValue> v;
            v.reserve(n.Value());
            for (std::size_t i = 0; i < n.Value(); ++i) {
                auto x = ReadChild(path_);
                if (x.HasError())
                    return Result<std::vector<CanonicalDecodedValue>>::Failure(x.ErrorValue());
                v.push_back(std::move(x).Value());
            }
            return Result<std::vector<CanonicalDecodedValue>>::Success(std::move(v));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadOptional */
    Result<std::optional<CanonicalDecodedValue>> CanonicalValueReader::ReadOptional() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto p = ReadBool();
        if (p.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(p.ErrorValue());
        if (!p.Value())
            return Result<std::optional<CanonicalDecodedValue>>::Success(std::nullopt);
        try {
            auto v = ReadChild(path_);
            return v.HasError() ? Result<std::optional<CanonicalDecodedValue>>::Failure(v.ErrorValue())
                                : Result<std::optional<CanonicalDecodedValue>>::Success(std::move(v).Value());
        } catch (const std::bad_alloc &) {
            return Result<std::optional<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadVariant */
    Result<std::pair<std::uint32_t, CanonicalDecodedValue>> CanonicalValueReader::ReadVariant(std::uint32_t count) {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto i = ReadUInt32();
        if (i.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(i.ErrorValue());
        if (!count || i.Value() >= count)
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        try {
            auto v = ReadChild(path_);
            return v.HasError() ? Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(v.ErrorValue())
                                : Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Success({i.Value(), std::move(v).Value()});
        } catch (const std::bad_alloc &) {
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadMap */
    Result<std::vector<CanonicalDecodedMapEntry>> CanonicalValueReader::ReadMap() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumCollectionElements);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(count.ErrorValue());
        auto charged = ChargeElements(count.Value(), sizeof(CanonicalDecodedMapEntry));
        if (charged.HasError())
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(charged.ErrorValue());
        try {
            std::vector<CanonicalDecodedMapEntry> entries;
            entries.reserve(count.Value());
            for (std::size_t index = 0; index < count.Value(); ++index) {
                auto key = ReadChild(path_);
                if (key.HasError())
                    return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(key.ErrorValue());
                auto value = ReadChild(path_);
                if (value.HasError())
                    return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(value.ErrorValue());
                if (!entries.empty() && !Less(entries.back().key.bytes_, key.Value().bytes_))
                    return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
                entries.push_back({std::move(key).Value(), std::move(value).Value()});
            }
            return Result<std::vector<CanonicalDecodedMapEntry>>::Success(std::move(entries));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadSet */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadSet() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumCollectionElements);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(count.ErrorValue());
        auto charged = ChargeElements(count.Value(), sizeof(CanonicalDecodedValue));
        if (charged.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(charged.ErrorValue());
        try {
            std::vector<CanonicalDecodedValue> values;
            values.reserve(count.Value());
            for (std::size_t index = 0; index < count.Value(); ++index) {
                auto value = ReadChild(path_);
                if (value.HasError())
                    return Result<std::vector<CanonicalDecodedValue>>::Failure(value.ErrorValue());
                if (!values.empty() && !Less(values.back().bytes_, value.Value().bytes_))
                    return Result<std::vector<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
                values.push_back(std::move(value).Value());
            }
            return Result<std::vector<CanonicalDecodedValue>>::Success(std::move(values));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadRecord */
    Result<std::vector<CanonicalDecodedField>> CanonicalValueReader::ReadRecord() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::vector<CanonicalDecodedField>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumFields);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedField>>::Failure(count.ErrorValue());
        auto charged = ChargeElements(count.Value(), sizeof(CanonicalDecodedField));
        if (charged.HasError())
            return Result<std::vector<CanonicalDecodedField>>::Failure(charged.ErrorValue());
        try {
            std::vector<CanonicalDecodedField> fields;
            fields.reserve(count.Value());
            for (std::size_t index = 0; index < count.Value(); ++index) {
                auto rawId = ReadUInt32();
                if (rawId.HasError())
                    return Result<std::vector<CanonicalDecodedField>>::Failure(rawId.ErrorValue());
                auto id = CanonicalFieldId::Create(rawId.Value());
                if (id.HasError() || (!fields.empty() && !(fields.back().id < id.Value())))
                    return Result<std::vector<CanonicalDecodedField>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
                auto childPath = path_;
                childPath.push_back(id.Value());
                auto value = ReadChild(std::move(childPath));
                if (value.HasError())
                    return Result<std::vector<CanonicalDecodedField>>::Failure(value.ErrorValue());
                fields.push_back({id.Value(), std::move(value).Value()});
            }
            return Result<std::vector<CanonicalDecodedField>>::Success(std::move(fields));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedField>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::RequireFinished */
    Result<void> CanonicalValueReader::RequireFinished() const {
        return offset_ == bytes_.size() ? Result<void>::Success() : Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }
}  // namespace Horo::Runtime
