#include "AiTestSupport.h"
#include "Horo/AI/BlackboardInstance.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::AI {
    using namespace TestSupport;

    namespace {
        [[nodiscard]] BlackboardValue Bool(const bool value) {
            return BlackboardValue{BlackboardScalarValue{value}};
        }

        [[nodiscard]] BlackboardValue Integer(const std::int64_t value) {
            return BlackboardValue{BlackboardScalarValue{value}};
        }

        [[nodiscard]] BlackboardKeyDescriptor Key(const std::uint64_t id, const BlackboardValueKind kind,
                                                  std::optional<BlackboardValue> value = Bool(false),
                                                  const BlackboardKeyPresence presence = BlackboardKeyPresence::Required,
                                                  const BlackboardKeyAccess access = BlackboardKeyAccess::ReadWrite) {
            return {.key = MakeIdentity<BlackboardKeyId>(id),
                    .kind = kind,
                    .cardinality = BlackboardValueCardinality::Scalar,
                    .maximumCollectionElements = 1,
                    .presence = presence,
                    .access = access,
                    .defaultValue = std::move(value)};
        }

        [[nodiscard]] std::shared_ptr<const BlackboardSchema> Schema(std::vector<BlackboardKeyDescriptor> keys,
                                                                     const std::uint64_t identity = 20, const std::uint32_t version = 1) {
            auto schema = BlackboardSchema::Capture(
                {MakeIdentity<BlackboardSchemaId>(identity), version, BlackboardUnknownValuePolicy::Reject, keys});
            REQUIRE(schema.HasValue());
            return std::make_shared<const BlackboardSchema>(std::move(schema).Value());
        }

        [[nodiscard]] BlackboardInstanceBinding Binding(const std::shared_ptr<const BlackboardSchema> &schema,
                                                        const std::uint64_t runtime = 7, const std::uint32_t generation = 1,
                                                        const std::uint64_t schemaGeneration = 1) {
            auto incarnation = AiRuntimeIncarnation::Create(runtime);
            REQUIRE(incarnation.HasValue());
            return {{incarnation.Value(), {3, 4}}, schema->Identity(), schema->Version(), schemaGeneration, generation};
        }

        [[nodiscard]] std::unique_ptr<BlackboardInstance> Instance(const std::shared_ptr<const BlackboardSchema> &schema) {
            auto instance = BlackboardInstance::Create(Binding(schema), schema);
            REQUIRE(instance.HasValue());
            return std::move(instance).Value();
        }
    }  // namespace

    TEST_CASE("Blackboard instance materializes typed schema defaults", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(2, BlackboardValueKind::Boolean, Bool(true)), Key(1, BlackboardValueKind::SignedInteger, Integer(9))});
        auto instance = Instance(schema);
        auto snapshot = instance->Snapshot().Value();
        CHECK(snapshot.Revision().Value() == 1);
        CHECK(snapshot.Read(MakeIdentity<BlackboardKeyId>(1)).Value() == std::optional{Integer(9)});
        CHECK(snapshot.Read(MakeIdentity<BlackboardKeyId>(2)).Value() == std::optional{Bool(true)});
        ExpectError(snapshot.Read(MakeIdentity<BlackboardKeyId>(99)), AIErrors::BlackboardUnknownValueRejected);
        static_assert(!std::is_default_constructible_v<BlackboardInstance>);
        static_assert(!std::is_copy_constructible_v<BlackboardWriteBatch>);
        static_assert(std::is_same_v<decltype(std::declval<const BlackboardWriteBatch>().Writes()), std::span<const BlackboardWrite>>);
        static_assert(std::is_same_v<decltype(std::declval<const BlackboardSnapshot>().Read(BlackboardKeyId{})),
                                     Result<std::optional<BlackboardValue>>>);
    }

    TEST_CASE("Blackboard instance rejects missing required defaults and invalid binding", "[unit][ai][blackboard-instance]") {
        auto missing = Schema({Key(1, BlackboardValueKind::Boolean, std::nullopt)});
        ExpectError(BlackboardInstance::Create(Binding(missing), missing), AIErrors::BlackboardInstanceInvalid);
        auto valid = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto binding = Binding(valid);
        binding.schemaGeneration = 0;
        ExpectError(BlackboardInstance::Create(binding, valid), AIErrors::BlackboardInstanceInvalid);
    }

    TEST_CASE("Blackboard batch commits atomically in deterministic key order", "[unit][ai][blackboard-instance]") {
        auto schema =
            Schema({Key(3, BlackboardValueKind::Boolean), Key(1, BlackboardValueKind::Boolean), Key(2, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto batch = instance->BeginWriteBatch().Value();
        REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(3), Bool(true)}).HasValue());
        REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        auto committed = instance->CommitAtBlackboardSync(std::move(batch));
        REQUIRE(committed.HasValue());
        CHECK(committed.Value().revision == 2);
        CHECK(std::ranges::equal(committed.Value().Changes(),
                                 std::array{MakeIdentity<BlackboardKeyId>(1), MakeIdentity<BlackboardKeyId>(3)}));
    }

    TEST_CASE("Blackboard invalid write rolls back complete batch", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean), Key(2, BlackboardValueKind::SignedInteger, Integer(0))});
        auto instance = Instance(schema);
        auto before = instance->Snapshot().Value();
        auto batch = instance->BeginWriteBatch().Value();
        REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(2), Bool(true)}).HasValue());
        ExpectError(instance->CommitAtBlackboardSync(std::move(batch)), AIErrors::BlackboardValueTypeMismatch);
        CHECK(before.Revision().Value() == 1);
        CHECK(before.Read(MakeIdentity<BlackboardKeyId>(1)).Value() == std::optional{Bool(false)});
    }

    TEST_CASE("Blackboard batches reject duplicate unknown and stale writes", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto duplicate = instance->BeginWriteBatch().Value();
        REQUIRE(duplicate.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        ExpectError(duplicate.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(false)}), AIErrors::BlackboardBatchInvalid);
        auto unknown = instance->BeginWriteBatch().Value();
        REQUIRE(unknown.Stage({MakeIdentity<BlackboardKeyId>(99), Bool(true)}).HasValue());
        ExpectError(instance->CommitAtBlackboardSync(std::move(unknown)), AIErrors::BlackboardUnknownValueRejected);
        auto stale = instance->BeginWriteBatch().Value();
        auto winner = instance->BeginWriteBatch().Value();
        REQUIRE(winner.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        REQUIRE(instance->CommitAtBlackboardSync(std::move(winner)).HasValue());
        ExpectError(instance->CommitAtBlackboardSync(std::move(stale)), AIErrors::BlackboardInstanceStale);
    }

    TEST_CASE("Blackboard batch enforces exact and one-over write capacity", "[unit][ai][blackboard-instance]") {
        std::vector<BlackboardKeyDescriptor> keys;
        for (std::uint64_t id = 1; id <= MaximumBlackboardKeys; ++id)
            keys.push_back(Key(id, BlackboardValueKind::Boolean));
        auto instance = Instance(Schema(std::move(keys)));
        auto batch = instance->BeginWriteBatch().Value();
        for (std::uint64_t id = 1; id <= MaximumBlackboardWritesPerBatch; ++id)
            REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(id), Bool(true)}).HasValue());
        ExpectError(batch.Stage({MakeIdentity<BlackboardKeyId>(MaximumBlackboardWritesPerBatch + 1), Bool(true)}),
                    AIErrors::BlackboardLimitExceeded);
        REQUIRE(instance->CommitAtBlackboardSync(std::move(batch)).HasValue());
    }

    TEST_CASE("Blackboard no-op and changed commits have monotonic revisions", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto noOp = instance->BeginWriteBatch().Value();
        REQUIRE(noOp.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(false)}).HasValue());
        auto unchanged = instance->CommitAtBlackboardSync(std::move(noOp)).Value();
        CHECK(unchanged.revision == 1);
        CHECK(unchanged.Changes().empty());
        auto changed = instance->BeginWriteBatch().Value();
        REQUIRE(changed.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        CHECK(instance->CommitAtBlackboardSync(std::move(changed)).Value().revision == 2);
    }

    TEST_CASE("Blackboard snapshots are immutable value copies across ordinary commits", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto before = instance->Snapshot().Value();
        auto changed = instance->BeginWriteBatch().Value();
        REQUIRE(changed.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        REQUIRE(instance->CommitAtBlackboardSync(std::move(changed)).HasValue());
        CHECK(before.Revision().Value() == 1);
        CHECK(before.Read(MakeIdentity<BlackboardKeyId>(1)).Value() == std::optional{Bool(false)});
        CHECK(instance->Snapshot().Value().Read(MakeIdentity<BlackboardKeyId>(1)).Value() == std::optional{Bool(true)});
    }

    TEST_CASE("Blackboard commits reject writes to read-only schema keys", "[unit][ai][blackboard-instance]") {
        auto schema =
            Schema({Key(1, BlackboardValueKind::Boolean, Bool(false), BlackboardKeyPresence::Required, BlackboardKeyAccess::ReadOnly)});
        auto instance = Instance(schema);
        auto batch = instance->BeginWriteBatch().Value();
        REQUIRE(batch.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        ExpectError(instance->CommitAtBlackboardSync(std::move(batch)), AIErrors::BlackboardBatchInvalid);
        CHECK(instance->Snapshot().Value().Revision().Value() == 1);
    }

    TEST_CASE("Blackboard replacement expires old snapshots and preserves compatible values", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto write = instance->BeginWriteBatch().Value();
        REQUIRE(write.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        REQUIRE(instance->CommitAtBlackboardSync(std::move(write)).HasValue());
        auto old = instance->Snapshot().Value();
        auto replacement = Schema({Key(1, BlackboardValueKind::Boolean), Key(2, BlackboardValueKind::Boolean)}, 20, 2);
        auto replacementBinding = Binding(replacement, 7, 2, 2);
        REQUIRE(instance->ReplaceAtBlackboardSync(replacementBinding, replacement).HasValue());
        ExpectError(old.Revision(), AIErrors::BlackboardInstanceStale);
        auto current = instance->Snapshot().Value();
        CHECK(current.Binding() == replacementBinding);
        CHECK(current.Read(MakeIdentity<BlackboardKeyId>(1)).Value() == std::optional{Bool(true)});
        CHECK(current.Revision().Value() == 3);
    }

    TEST_CASE("Blackboard replacement rejects batches from the previous generation", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto stale = instance->BeginWriteBatch().Value();
        auto replacement = Schema({Key(1, BlackboardValueKind::Boolean)}, 20, 2);
        REQUIRE(instance->ReplaceAtBlackboardSync(Binding(replacement, 7, 2, 2), replacement).HasValue());
        ExpectError(instance->CommitAtBlackboardSync(std::move(stale)), AIErrors::BlackboardInstanceStale);
    }

    TEST_CASE("Blackboard failed replacement preserves active instance", "[unit][ai][blackboard-instance]") {
        auto schema = Schema({Key(1, BlackboardValueKind::Boolean)});
        auto instance = Instance(schema);
        auto old = instance->Snapshot().Value();
        auto incompatible = Schema({Key(1, BlackboardValueKind::SignedInteger, Integer(0))}, 20, 2);
        ExpectError(instance->ReplaceAtBlackboardSync(Binding(incompatible, 7, 2, 2), incompatible), AIErrors::BlackboardValueTypeMismatch);
        CHECK(old.Revision().Value() == 1);
        CHECK(instance->Snapshot().Value().Binding().instanceGeneration == 1);

        auto foreignBinding = Binding(schema, 8, 2, 2);
        ExpectError(instance->ReplaceAtBlackboardSync(foreignBinding, schema), AIErrors::BlackboardInstanceInvalid);
        auto sameSchemaGeneration = Binding(schema, 7, 2, 1);
        ExpectError(instance->ReplaceAtBlackboardSync(sameSchemaGeneration, schema), AIErrors::BlackboardInstanceInvalid);
        CHECK(old.Revision().Value() == 1);
    }

    TEST_CASE("Blackboard reset and teardown are explicit lifecycle paths", "[unit][ai][blackboard-instance]") {
        auto schema = Schema(
            {Key(1, BlackboardValueKind::Boolean), Key(2, BlackboardValueKind::Boolean, std::nullopt, BlackboardKeyPresence::Optional)});
        auto instance = Instance(schema);
        auto write = instance->BeginWriteBatch().Value();
        REQUIRE(write.Stage({MakeIdentity<BlackboardKeyId>(1), Bool(true)}).HasValue());
        REQUIRE(write.Stage({MakeIdentity<BlackboardKeyId>(2), Bool(true)}).HasValue());
        REQUIRE(instance->CommitAtBlackboardSync(std::move(write)).HasValue());
        auto reset = instance->ResetAtBlackboardSync().Value();
        CHECK(reset.revision == 3);
        CHECK(std::ranges::equal(reset.Changes(), std::array{MakeIdentity<BlackboardKeyId>(1), MakeIdentity<BlackboardKeyId>(2)}));
        CHECK(instance->Snapshot().Value().Read(MakeIdentity<BlackboardKeyId>(2)).Value() == std::nullopt);
        auto snapshot = instance->Snapshot().Value();
        instance->TeardownAtBlackboardSync();
        instance->TeardownAtBlackboardSync();
        CHECK_FALSE(instance->IsActive());
        ExpectError(snapshot.Read(MakeIdentity<BlackboardKeyId>(1)), AIErrors::BlackboardInstanceStale);
        ExpectError(instance->BeginWriteBatch(), AIErrors::BlackboardInstanceStale);
    }
}  // namespace Horo::AI
