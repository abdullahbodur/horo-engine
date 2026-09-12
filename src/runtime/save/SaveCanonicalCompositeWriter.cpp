#include "Horo/Runtime/Save/SaveCanonicalCodec.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveCanonicalCodecInternal.h"

#include <algorithm>
#include <new>
#include <ranges>
#include <vector>

namespace Horo::Runtime {
    Result<void> CanonicalValueWriter::CommitStaged(CanonicalValueWriter &&staging) {
        auto sealed = std::move(staging).Finalize();
        return sealed.HasError() ? Fail(std::move(sealed).ErrorValue()) : Append(sealed.Value().Bytes());
    }

    template <typename WritePayload> Result<void> CanonicalValueWriter::WriteStaged(WritePayload writePayload) {
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = writePayload(staging);
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    template <typename Value, typename Less, typename Equal, typename WriteValue>
    Result<void> CanonicalValueWriter::WriteOrderedCollection(const std::span<const Value> values, Less less, Equal equal,
                                                              WriteValue writeValue) {
        return WriteStaged([this, values, less = std::move(less), equal = std::move(equal),
                            writeValue = std::move(writeValue)](CanonicalValueWriter &staging) {
            bool duplicate{};
            const auto ordered = CanonicalCodecDetail::OrderedUnique(values, less, equal, duplicate);
            if (duplicate)
                return Result<void>::Failure(ErrorAt(SaveErrors::CanonicalCodecDuplicate));

            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *value : ordered) {
                if (written.HasError())
                    break;
                written = writeValue(staging, *value);
            }
            return written;
        });
    }

    /** @copydoc CanonicalValueWriter::WriteSequence */
    Result<void> CanonicalValueWriter::WriteSequence(const std::span<const CanonicalEncodedValue> values) {
        if (auto admitted = AdmitCollection(CanonicalCodecDetail::MaximumDepth(values), values.size(), limits_.maximumCollectionElements);
            admitted.HasError()) {
            return admitted;
        }
        return WriteStaged([values](CanonicalValueWriter &staging) {
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(values.size()));
            for (const auto &value : values) {
                if (written.HasError())
                    break;
                written = staging.AppendLengthDelimited(value.Bytes());
            }
            return written;
        });
    }

    /** @copydoc CanonicalValueWriter::WriteOptional */
    Result<void> CanonicalValueWriter::WriteOptional(const std::optional<CanonicalEncodedValue> &value) {
        if (auto admitted = AdmitComposite(value ? value->StructuralDepth() : 0); admitted.HasError())
            return admitted;
        return WriteStaged([&value](CanonicalValueWriter &staging) {
            auto written = staging.WriteBool(value.has_value());
            if (written.HasValue() && value)
                written = staging.AppendLengthDelimited(value->Bytes());
            return written;
        });
    }

    /** @copydoc CanonicalValueWriter::WriteVariant */
    Result<void> CanonicalValueWriter::WriteVariant(const std::uint32_t index, const std::uint32_t alternativeCount,
                                                    const CanonicalEncodedValue &value) {
        if (auto admitted = AdmitComposite(value.StructuralDepth()); admitted.HasError())
            return admitted;
        if (!alternativeCount || index >= alternativeCount)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecInvalid));
        return WriteStaged([index, &value](CanonicalValueWriter &staging) {
            auto written = staging.WriteUInt32(index);
            if (written.HasValue())
                written = staging.AppendLengthDelimited(value.Bytes());
            return written;
        });
    }

    /** @copydoc CanonicalValueWriter::WriteMap */
    Result<void> CanonicalValueWriter::WriteMap(const std::span<const CanonicalMapEntry> entries) {
        std::size_t childDepth{};
        for (const auto &entry : entries)
            childDepth = std::max({childDepth, entry.key.StructuralDepth(), entry.value.StructuralDepth()});
        if (auto admitted = AdmitCollection(childDepth, entries.size(), limits_.maximumCollectionElements); admitted.HasError())
            return admitted;
        return WriteOrderedCollection(entries, [](const auto *left, const auto *right) {
            return CanonicalCodecDetail::BytesLess(left->key.Bytes(), right->key.Bytes());
        }, [](const auto &left, const auto &right) {
            return CanonicalCodecDetail::BytesEqual(left.key.Bytes(), right.key.Bytes());
        }, [](CanonicalValueWriter &staging, const auto &entry) {
            auto written = staging.AppendLengthDelimited(entry.key.Bytes());
            return written.HasError() ? written : staging.AppendLengthDelimited(entry.value.Bytes());
        });
    }

    /** @copydoc CanonicalValueWriter::WriteSet */
    Result<void> CanonicalValueWriter::WriteSet(const std::span<const CanonicalEncodedValue> values) {
        if (auto admitted = AdmitCollection(CanonicalCodecDetail::MaximumDepth(values), values.size(), limits_.maximumCollectionElements);
            admitted.HasError()) {
            return admitted;
        }
        return WriteOrderedCollection(values, [](const auto *left, const auto *right) {
            return CanonicalCodecDetail::BytesLess(left->Bytes(), right->Bytes());
        }, [](const auto &left, const auto &right) {
            return CanonicalCodecDetail::BytesEqual(left.Bytes(), right.Bytes());
        }, [](CanonicalValueWriter &staging, const auto &value) {
            return staging.AppendLengthDelimited(value.Bytes());
        });
    }

    /** @copydoc CanonicalValueWriter::WriteRecord */
    Result<void> CanonicalValueWriter::WriteRecord(const std::span<const CanonicalRecordField> fields) {
        std::size_t childDepth{};
        for (const auto &field : fields)
            childDepth = std::max(childDepth, field.value.StructuralDepth());
        if (auto admitted = AdmitCollection(childDepth, fields.size(), limits_.maximumFields); admitted.HasError())
            return admitted;
        return WriteOrderedCollection(fields, [](const auto *left, const auto *right) {
            return left->id < right->id;
        }, [](const auto &left, const auto &right) {
            return left.id == right.id;
        }, [](CanonicalValueWriter &staging, const auto &field) {
            auto written = staging.WriteUInt32(field.id.Value());
            return written.HasError() ? written : staging.AppendLengthDelimited(field.value.Bytes());
        });
    }
}  // namespace Horo::Runtime
