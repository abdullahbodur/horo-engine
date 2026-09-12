#include "AiTestSupport.h"
#include "Horo/AI/PerceptionDescriptorRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace Horo::AI {
    namespace {
        using TestSupport::ExpectError;
        using TestSupport::MakeIdentity;

        [[nodiscard]] PerceptionDescriptorOrigin Origin(const PerceptionDescriptorSourceKind kind, const std::uint64_t provider,
                                                        const std::uint32_t version = 1) {
            return {kind, MakeIdentity<PerceptionProviderId>(provider), version};
        }

        [[nodiscard]] SenseTypeDescriptor Sense(const std::uint64_t identity, const PerceptionDescriptorSourceKind kind,
                                                const std::uint64_t provider, std::string name,
                                                const PerceptionCapabilitySet capabilities = {}) {
            return {MakeIdentity<SenseTypeId>(identity), Origin(kind, provider), std::move(name), capabilities};
        }

        [[nodiscard]] StimulusTypeDescriptor Stimulus(const std::uint64_t identity, const PerceptionDescriptorSourceKind kind,
                                                      const std::uint64_t provider, std::string name,
                                                      const PerceptionCapabilitySet capabilities = {}) {
            return {MakeIdentity<StimulusTypeId>(identity), Origin(kind, provider), std::move(name), 1, capabilities};
        }

        [[nodiscard]] PerceptionListenerDescriptor Listener(
            const std::uint64_t identity, const SenseTypeId sense, const StimulusTypeId stimulus,
            const PerceptionDependencyPolicy policy = PerceptionDependencyPolicy::Required) {
            PerceptionListenerDescriptor descriptor;
            descriptor.identity = MakeIdentity<PerceptionListenerTypeId>(identity);
            descriptor.origin = Origin(PerceptionDescriptorSourceKind::Package, 300);
            descriptor.displayName = "Listener";
            descriptor.sense = sense;
            descriptor.stimuli[0].identity = stimulus;
            descriptor.stimulusCount = 1;
            descriptor.dependencyPolicy = policy;
            return descriptor;
        }

        TEST_CASE("Perception registry resolves native script and package descriptors by stable identity",
                  "[unit][ai][perception][registry]") {
            const std::array senses{
                Sense(30, PerceptionDescriptorSourceKind::Script, 103, "Script thermal"),
                Sense(10, PerceptionDescriptorSourceKind::Native, 101, "Sight"),
                Sense(20, PerceptionDescriptorSourceKind::Package, 102, "Package sonar"),
            };
            const std::array stimuli{
                Stimulus(20, PerceptionDescriptorSourceKind::Package, 202, "Sonar return"),
                Stimulus(10, PerceptionDescriptorSourceKind::Native, 201, "Visual observation"),
            };
            const std::array listeners{Listener(2, senses[0].identity, stimuli[1].identity),
                                       Listener(1, senses[2].identity, stimuli[0].identity)};
            auto captured = PerceptionDescriptorRegistry::Capture({senses, stimuli, listeners}, {});
            REQUIRE(captured.HasValue());
            auto registry = std::move(captured).Value();

            REQUIRE(registry.Senses().size() == 3);
            CHECK(registry.Senses()[0].identity.Value() == 10);
            CHECK(registry.Senses()[1].origin.kind == PerceptionDescriptorSourceKind::Package);
            CHECK(registry.Senses()[2].displayName == "Script thermal");
            REQUIRE(registry.Listeners().size() == 2);
            CHECK(registry.Listeners()[0].descriptor.identity.Value() == 1);
            CHECK(registry.Listeners()[0].availability == PerceptionListenerAvailability::Available);
            CHECK(registry.FindSense(senses[0].identity).Value().origin.provider.Value() == 103);
            CHECK(registry.FindStimulus(stimuli[0].identity).Value().origin.provider.Value() == 202);
            CHECK(registry.FindListener(listeners[0].identity).Value().descriptor.sense == senses[0].identity);

            static_assert(!std::is_copy_constructible_v<PerceptionDescriptorRegistry>);
            static_assert(std::is_move_constructible_v<PerceptionDescriptorRegistry>);
        }

        TEST_CASE("Perception display names never participate in persistent identity", "[unit][ai][perception][registry]") {
            const std::array senses{Sense(9, PerceptionDescriptorSourceKind::Native, 1, "Old display name")};
            const std::array stimuli{Stimulus(8, PerceptionDescriptorSourceKind::Native, 1, "Old stimulus name")};
            const std::array listeners{Listener(7, senses[0].identity, stimuli[0].identity)};
            auto first = PerceptionDescriptorRegistry::Capture({senses, stimuli, listeners}, {});
            REQUIRE(first.HasValue());

            auto renamedSenses = senses;
            auto renamedStimuli = stimuli;
            renamedSenses[0].displayName = "Localized sight";
            renamedStimuli[0].displayName = "Localized observation";
            auto second = PerceptionDescriptorRegistry::Capture({renamedSenses, renamedStimuli, listeners}, {});
            REQUIRE(second.HasValue());
            CHECK(first.Value().Senses()[0].identity == second.Value().Senses()[0].identity);
            CHECK(first.Value().Stimuli()[0].identity == second.Value().Stimuli()[0].identity);
            CHECK(SerializeAiIdentity(first.Value().Senses()[0].identity) == SerializeAiIdentity(second.Value().Senses()[0].identity));
        }

        TEST_CASE("Perception registry rejects duplicate identities independent of contribution order",
                  "[unit][ai][perception][registry]") {
            const auto native = Sense(1, PerceptionDescriptorSourceKind::Native, 10, "Native");
            const auto package = Sense(1, PerceptionDescriptorSourceKind::Package, 20, "Package collision");
            const std::array first{native, package};
            const std::array second{package, native};
            ExpectError(PerceptionDescriptorRegistry::Capture({.senses = first}, {}), AIErrors::PerceptionDescriptorConflict);
            ExpectError(PerceptionDescriptorRegistry::Capture({.senses = second}, {}), AIErrors::PerceptionDescriptorConflict);

            const auto stimulus = Stimulus(2, PerceptionDescriptorSourceKind::Native, 10, "Stimulus");
            const std::array stimuli{stimulus};
            const std::array duplicateListeners{Listener(4, native.identity, stimulus.identity),
                                                Listener(4, native.identity, stimulus.identity)};
            const std::array oneSense{native};
            ExpectError(PerceptionDescriptorRegistry::Capture({oneSense, stimuli, duplicateListeners}, {}),
                        AIErrors::PerceptionDescriptorConflict);
        }

        TEST_CASE("Missing optional perception dependencies disable only dependent listeners", "[unit][ai][perception][registry]") {
            const std::array senses{Sense(1, PerceptionDescriptorSourceKind::Native, 1, "Sight")};
            const std::array stimuli{Stimulus(1, PerceptionDescriptorSourceKind::Native, 1, "Visual")};
            const auto available = Listener(1, senses[0].identity, stimuli[0].identity);
            const auto missingSense = Listener(2, MakeIdentity<SenseTypeId>(99), stimuli[0].identity, PerceptionDependencyPolicy::Optional);
            const auto missingStimulus =
                Listener(3, senses[0].identity, MakeIdentity<StimulusTypeId>(99), PerceptionDependencyPolicy::Optional);
            const std::array listeners{missingStimulus, available, missingSense};
            auto captured = PerceptionDescriptorRegistry::Capture({senses, stimuli, listeners}, {});
            REQUIRE(captured.HasValue());
            REQUIRE(captured.Value().Listeners().size() == 3);
            CHECK(captured.Value().Listeners()[0].availability == PerceptionListenerAvailability::Available);
            CHECK(captured.Value().Listeners()[1].availability == PerceptionListenerAvailability::MissingSense);
            CHECK(captured.Value().Listeners()[2].availability == PerceptionListenerAvailability::MissingStimulus);

            const std::array requiredMissing{Listener(4, MakeIdentity<SenseTypeId>(88), stimuli[0].identity)};
            ExpectError(PerceptionDescriptorRegistry::Capture({senses, stimuli, requiredMissing}, {}),
                        AIErrors::PerceptionDependencyMissing);
        }

        TEST_CASE("Perception capability admission is explicit and listener scoped", "[unit][ai][perception][registry]") {
            const auto lineOfSight = PerceptionCapabilitySet::Of(PerceptionCapability::PhysicsLineOfSight);
            const std::array senses{Sense(1, PerceptionDescriptorSourceKind::Native, 1, "Sight", lineOfSight)};
            const std::array stimuli{Stimulus(1, PerceptionDescriptorSourceKind::Native, 1, "Visual")};
            const std::array required{Listener(1, senses[0].identity, stimuli[0].identity)};
            ExpectError(PerceptionDescriptorRegistry::Capture({senses, stimuli, required}, {}), AIErrors::PerceptionCapabilityUnavailable);
            auto supported = PerceptionDescriptorRegistry::Capture({senses, stimuli, required}, lineOfSight);
            REQUIRE(supported.HasValue());
            CHECK(supported.Value().Listeners()[0].availability == PerceptionListenerAvailability::Available);

            const std::array optional{Listener(1, senses[0].identity, stimuli[0].identity, PerceptionDependencyPolicy::Optional)};
            auto unavailable = PerceptionDescriptorRegistry::Capture({senses, stimuli, optional}, {});
            REQUIRE(unavailable.HasValue());
            CHECK(unavailable.Value().Listeners()[0].availability == PerceptionListenerAvailability::CapabilityUnavailable);
        }

        TEST_CASE("Perception listeners reject incompatible sense and stimulus versions", "[unit][ai][perception][registry]") {
            const std::array senses{Sense(1, PerceptionDescriptorSourceKind::Native, 1, "Sight")};
            auto versionedStimulus = Stimulus(1, PerceptionDescriptorSourceKind::Native, 1, "Visual");
            versionedStimulus.payloadVersion = 3;
            const std::array stimuli{versionedStimulus};

            auto incompatibleSense = Listener(1, senses[0].identity, stimuli[0].identity);
            incompatibleSense.minimumSenseVersion = 2;
            const std::array senseListeners{incompatibleSense};
            ExpectError(PerceptionDescriptorRegistry::Capture({senses, stimuli, senseListeners}, {}),
                        AIErrors::PerceptionDescriptorIncompatible);

            auto incompatibleStimulus = Listener(2, senses[0].identity, stimuli[0].identity);
            incompatibleStimulus.stimuli[0].maximumPayloadVersion = 2;
            const std::array stimulusListeners{incompatibleStimulus};
            ExpectError(PerceptionDescriptorRegistry::Capture({senses, stimuli, stimulusListeners}, {}),
                        AIErrors::PerceptionDescriptorIncompatible);

            incompatibleStimulus.dependencyPolicy = PerceptionDependencyPolicy::Optional;
            const std::array optionalListeners{incompatibleStimulus};
            auto optional = PerceptionDescriptorRegistry::Capture({senses, stimuli, optionalListeners}, {});
            REQUIRE(optional.HasValue());
            CHECK(optional.Value().Listeners()[0].availability == PerceptionListenerAvailability::IncompatibleStimulus);
        }

        TEST_CASE("Perception registry rejects malformed and hostile bounded input", "[unit][ai][perception][registry]") {
            auto validSense = Sense(1, PerceptionDescriptorSourceKind::Native, 1, "Sight");
            const std::array invalidSource{[&validSense] {
                auto value = validSense;
                value.origin.kind = PerceptionDescriptorSourceKind::Count;
                return value;
            }()};
            ExpectError(PerceptionDescriptorRegistry::Capture({.senses = invalidSource}, {}), AIErrors::PerceptionDescriptorInvalid);

            const std::array tooLongName{Sense(1, PerceptionDescriptorSourceKind::Native, 1, std::string(5, 'x'))};
            const PerceptionDescriptorRegistryLimits tinyName{.maximumDisplayNameBytes = 4};
            ExpectError(PerceptionDescriptorRegistry::Capture({.senses = tooLongName}, {}, tinyName),
                        AIErrors::PerceptionDescriptorInvalid);

            const std::array senses{validSense};
            const auto stimulus = Stimulus(1, PerceptionDescriptorSourceKind::Native, 1, "Visual");
            const std::array stimuli{stimulus};
            auto duplicateStimulusListener = Listener(1, validSense.identity, stimulus.identity);
            duplicateStimulusListener.stimuli[1].identity = stimulus.identity;
            duplicateStimulusListener.stimulusCount = 2;
            const std::array duplicateStimuli{duplicateStimulusListener};
            ExpectError(PerceptionDescriptorRegistry::Capture({senses, stimuli, duplicateStimuli}, {}),
                        AIErrors::PerceptionDescriptorInvalid);

            const PerceptionDescriptorRegistryLimits noSenseCapacity{.maximumSenseTypes = 1};
            const std::array overLimit{senses[0], Sense(2, PerceptionDescriptorSourceKind::Native, 1, "Hearing")};
            ExpectError(PerceptionDescriptorRegistry::Capture({.senses = overLimit}, {}, noSenseCapacity),
                        AIErrors::PerceptionDescriptorLimitExceeded);

            const auto invalidCapabilities = PerceptionCapabilitySet::Of(static_cast<PerceptionCapability>(255));
            ExpectError(PerceptionDescriptorRegistry::Capture({}, invalidCapabilities), AIErrors::PerceptionDescriptorInvalid);
        }

        TEST_CASE("Captured perception registry owns immutable source data", "[unit][ai][perception][registry]") {
            std::array senses{Sense(1, PerceptionDescriptorSourceKind::Native, 1, "Sight")};
            std::array stimuli{Stimulus(1, PerceptionDescriptorSourceKind::Native, 1, "Visual")};
            std::array listeners{Listener(1, senses[0].identity, stimuli[0].identity)};
            auto captured = PerceptionDescriptorRegistry::Capture({senses, stimuli, listeners}, {});
            REQUIRE(captured.HasValue());
            senses[0].displayName = "Mutated caller text";
            stimuli[0].identity = MakeIdentity<StimulusTypeId>(99);
            listeners[0].sense = MakeIdentity<SenseTypeId>(99);
            CHECK(captured.Value().Senses()[0].displayName == "Sight");
            CHECK(captured.Value().Stimuli()[0].identity.Value() == 1);
            CHECK(captured.Value().Listeners()[0].descriptor.sense.Value() == 1);
        }
    }  // namespace
}  // namespace Horo::AI
