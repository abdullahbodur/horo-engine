#include "Horo/Runtime/Save/SaveCanonicalCodec.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveCanonicalCodecInternal.h"

#include <new>
#include <utility>
#include <vector>

namespace Horo::Runtime {
    Result<CanonicalDecodedValue> CanonicalValueReader::ReadChild(std::vector<CanonicalFieldId> path) {
        auto size = ReadLength(limits_.maximumBytes);
        if (size.HasError())
            return Result<CanonicalDecodedValue>::Failure(size.ErrorValue());
        auto bytes = ReadExactBytes(size.Value());
        if (bytes.HasError())
            return Result<CanonicalDecodedValue>::Failure(bytes.ErrorValue());
        return Result<CanonicalDecodedValue>::Success(CanonicalDecodedValue{bytes.Value(), limits_, state_, depth_ + 1, std::move(path)});
    }

    /** @copydoc CanonicalValueReader::ReadSequence */
    Result<std::vector<CanonicalDecodedValue>> CanonicalValueReader::ReadSequence() {
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
                values.push_back(std::move(value).Value());
            }
            return Result<std::vector<CanonicalDecodedValue>>::Success(std::move(values));
        } catch (const std::bad_alloc &) {
            return Result<std::vector<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadOptional */
    Result<std::optional<CanonicalDecodedValue>> CanonicalValueReader::ReadOptional() {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto present = ReadBool();
        if (present.HasError())
            return Result<std::optional<CanonicalDecodedValue>>::Failure(present.ErrorValue());
        if (!present.Value())
            return Result<std::optional<CanonicalDecodedValue>>::Success(std::nullopt);
        try {
            auto value = ReadChild(path_);
            return value.HasError() ? Result<std::optional<CanonicalDecodedValue>>::Failure(value.ErrorValue())
                                    : Result<std::optional<CanonicalDecodedValue>>::Success(std::move(value).Value());
        } catch (const std::bad_alloc &) {
            return Result<std::optional<CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueReader::ReadVariant */
    Result<std::pair<std::uint32_t, CanonicalDecodedValue>> CanonicalValueReader::ReadVariant(const std::uint32_t alternativeCount) {
        auto admitted = AdmitComposite();
        if (admitted.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(admitted.ErrorValue());
        auto index = ReadUInt32();
        if (index.HasError())
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(index.ErrorValue());
        if (!alternativeCount || index.Value() >= alternativeCount)
            return Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(ErrorAt(SaveErrors::CanonicalCodecCorrupt));
        try {
            auto value = ReadChild(path_);
            return value.HasError()
                       ? Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Failure(value.ErrorValue())
                       : Result<std::pair<std::uint32_t, CanonicalDecodedValue>>::Success({index.Value(), std::move(value).Value()});
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
                if (!entries.empty() && !CanonicalCodecDetail::BytesLess(entries.back().key.bytes_, key.Value().bytes_))
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
                if (!values.empty() && !CanonicalCodecDetail::BytesLess(values.back().bytes_, value.Value().bytes_))
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
}  // namespace Horo::Runtime
