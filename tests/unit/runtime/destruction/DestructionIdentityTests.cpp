#include "Horo/Destruction/DestructionIdentity.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Horo::Destruction {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        FractureAssetId Asset(const std::uint8_t suffix = 1) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = suffix;
            auto asset = FractureAssetId::Create(Assets::AssetId::FromBytes(bytes));
            REQUIRE(asset.HasValue());
            return asset.Value();
        }

        Sha256Digest Digest(const std::uint8_t suffix = 1) {
            Sha256Digest digest{};
            digest.bytes.back() = suffix;
            return digest;
        }

        FractureArtifactContentIdentity Content(const std::uint8_t asset = 1, const std::uint64_t revision = 2,
                                                const std::uint8_t digest = 3) {
            auto content = FractureArtifactContentIdentity::Create(Asset(asset), Id<FractureContentRevision>(revision), Digest(digest));
            REQUIRE(content.HasValue());
            return content.Value();
        }

        DestructionHandle Handle(const std::uint64_t world = 1, const std::uint64_t destructible = 2, const std::uint64_t generation = 3) {
            return {Id<DestructionWorldId>(world), Id<DestructibleId>(destructible), Id<DestructionGeneration>(generation)};
        }

        DestructionCommandId Command(const DestructionHandle target = Handle(), const std::uint64_t value = 4) {
            return {target, Id<DestructionCommandValue>(value)};
        }

        DestructionEventOccurrenceId Event(const DestructionHandle source = Handle(), const std::uint64_t revision = 5,
                                           const DestructionFactKind kind = DestructionFactKind::ChunksActivated,
                                           const std::uint32_t ordinal = 6) {
            return {source, Id<DestructionStateRevision>(revision), kind, ordinal};
        }

        template <typename Value> void CheckError(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            const Error &actual = result.ErrorValue();
            CHECK(expected.domain.Value() == actual.domain.Value());
            CHECK(expected.code.Value() == actual.code.Value());
        }
    }  // namespace

    TEST_CASE("Destruction authored identities are strong stable values", "[unit][destruction][identity]") {
        REQUIRE(DestructibleId::Create(0).HasError());
        REQUIRE(DestructionChunkId::Create(0).HasError());
        REQUIRE(FractureContentRevision::Create(0).HasError());
        REQUIRE(FractureAssetId::Create({}).HasError());
        static_assert(!std::is_same_v<DestructibleId, DestructionChunkId>);
        static_assert(!std::is_convertible_v<std::uint64_t, DestructibleId>);
        static_assert(std::is_trivially_copyable_v<DestructibleId>);
        static_assert(std::is_trivially_copyable_v<DestructionHandle>);
        const std::set ordered{Id<DestructionChunkId>(9), Id<DestructionChunkId>(2), Id<DestructionChunkId>(5)};
        CHECK(ordered == std::set{Id<DestructionChunkId>(2), Id<DestructionChunkId>(5), Id<DestructionChunkId>(9)});
        const auto bytes = SerializeDestructionStableIdentity(Id<DestructibleId>(0x0102030405060708ULL));
        CHECK((bytes == SerializedDestructionStableIdentity{1, 2, 3, 4, 5, 6, 7, 8}));
        CHECK(DeserializeDestructionStableIdentity<DestructibleIdentityTag>(bytes).Value() == Id<DestructibleId>(0x0102030405060708ULL));
        CheckError(DeserializeDestructionStableIdentity<DestructibleIdentityTag>({}), DestructionErrors::SerializedIdentityInvalid);
    }

    TEST_CASE("Fracture asset identity is path independent and round trips exact bytes", "[unit][destruction][identity]") {
        const FractureAssetId asset = Asset(0x7f);
        const auto bytes = SerializeFractureAssetId(asset);
        CHECK(bytes.front() == 0);
        CHECK(bytes.back() == 0x7f);
        CHECK(DeserializeFractureAssetId(bytes).Value() == asset);
        CHECK(DeserializeFractureAssetId({}).ErrorValue().code.Value() == DestructionErrors::SerializedIdentityInvalid.code.Value());
    }

    TEST_CASE("Fracture content identity requires asset revision and semantic digest", "[unit][destruction][identity]") {
        CHECK_FALSE(FractureArtifactContentIdentity{}.IsValid());
        REQUIRE(FractureArtifactContentIdentity::Create({}, Id<FractureContentRevision>(1), Digest()).HasError());
        REQUIRE(FractureArtifactContentIdentity::Create(Asset(), {}, Digest()).HasError());
        REQUIRE(FractureArtifactContentIdentity::Create(Asset(), Id<FractureContentRevision>(1), {}).HasError());
        const auto content = Content(4, 0x0102030405060708ULL, 9);
        const auto bytes = SerializeFractureArtifactContentIdentity(content);
        CHECK(bytes[15] == 4);
        CHECK(bytes[16] == 0x01);
        CHECK(bytes[23] == 0x08);
        CHECK(bytes[55] == 9);
        CHECK(DeserializeFractureArtifactContentIdentity(bytes).Value() == content);
    }

    TEST_CASE("Stable chunk identity is independent of table order and exact-content scoped", "[unit][destruction][identity]") {
        std::vector<DestructionChunkId> imported{Id<DestructionChunkId>(30), Id<DestructionChunkId>(10), Id<DestructionChunkId>(20)};
        std::ranges::sort(imported);
        CHECK(imported == std::vector{Id<DestructionChunkId>(10), Id<DestructionChunkId>(20), Id<DestructionChunkId>(30)});
        const FractureChunkIdentity original{Content(1, 2, 3), imported[1]};
        const FractureChunkIdentity recookedSame{Content(1, 2, 3), Id<DestructionChunkId>(20)};
        const FractureChunkIdentity replacement{Content(1, 3, 4), Id<DestructionChunkId>(20)};
        CHECK(original == recookedSame);
        CHECK(original != replacement);
        CHECK(DeserializeFractureChunkIdentity(SerializeFractureChunkIdentity(original)).Value() == original);
        CheckError(ValidateFractureChunkAccess(original, replacement.content), DestructionErrors::StaleContent);
    }

    TEST_CASE("Runtime handles reject foreign owners and retired generations", "[unit][destruction][identity]") {
        const DestructionHandle current = Handle(1, 2, 4);
        CHECK(ValidateDestructionHandleAccess(current, current).HasValue());
        CheckError(ValidateDestructionHandleAccess(Handle(2, 2, 4), current), DestructionErrors::IdentityUnknown);
        CheckError(ValidateDestructionHandleAccess(Handle(1, 9, 4), current), DestructionErrors::IdentityUnknown);
        CheckError(ValidateDestructionHandleAccess(Handle(1, 2, 3), current), DestructionErrors::StaleGeneration);
        CheckError(ValidateDestructionHandleAccess({}, current), DestructionErrors::IdentityInvalid);
        CHECK(DeserializeDestructionHandle(SerializeDestructionHandle(current)).Value() == current);
    }

    TEST_CASE("Command identity retains target generation across retries and replacement", "[unit][destruction][identity]") {
        const DestructionCommandId command = Command(Handle(7, 8, 9), 10);
        CHECK(command.IsValid());
        CHECK(DeserializeDestructionCommandId(SerializeDestructionCommandId(command)).Value() == command);
        CHECK(ValidateDestructionCommandAccess(command, command.target).HasValue());
        CheckError(ValidateDestructionCommandAccess(command, Handle(7, 8, 10)), DestructionErrors::StaleGeneration);
        CheckError(ValidateDestructionCommandAccess({}, command.target), DestructionErrors::IdentityInvalid);
    }

    TEST_CASE("Event occurrence identity is deterministic and collision free within a committed revision",
              "[unit][destruction][identity]") {
        const DestructionEventOccurrenceId first = Event(Handle(2, 3, 4), 5, DestructionFactKind::SupportLost, 0);
        const DestructionEventOccurrenceId repeated = Event(Handle(2, 3, 4), 5, DestructionFactKind::SupportLost, 0);
        const DestructionEventOccurrenceId next = Event(Handle(2, 3, 4), 5, DestructionFactKind::SupportLost, 1);
        const DestructionEventOccurrenceId otherKind = Event(Handle(2, 3, 4), 5, DestructionFactKind::ChunksDormant, 0);

        CHECK(first == repeated);
        CHECK(first != next);
        CHECK(first != otherKind);
        const auto bytes = SerializeDestructionEventOccurrenceId(first);
        CHECK(bytes[23] == 4);
        CHECK(bytes[31] == 5);
        CHECK(bytes[32] == static_cast<std::uint8_t>(DestructionFactKind::SupportLost));
        CHECK(bytes[36] == 0);
        CHECK(DeserializeDestructionEventOccurrenceId(bytes).Value() == first);
    }

    TEST_CASE("Event occurrence access rejects retired generations and stale semantic revisions", "[unit][destruction][identity]") {
        const auto occurrence = Event(Handle(1, 2, 3), 8);
        CHECK(ValidateDestructionEventAccess(occurrence, occurrence.source, occurrence.stateRevision).HasValue());
        CheckError(ValidateDestructionEventAccess(occurrence, Handle(1, 2, 4), occurrence.stateRevision),
                   DestructionErrors::StaleGeneration);
        CheckError(ValidateDestructionEventAccess(occurrence, occurrence.source, Id<DestructionStateRevision>(9)),
                   DestructionErrors::StaleRevision);
        CheckError(ValidateDestructionEventAccess({}, occurrence.source, occurrence.stateRevision), DestructionErrors::IdentityInvalid);
    }

    TEST_CASE("Canonical decoders reject every invalid identity dimension", "[unit][destruction][identity]") {
        auto contentBytes = SerializeFractureArtifactContentIdentity(Content());
        std::fill_n(contentBytes.begin() + 16, 8, 0);
        CheckError(DeserializeFractureArtifactContentIdentity(contentBytes), DestructionErrors::SerializedIdentityInvalid);
        contentBytes = SerializeFractureArtifactContentIdentity(Content());
        std::fill(contentBytes.begin() + 24, contentBytes.end(), 0);
        CheckError(DeserializeFractureArtifactContentIdentity(contentBytes), DestructionErrors::SerializedIdentityInvalid);

        for (const std::size_t invalidOffset : {std::size_t{0}, std::size_t{8}, std::size_t{16}}) {
            auto handleBytes = SerializeDestructionHandle(Handle());
            std::fill_n(handleBytes.begin() + static_cast<std::ptrdiff_t>(invalidOffset), 8, 0);
            CheckError(DeserializeDestructionHandle(handleBytes), DestructionErrors::SerializedIdentityInvalid);
        }

        auto chunkBytes = SerializeFractureChunkIdentity({Content(), Id<DestructionChunkId>(7)});
        std::fill_n(chunkBytes.begin() + 56, 8, 0);
        CheckError(DeserializeFractureChunkIdentity(chunkBytes), DestructionErrors::SerializedIdentityInvalid);

        auto commandBytes = SerializeDestructionCommandId(Command());
        std::fill_n(commandBytes.begin() + 24, 8, 0);
        CheckError(DeserializeDestructionCommandId(commandBytes), DestructionErrors::SerializedIdentityInvalid);

        auto eventBytes = SerializeDestructionEventOccurrenceId(Event());
        std::fill_n(eventBytes.begin() + 24, 8, 0);
        CheckError(DeserializeDestructionEventOccurrenceId(eventBytes), DestructionErrors::SerializedIdentityInvalid);
        eventBytes = SerializeDestructionEventOccurrenceId(Event());
        eventBytes[32] = 0xff;
        CheckError(DeserializeDestructionEventOccurrenceId(eventBytes), DestructionErrors::SerializedIdentityInvalid);
    }

    TEST_CASE("Replacement lifecycle preserves authored identity and retires runtime and content generations",
              "[unit][destruction][identity]") {
        const DestructibleId authored = Id<DestructibleId>(42);
        const DestructionHandle before{Id<DestructionWorldId>(3), authored, Id<DestructionGeneration>(7)};
        const DestructionHandle after{before.world, authored, AdvanceDestructionGeneration(before.generation).Value()};
        const auto oldContent = Content(9, 11, 12);
        const auto newContent =
            FractureArtifactContentIdentity::Create(oldContent.Asset(), AdvanceFractureContentRevision(oldContent.Revision()).Value(),
                                                    Digest(13))
                .Value();

        CHECK(before.destructible == after.destructible);
        CheckError(ValidateDestructionHandleAccess(before, after), DestructionErrors::StaleGeneration);
        CheckError(ValidateFractureContentAccess(oldContent, newContent), DestructionErrors::StaleContent);
    }

    TEST_CASE("Generation and revision advancement never wraps or admits invalid state", "[unit][destruction][identity]") {
        CHECK(AdvanceDestructionGeneration(Id<DestructionGeneration>(8)).Value() == Id<DestructionGeneration>(9));
        CHECK(AdvanceDestructionStateRevision(Id<DestructionStateRevision>(8)).Value() == Id<DestructionStateRevision>(9));
        CHECK(AdvanceFractureContentRevision(Id<FractureContentRevision>(8)).Value() == Id<FractureContentRevision>(9));
        CheckError(AdvanceDestructionGeneration({}), DestructionErrors::IdentityInvalid);
        CheckError(AdvanceDestructionGeneration(Id<DestructionGeneration>(std::numeric_limits<std::uint64_t>::max())),
                   DestructionErrors::GenerationExhausted);
        CheckError(AdvanceDestructionStateRevision(Id<DestructionStateRevision>(std::numeric_limits<std::uint64_t>::max())),
                   DestructionErrors::RevisionExhausted);
        CheckError(AdvanceFractureContentRevision(Id<FractureContentRevision>(std::numeric_limits<std::uint64_t>::max())),
                   DestructionErrors::RevisionExhausted);
    }

    TEST_CASE("Destruction identity errors are stable and unique", "[unit][destruction][identity]") {
        const std::array errors{DestructionErrors::IdentityInvalid,         DestructionErrors::IdentityUnknown,
                                DestructionErrors::StaleGeneration,         DestructionErrors::StaleContent,
                                DestructionErrors::StaleRevision,           DestructionErrors::GenerationExhausted,
                                DestructionErrors::RevisionExhausted,       DestructionErrors::SerializedIdentityInvalid,
                                DestructionErrors::DescriptorInvalid,       DestructionErrors::TierInvalid,
                                DestructionErrors::FeatureUnsatisfied,      DestructionErrors::RuntimeGeometryUnsupported,
                                DestructionErrors::LimitProfileInvalid,     DestructionErrors::LimitExceeded,
                                DestructionErrors::StaleConfiguration,      DestructionErrors::StateInvalid,
                                DestructionErrors::InvalidDamage,           DestructionErrors::DuplicateCommand,
                                DestructionErrors::StateTerminal,           DestructionErrors::CancelledBeforeCommit,
                                DestructionErrors::ShutdownInProgress,      DestructionErrors::CommandInvalid,
                                DestructionErrors::CommandLimitExceeded,    DestructionErrors::CommandAuthorityDenied,
                                DestructionErrors::CommandUnsupported,      DestructionErrors::CommandResultInvalid,
                                DestructionErrors::RegistryInvalid,         DestructionErrors::RegistryDuplicate,
                                DestructionErrors::RegistryCapacityExceeded};
        std::set<std::string_view> codes;
        for (const auto &error : errors) {
            CHECK(error.domain.Value() == std::string_view{"horo.destruction"});
            CHECK_FALSE(error.summary.empty());
            codes.insert(error.code.Value());
        }
        CHECK(codes.size() == errors.size());
    }
}  // namespace Horo::Destruction
