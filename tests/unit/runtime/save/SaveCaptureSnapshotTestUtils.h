#pragma once

#include "Horo/Runtime/Save/SaveCaptureSnapshot.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "SaveTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Horo::Runtime::CaptureTestSupport {
    class CountingCaptureAdapter final : public ICanonicalStateAdapter {
    public:
        explicit CountingCaptureAdapter(std::shared_ptr<int> destructionCount) : destructionCount_(std::move(destructionCount)) {}

        ~CountingCaptureAdapter() override {
            ++*destructionCount_;
        }

        [[nodiscard]] Result<CanonicalCaptureDisposition> Capture(const CanonicalCaptureContext &, ICanonicalCaptureSink &) const override {
            return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Omitted);
        }

    private:
        std::shared_ptr<int> destructionCount_;
    };

    using CaptureCallback = std::function<Result<CanonicalCaptureDisposition>(const CanonicalCaptureContext &, ICanonicalCaptureSink &)>;

    class CallbackCaptureAdapter final : public ICanonicalStateAdapter {
    public:
        CallbackCaptureAdapter(CaptureCallback callback, std::shared_ptr<int> destructionCount)
            : callback_(std::move(callback)), destructionCount_(std::move(destructionCount)) {}

        ~CallbackCaptureAdapter() override {
            ++*destructionCount_;
        }

        [[nodiscard]] Result<CanonicalCaptureDisposition> Capture(const CanonicalCaptureContext &context,
                                                                  ICanonicalCaptureSink &sink) const override {
            return callback_(context, sink);
        }

    private:
        CaptureCallback callback_;
        std::shared_ptr<int> destructionCount_;
    };

    class SegmentedTestPayload final : public IImmutableCanonicalPayload {
    public:
        explicit SegmentedTestPayload(std::vector<std::vector<std::byte>> segments,
                                      std::shared_ptr<std::vector<std::string>> destructionEvents = {},
                                      const std::optional<std::uint64_t> declaredByteLength = std::nullopt)
            : segments_(std::move(segments)), destructionEvents_(std::move(destructionEvents)) {
            for (const auto &segment : segments_)
                byteLength_ += static_cast<std::uint64_t>(segment.size());
            if (declaredByteLength.has_value())
                byteLength_ = *declaredByteLength;
        }

        ~SegmentedTestPayload() override {
            if (destructionEvents_ != nullptr)
                destructionEvents_->push_back("payload");
        }

        [[nodiscard]] std::uint64_t ByteLength() const noexcept override {
            return byteLength_;
        }

        [[nodiscard]] std::size_t SegmentCount() const noexcept override {
            return segments_.size();
        }

        [[nodiscard]] std::span<const std::byte> Segment(const std::size_t index) const noexcept override {
            return index < segments_.size() ? std::span<const std::byte>{segments_[index]} : std::span<const std::byte>{};
        }

    private:
        std::vector<std::vector<std::byte>> segments_;
        std::shared_ptr<std::vector<std::string>> destructionEvents_;
        std::uint64_t byteLength_{};
    };

    class LeaseCaptureAdapter final : public ICanonicalStateAdapter {
    public:
        LeaseCaptureAdapter(SaveRecordId record, std::shared_ptr<const IImmutableCanonicalPayload> payload,
                            std::shared_ptr<std::vector<std::string>> destructionEvents)
            : record_(std::move(record)), payload_(std::move(payload)), destructionEvents_(std::move(destructionEvents)) {}

        ~LeaseCaptureAdapter() override {
            destructionEvents_->push_back("adapter");
        }

        [[nodiscard]] Result<CanonicalCaptureDisposition> Capture(const CanonicalCaptureContext &,
                                                                  ICanonicalCaptureSink &sink) const override {
            const Result<void> written = sink.WriteImmutable(record_, std::move(payload_));
            if (written.HasError())
                return Result<CanonicalCaptureDisposition>::Failure(written.ErrorValue());
            return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Captured);
        }

    private:
        SaveRecordId record_;
        mutable std::shared_ptr<const IImmutableCanonicalPayload> payload_;
        std::shared_ptr<std::vector<std::string>> destructionEvents_;
    };

    [[nodiscard]] inline SaveParticipantId Participant(const std::string_view value) {
        return SaveParticipantId::Parse(value).Value();
    }

    [[nodiscard]] inline CanonicalStateParticipantDescriptor Descriptor(const std::string_view participant,
                                                                        std::vector<SaveRecordId> records, const bool required = true,
                                                                        const SaveParticipantRole roles = SaveParticipantRole::Capture) {
        return {
            .participant = Participant(participant),
            .schemaVersion = Test::V<ParticipantSchemaVersion>(1),
            .scope = SaveParticipantScope::RuntimeScene,
            .roles = roles,
            .required = required,
            .limits = {.maximumPayloadBytes = 64, .maximumRecordCount = 8, .maximumNestingDepth = 8},
            .dependencies = {},
            .ownedRecords = std::move(records),
        };
    }

    inline void Register(CanonicalStateParticipantRegistry &registry, CanonicalStateParticipantDescriptor descriptor,
                         const std::shared_ptr<int> &destructionCount) {
        REQUIRE(registry.Register(std::move(descriptor), std::make_shared<CountingCaptureAdapter>(destructionCount)).HasValue());
    }

    inline void Register(CanonicalStateParticipantRegistry &registry, CanonicalStateParticipantDescriptor descriptor,
                         std::shared_ptr<const ICanonicalStateAdapter> adapter) {
        REQUIRE(registry.Register(std::move(descriptor), std::move(adapter)).HasValue());
    }

    [[nodiscard]] inline RuntimeSaveCaptureProvenance Provenance(const SaveParticipantRegistrySnapshot &participants,
                                                                 const std::uint8_t capturedState = 90) {
        return {
            .capturedState = Test::Id<CapturedStateId>(capturedState),
            .epoch = {.value = 41},
            .sceneIncarnation = 7,
            .sceneRevision = 12,
            .registryGeneration = participants.Generation(),
        };
    }

    [[nodiscard]] inline CanonicalCaptureRecord CaptureRecord(const std::string_view participant, const SaveRecordId record,
                                                              const std::uint32_t schema = 1) {
        return {
            .participant = Participant(participant),
            .schemaVersion = Test::V<ParticipantSchemaVersion>(schema),
            .record = record,
        };
    }

    template <typename T> inline void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
        REQUIRE(result.HasError());
        INFO(result.ErrorValue().message);
        REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        REQUIRE_FALSE(result.ErrorValue().message.empty());
    }
}  // namespace Horo::Runtime::CaptureTestSupport
