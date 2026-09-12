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

    /** @copydoc CanonicalValueWriter::WriteSequence */
    Result<void> CanonicalValueWriter::WriteSequence(const std::span<const CanonicalEncodedValue> values) {
        auto admitted = AdmitComposite(CanonicalCodecDetail::MaximumDepth(values));
        if (admitted.HasError())
            return admitted;
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(values.size()));
            for (const auto &value : values) {
                if (written.HasError())
                    return Fail(std::move(written).ErrorValue());
                written = staging.AppendLengthDelimited(value.Bytes());
            }
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteOptional */
    Result<void> CanonicalValueWriter::WriteOptional(const std::optional<CanonicalEncodedValue> &value) {
        auto admitted = AdmitComposite(value ? value->StructuralDepth() : 0);
        if (admitted.HasError())
            return admitted;
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteBool(value.has_value());
            if (written.HasValue() && value)
                written = staging.AppendLengthDelimited(value->Bytes());
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteVariant */
    Result<void> CanonicalValueWriter::WriteVariant(const std::uint32_t index, const std::uint32_t alternativeCount,
                                                    const CanonicalEncodedValue &value) {
        auto admitted = AdmitComposite(value.StructuralDepth());
        if (admitted.HasError())
            return admitted;
        if (!alternativeCount || index >= alternativeCount)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecInvalid));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(index);
            if (written.HasValue())
                written = staging.AppendLengthDelimited(value.Bytes());
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteMap */
    Result<void> CanonicalValueWriter::WriteMap(const std::span<const CanonicalMapEntry> entries) {
        std::size_t childDepth{};
        for (const auto &entry : entries)
            childDepth = std::max({childDepth, entry.key.StructuralDepth(), entry.value.StructuralDepth()});
        auto admitted = AdmitComposite(childDepth);
        if (admitted.HasError())
            return admitted;
        if (entries.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            bool duplicate{};
            const auto ordered = CanonicalCodecDetail::OrderedUnique(entries, [](const auto *left, const auto *right) {
                return CanonicalCodecDetail::BytesLess(left->key.Bytes(), right->key.Bytes());
            }, [](const auto &left, const auto &right) {
                return CanonicalCodecDetail::BytesEqual(left.key.Bytes(), right.key.Bytes());
            }, duplicate);
            if (duplicate)
                return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *entry : ordered) {
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(entry->key.Bytes());
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(entry->value.Bytes());
            }
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteSet */
    Result<void> CanonicalValueWriter::WriteSet(const std::span<const CanonicalEncodedValue> values) {
        auto admitted = AdmitComposite(CanonicalCodecDetail::MaximumDepth(values));
        if (admitted.HasError())
            return admitted;
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            bool duplicate{};
            const auto ordered = CanonicalCodecDetail::OrderedUnique(values, [](const auto *left, const auto *right) {
                return CanonicalCodecDetail::BytesLess(left->Bytes(), right->Bytes());
            }, [](const auto &left, const auto &right) {
                return CanonicalCodecDetail::BytesEqual(left.Bytes(), right.Bytes());
            }, duplicate);
            if (duplicate)
                return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *value : ordered) {
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(value->Bytes());
            }
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteRecord */
    Result<void> CanonicalValueWriter::WriteRecord(const std::span<const CanonicalRecordField> fields) {
        std::size_t childDepth{};
        for (const auto &field : fields)
            childDepth = std::max(childDepth, field.value.StructuralDepth());
        auto admitted = AdmitComposite(childDepth);
        if (admitted.HasError())
            return admitted;
        if (fields.size() > limits_.maximumFields)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        Error allocationFailure = ErrorAt(SaveErrors::CanonicalCodecAllocationFailed);
        try {
            bool duplicate{};
            const auto ordered = CanonicalCodecDetail::OrderedUnique(fields, [](const auto *left, const auto *right) {
                return left->id < right->id;
            }, [](const auto &left, const auto &right) {
                return left.id == right.id;
            }, duplicate);
            if (duplicate)
                return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *field : ordered) {
                if (written.HasValue())
                    written = staging.WriteUInt32(field->id.Value());
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(field->value.Bytes());
            }
            return written.HasError() ? Fail(std::move(written).ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(std::move(allocationFailure));
        }
    }
}  // namespace Horo::Runtime
