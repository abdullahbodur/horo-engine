#include "SaveCaptureSnapshotTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Runtime {
    namespace {
        using namespace CaptureTestSupport;

        TEST_CASE("Capture owns borrowed bytes and seals deterministic immutable provenance", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId firstRecord = Test::Id<SaveRecordId>(1);
            const SaveRecordId secondRecord = Test::Id<SaveRecordId>(2);
            Register(registry, Descriptor("project.capture.second", {secondRecord}), destructionCount);
            Register(registry, Descriptor("project.capture.first", {firstRecord}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            const RuntimeSaveCaptureProvenance provenance = Provenance(participants);
            auto builder = RuntimeSaveCaptureBuilder::Create(provenance, participants).Value();

            std::array secondBytes{std::byte{0x20}, std::byte{0x21}};
            std::array firstBytes{std::byte{0x10}, std::byte{0x11}, std::byte{0x12}};
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.second", secondRecord), secondBytes).HasValue());
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.first", firstRecord), firstBytes).HasValue());
            secondBytes.front() = std::byte{0x7f};
            firstBytes.front() = std::byte{0x7f};

            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.IsValid());
            REQUIRE(snapshot.Provenance() == provenance);
            REQUIRE(snapshot.PayloadByteLength() == 5);
            REQUIRE(snapshot.Records().size() == 2);
            REQUIRE(snapshot.Records()[0].Record().participant.Value() == "project.capture.first");
            REQUIRE(snapshot.Records()[1].Record().participant.Value() == "project.capture.second");
            REQUIRE(snapshot.Records()[0].Segment(0).front() == std::byte{0x10});
            REQUIRE(snapshot.Records()[1].Segment(0).front() == std::byte{0x20});

            const RuntimeSaveSnapshot workerCopy = snapshot;
            REQUIRE(workerCopy.Records()[0].Segment(0).data() == snapshot.Records()[0].Segment(0).data());
            REQUIRE_FALSE(RuntimeSaveSnapshot{}.IsValid());
            REQUIRE(RuntimeSaveSnapshot{}.Records().empty());
        }

        TEST_CASE("Capture adapters receive exact admission and write only through the scoped host sink", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            auto observedContext = std::make_shared<std::optional<CanonicalCaptureContext>>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(15);
            auto adapter = std::make_shared<CallbackCaptureAdapter>(
                [observedContext, record](const CanonicalCaptureContext &context,
                                          ICanonicalCaptureSink &sink) -> Result<CanonicalCaptureDisposition> {
                *observedContext = context;
                const std::array bytes{std::byte{0x15}, std::byte{0x16}};
                const Result<void> written = sink.WriteCopied(record, bytes);
                if (written.HasError())
                    return Result<CanonicalCaptureDisposition>::Failure(written.ErrorValue());
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Captured);
            },
                destructionCount);
            Register(registry, Descriptor("project.capture.adapter", {record}), std::move(adapter));
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            RuntimeSaveCaptureLimits limits{
                .maximumParticipants = 1,
                .maximumRecords = 1,
                .maximumSegments = 2,
                .maximumPayloadBytes = 8,
                .maximumCopiedRecordBytes = 4,
            };
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits).Value();

            REQUIRE(builder.CaptureParticipants().HasValue());
            REQUIRE(observedContext->has_value());
            REQUIRE(observedContext->value().provenance == Provenance(participants));
            REQUIRE(observedContext->value().participant.Value() == "project.capture.adapter");
            REQUIRE(observedContext->value().schemaVersion == Test::V<ParticipantSchemaVersion>(1));
            REQUIRE(observedContext->value().scope == SaveParticipantScope::RuntimeScene);
            REQUIRE(observedContext->value().admission.operationRecords == 1);
            REQUIRE(observedContext->value().admission.operationSegments == 2);
            REQUIRE(observedContext->value().admission.operationPayloadBytes == 8);
            REQUIRE(observedContext->value().admission.participantRecords == 8);
            REQUIRE(observedContext->value().admission.participantPayloadBytes == 64);
            REQUIRE(observedContext->value().admission.maximumCopiedRecordBytes == 4);

            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.Records().front().Segment(0)[1] == std::byte{0x16});
            REQUIRE(snapshot.Participants().size() == 1);
            REQUIRE(snapshot.Participants().front().disposition == CanonicalCaptureDisposition::Captured);
            REQUIRE(snapshot.Participants().front().records == std::vector{record});
        }

        TEST_CASE("Capture adapters run in the registry dependency plan", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            auto order = std::make_shared<std::vector<std::string>>();
            CanonicalStateParticipantRegistry registry;
            auto consumer = Descriptor("project.capture.a_consumer", {Test::Id<SaveRecordId>(26)}, false);
            consumer.dependencies = {{Participant("project.capture.z_provider"), SaveParticipantDependencyRequirement::Required,
                                      SaveParticipantDependencyPhase::Capture}};
            Register(registry, std::move(consumer),
                     std::make_shared<CallbackCaptureAdapter>([order](const CanonicalCaptureContext &context, ICanonicalCaptureSink &) {
                order->push_back(context.participant.Value());
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Omitted);
            }, destructionCount));
            Register(registry, Descriptor("project.capture.z_provider", {Test::Id<SaveRecordId>(27)}, false),
                     std::make_shared<CallbackCaptureAdapter>([order](const CanonicalCaptureContext &context, ICanonicalCaptureSink &) {
                order->push_back(context.participant.Value());
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Omitted);
            }, destructionCount));
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();

            REQUIRE(builder.CaptureParticipants().HasValue());
            CHECK(*order == std::vector<std::string>{"project.capture.z_provider", "project.capture.a_consumer"});
        }

        TEST_CASE("Large captures retain segmented immutable leases and destroy payload before module adapter", "[unit][save][capture]") {
            auto events = std::make_shared<std::vector<std::string>>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(16);
            auto payload = std::make_shared<SegmentedTestPayload>(std::vector<std::vector<std::byte>>{{std::byte{0x01}, std::byte{0x02}},
                                                                                                      {std::byte{0x03}, std::byte{0x04},
                                                                                                       std::byte{0x05}}},
                                                                  events);
            Register(registry, Descriptor("project.capture.leased", {record}),
                     std::make_shared<LeaseCaptureAdapter>(record, std::move(payload), events));
            SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            RuntimeSaveCaptureLimits limits;
            limits.maximumCopiedRecordBytes = 1;
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits).Value();
            REQUIRE(builder.CaptureParticipants().HasValue());
            RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            registry.Close();
            participants = {};

            REQUIRE(snapshot.Records().front().ByteLength() == 5);
            REQUIRE(snapshot.Records().front().SegmentCount() == 2);
            REQUIRE(snapshot.Records().front().Segment(1).front() == std::byte{0x03});
            REQUIRE(events->empty());
            snapshot = {};
            REQUIRE(*events == std::vector<std::string>{"payload", "adapter"});
        }

        TEST_CASE("Optional omission is explicit in the immutable participant projection", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId requiredRecord = Test::Id<SaveRecordId>(17);
            const SaveRecordId optionalRecord = Test::Id<SaveRecordId>(18);
            Register(registry, Descriptor("project.capture.required", {requiredRecord}),
                     std::make_shared<
                         CallbackCaptureAdapter>([requiredRecord](const CanonicalCaptureContext &, ICanonicalCaptureSink &sink) {
                const std::array bytes{std::byte{0x17}};
                const Result<void> written = sink.WriteCopied(requiredRecord, bytes);
                if (written.HasError())
                    return Result<CanonicalCaptureDisposition>::Failure(written.ErrorValue());
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Captured);
            }, destructionCount));
            Register(registry, Descriptor("project.capture.optional.omitted", {optionalRecord}, false),
                     std::make_shared<CallbackCaptureAdapter>([](const CanonicalCaptureContext &, ICanonicalCaptureSink &) {
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Omitted);
            }, destructionCount));
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            REQUIRE(builder.CaptureParticipants().HasValue());

            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.Participants().size() == 2);
            REQUIRE(snapshot.Participants()[0].participant.Value() == "project.capture.optional.omitted");
            REQUIRE(snapshot.Participants()[0].disposition == CanonicalCaptureDisposition::Omitted);
            REQUIRE(snapshot.Participants()[0].records.empty());
            REQUIRE(snapshot.Participants()[1].participant.Value() == "project.capture.required");
            REQUIRE(snapshot.Participants()[1].disposition == CanonicalCaptureDisposition::Captured);
            REQUIRE(snapshot.Participants()[1].records == std::vector{requiredRecord});
        }

        TEST_CASE("Capture validates exact safe-point provenance and registry generation", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            Register(registry, Descriptor("project.capture.context", {Test::Id<SaveRecordId>(3)}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();

            auto invalid = Provenance(participants);
            invalid.capturedState = {};
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureContextInvalid);
            invalid = Provenance(participants);
            invalid.epoch = {};
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureContextInvalid);
            invalid = Provenance(participants);
            invalid.sceneIncarnation = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureContextInvalid);
            invalid = Provenance(participants);
            invalid.sceneRevision = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureContextInvalid);
            invalid = Provenance(participants);
            invalid.registryGeneration = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureContextInvalid);
            invalid = Provenance(participants);
            ++invalid.registryGeneration;
            RequireError(RuntimeSaveCaptureBuilder::Create(invalid, participants), SaveErrors::CaptureRegistryStale);
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), SaveParticipantRegistrySnapshot{}),
                         SaveErrors::CaptureContextInvalid);
        }

        TEST_CASE("Capture validates every operation allocation bound", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            Register(registry, Descriptor("project.capture.bounds", {Test::Id<SaveRecordId>(27)}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();

            RuntimeSaveCaptureLimits limits;
            limits.maximumParticipants = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumParticipants = MaximumSaveParticipantCount + 1;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumRecords = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumRecords = MaximumRuntimeSaveCaptureRecords + 1;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumSegments = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumSegments = MaximumRuntimeSaveCaptureSegments + 1;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumPayloadBytes = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumPayloadBytes = MaximumRuntimeSaveCapturePayloadBytes + 1;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumCopiedRecordBytes = 0;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
            limits = {};
            limits.maximumCopiedRecordBytes = MaximumRuntimeSaveCapturePayloadBytes + 1;
            RequireError(RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits),
                         SaveErrors::CaptureContextInvalid);
        }

        TEST_CASE("Capture rejects foreign schema role and record provenance", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId captureRecord = Test::Id<SaveRecordId>(4);
            const SaveRecordId restoreRecord = Test::Id<SaveRecordId>(5);
            Register(registry, Descriptor("project.capture.owner", {captureRecord}), destructionCount);
            Register(registry, Descriptor("project.restore.owner", {restoreRecord}, false, SaveParticipantRole::Restore), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            const std::array bytes{std::byte{0x01}};

            RequireError(builder.AddRecord(CaptureRecord("project.capture.missing", captureRecord), bytes),
                         SaveErrors::CaptureRecordInvalid);
            RequireError(builder.AddRecord(CaptureRecord("project.capture.owner", captureRecord, 2), bytes),
                         SaveErrors::CaptureRecordInvalid);
            RequireError(builder.AddRecord(CaptureRecord("project.capture.owner", Test::Id<SaveRecordId>(6)), bytes),
                         SaveErrors::CaptureRecordInvalid);
            RequireError(builder.AddRecord(CaptureRecord("project.restore.owner", restoreRecord), bytes), SaveErrors::CaptureRecordInvalid);
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.owner", captureRecord), bytes).HasValue());
            REQUIRE(builder.Seal().HasValue());
        }

        TEST_CASE("Capture checks participant and aggregate budgets before retaining bytes", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(7);
            auto descriptor = Descriptor("project.capture.bounded", {record});
            descriptor.limits.maximumPayloadBytes = 2;
            Register(registry, std::move(descriptor), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();

            RuntimeSaveCaptureLimits limits{.maximumParticipants = 1, .maximumRecords = 1, .maximumPayloadBytes = 2};
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits).Value();
            const std::array oversized{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
            RequireError(builder.AddRecord(CaptureRecord("project.capture.bounded", record), oversized), SaveErrors::CaptureBudgetExceeded);

            const std::array admitted{std::byte{0x04}, std::byte{0x05}};
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.bounded", record), admitted).HasValue());
            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.PayloadByteLength() == 2);
            REQUIRE(snapshot.Records().front().Segment(0).data() != admitted.data());
        }

        TEST_CASE("Capture accounting rejects cumulative byte and record count overflow transactionally", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId first = Test::Id<SaveRecordId>(13);
            const SaveRecordId second = Test::Id<SaveRecordId>(14);
            Register(registry, Descriptor("project.capture.accounting", {first, second}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            const std::array twoBytes{std::byte{0x01}, std::byte{0x02}};
            const std::array oneByte{std::byte{0x03}};

            RuntimeSaveCaptureLimits byteLimits{.maximumParticipants = 1, .maximumRecords = 2, .maximumPayloadBytes = 3};
            auto byteBounded = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, byteLimits).Value();
            REQUIRE(byteBounded.AddRecord(CaptureRecord("project.capture.accounting", first), twoBytes).HasValue());
            RequireError(byteBounded.AddRecord(CaptureRecord("project.capture.accounting", second), twoBytes),
                         SaveErrors::CaptureBudgetExceeded);
            REQUIRE(byteBounded.AddRecord(CaptureRecord("project.capture.accounting", second), oneByte).HasValue());
            REQUIRE(byteBounded.Seal().Value().PayloadByteLength() == 3);

            RuntimeSaveCaptureLimits recordLimits{.maximumParticipants = 1, .maximumRecords = 1, .maximumPayloadBytes = 3};
            auto recordBounded = RuntimeSaveCaptureBuilder::Create(Provenance(participants, 92), participants, recordLimits).Value();
            REQUIRE(recordBounded.AddRecord(CaptureRecord("project.capture.accounting", first), oneByte).HasValue());
            RequireError(recordBounded.AddRecord(CaptureRecord("project.capture.accounting", second), oneByte),
                         SaveErrors::CaptureBudgetExceeded);
        }

        TEST_CASE("Participant-local and immutable segment limits reject before ownership transfer", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(19);
            auto descriptor = Descriptor("project.capture.local_limit", {record});
            descriptor.limits.maximumPayloadBytes = 2;
            Register(registry, std::move(descriptor), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            RuntimeSaveCaptureLimits limits;
            limits.maximumSegments = 1;
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants, limits).Value();
            const std::array oversized{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
            RequireError(builder.AddRecord(CaptureRecord("project.capture.local_limit", record), oversized),
                         SaveErrors::CaptureBudgetExceeded);

            auto tooManySegments =
                std::make_shared<SegmentedTestPayload>(std::vector<std::vector<std::byte>>{{std::byte{0x01}}, {std::byte{0x02}}});
            RequireError(builder.AddImmutableRecord(CaptureRecord("project.capture.local_limit", record), std::move(tooManySegments)),
                         SaveErrors::CaptureBudgetExceeded);
            auto malformed = std::make_shared<SegmentedTestPayload>(std::vector<std::vector<std::byte>>{{std::byte{0x01}}}, nullptr, 2);
            RequireError(builder.AddImmutableRecord(CaptureRecord("project.capture.local_limit", record), std::move(malformed)),
                         SaveErrors::CaptureRecordInvalid);

            auto valid = std::make_shared<SegmentedTestPayload>(std::vector<std::vector<std::byte>>{{std::byte{0x01}, std::byte{0x02}}});
            REQUIRE(builder.AddImmutableRecord(CaptureRecord("project.capture.local_limit", record), std::move(valid)).HasValue());
            REQUIRE(builder.Seal().Value().PayloadByteLength() == 2);
        }

        TEST_CASE("Canonical ordering includes records owned by the same participant", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId low = Test::Id<SaveRecordId>(20);
            const SaveRecordId high = Test::Id<SaveRecordId>(21);
            Register(registry, Descriptor("project.capture.same_owner", {high, low}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            const std::array bytes{std::byte{0x01}};
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.same_owner", high), bytes).HasValue());
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.same_owner", low), bytes).HasValue());

            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.Records()[0].Record().record == low);
            REQUIRE(snapshot.Records()[1].Record().record == high);
            REQUIRE(snapshot.Participants().front().records == std::vector{low, high});
        }

        TEST_CASE("Moved-from builders fail closed and self-move preserves the live builder", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(22);
            Register(registry, Descriptor("project.capture.move", {record}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto original = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            auto moved = std::move(original);
            const std::array bytes{std::byte{0x22}};

            RequireError(original.AddRecord(CaptureRecord("project.capture.move", record), bytes), SaveErrors::CaptureAlreadySealed);
            RequireError(original.CaptureParticipants(), SaveErrors::CaptureAlreadySealed);
            RequireError(original.Seal(), SaveErrors::CaptureAlreadySealed);
            moved = std::move(moved);
            REQUIRE(moved.AddRecord(CaptureRecord("project.capture.move", record), bytes).HasValue());
            REQUIRE(moved.Seal().Value().IsValid());
        }

        TEST_CASE("Successful seal transfers the registry lease out of the spent builder", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(23);
            Register(registry, Descriptor("project.capture.seal_lease", {record}), destructionCount);
            SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            const std::array bytes{std::byte{0x23}};
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.seal_lease", record), bytes).HasValue());
            RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            registry.Close();
            participants = {};
            REQUIRE(*destructionCount == 0);

            snapshot = {};
            REQUIRE(*destructionCount == 1);
            RequireError(builder.Seal(), SaveErrors::CaptureAlreadySealed);
        }

        TEST_CASE("Adapter allocation and ignored sink failures roll capture back with typed errors", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(24);
            Register(registry, Descriptor("project.capture.allocation", {record}),
                     std::make_shared<CallbackCaptureAdapter>(
                         [](const CanonicalCaptureContext &, ICanonicalCaptureSink &) -> Result<CanonicalCaptureDisposition> {
                throw std::bad_alloc{};
            }, destructionCount));
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            RequireError(builder.CaptureParticipants(), SaveErrors::CaptureAllocationFailed);
            const std::array bytes{std::byte{0x24}};
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.allocation", record), bytes).HasValue());
            REQUIRE(builder.Seal().HasValue());

            CanonicalStateParticipantRegistry ignoredRegistry;
            const SaveRecordId ignoredRecord = Test::Id<SaveRecordId>(25);
            auto descriptor = Descriptor("project.capture.ignored_sink", {ignoredRecord});
            descriptor.limits.maximumPayloadBytes = 1;
            Register(ignoredRegistry, std::move(descriptor),
                     std::make_shared<
                         CallbackCaptureAdapter>([ignoredRecord](const CanonicalCaptureContext &, ICanonicalCaptureSink &sink) {
                const std::array oversized{std::byte{0x01}, std::byte{0x02}};
                static_cast<void>(sink.WriteCopied(ignoredRecord, oversized));
                return Result<CanonicalCaptureDisposition>::Success(CanonicalCaptureDisposition::Captured);
            }, destructionCount));
            const SaveParticipantRegistrySnapshot ignoredParticipants = ignoredRegistry.Snapshot().Value();
            auto ignored = RuntimeSaveCaptureBuilder::Create(Provenance(ignoredParticipants, 93), ignoredParticipants).Value();
            RequireError(ignored.CaptureParticipants(), SaveErrors::CaptureAdapterContractInvalid);
            REQUIRE(ignored.AddRecord(CaptureRecord("project.capture.ignored_sink", ignoredRecord), bytes).HasValue());
            REQUIRE(ignored.Seal().HasValue());
        }

        TEST_CASE("Capture error descriptors expose stable actionable diagnostics", "[unit][save][capture]") {
            const std::array descriptors{
                &SaveErrors::CaptureContextInvalid, &SaveErrors::CaptureRegistryStale,    &SaveErrors::CaptureRecordInvalid,
                &SaveErrors::CaptureBudgetExceeded, &SaveErrors::CaptureRecordDuplicate,  &SaveErrors::CaptureIncomplete,
                &SaveErrors::CaptureAlreadySealed,  &SaveErrors::CaptureAllocationFailed, &SaveErrors::CaptureAdapterContractInvalid,
            };
            for (const ErrorCodeDescriptor *descriptor : descriptors) {
                REQUIRE(descriptor->domain.Value() == "horo.save");
                REQUIRE_FALSE(descriptor->code.Value().empty());
                REQUIRE(descriptor->defaultSeverity == ErrorSeverity::Error);
                REQUIRE_FALSE(descriptor->summary.empty());
                REQUIRE_FALSE(descriptor->remediationHint.empty());
            }
            REQUIRE(SaveErrors::CaptureAllocationFailed.retryable);
            REQUIRE_FALSE(SaveErrors::CaptureAdapterContractInvalid.retryable);
        }

        TEST_CASE("Capture seal is transactional and requires complete participant record sets", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId first = Test::Id<SaveRecordId>(8);
            const SaveRecordId second = Test::Id<SaveRecordId>(9);
            Register(registry, Descriptor("project.capture.complete", {first, second}), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            const std::array bytes{std::byte{0x09}};

            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.complete", first), bytes).HasValue());
            RequireError(builder.AddRecord(CaptureRecord("project.capture.complete", first), bytes), SaveErrors::CaptureRecordDuplicate);
            RequireError(builder.Seal(), SaveErrors::CaptureIncomplete);
            REQUIRE(builder.AddRecord(CaptureRecord("project.capture.complete", second), bytes).HasValue());
            REQUIRE(builder.Seal().HasValue());
            RequireError(builder.AddRecord(CaptureRecord("project.capture.complete", second), bytes), SaveErrors::CaptureAlreadySealed);
            RequireError(builder.Seal(), SaveErrors::CaptureAlreadySealed);
        }

        TEST_CASE("Optional capture owners are absent or complete but never partially mixed", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId first = Test::Id<SaveRecordId>(10);
            const SaveRecordId second = Test::Id<SaveRecordId>(11);
            Register(registry, Descriptor("project.capture.optional", {first, second}, false), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();

            auto absent = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
            const RuntimeSaveSnapshot empty = absent.Seal().Value();
            REQUIRE(empty.IsValid());
            REQUIRE(empty.Records().empty());

            auto partial = RuntimeSaveCaptureBuilder::Create(Provenance(participants, 91), participants).Value();
            const std::array bytes{std::byte{0x0a}};
            REQUIRE(partial.AddRecord(CaptureRecord("project.capture.optional", first), bytes).HasValue());
            RequireError(partial.Seal(), SaveErrors::CaptureIncomplete);
        }

        TEST_CASE("Detached snapshot pins payload and adapter leases through shutdown", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            RuntimeSaveSnapshot detached;
            {
                CanonicalStateParticipantRegistry registry;
                const SaveRecordId record = Test::Id<SaveRecordId>(12);
                Register(registry, Descriptor("project.capture.lifetime", {record}), destructionCount);
                const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
                auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants), participants).Value();
                const std::array bytes{std::byte{0x0c}};
                REQUIRE(builder.AddRecord(CaptureRecord("project.capture.lifetime", record), bytes).HasValue());
                detached = builder.Seal().Value();
                registry.Close();
                REQUIRE(*destructionCount == 0);
            }

            REQUIRE(detached.IsValid());
            REQUIRE(detached.Records().front().Segment(0).front() == std::byte{0x0c});
            REQUIRE(*destructionCount == 0);
            RuntimeSaveSnapshot cancellationOwner = detached;
            detached = {};
            REQUIRE(*destructionCount == 0);
            cancellationOwner = {};
            REQUIRE(*destructionCount == 1);
        }
    }  // namespace
}  // namespace Horo::Runtime
