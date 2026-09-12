#include "Horo/Runtime/Save/SaveCanonicalCodec.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveCanonicalCodecInternal.h"

#include <new>
#include <utility>
#include <vector>

namespace Horo::Runtime {
    Result<CanonicalDecodedValue> CanonicalValueReader::ReadChild(std::shared_ptr<const CanonicalPathNode> path) {
        auto size = ReadLength(limits_.maximumBytes);
        if (size.HasError())
            return Result<CanonicalDecodedValue>::Failure(size.ErrorValue());
        auto bytes = ReadExactBytes(size.Value());
        if (bytes.HasError())
            return Result<CanonicalDecodedValue>::Failure(bytes.ErrorValue());
        return Result<CanonicalDecodedValue>::Success(CanonicalDecodedValue{bytes.Value(), limits_, state_, depth_ + 1, std::move(path)});
    }

    /** @copydoc CanonicalValueReader::ReadValueCollection */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadValueCollection(const ValueCollectionOrder order) {
        if (auto admitted = AdmitComposite(); admitted.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumCollectionElements);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(count.ErrorValue());
        if (auto charged = AdmitElements(count.Value(), sizeof(CanonicalDecodedValue), sizeof(std::uint32_t)); charged.HasError())
            return Result<std::vector<CanonicalDecodedValue>>::Failure(charged.ErrorValue());
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            std::vector<CanonicalDecodedValue> values;
            values.reserve(count.Value());
            for (std::size_t index = 0; index < count.Value(); ++index) {
                auto value = ReadChild(path_);
                if (value.HasError())
                    return Result<std::vector<CanonicalDecodedValue>>::Failure(value.ErrorValue());
                if (order == ValueCollectionOrder::RequireCanonical && !values.empty() &&
                    !CanonicalCodecDetail::BytesLess(values.back().bytes_, value.Value().bytes_))
                    return Result<std::vector<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
                values.emplace_back(std::move(value).Value());
            }
            return Result<std::vector<CanonicalDecodedValue>>::Success(std::move(values));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedValue>>::Failure(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueReader::ReadSequence */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadSequence() {
        return ReadValueCollection(ValueCollectionOrder::Preserve);
    }

    /** @copydoc CanonicalValueReader::ReadOptional */
    Result<std::optional<CanonicalDecodedValue>> CanonicalValueReader::ReadOptional() {
        if (auto admitted = AdmitComposite(); admitted.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto present = ReadBool();
        if (present.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(present.ErrorValue());
        if (!present.Value())
            return Result<std::optional<CanonicalDecodedValue>>::Success(std::nullopt);
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            auto value = ReadChild(path_);
            return value.HasError() ? Result<std::optional<CanonicalDecodedValue>>::Failure(value.ErrorValue())
                                    : Result<std::optional<CanonicalDecodedValue>>::Success(std::move(value).Value());
        } catch (const std::bad_alloc &) {
            return Result<std::optional<CanonicalDecodedValue>>::Failure(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueReader::ReadVariant */
    Result<std::pair<std::uint32_t, CanonicalDecodedValue>> CanonicalValueReader::ReadVariant(const std::uint32_t alternativeCount) {
        if (auto admitted = AdmitComposite(); admitted.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto index = ReadUInt32();
        if (index.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(index.ErrorValue());
        if (!alternativeCount || index.Value() >= alternativeCount)
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            auto value = ReadChild(path_);
            return value.HasError()
                       ? Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(value.ErrorValue())
                       : Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Success({index.Value(), std::move(value).Value()});
        } catch (const std::bad_alloc &) {
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueReader::ReadMap */
    Result<std::vector<CanonicalDecodedMapEntry>> CanonicalValueReader::ReadMap() {
        if (auto admitted = AdmitComposite(); admitted.HasError())
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumCollectionElements);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(count.ErrorValue());
        if (auto charged = AdmitElements(count.Value(), sizeof(CanonicalDecodedMapEntry), 2 * sizeof(std::uint32_t)); charged.HasError()) {
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(charged.ErrorValue());
        }
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
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
                if (!entries.empty() && !CanonicalCodecDetail::BytesLess(entries.back().key.bytes_, key.Value().bytes_))
                    return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
                entries.emplace_back(std::move(key).Value(), std::move(value).Value());
            }
            return Result<std::vector<CanonicalDecodedMapEntry>>::Success(std::move(entries));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedMapEntry>>::Failure(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueReader::ReadSet */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadSet() {
        return ReadValueCollection(ValueCollectionOrder::RequireCanonical);
    }

    /** @copydoc CanonicalValueReader::ReadRecord */
    Result<std::vector<CanonicalDecodedField>> CanonicalValueReader::ReadRecord() {
        if (auto admitted = AdmitComposite(); admitted.HasError())
            return Result<std::vector<CanonicalDecodedField>>::Failure(admitted.ErrorValue());
        auto count = ReadLength(limits_.maximumFields);
        if (count.HasError())
            return Result<std::vector<CanonicalDecodedField>>::Failure(count.ErrorValue());
        if (auto charged = AdmitElements(count.Value(), sizeof(CanonicalDecodedField), 2 * sizeof(std::uint32_t)); charged.HasError()) {
            return Result<std::vector<CanonicalDecodedField>>::Failure(charged.ErrorValue());
        }
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
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
                if (auto pathCharge = Charge(sizeof(CanonicalPathNode)); pathCharge.HasError())
                    return Result<std::vector<CanonicalDecodedField>>::Failure(pathCharge.ErrorValue());
                auto childPath = std::make_shared<CanonicalPathNode>(id.Value(), path_, path_ ? path_->depth + 1 : 1);
                auto value = ReadChild(std::move(childPath));
                if (value.HasError())
                    return Result<std::vector<CanonicalDecodedField>>::Failure(value.ErrorValue());
                fields.emplace_back(id.Value(), std::move(value).Value());
            }
            return Result<std::vector<CanonicalDecodedField>>::Success(std::move(fields));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedField>>::Failure(std::move(allocationFailure));
        }
    }
}  // namespace Horo::Runtime
