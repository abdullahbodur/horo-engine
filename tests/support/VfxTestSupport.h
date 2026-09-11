#pragma once

#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Vfx/ParticleSystemDescriptor.h"
#include "Horo/Vfx/VfxErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string_view>
#include <utility>

namespace Horo::Vfx::Tests {
    [[nodiscard]] inline Assets::AssetId ParticleMaterial(const std::string_view text = "00112233-4455-6677-8899-aabbccddeeff") {
        auto material = Assets::AssetId::Parse(text);
        REQUIRE(material.HasValue());
        return material.Value();
    }

    [[nodiscard]] inline EmitterId ParticleEmitter() {
        auto scope = VfxIdentityScope::Create(42);
        REQUIRE(scope.HasValue());
        auto emitter = MakeVfxIdentity<EmitterIdentityTag>(scope.Value(), 7, 3);
        REQUIRE(emitter.HasValue());
        return emitter.Value();
    }

    [[nodiscard]] inline ParticleSystemDescriptorData ValidParticleDescriptorData() {
        return {.version = CurrentParticleDescriptorSchemaVersion,
                .emitter = ParticleEmitter(),
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
                .material = ParticleMaterial(),
                .renderMode = ParticleRenderMode::Billboard,
                .sortMode = ParticleSortMode::ByDistance,
                .collisionMode = ParticleCollisionMode::SceneDepth};
    }

    [[nodiscard]] inline ErrorCodeRegistry ParticleErrorRegistry() {
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

    [[nodiscard]] inline ParticleSystemDescriptor ParticleDescriptor(ParticleSystemDescriptorData data) {
        auto validation = ValidateParticleSystemDescriptor(std::move(data), ParticleErrorRegistry());
        REQUIRE(validation.HasValue());
        REQUIRE(validation.Value().Accepted());
        return *validation.Value().descriptor;
    }
}  // namespace Horo::Vfx::Tests
