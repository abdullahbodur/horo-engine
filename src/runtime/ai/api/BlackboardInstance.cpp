#include "Horo/AI/BlackboardInstance.h"

#include "Horo/AI/AIErrors.h"

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace Horo::AI {
    namespace {
        using BlackboardValueStorage = std::vector<std::optional<BlackboardValue>>;
        static_assert(std::is_nothrow_copy_assignable_v<std::optional<BlackboardValue>>);

        [[nodiscard]] Error Failure(const ErrorCodeDescriptor &descriptor) {
            return MakeError(descriptor);
        }

        [[nodiscard]] std::size_t FindKey(const BlackboardSchema &schema, const BlackboardKeyId key) noexcept {
            const auto keys = schema.Keys();
            const auto found = std::ranges::lower_bound(keys, key, {}, &BlackboardKeyDescriptor::key);
            return found != keys.end() && found->key == key ? static_cast<std::size_t>(found - keys.begin()) : keys.size();
        }

        [[nodiscard]] Result<BlackboardValueStorage> BuildDefaultValues(const BlackboardSchema &schema) {
            try {
                BlackboardValueStorage values;
                values.reserve(schema.Keys().size());
                for (const auto &key : schema.Keys()) {
                    if (key.presence == BlackboardKeyPresence::Required && !key.defaultValue.has_value())
                        return Result<BlackboardValueStorage>::Failure(Failure(AIErrors::BlackboardInstanceInvalid));
                    values.push_back(key.defaultValue);
                }
                return Result<BlackboardValueStorage>::Success(std::move(values));
            } catch (const std::bad_alloc &) {
                return Result<BlackboardValueStorage>::Failure(Failure(AIErrors::BlackboardStorageUnavailable));
            }
        }

        [[nodiscard]] Result<BlackboardValueStorage> BuildReplacementValues(const BlackboardSchema &currentSchema,
                                                                            const BlackboardValueStorage &currentValues,
                                                                            const BlackboardSchema &replacementSchema) {
            auto candidate = BuildDefaultValues(replacementSchema);
            if (candidate.HasError())
                return candidate;
            auto values = std::move(candidate).Value();
            for (std::size_t index = 0; index < replacementSchema.Keys().size(); ++index) {
                const auto oldIndex = FindKey(currentSchema, replacementSchema.Keys()[index].key);
                if (oldIndex == currentSchema.Keys().size() || !currentValues[oldIndex].has_value())
                    continue;
                if (ValidateBlackboardValue(*currentValues[oldIndex], &replacementSchema.Keys()[index],
                                            replacementSchema.UnknownValuePolicy())
                        .HasError())
                    return Result<BlackboardValueStorage>::Failure(Failure(AIErrors::BlackboardValueTypeMismatch));
                values[index] = currentValues[oldIndex];
            }
            return Result<BlackboardValueStorage>::Success(std::move(values));
        }
    }  // namespace

    /** @copydoc BlackboardInstanceBinding::IsValid */
    bool BlackboardInstanceBinding::IsValid() const noexcept {
        return agent.IsValid() && schema.IsValid() && schemaVersion != 0 && schemaGeneration != 0 && instanceGeneration != 0;
    }

    /** @copydoc BlackboardWriteBatch::Stage */
    Result<void> BlackboardWriteBatch::Stage(BlackboardWrite write) {
        if (!write.key.IsValid())
            return Result<void>::Failure(Failure(AIErrors::BlackboardBatchInvalid));
        if (writeCount_ >= MaximumBlackboardWritesPerBatch)
            return Result<void>::Failure(Failure(AIErrors::BlackboardLimitExceeded));
        if (std::ranges::any_of(Writes(), [&write](const auto &existing) {
            return existing.key == write.key;
        }))
            return Result<void>::Failure(Failure(AIErrors::BlackboardBatchInvalid));
        writes_[writeCount_++] = std::move(write);
        return Result<void>::Success();
    }

    /** @copydoc BlackboardSnapshot::Revision */
    Result<std::uint64_t> BlackboardSnapshot::Revision() const {
        if (!generationActive_->load())
            return Result<std::uint64_t>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        return Result<std::uint64_t>::Success(revision_);
    }

    /** @copydoc BlackboardSnapshot::Read */
    Result<std::optional<BlackboardValue>> BlackboardSnapshot::Read(const BlackboardKeyId key) const {
        if (!generationActive_->load())
            return Result<std::optional<BlackboardValue>>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        const std::size_t index = FindKey(*schema_, key);
        if (index == schema_->Keys().size())
            return Result<std::optional<BlackboardValue>>::Failure(Failure(AIErrors::BlackboardUnknownValueRejected));
        return Result<std::optional<BlackboardValue>>::Success(values_[index]);
    }

    /** @copydoc BlackboardInstance::Create */
    Result<std::unique_ptr<BlackboardInstance>> BlackboardInstance::Create(BlackboardInstanceBinding binding,
                                                                           std::shared_ptr<const BlackboardSchema> schema) {
        if (!binding.IsValid() || schema == nullptr || binding.schema != schema->Identity() || binding.schemaVersion != schema->Version())
            return Result<std::unique_ptr<BlackboardInstance>>::Failure(Failure(AIErrors::BlackboardInstanceInvalid));
        auto values = BuildDefaultValues(*schema);
        if (values.HasError())
            return Result<std::unique_ptr<BlackboardInstance>>::Failure(values.ErrorValue());
        try {
            auto generationActive = std::make_shared<std::atomic_bool>(true);
            auto scratch = values.Value();
            return Result<std::unique_ptr<BlackboardInstance>>::Success(
                std::unique_ptr<BlackboardInstance>{new BlackboardInstance(binding, std::move(schema), std::move(values).Value(),
                                                                           std::move(scratch), std::move(generationActive))});
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<BlackboardInstance>>::Failure(Failure(AIErrors::BlackboardStorageUnavailable));
        }
    }

    /** @copydoc BlackboardInstance::Snapshot */
    Result<BlackboardSnapshot> BlackboardInstance::Snapshot() const {
        if (!active_)
            return Result<BlackboardSnapshot>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        std::array<std::optional<BlackboardValue>, MaximumBlackboardKeys> snapshotValues{};
        std::ranges::copy(values_, snapshotValues.begin());
        return Result<BlackboardSnapshot>::Success(
            BlackboardSnapshot{binding_, schema_, std::move(snapshotValues), revision_, generationActive_});
    }

    /** @copydoc BlackboardInstance::BeginWriteBatch */
    Result<BlackboardWriteBatch> BlackboardInstance::BeginWriteBatch() const {
        if (!active_)
            return Result<BlackboardWriteBatch>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        return Result<BlackboardWriteBatch>::Success(BlackboardWriteBatch{binding_, revision_});
    }

    /** @copydoc BlackboardInstance::CommitAtBlackboardSync */
    Result<BlackboardCommitResult> BlackboardInstance::CommitAtBlackboardSync(BlackboardWriteBatch batch) {
        if (!active_ || batch.Binding() != binding_ || batch.BaseRevision() != revision_)
            return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        std::ranges::sort(std::span{batch.writes_.data(), batch.writeCount_}, {}, &BlackboardWrite::key);
        scratch_ = values_;
        BlackboardCommitResult result{.revision = revision_};
        for (const auto &write : batch.Writes()) {
            const std::size_t index = FindKey(*schema_, write.key);
            if (index == schema_->Keys().size())
                return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardUnknownValueRejected));
            if (schema_->Keys()[index].access == BlackboardKeyAccess::ReadOnly)
                return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardBatchInvalid));
            if (const auto valid = ValidateBlackboardValue(write.value, &schema_->Keys()[index], schema_->UnknownValuePolicy());
                valid.HasError())
                return Result<BlackboardCommitResult>::Failure(valid.ErrorValue());
            if (!scratch_[index].has_value() || *scratch_[index] != write.value) {
                scratch_[index] = write.value;
                result.changedKeys[result.changedKeyCount++] = write.key;
            }
        }
        if (result.changedKeyCount == 0)
            return Result<BlackboardCommitResult>::Success(std::move(result));
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
            return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardRevisionExhausted));
        values_.swap(scratch_);
        result.revision = ++revision_;
        return Result<BlackboardCommitResult>::Success(std::move(result));
    }

    /** @copydoc BlackboardInstance::ReplaceAtBlackboardSync */
    Result<void> BlackboardInstance::ReplaceAtBlackboardSync(BlackboardInstanceBinding replacementBinding,
                                                             std::shared_ptr<const BlackboardSchema> replacementSchema) {
        if (!active_ || replacementSchema == nullptr || !replacementBinding.IsValid() || replacementBinding.agent != binding_.agent ||
            replacementBinding.schema != replacementSchema->Identity() ||
            replacementBinding.schemaVersion != replacementSchema->Version() ||
            replacementBinding.schemaGeneration <= binding_.schemaGeneration ||
            binding_.instanceGeneration == std::numeric_limits<std::uint32_t>::max() ||
            replacementBinding.instanceGeneration != binding_.instanceGeneration + 1)
            return Result<void>::Failure(Failure(AIErrors::BlackboardInstanceInvalid));
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
            return Result<void>::Failure(Failure(AIErrors::BlackboardRevisionExhausted));
        auto replacement = BuildReplacementValues(*schema_, values_, *replacementSchema);
        if (replacement.HasError())
            return Result<void>::Failure(replacement.ErrorValue());
        try {
            auto replacementGeneration = std::make_shared<std::atomic_bool>(true);
            auto replacementScratch = replacement.Value();
            values_ = std::move(replacement).Value();
            scratch_ = std::move(replacementScratch);
            schema_ = std::move(replacementSchema);
            binding_ = replacementBinding;
            ++revision_;
            generationActive_->store(false);
            generationActive_ = std::move(replacementGeneration);
            return Result<void>::Success();
        } catch (const std::bad_alloc &) {
            return Result<void>::Failure(Failure(AIErrors::BlackboardStorageUnavailable));
        }
    }

    /** @copydoc BlackboardInstance::ResetAtBlackboardSync */
    Result<BlackboardCommitResult> BlackboardInstance::ResetAtBlackboardSync() {
        if (!active_)
            return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardInstanceStale));
        auto defaults = BuildDefaultValues(*schema_);
        if (defaults.HasError())
            return Result<BlackboardCommitResult>::Failure(defaults.ErrorValue());
        BlackboardCommitResult result{.revision = revision_};
        for (std::size_t index = 0; index < schema_->Keys().size(); ++index) {
            if (values_[index] != defaults.Value()[index])
                result.changedKeys[result.changedKeyCount++] = schema_->Keys()[index].key;
        }
        if (result.changedKeyCount == 0)
            return Result<BlackboardCommitResult>::Success(std::move(result));
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
            return Result<BlackboardCommitResult>::Failure(Failure(AIErrors::BlackboardRevisionExhausted));
        values_ = std::move(defaults).Value();
        result.revision = ++revision_;
        return Result<BlackboardCommitResult>::Success(std::move(result));
    }

    /** @copydoc BlackboardInstance::TeardownAtBlackboardSync */
    void BlackboardInstance::TeardownAtBlackboardSync() noexcept {
        if (!active_)
            return;
        active_ = false;
        generationActive_->store(false);
        schema_.reset();
        values_.clear();
        scratch_.clear();
    }
}  // namespace Horo::AI
