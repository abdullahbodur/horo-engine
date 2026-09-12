#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveCanonicalCodecInternal.h"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace Horo::Runtime {
    struct CanonicalReadState final {
        std::size_t decodedBytes{};
    };

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
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (value.size() > limits_.maximumBytes || bytes_.size() > limits_.maximumBytes - value.size())
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        try {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
            return Result<void>::Success();
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    Result<void> CanonicalValueWriter::AppendLengthDelimited(const std::span<const std::byte> value) {
        constexpr std::size_t LengthBytes = sizeof(std::uint32_t);
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (limits_.maximumBytes < LengthBytes || value.size() > std::numeric_limits<std::uint32_t>::max() ||
            value.size() > limits_.maximumBytes - LengthBytes || bytes_.size() > limits_.maximumBytes - LengthBytes - value.size())
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto length = WriteUInt32(static_cast<std::uint32_t>(value.size()));
        return length.HasError() ? length : Append(value);
    }

    Result<void> CanonicalValueWriter::AdmitComposite(const std::size_t childDepth) {
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Fail(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (childDepth >= limits_.maximumNestingDepth)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        structuralDepth_ = std::max(structuralDepth_, childDepth + 1);
        return Result<void>::Success();
    }

    /** @copydoc CanonicalValueWriter::Finalize */
    Result<CanonicalEncodedValue> CanonicalValueWriter::Finalize() && {
        if (failure_)
            return Result<CanonicalEncodedValue>::Failure(*failure_);
        if (!CanonicalCodecDetail::ValidLimits(limits_))
            return Result<CanonicalEncodedValue>::Failure(ErrorAt(SaveErrors::CanonicalCodecConfigurationInvalid));
        return Result<CanonicalEncodedValue>::Success(CanonicalEncodedValue{std::move(bytes_), structuralDepth_});
    }

    CanonicalValueReader::CanonicalValueReader(std::span<const std::byte> bytes, CanonicalCodecLimits limits,
                                               std::shared_ptr<CanonicalReadState> state, const std::size_t depth,
                                               std::vector<CanonicalFieldId> path)
        : bytes_(bytes), limits_(limits), state_(std::move(state)), depth_(depth), path_(std::move(path)) {}

    /** @copydoc CanonicalValueReader::Create */
    Result<CanonicalValueReader> CanonicalValueReader::Create(const std::span<const std::byte> bytes, const CanonicalCodecLimits limits) {
        if (!CanonicalCodecDetail::ValidLimits(limits))
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecConfigurationInvalid));
        if (bytes.size() > limits.maximumBytes)
            return Result<CanonicalValueReader>::Failure(MakeError(SaveErrors::CanonicalCodecLimitExceeded));
        try {
            return Result<CanonicalValueReader>::Success(
                CanonicalValueReader{bytes, limits, std::make_shared<CanonicalReadState>(), 0, {}});
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

    Error CanonicalValueReader::ErrorAt(const ErrorCodeDescriptor &descriptor) const {
        Error error = MakeError(descriptor);
        std::string source{"canonical"};
        for (const auto field : path_)
            source += "/field:" + std::to_string(field.Value());
        error.diagnostics.push_back(
            {DiagnosticCode{"save.canonical_codec.location"},
             DiagnosticSeverity::Error,
             std::string{descriptor.summary},
             {std::move(source), 0,
              static_cast<std::uint32_t>(std::min(offset_, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())))}});
        return error;
    }

    Result<void> CanonicalValueReader::Charge(const std::size_t bytes) {
        if (state_->decodedBytes > limits_.maximumDecodedBytes || bytes > limits_.maximumDecodedBytes - state_->decodedBytes)
            return Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        state_->decodedBytes += bytes;
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
    Result<std::span<const std::byte>> CanonicalValueReader::ReadExactBytes(const std::size_t count) {
        if (offset_ > bytes_.size() || count > bytes_.size() - offset_)
            return Result<std::span<const std::byte>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        const auto value = bytes_.subspan(offset_, count);
        offset_ += count;
        return Result<std::span<const std::byte>>::Success(value);
    }

    Result<std::size_t> CanonicalValueReader::ReadLength(const std::size_t maximum) {
        auto length = ReadUInt32();
        if (length.HasError())
            return Result<std::size_t>::Failure(length.ErrorValue());
        return length.Value() <= maximum ? Result<std::size_t>::Success(length.Value())
                                         : Result<std::size_t>::Failure(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
    }

    /** @copydoc CanonicalValueReader::RequireFinished */
    Result<void> CanonicalValueReader::RequireFinished() const {
        return offset_ == bytes_.size() ? Result<void>::Success() : Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
    }
}  // namespace Horo::Runtime
