#include "Horo/WorldStreaming/OriginFrame.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;

        OriginFrameBinding Binding(const std::uint64_t revision = 1, const std::uint64_t generation = 1, const std::uint64_t identity = 7) {
            return {
                IdentityFrom<OriginFrameId>(identity),
                IdentityFrom<OriginFrameRevision>(revision),
                IdentityFrom<OriginGeneration>(generation),
            };
        }

        OriginFrame Frame(const std::int64_t x = 0, const std::int64_t y = 0, const std::int64_t z = 0,
                          const OriginFrameBinding binding = Binding()) {
            return OriginFrame::Create(binding, Math::WorldCoordinate64::FromMillimeters(x, y, z)).Value();
        }

        void RequireErrorCode(const Error &error, const ErrorCodeDescriptor &expected) {
            REQUIRE(error.code.Value() == expected.code.Value());
        }

        TEST_CASE("Origin frame identities are non-zero typed and non-wrapping", "[unit][world_streaming][origin_frame]") {
            REQUIRE_FALSE(OriginFrameBinding{}.IsValid());
            REQUIRE_FALSE(OriginFrame::Create({}, {}).HasValue());
            REQUIRE(NextOriginGeneration({}).HasError());
            REQUIRE(NextOriginFrameRevision({}).HasError());
            REQUIRE(NextOriginGeneration(IdentityFrom<OriginGeneration>(9)).Value().Value() == 10);
            REQUIRE(NextOriginFrameRevision(IdentityFrom<OriginFrameRevision>(11)).Value().Value() == 12);

            const auto maximum = std::numeric_limits<std::uint64_t>::max();
            RequireErrorCode(NextOriginGeneration(IdentityFrom<OriginGeneration>(maximum)).ErrorValue(),
                             WorldStreamingErrors::GenerationExhausted);
            RequireErrorCode(NextOriginFrameRevision(IdentityFrom<OriginFrameRevision>(maximum)).ErrorValue(),
                             WorldStreamingErrors::GenerationExhausted);
            static_assert(!std::is_same_v<OriginFrameId, OriginFrameRevision>);
            static_assert(!std::is_same_v<OriginFrameRevision, OriginGeneration>);
            static_assert(!std::is_convertible_v<Math::Vec3, OriginLocalCoordinate>);
            static_assert(std::is_same_v<decltype(std::declval<const OriginFrame &>().Origin()), const Math::WorldCoordinate64 &>);
        }

        TEST_CASE("Origin frames convert zero nonzero cell-boundary and negative coordinates", "[unit][world_streaming][origin_frame]") {
            const auto origin = Frame(-1, Math::WorldCoordinate64::CanonicalCellSizeMillimeters, -1'024'001);
            const auto global = Math::WorldCoordinate64::FromMillimeters(999, 1'024'000, -1'024'000);
            const auto local = origin.ToLocal(global).Value();
            REQUIRE(local.Value() == (Math::Vec3{1.0F, 0.0F, 0.001F}));
            REQUIRE(local.Frame() == origin.Binding().identity);
            REQUIRE(local.Generation() == origin.Binding().generation);
            REQUIRE(origin.ToGlobal(local).Value() == global);
            REQUIRE(origin.Origin().Millimeters()[0] == -1);

            const auto boundary =
                Frame().ToLocal(Math::WorldCoordinate64::FromMillimeters(OriginFrame::MaximumLocalHalfExtentMillimeters,
                                                                         -OriginFrame::MaximumLocalHalfExtentMillimeters, 0));
            REQUIRE(boundary.HasValue());
            REQUIRE(Frame().ToGlobal(boundary.Value()).Value().Millimeters()[0] == OriginFrame::MaximumLocalHalfExtentMillimeters);
        }

        TEST_CASE("Origin frame conversions reject one-over local extent and signed overflow", "[unit][world_streaming][origin_frame]") {
            const auto over = OriginFrame::MaximumLocalHalfExtentMillimeters + 1;
            RequireErrorCode(Frame().ToLocal(Math::WorldCoordinate64::FromMillimeters(over, 0, 0)).ErrorValue(),
                             WorldStreamingErrors::OriginFrameRangeExceeded);
            RequireErrorCode(Frame().ToLocal(Math::WorldCoordinate64::FromMillimeters(-over, 0, 0)).ErrorValue(),
                             WorldStreamingErrors::OriginFrameRangeExceeded);

            const auto nearMaximum = Frame(std::numeric_limits<std::int64_t>::max(), 0, 0);
            const auto positive =
                OriginLocalCoordinate::Create({0.001F, 0.0F, 0.0F}, nearMaximum.Binding().identity, nearMaximum.Binding().generation)
                    .Value();
            RequireErrorCode(nearMaximum.ToGlobal(positive).ErrorValue(), WorldStreamingErrors::OriginFrameRangeExceeded);
            const auto extremes = Frame(std::numeric_limits<std::int64_t>::min(), 0, 0)
                                      .ToLocal(Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::max(), 0, 0));
            RequireErrorCode(extremes.ErrorValue(), WorldStreamingErrors::OriginFrameRangeExceeded);
        }

        TEST_CASE("Externally supplied local coordinates reject nonfinite range and precision loss",
                  "[unit][world_streaming][origin_frame]") {
            const auto binding = Binding();
            RequireErrorCode(OriginLocalCoordinate::Create({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, binding.identity,
                                                           binding.generation)
                                 .ErrorValue(),
                             WorldStreamingErrors::OriginFrameInvalid);
            RequireErrorCode(OriginLocalCoordinate::Create({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, binding.identity,
                                                           binding.generation)
                                 .ErrorValue(),
                             WorldStreamingErrors::OriginFrameInvalid);
            RequireErrorCode(OriginLocalCoordinate::Create({8192.001F, 0.0F, 0.0F}, binding.identity, binding.generation).ErrorValue(),
                             WorldStreamingErrors::OriginFrameRangeExceeded);
            RequireErrorCode(OriginLocalCoordinate::Create({0.0004F, 0.0F, 0.0F}, binding.identity, binding.generation).ErrorValue(),
                             WorldStreamingErrors::OriginFramePrecisionLoss);
            REQUIRE(OriginLocalCoordinate::Create({-8192.0F, 8192.0F, 0.001F}, binding.identity, binding.generation).HasValue());
            const auto farMillimeter = Frame().ToLocal(Math::WorldCoordinate64::FromMillimeters(8'191'999, 0, 0)).Value();
            REQUIRE(OriginLocalCoordinate::Create(farMillimeter.Value(), binding.identity, binding.generation).HasValue());
            REQUIRE(OriginLocalCoordinate::Create({-0.0F, 0.0F, 0.0F}, binding.identity, binding.generation).HasValue());

            for (const auto millimeters : {-8'191'999LL, -1'024'001LL, -1LL, 0LL, 1LL, 1'024'001LL, 8'191'999LL}) {
                const auto global = Math::WorldCoordinate64::FromMillimeters(millimeters, -millimeters, millimeters);
                REQUIRE(Frame().ToGlobal(Frame().ToLocal(global).Value()).Value() == global);
            }
        }

        TEST_CASE("Origin local coordinates reject stale and cross-frame conversion", "[unit][world_streaming][origin_frame]") {
            const auto first = Frame();
            const auto local = first.ToLocal(Math::WorldCoordinate64::FromMillimeters(1000, 0, 0)).Value();
            RequireErrorCode(Frame(0, 0, 0, Binding(2, 2)).ToGlobal(local).ErrorValue(), WorldStreamingErrors::OriginFrameStale);
            RequireErrorCode(Frame(0, 0, 0, Binding(1, 1, 8)).ToGlobal(local).ErrorValue(), WorldStreamingErrors::OriginFrameStale);
        }

        TEST_CASE("Origin frame replacement is staged atomic cancellable and expires old leases", "[unit][world_streaming][origin_frame]") {
            auto owner = OriginFrameOwner::Create(Frame()).Value();
            const auto oldLease = owner->Lease().Value();
            const auto sameOriginSuccessor = Frame(0, 0, 0, Binding(2, 2));
            REQUIRE(owner->StageReplacement(sameOriginSuccessor).HasValue());
            REQUIRE(oldLease.Get().HasValue());
            owner->CancelReplacement();
            RequireErrorCode(owner->PublishReplacement().ErrorValue(), WorldStreamingErrors::OriginFrameLifecycleUnavailable);
            REQUIRE(owner->Lease().Value().Get().Value().Binding() == Binding());

            REQUIRE(owner->StageReplacement(sameOriginSuccessor).HasValue());
            REQUIRE(owner->PublishReplacement().HasValue());
            RequireErrorCode(oldLease.Get().ErrorValue(), WorldStreamingErrors::OriginFrameStale);
            REQUIRE(owner->Lease().Value().Get().Value() == sameOriginSuccessor);
        }

        TEST_CASE("Invalid origin frame replacement preserves the active publication", "[unit][world_streaming][origin_frame]") {
            auto owner = OriginFrameOwner::Create(Frame(1, 2, 3)).Value();
            const auto active = owner->Lease().Value();
            for (const auto candidate : {Frame(4, 5, 6, Binding(1, 1)), Frame(4, 5, 6, Binding(3, 2)), Frame(4, 5, 6, Binding(2, 3)),
                                         Frame(4, 5, 6, Binding(2, 2, 8))}) {
                RequireErrorCode(owner->StageReplacement(candidate).ErrorValue(), WorldStreamingErrors::OriginFrameStale);
                REQUIRE(active.Get().Value().Origin() == Math::WorldCoordinate64::FromMillimeters(1, 2, 3));
            }
        }

        TEST_CASE("Origin frame shutdown cancels candidates and invalidates all leases", "[unit][world_streaming][origin_frame]") {
            auto owner = OriginFrameOwner::Create(Frame()).Value();
            const auto lease = owner->Lease().Value();
            REQUIRE(owner->StageReplacement(Frame(10, 0, 0, Binding(2, 2))).HasValue());
            owner->Shutdown();
            owner->Shutdown();
            REQUIRE_FALSE(owner->IsActive());
            RequireErrorCode(lease.Get().ErrorValue(), WorldStreamingErrors::OriginFrameStale);
            RequireErrorCode(owner->Lease().ErrorValue(), WorldStreamingErrors::OriginFrameLifecycleUnavailable);
            RequireErrorCode(owner->PublishReplacement().ErrorValue(), WorldStreamingErrors::OriginFrameLifecycleUnavailable);

            auto destroyedOwner = OriginFrameOwner::Create(Frame()).Value();
            const auto destroyedLease = destroyedOwner->Lease().Value();
            destroyedOwner.reset();
            RequireErrorCode(destroyedLease.Get().ErrorValue(), WorldStreamingErrors::OriginFrameStale);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
