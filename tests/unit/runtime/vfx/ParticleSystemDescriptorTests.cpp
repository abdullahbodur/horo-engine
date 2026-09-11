#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Vfx/ParticleSystemDescriptor.h"
#include "Horo/Vfx/VfxErrors.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Horo::Vfx {
    namespace {
        [[nodiscard]] Assets::AssetId Material(const std::string_view text = "00112233-4455-6677-8899-aabbccddeeff") {
            auto material = Assets::AssetId::Parse(text);
            REQUIRE(material.HasValue());
            return material.Value();
        }

        [[nodiscard]] EmitterId Emitter() {
            auto scope = VfxIdentityScope::Create(42);
            REQUIRE(scope.HasValue());
            auto emitter = MakeVfxIdentity<EmitterIdentityTag>(scope.Value(), 7, 3);
            REQUIRE(emitter.HasValue());
            return emitter.Value();
        }

        [[nodiscard]] ParticleSystemDescriptorData ValidData() {
            return {.version = CurrentParticleDescriptorSchemaVersion,
                    .emitter = Emitter(),
                    .simulationPreference = SimulationPreference::PreferGPU,
                    .maximumParticles = 4'096,
                    .shape = ParticleEmitterShape::Cone,
                    .spawnRate = {100.0, 500.0},
                    .lifetimeKind = ParticleLifetimeKind::Finite,
                    .lifetimeSeconds = {0.5, 4.0},
                    .killCondition = ParticleKillCondition::Lifetime,
                    .initialSpeed = {-2.0, 12.0},
                    .initialSize = {0.1, 2.0},
                    .initialOpacity = {0.25, 1.0},
                    .material = Material(),
                    .renderMode = ParticleRenderMode::Billboard,
                    .sortMode = ParticleSortMode::ByDistance,
                    .collisionMode = ParticleCollisionMode::SceneDepth};
        }

        [[nodiscard]] ErrorCodeRegistry Registry() {
            const std::array descriptors{&VfxErrors::ParticleDescriptorMalformed,
                                         &VfxErrors::ParticleDescriptorDuplicate,
                                         &VfxErrors::ParticleDescriptorVersionUnsupported,
                                         &VfxErrors::ParticleDescriptorLimitExceeded,
                                         &VfxErrors::ParticleRangeInvalid,
                                         &VfxErrors::ParticleLifetimeUnbounded,
                                         &VfxErrors::ParticleModeIncompatible,
                                         &VfxErrors::ParticleMaterialMissing,
                                         &VfxErrors::ParticleMaterialTypeMismatch,
                                         &VfxErrors::ParticleMaterialUnloadable,
                                         &VfxErrors::ParticleCookTierExceeded};
            ModuleDescriptor module{.id = {"horo.vfx"},
                                    .version = {1, 0, 0},
                                    .errorDomains = {
                                        {.id = ErrorDomainId{"horo.vfx"}, .descriptors = {descriptors.begin(), descriptors.end()}}}};
            auto registry = BuildErrorCodeRegistry(std::span{&module, 1});
            REQUIRE(registry.HasValue());
            return registry.Value();
        }

        [[nodiscard]] std::string ValidJson() {
            return R"json({
                "schemaVersion":{"major":1,"minor":0},
                "emitterId":{"scope":42,"slot":7,"generation":3},
                "simulationPreference":"preferGpu",
                "maximumParticles":4096,
                "shape":"cone",
                "spawnRate":{"minimum":100.0,"maximum":500.0},
                "lifetime":{"kind":"finite","seconds":{"minimum":0.5,"maximum":4.0},"killCondition":"lifetime"},
                "initialSpeed":{"minimum":-2.0,"maximum":12.0},
                "initialSize":{"minimum":0.1,"maximum":2.0},
                "initialOpacity":{"minimum":0.25,"maximum":1.0},
                "materialId":"00112233-4455-6677-8899-aabbccddeeff",
                "renderMode":"billboard",
                "sortMode":"byDistance",
                "collisionMode":"sceneDepth"
            })json";
        }

        void RequireError(const Error &error, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(error.domain.Value() == descriptor.domain.Value());
            REQUIRE(error.code.Value() == descriptor.code.Value());
        }
    }  // namespace

    TEST_CASE("Particle descriptor parses and admits canonical source", "[unit][vfx][particle-descriptor]") {
        auto parsed = ParseParticleSystemDescriptor(ValidJson());
        REQUIRE(parsed.HasValue());
        REQUIRE(parsed.Value() == ValidData());

        auto validation = ValidateParticleSystemDescriptor(std::move(parsed).Value(), Registry(), "assets/fire.particle");
        REQUIRE(validation.HasValue());
        REQUIRE(validation.Value().Accepted());
        REQUIRE(validation.Value().diagnostics.Empty());
        REQUIRE(validation.Value().descriptor->Data() == ValidData());
    }

    TEST_CASE("Particle descriptor parser rejects duplicate malformed versioned and oversized input", "[unit][vfx][particle-descriptor]") {
        auto duplicate = ParseParticleSystemDescriptor(R"({"schemaVersion":{},"schemaVersion":{}})");
        REQUIRE(duplicate.HasError());
        RequireError(duplicate.ErrorValue(), VfxErrors::ParticleDescriptorDuplicate);

        auto malformed = ParseParticleSystemDescriptor("{");
        REQUIRE(malformed.HasError());
        RequireError(malformed.ErrorValue(), VfxErrors::ParticleDescriptorMalformed);

        std::string newer = ValidJson();
        newer.replace(newer.find("\"major\":1"), std::string{"\"major\":1"}.size(), "\"major\":2");
        auto unsupported = ParseParticleSystemDescriptor(newer);
        REQUIRE(unsupported.HasError());
        RequireError(unsupported.ErrorValue(), VfxErrors::ParticleDescriptorVersionUnsupported);

        ParticleDescriptorLimits limits;
        limits.maximumSourceBytes = 32;
        auto oversized = ParseParticleSystemDescriptor(ValidJson(), limits);
        REQUIRE(oversized.HasError());
        RequireError(oversized.ErrorValue(), VfxErrors::ParticleDescriptorLimitExceeded);

        std::string deeplyNested(ParticleDescriptorHardLimits::JsonDepth + 1, '[');
        deeplyNested.append(ParticleDescriptorHardLimits::JsonDepth + 1, ']');
        auto tooDeep = ParseParticleSystemDescriptor(deeplyNested);
        REQUIRE(tooDeep.HasError());
        RequireError(tooDeep.ErrorValue(), VfxErrors::ParticleDescriptorLimitExceeded);
    }

    TEST_CASE("Particle descriptor reports every independent semantic finding deterministically", "[unit][vfx][particle-descriptor]") {
        auto candidate = ValidData();
        candidate.maximumParticles = 0;
        candidate.spawnRate = {std::numeric_limits<double>::quiet_NaN(), -1.0};
        candidate.initialSize = {0.0, -1.0};
        candidate.initialOpacity = {-1.0, 2.0};
        candidate.lifetimeKind = ParticleLifetimeKind::Infinite;
        candidate.lifetimeSeconds = {1.0, 2.0};
        candidate.killCondition = ParticleKillCondition::None;
        candidate.renderMode = ParticleRenderMode::Ribbon;
        candidate.sortMode = ParticleSortMode::OldestFirst;

        auto validation = ValidateParticleSystemDescriptor(std::move(candidate), Registry(), "assets/hostile.particle");
        REQUIRE(validation.HasValue());
        REQUIRE_FALSE(validation.Value().Accepted());
        REQUIRE_FALSE(validation.Value().descriptor.has_value());
        REQUIRE(validation.Value().diagnostics.Size() == 7);
        REQUIRE(validation.Value().diagnostics.Count(DiagnosticSeverity::Error) == 7);
        REQUIRE(validation.Value().diagnostics.Diagnostics().front().location.source == "assets/hostile.particle");
        REQUIRE(std::ranges::is_sorted(validation.Value().diagnostics.Diagnostics(), {}, [](const ValidationDiagnostic &finding) {
            return std::pair{finding.domain.Value(), finding.code.Value()};
        }));
    }

    TEST_CASE("Infinite particles require an independent bounded kill condition", "[unit][vfx][particle-descriptor]") {
        auto unbounded = ValidData();
        unbounded.lifetimeKind = ParticleLifetimeKind::Infinite;
        unbounded.lifetimeSeconds = {};
        unbounded.killCondition = ParticleKillCondition::None;
        unbounded.collisionMode = ParticleCollisionMode::None;
        auto rejected = ValidateParticleSystemDescriptor(std::move(unbounded), Registry(), "assets/smoke.particle");
        REQUIRE(rejected.HasValue());
        REQUIRE_FALSE(rejected.Value().Accepted());
        REQUIRE(rejected.Value().diagnostics.Diagnostics().front().code.Value() == VfxErrors::ParticleLifetimeUnbounded.code.Value());

        auto bounded = ValidData();
        bounded.lifetimeKind = ParticleLifetimeKind::Infinite;
        bounded.lifetimeSeconds = {};
        bounded.killCondition = ParticleKillCondition::Collision;
        bounded.collisionMode = ParticleCollisionMode::Planes;
        auto accepted = ValidateParticleSystemDescriptor(std::move(bounded), Registry(), "assets/smoke.particle");
        REQUIRE(accepted.HasValue());
        REQUIRE(accepted.Value().Accepted());
    }

    TEST_CASE("Particle descriptor validates exact boundaries and hostile project limits", "[unit][vfx][particle-descriptor]") {
        auto boundary = ValidData();
        boundary.maximumParticles = ParticleDescriptorHardLimits::Particles;
        boundary.spawnRate.maximum = ParticleDescriptorHardLimits::SpawnRate;
        auto accepted = ValidateParticleSystemDescriptor(boundary, Registry());
        REQUIRE(accepted.HasValue());
        REQUIRE(accepted.Value().Accepted());

        boundary.maximumParticles += 1;
        boundary.spawnRate.maximum += 1.0;
        auto rejected = ValidateParticleSystemDescriptor(std::move(boundary), Registry());
        REQUIRE(rejected.HasValue());
        REQUIRE_FALSE(rejected.Value().Accepted());
        REQUIRE(rejected.Value().diagnostics.Size() == 2);

        ParticleDescriptorLimits invalid;
        invalid.maximumJsonDepth = ParticleDescriptorHardLimits::JsonDepth + 1;
        auto invalidLimits = ValidateParticleSystemDescriptor(ValidData(), Registry(), "particle-system", invalid);
        REQUIRE(invalidLimits.HasError());
        RequireError(invalidLimits.ErrorValue(), VfxErrors::ParticleDescriptorLimitExceeded);
    }

    TEST_CASE("Particle cook checks tier and exact material evidence in one full pass", "[unit][vfx][particle-descriptor]") {
        auto validation = ValidateParticleSystemDescriptor(ValidData(), Registry());
        REQUIRE(validation.HasValue());
        REQUIRE(validation.Value().Accepted());

        const ParticleCookProfile constrained{ParticleCookTier::Compact, 1'024, 10.0};
        const ParticleMaterialEvidence missing{Material(), ParticleMaterialAvailability::Missing};
        auto cook = BuildParticleSystemCookPlan(*validation.Value().descriptor, constrained, missing, Registry(), "assets/fire.particle");
        REQUIRE(cook.HasValue());
        REQUIRE_FALSE(cook.Value().Accepted());
        REQUIRE(cook.Value().diagnostics.Size() == 2);
        REQUIRE_FALSE(cook.Value().plan.has_value());
    }

    TEST_CASE("Particle cook admits stable material identity and canonical tier", "[unit][vfx][particle-descriptor]") {
        auto validation = ValidateParticleSystemDescriptor(ValidData(), Registry());
        REQUIRE(validation.HasValue());
        auto profile = GetParticleCookProfile(ParticleCookTier::Standard);
        REQUIRE(profile.HasValue());

        const ParticleMaterialEvidence available{Material(), ParticleMaterialAvailability::Available};
        auto cook = BuildParticleSystemCookPlan(*validation.Value().descriptor, profile.Value(), available, Registry());
        REQUIRE(cook.HasValue());
        REQUIRE(cook.Value().Accepted());
        REQUIRE(cook.Value().diagnostics.Empty());
        REQUIRE(cook.Value().plan->material == Material());
        REQUIRE(cook.Value().plan->emitter == Emitter());
    }

    TEST_CASE("Particle validation fails closed when required diagnostics are not registered", "[unit][vfx][particle-descriptor]") {
        ModuleDescriptor emptyModule{.id = {"horo.empty"}, .version = {1, 0, 0}};
        auto emptyRegistry = BuildErrorCodeRegistry(std::span{&emptyModule, 1});
        REQUIRE(emptyRegistry.HasValue());
        auto invalid = ValidData();
        invalid.maximumParticles = 0;
        auto result = ValidateParticleSystemDescriptor(std::move(invalid), emptyRegistry.Value());
        REQUIRE(result.HasError());
    }

    TEST_CASE("Particle descriptor compatibility policy is explicit", "[unit][vfx][particle-descriptor]") {
        REQUIRE(ClassifyParticleDescriptorCompatibility({1, 0}) == ParticleDescriptorCompatibility::Exact);
        REQUIRE(ClassifyParticleDescriptorCompatibility({1, 1}) == ParticleDescriptorCompatibility::Unsupported);
        REQUIRE(ClassifyParticleDescriptorCompatibility({0, 9}) == ParticleDescriptorCompatibility::Unsupported);
        REQUIRE(ClassifyParticleDescriptorCompatibility({2, 0}) == ParticleDescriptorCompatibility::Unsupported);
        REQUIRE(GetParticleCookProfile(ParticleCookTier::Count).HasError());
    }
}  // namespace Horo::Vfx
