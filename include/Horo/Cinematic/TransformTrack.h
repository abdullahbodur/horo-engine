#pragma once

/**
 * @file TransformTrack.h
 * @brief Bounded hierarchy-aware cinematic transform-track evaluation.
 */

#include "Horo/Cinematic/CinematicIdentity.h"
#include "Horo/Cinematic/CurveSampling.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Math/WorldCoordinate64.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Cinematic {
    /** @brief Current binary contract understood by transform-track evaluation. */
    struct TransformTrackVersion final {
        std::uint16_t major{1};
        std::uint16_t minor{};
        constexpr auto operator<=>(const TransformTrackVersion &) const noexcept = default;
    };

    inline constexpr TransformTrackVersion CurrentTransformTrackVersion{1, 0};
    inline constexpr std::size_t MaximumTransformTracks = 1'024;

    /** @brief Ten validated scalar channels composing one local scene transform. */
    struct TransformCurveSet final {
        std::array<ScalarCurveView, 3> translation;
        std::array<ScalarCurveView, 4> rotation;
        std::array<ScalarCurveView, 3> scale;
    };

    /** @brief Immutable track metadata and borrowed validated curves admitted at activation. */
    struct TransformTrackDescriptor final {
        TransformTrackVersion version{CurrentTransformTrackVersion};
        TrackId track;
        TransformBindingId binding;
        std::optional<TransformBindingId> parent;
        std::optional<Math::WorldCoordinate64> rootAnchor;
        TransformCurveSet curves;
    };

    /** @brief Exact scene binding topology used to compile one evaluation plan. */
    struct TransformBindingSnapshot final {
        TransformBindingId binding;
        std::optional<TransformBindingId> parent;
        constexpr auto operator<=>(const TransformBindingSnapshot &) const noexcept = default;
    };

    /** @brief Generation fence for a replaceable scene and its immutable binding topology. */
    struct TransformSceneVersion final {
        std::uint64_t sceneGeneration{};
        std::uint64_t bindingRevision{};
        constexpr auto operator<=>(const TransformSceneVersion &) const noexcept = default;
    };

    /** @brief Per-evaluation spatial frame; origin changes never mutate authored curves. */
    struct TransformEvaluationContext final {
        TransformSceneVersion scene;
        Math::WorldCoordinate64 origin;
        std::uint64_t originEpoch{};
        constexpr auto operator<=>(const TransformEvaluationContext &) const noexcept = default;
    };

    /** @brief One sampled local transform in deterministic parent-before-child order. */
    struct TransformEvaluationValue final {
        TrackId track;
        TransformBindingId binding;
        std::optional<TransformBindingId> parent;
        Math::Transform localTransform;
        bool rootRebased{};
    };

    /**
     * @brief Activation-built transform plan with allocation-free random-access evaluation.
     * @note Track curves borrow immutable caller-owned key storage for the plan lifetime.
     */
    class TransformEvaluationPlan final {
    public:
        /**
         * @brief Validates versions, identities, exact bindings, hierarchy and capacity, then compiles stable order.
         * @param scene Non-zero scene/binding generation fence captured at activation.
         * @param tracks Transform tracks whose borrowed curve storage outlives the plan.
         * @param bindings Complete topology containing every track binding and ancestor.
         * @return Compiled plan or a typed version, binding, hierarchy, or capacity failure.
         */
        [[nodiscard]] static Result<TransformEvaluationPlan> Create(TransformSceneVersion scene,
                                                                    std::span<const TransformTrackDescriptor> tracks,
                                                                    std::span<const TransformBindingSnapshot> bindings);

        /**
         * @brief Samples every transform directly at an arbitrary time into caller-owned storage.
         * @param time Exact random-access curve time.
         * @param context Current exact scene and floating-origin frame.
         * @param output Storage with at least TrackCount entries; no partial output is committed on preflight failure.
         * @return Number of parent-ordered values written or a typed stale, bounds, or sampling failure.
         * @note The successful path performs no allocation, blocking work, or mutable history update.
         */
        [[nodiscard]] Result<std::size_t> Evaluate(CurveTime time, const TransformEvaluationContext &context,
                                                   std::span<TransformEvaluationValue> output) const;

        /** @brief Returns the exact activation scene fence. @return Captured scene version. */
        [[nodiscard]] TransformSceneVersion SceneVersion() const noexcept;
        /** @brief Returns the number of admitted tracks. @return Bounded track count. */
        [[nodiscard]] std::size_t TrackCount() const noexcept;

    private:
        TransformEvaluationPlan(TransformSceneVersion scene, std::vector<TransformTrackDescriptor> orderedTracks) noexcept;

        TransformSceneVersion scene_;
        std::vector<TransformTrackDescriptor> orderedTracks_;
    };
}  // namespace Horo::Cinematic
