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
        return sealed.HasError() ? Fail(sealed.ErrorValue()) : Append(sealed.Value().Bytes());
    }

    /** @copydoc CanonicalValueWriter::WriteSequence */
    Result<void> CanonicalValueWriter::WriteSequence(const std::span<const CanonicalEncodedValue> values) {
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto admitted = AdmitComposite(CanonicalCodecDetail::MaximumDepth(values));
        if (admitted.HasError())
            return admitted;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(values.size()));
            for (const auto &value : values) {
                if (written.HasError())
                    return Fail(written.ErrorValue());
                written = staging.AppendLengthDelimited(value.Bytes());
            }
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteOptional */
    Result<void> CanonicalValueWriter::WriteOptional(const std::optional<CanonicalEncodedValue> &value) {
        auto admitted = AdmitComposite(value ? value->StructuralDepth() : 0);
        if (admitted.HasError())
            return admitted;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteBool(value.has_value());
            if (written.HasValue() && value)
                written = staging.AppendLengthDelimited(value->Bytes());
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteVariant */
    Result<void> CanonicalValueWriter::WriteVariant(const std::uint32_t index, const std::uint32_t alternativeCount,
                                                    const CanonicalEncodedValue &value) {
        if (!alternativeCount || index >= alternativeCount)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecInvalid));
        auto admitted = AdmitComposite(value.StructuralDepth());
        if (admitted.HasError())
            return admitted;
        try {
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(index);
            if (written.HasValue())
                written = staging.AppendLengthDelimited(value.Bytes());
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteMap */
    Result<void> CanonicalValueWriter::WriteMap(const std::span<const CanonicalMapEntry> entries) {
        if (entries.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        std::size_t childDepth{};
        for (const auto &entry : entries)
            childDepth = std::max({childDepth, entry.key.StructuralDepth(), entry.value.StructuralDepth()});
        auto admitted = AdmitComposite(childDepth);
        if (admitted.HasError())
            return admitted;
        try {
            std::vector<const CanonicalMapEntry *> ordered;
            ordered.reserve(entries.size());
            for (const auto &entry : entries)
                ordered.push_back(&entry);
            std::ranges::sort(ordered, [](const auto *left, const auto *right) {
                return CanonicalCodecDetail::BytesLess(left->key.Bytes(), right->key.Bytes());
            });
            for (std::size_t index = 1; index < ordered.size(); ++index) {
                if (CanonicalCodecDetail::BytesEqual(ordered[index - 1]->key.Bytes(), ordered[index]->key.Bytes()))
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            }
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *entry : ordered) {
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(entry->key.Bytes());
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(entry->value.Bytes());
            }
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteSet */
    Result<void> CanonicalValueWriter::WriteSet(const std::span<const CanonicalEncodedValue> values) {
        if (values.size() > limits_.maximumCollectionElements)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        auto admitted = AdmitComposite(CanonicalCodecDetail::MaximumDepth(values));
        if (admitted.HasError())
            return admitted;
        try {
            std::vector<const CanonicalEncodedValue *> ordered;
            ordered.reserve(values.size());
            for (const auto &value : values)
                ordered.push_back(&value);
            std::ranges::sort(ordered, [](const auto *left, const auto *right) {
                return CanonicalCodecDetail::BytesLess(left->Bytes(), right->Bytes());
            });
            for (std::size_t index = 1; index < ordered.size(); ++index) {
                if (CanonicalCodecDetail::BytesEqual(ordered[index - 1]->Bytes(), ordered[index]->Bytes()))
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            }
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *value : ordered) {
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(value->Bytes());
            }
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }

    /** @copydoc CanonicalValueWriter::WriteRecord */
    Result<void> CanonicalValueWriter::WriteRecord(const std::span<const CanonicalRecordField> fields) {
        if (fields.size() > limits_.maximumFields)
            return Fail(ErrorAt(SaveErrors::CanonicalCodecLimitExceeded));
        std::size_t childDepth{};
        for (const auto &field : fields)
            childDepth = std::max(childDepth, field.value.StructuralDepth());
        auto admitted = AdmitComposite(childDepth);
        if (admitted.HasError())
            return admitted;
        try {
            std::vector<const CanonicalRecordField *> ordered;
            ordered.reserve(fields.size());
            for (const auto &field : fields)
                ordered.push_back(&field);
            std::ranges::sort(ordered, [](const auto *left, const auto *right) {
                return left->id < right->id;
            });
            for (std::size_t index = 1; index < ordered.size(); ++index) {
                if (ordered[index - 1]->id == ordered[index]->id)
                    return Fail(ErrorAt(SaveErrors::CanonicalCodecDuplicate));
            }
            CanonicalValueWriter staging{limits_, path_};
            auto written = staging.WriteUInt32(static_cast<std::uint32_t>(ordered.size()));
            for (const auto *field : ordered) {
                if (written.HasValue())
                    written = staging.WriteUInt32(field->id.Value());
                if (written.HasValue())
                    written = staging.AppendLengthDelimited(field->value.Bytes());
            }
            return written.HasError() ? Fail(written.ErrorValue()) : CommitStaged(std::move(staging));
        } catch (const std::bad_alloc &) {
            return Fail(ErrorAt(SaveErrors::CanonicalCodecAllocationFailed));
        }
    }
}  // namespace Horo::Runtime
