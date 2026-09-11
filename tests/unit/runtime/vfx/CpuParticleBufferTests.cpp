#include "Horo/Vfx/CpuParticleBuffer.h"
#include "Horo/Vfx/VfxErrors.h"
#include "support/AllocationProbe.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <thread>
#include <utility>

namespace Horo::Vfx {
    namespace {
        ParticleBufferId BufferId(const std::uint32_t generation = 1) {
            auto scope = VfxIdentityScope::Create(41);
            REQUIRE(scope.HasValue());
            auto id = MakeVfxIdentity<ParticleBufferIdentityTag>(scope.Value(), 7, generation);
            REQUIRE(id.HasValue());
            return id.Value();
        }

        ParticleSimulationId ParticleId(const std::uint64_t value) {
            auto id = ParticleSimulationId::Create(value);
            REQUIRE(id.HasValue());
            return id.Value();
        }

        CpuParticleBuffer Buffer(const std::uint32_t capacity = 8, const std::uint32_t customStreams = 2) {
            auto result = CpuParticleBuffer::Create({.buffer = BufferId(), .capacity = capacity, .customFloatStreams = customStreams});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        template <typename Value> [[nodiscard]] bool HasErrorCode(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            return result.HasError() && result.ErrorValue().code.Value() == expected.code.Value();
        }
    }  // namespace

    TEST_CASE("CPU particle storage exposes aligned packed SoA streams", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer buffer = Buffer(4, 2);
        const CpuParticleHandle first = buffer.Spawn(ParticleId(100)).Value();
        const CpuParticleHandle second = buffer.Spawn(ParticleId(101)).Value();

        auto viewResult = buffer.View();
        REQUIRE(viewResult.HasValue());
        CpuParticleSoAView view = viewResult.Value();
        REQUIRE(view.positionX.size() == 2);
        REQUIRE(view.customFloatStreamCount == 2);
        CHECK(reinterpret_cast<std::uintptr_t>(view.positionX.data()) % CpuParticleBufferHardLimits::StreamAlignment == 0);
        CHECK(reinterpret_cast<std::uintptr_t>(view.velocityZ.data()) % CpuParticleBufferHardLimits::StreamAlignment == 0);
        CHECK(reinterpret_cast<std::uintptr_t>(view.packedColor.data()) % CpuParticleBufferHardLimits::StreamAlignment == 0);
        CHECK(reinterpret_cast<std::uintptr_t>(view.customFloats[1].data()) % CpuParticleBufferHardLimits::StreamAlignment == 0);

        view.positionX[0] = 3.0F;
        view.positionX[1] = 9.0F;
        view.customFloats[0][1] = 17.0F;
        view.packedColor[1] = 0xAABBCCDDU;
        REQUIRE(buffer.Kill(first).HasValue());

        CHECK(buffer.ResolveDenseIndex(second).Value() == 0);
        const CpuParticleSoAView compacted = buffer.View().Value();
        CHECK(compacted.positionX[0] == 9.0F);
        CHECK(compacted.customFloats[0][0] == 17.0F);
        CHECK(compacted.packedColor[0] == 0xAABBCCDDU);
        CHECK(HasErrorCode(buffer.ResolveDenseIndex(first), VfxErrors::ParticleHandleStale));

        REQUIRE(buffer.Kill(second).HasValue());
        const CpuParticleHandle reused = buffer.Spawn(ParticleId(102)).Value();
        CHECK(buffer.ResolveDenseIndex(reused).Value() == 0);
        const CpuParticleSoAView zeroed = buffer.View().Value();
        CHECK(zeroed.positionX[0] == 0.0F);
        CHECK(zeroed.customFloats[0][0] == 0.0F);
        CHECK(zeroed.packedColor[0] == 0U);
    }

    TEST_CASE("CPU particle slots reject stale handles across heavy reuse", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer buffer = Buffer(1, 0);
        CpuParticleHandle previous = buffer.Spawn(ParticleId(1)).Value();
        bool churnSucceeded = true;
        for (std::uint64_t identity = 2; identity <= 50'000; ++identity) {
            churnSucceeded = churnSucceeded && buffer.Kill(previous).HasValue();
            CpuParticleHandle next{};
            auto spawned = buffer.Spawn(ParticleId(identity));
            churnSucceeded = churnSucceeded && spawned.HasValue();
            if (spawned.HasValue())
                next = spawned.Value();
            churnSucceeded = churnSucceeded && buffer.ResolveDenseIndex(previous).HasError();
            previous = next;
        }
        CHECK(churnSucceeded);
        CHECK(buffer.Statistics().active == 1);
        CHECK(buffer.Statistics().available == 0);
        CHECK(buffer.Statistics().staleAccesses == 49'999);
    }

    TEST_CASE("CPU particle frame-hot spawn kill and views allocate nothing", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer buffer = Buffer(64, 4);
        std::array<CpuParticleHandle, 64> handles{};
        const std::size_t before = Tests::AllocationProbe::Count();
        bool operationsSucceeded = true;
        for (std::uint64_t identity = 1; identity <= handles.size(); ++identity) {
            auto spawned = buffer.Spawn(ParticleId(identity));
            operationsSucceeded = operationsSucceeded && spawned.HasValue();
            if (spawned.HasValue())
                handles[identity - 1U] = spawned.Value();
        }
        for (std::size_t index = 0; index < handles.size(); index += 2)
            operationsSucceeded = operationsSucceeded && buffer.Kill(handles[index]).HasValue();
        operationsSucceeded = operationsSucceeded && buffer.View().HasValue();
        const std::size_t after = Tests::AllocationProbe::Count();

        CHECK(operationsSucceeded);
        CHECK(after == before);
        CHECK(buffer.Statistics().active == 32);
    }

    TEST_CASE("CPU particle storage enforces capacity identity and lifecycle", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer buffer = Buffer(2, 0);
        const CpuParticleHandle first = buffer.Spawn(ParticleId(10)).Value();
        REQUIRE(buffer.Spawn(ParticleId(11)).HasValue());
        CHECK(HasErrorCode(buffer.Spawn(ParticleId(12)), VfxErrors::ParticleBufferCapacityExceeded));
        CHECK(HasErrorCode(buffer.Spawn(ParticleId(10)), VfxErrors::ParticleSimulationIdentityInvalid));

        CpuParticleHandle foreign = first;
        foreign.buffer = BufferId(2);
        CHECK(HasErrorCode(buffer.Kill(foreign), VfxErrors::ParticleHandleInvalid));
        REQUIRE(buffer.Clear().HasValue());
        CHECK(HasErrorCode(buffer.ResolveDenseIndex(first), VfxErrors::ParticleHandleStale));
        CHECK(buffer.Statistics().active == 0);
        CHECK(buffer.Statistics().available == 2);

        const CpuParticleHandle afterClear = buffer.Spawn(ParticleId(12)).Value();
        CHECK(buffer.ResolveDenseIndex(afterClear).Value() == 0);

        REQUIRE(buffer.Shutdown().HasValue());
        REQUIRE(buffer.Shutdown().HasValue());
        CHECK(HasErrorCode(buffer.ResolveDenseIndex(afterClear), VfxErrors::ParticleBufferShutDown));
        CHECK(HasErrorCode(buffer.Spawn(ParticleId(20)), VfxErrors::ParticleBufferShutDown));
        CHECK(HasErrorCode(buffer.View(), VfxErrors::ParticleBufferShutDown));
    }

    TEST_CASE("CPU particle storage rejects malformed and over-budget creation", "[unit][vfx][particle-buffer]") {
        CHECK(HasErrorCode(CpuParticleBuffer::Create({.buffer = {}, .capacity = 1}), VfxErrors::ParticleBufferInvalid));
        CHECK(HasErrorCode(CpuParticleBuffer::Create({.buffer = BufferId(), .capacity = 0}), VfxErrors::ParticleBufferInvalid));
        CHECK(HasErrorCode(CpuParticleBuffer::Create({.buffer = BufferId(), .capacity = CpuParticleBufferHardLimits::Particles + 1U}),
                           VfxErrors::ParticleBufferInvalid));
        CHECK(HasErrorCode(CpuParticleBuffer::Create({.buffer = BufferId(),
                                                      .capacity = 1,
                                                      .customFloatStreams = CpuParticleBufferHardLimits::CustomFloatStreams + 1U}),
                           VfxErrors::ParticleBufferInvalid));
        CHECK(HasErrorCode(CpuParticleBuffer::Create({.buffer = BufferId(), .capacity = 64, .maximumBytes = 1}),
                           VfxErrors::ParticleBufferInvalid));
    }

    TEST_CASE("CPU particle storage is owner-thread affine", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer buffer = Buffer(2, 0);
        const ParticleSimulationId particle = ParticleId(1);
        std::atomic<bool> rejected{false};
        std::thread worker([&] {
            auto result = buffer.Spawn(particle);
            rejected.store(HasErrorCode(result, VfxErrors::ParticleBufferThreadViolation));
        });
        worker.join();
        CHECK(rejected.load());
        CHECK(buffer.Statistics().active == 0);
    }

    TEST_CASE("CPU particle storage move transfers ownership and leaves the source inert", "[unit][vfx][particle-buffer]") {
        CpuParticleBuffer source = Buffer(2, 0);
        const CpuParticleHandle particle = source.Spawn(ParticleId(1)).Value();
        CpuParticleBuffer destination = Buffer(1, 0);

        destination = std::move(source);

        CHECK(source.Statistics().capacity == 0);
        CHECK(HasErrorCode(source.Spawn(ParticleId(2)), VfxErrors::ParticleBufferShutDown));
        CHECK(destination.ResolveDenseIndex(particle).Value() == 0);
        CHECK(destination.Statistics().capacity == 2);
    }
}  // namespace Horo::Vfx
