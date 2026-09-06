#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveParticipantRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace Horo::Runtime {
    namespace {
        class TestAdapter final : public ICanonicalStateAdapter {
        public:
            explicit TestAdapter(std::shared_ptr<int> destructionCount) : destructionCount_(std::move(destructionCount)) {}

            ~TestAdapter() override {
                ++*destructionCount_;
            }

        private:
            std::shared_ptr<int> destructionCount_;
        };

        SaveParticipantId Participant(const std::string_view value) {
            return SaveParticipantId::Parse(value).Value();
        }

        SaveRecordId Record(const std::string_view value) {
            return SaveRecordId::Parse(value).Value();
        }

        ParticipantSchemaVersion Schema(const std::uint32_t value = 1) {
            return ParticipantSchemaVersion::Create(value).Value();
        }

        CanonicalStateParticipantDescriptor Descriptor(const std::string_view participant,
                                                       const std::string_view record = "00112233-4455-6677-8899-aabbccddeeff") {
            return {
                .participant = Participant(participant),
                .schemaVersion = Schema(),
                .scope = SaveParticipantScope::RuntimeScene,
                .roles = SaveParticipantRole::Capture | SaveParticipantRole::Restore,
                .required = true,
                .limits = {.maximumPayloadBytes = 1024, .maximumRecordCount = 16, .maximumNestingDepth = 8},
                .dependencies = {},
                .ownedRecords = {Record(record)},
            };
        }

        std::shared_ptr<const ICanonicalStateAdapter> Adapter(const std::shared_ptr<int> &destructionCount) {
            return std::make_shared<TestAdapter>(destructionCount);
        }

        TEST_CASE("Participant descriptors reject invalid metadata before snapshot publication", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            auto invalid = Descriptor("horo.test.invalid");
            invalid.schemaVersion = {};
            REQUIRE(registry.Register(std::move(invalid), Adapter(destructionCount)).HasError());
            REQUIRE(registry.Register(Descriptor("horo.test.missing_adapter"), nullptr).ErrorValue().code.Value() ==
                    SaveErrors::ParticipantAdapterMissing.code.Value());

            invalid = Descriptor("horo.test.no_roles");
            invalid.roles = SaveParticipantRole::None;
            REQUIRE(registry.Register(std::move(invalid), Adapter(destructionCount)).HasError());
            invalid = Descriptor("horo.test.unbounded");
            invalid.limits.maximumPayloadBytes = 0;
            REQUIRE(registry.Register(std::move(invalid), Adapter(destructionCount)).HasError());
            invalid = Descriptor("horo.test.duplicate_record");
            invalid.ownedRecords.push_back(invalid.ownedRecords.front());
            REQUIRE(registry.Register(std::move(invalid), Adapter(destructionCount)).HasError());
        }

        TEST_CASE("Participant registration is unique bounded and generation checked", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const auto first = registry.Register(Descriptor("horo.test.scene"), Adapter(destructionCount));
            REQUIRE(first.HasValue());
            REQUIRE(first.Value().registryGeneration == 2);
            REQUIRE(registry.Generation() == first.Value().registryGeneration);
            REQUIRE(registry.Register(Descriptor("horo.test.scene"), Adapter(destructionCount)).ErrorValue().code.Value() ==
                    SaveErrors::ParticipantDuplicate.code.Value());

            REQUIRE(registry.Register(Descriptor("horo.test.other"), Adapter(destructionCount)).ErrorValue().code.Value() ==
                    SaveErrors::ParticipantRecordOwnershipDuplicate.code.Value());

            const auto snapshot = registry.Snapshot();
            REQUIRE(snapshot.HasValue());
            REQUIRE(snapshot.Value().Generation() == first.Value().registryGeneration);
            REQUIRE(snapshot.Value().Bindings().size() == 1);
            REQUIRE(snapshot.Value().Find(Participant("horo.test.scene")) != nullptr);
        }

        TEST_CASE("Participant registry enforces its explicit capacity", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            for (std::size_t index = 0; index < MaximumSaveParticipantCount; ++index) {
                auto descriptor = Descriptor("horo.test.placeholder");
                descriptor.participant = Participant("horo.test.p_" + std::to_string(index));
                SaveIdentityDetail::Bytes bytes{};
                bytes[0] = 1;
                bytes[14] = static_cast<std::uint8_t>(index >> 8U);
                bytes[15] = static_cast<std::uint8_t>(index);
                descriptor.ownedRecords = {SaveRecordId::FromBytes(bytes).Value()};
                REQUIRE(registry.Register(std::move(descriptor), Adapter(destructionCount)).HasValue());
            }
            auto overflow = Descriptor("horo.test.overflow", "ffffffff-ffff-ffff-ffff-ffffffffffff");
            REQUIRE(registry.Register(std::move(overflow), Adapter(destructionCount)).ErrorValue().code.Value() ==
                    SaveErrors::ParticipantRegistryCapacityExceeded.code.Value());
        }

        TEST_CASE("Registry snapshots are immutable and retain exact adapter leases", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            SaveParticipantRegistrySnapshot firstSnapshot;
            {
                CanonicalStateParticipantRegistry registry;
                REQUIRE(registry.Register(Descriptor("horo.test.second", "10112233-4455-6677-8899-aabbccddeeff"), Adapter(destructionCount))
                            .HasValue());
                REQUIRE(registry.Register(Descriptor("horo.test.first"), Adapter(destructionCount)).HasValue());
                firstSnapshot = registry.Snapshot().Value();
                REQUIRE(firstSnapshot.Bindings()[0].Descriptor().participant.Value() == "horo.test.first");
                REQUIRE(firstSnapshot.Bindings()[1].Descriptor().participant.Value() == "horo.test.second");

                REQUIRE(registry.Unregister(Participant("horo.test.first")).Value());
                const auto secondSnapshot = registry.Snapshot().Value();
                REQUIRE(secondSnapshot.Generation() != firstSnapshot.Generation());
                REQUIRE(secondSnapshot.Bindings().size() == 1);
                REQUIRE(firstSnapshot.Bindings().size() == 2);
                registry.Close();
                REQUIRE(registry.IsClosed());
                REQUIRE(registry.Snapshot().HasError());
                REQUIRE(registry.Register(Descriptor("horo.test.late"), nullptr).ErrorValue().code.Value() ==
                        SaveErrors::ParticipantRegistryClosed.code.Value());
                REQUIRE(*destructionCount == 0);
            }
            REQUIRE(*destructionCount == 0);
            firstSnapshot = {};
            REQUIRE(*destructionCount == 2);
        }

        TEST_CASE("Snapshot publication rejects missing and cyclic dependencies", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry missing;
            auto dependent = Descriptor("horo.test.dependent");
            dependent.dependencies = {Participant("horo.test.provider")};
            REQUIRE(missing.Register(std::move(dependent), Adapter(destructionCount)).HasValue());
            REQUIRE(missing.Snapshot().ErrorValue().code.Value() == SaveErrors::ParticipantDependencyMissing.code.Value());

            CanonicalStateParticipantRegistry cyclic;
            auto first = Descriptor("horo.test.first");
            first.dependencies = {Participant("horo.test.second")};
            auto second = Descriptor("horo.test.second", "10112233-4455-6677-8899-aabbccddeeff");
            second.dependencies = {Participant("horo.test.first")};
            REQUIRE(cyclic.Register(std::move(first), Adapter(destructionCount)).HasValue());
            REQUIRE(cyclic.Register(std::move(second), Adapter(destructionCount)).HasValue());
            REQUIRE(cyclic.Snapshot().ErrorValue().code.Value() == SaveErrors::ParticipantDependencyCycle.code.Value());
        }

        TEST_CASE("Descriptors remain inert until explicit registry operations", "[unit][save][registry]") {
            auto destructionCount = std::make_shared<int>();
            auto adapter = Adapter(destructionCount);
            const CanonicalStateParticipantDescriptor descriptor = Descriptor("horo.test.inert");
            const auto copy = descriptor;
            REQUIRE(copy == descriptor);
            REQUIRE(adapter.use_count() == 1);
            REQUIRE(*destructionCount == 0);
        }
    }  // namespace
}  // namespace Horo::Runtime
