#pragma once

/**
 * @file TransformTrack.h
 * @brief Stateless transform-track sampling and hierarchy-aware runtime application.
 */

#include "Horo/Cinematic/CurveSampling.h"
#include "Horo/Math/WorldCoordinate64.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace Horo::Cinematic {
    /** @brief Maximum transform tracks admitted by one hierarchy-order operation. */
    inline constexpr std::size_t MaximumTransformTracksPerEvaluation = 32'768;

    /** @brief Ten validated scalar channels forming one local TRS curve. */
    struct TransformTrackCurves final {
        std::array<ScalarCurveView, 3> position; /**< Local X, Y, and Z position channels. */
        std::array<ScalarCurveView, 4> rotation; /**< Quaternion X, Y, Z, and W channels. */
        std::array<ScalarCurveView, 3> scale;    /**< Local X, Y, and Z scale channels. */
    };

    /** @brief Durable scene binding and coordinate-space declaration for one transform track. */
    struct TransformTrackBinding final {
        Runtime::SceneObjectId target;                     /**< Stable authored target resolved on every application. */
        std::optional<Runtime::SceneObjectId> parent;      /**< Stable authored parent for a child-local track. */
        std::optional<Math::WorldCoordinate64> rootAnchor; /**< Canonical anchor required only for a root track. */
    };

    /** @brief Immutable floating-origin snapshot used to localize a root sample exactly once. */
    struct TransformOriginEpoch final {
        Math::WorldCoordinate64 origin; /**< Canonical active floating origin. */
        std::uint64_t generation{};     /**< Non-zero monotonic generation fencing the snapshot. */
    };

    /** @brief Caller-owned allocation-free workspace for deterministic hierarchy ordering. */
    struct TransformTrackOrderWorkspace final {
        std::span<std::uint8_t> states;        /**< One unvisited/visiting/emitted byte per input track. */
        std::span<std::size_t> sortedIndices;  /**< Scratch indices sorted by stable target identity. */
        std::span<std::size_t> traversalStack; /**< Scratch parent-chain indices. */
        std::span<std::size_t> orderedIndices; /**< Output indices in parent-before-child order. */
    };

    /** @brief Complete atomic local-transform candidate for a RuntimeScene safe-point queue. */
    struct TransformTrackCommandBatch final {
        Runtime::SceneCommandBuffer commands; /**< Owned batch for RuntimeScene or RuntimeSceneService. */
        std::size_t transformsQueued{};       /**< Number of current runtime targets represented. */
        std::uint64_t originGeneration{};     /**< Origin epoch used for every root localization. */
    };

    /**
     * @brief Validated borrowed transform track with history-independent random-access sampling.
     * @note The scalar views retain caller-owned immutable key spans for this view's lifetime.
     */
    class TransformTrackView final {
    public:
        /**
         * @brief Validates a durable target, parent/anchor space, and borrowed scalar channels.
         * @param binding Stable scene binding; roots require exactly one canonical anchor and children require none.
         * @param curves Ten already validated scalar curve views.
         * @return Borrowed transform track or a typed malformed-binding error.
         */
        [[nodiscard]] static Result<TransformTrackView> Create(TransformTrackBinding binding, TransformTrackCurves curves);

        /**
         * @brief Samples a local transform directly at an arbitrary timeline time.
         * @param time Exact sample time; prior samples and seek direction are irrelevant.
         * @param origin Immutable origin epoch; roots localize their canonical anchor against it exactly once.
         * @return Finite local transform with a normalized quaternion, or the first typed curve/transform error.
         * @note Successful sampling is allocation-free and bounded by ten scalar samples.
         */
        [[nodiscard]] Result<Math::Transform> Sample(CurveTime time, const TransformOriginEpoch &origin) const;

        /** @brief Returns the immutable durable binding. @return Binding captured at validation. */
        [[nodiscard]] const TransformTrackBinding &Binding() const noexcept;

    private:
        TransformTrackView(TransformTrackBinding binding, TransformTrackCurves curves) noexcept;

        TransformTrackBinding binding_;
        TransformTrackCurves curves_;
    };

    /**
     * @brief Builds deterministic parent-before-child order without scene access or heap allocation.
     * @param tracks Validated tracks; target identities must be unique.
     * @param workspace Caller-owned state, sorted-index, traversal, and output spans of at least tracks.size().
     * @return Number of ordered entries or a typed duplicate, cycle, or capacity error.
     */
    [[nodiscard]] Result<std::size_t> BuildTransformTrackOrder(std::span<const TransformTrackView> tracks,
                                                               TransformTrackOrderWorkspace workspace);

    /**
     * @brief Resolves current scene generations and builds one atomic local-transform batch in hierarchy order.
     * @param tracks Validated tracks to apply as one batch.
     * @param time Exact random-access timeline time.
     * @param origin One immutable origin epoch shared by the complete batch.
     * @param scene Current immutable runtime scene view used only for generation-checked binding resolution.
     * @param workspace Caller-owned allocation-free hierarchy-order workspace.
     * @return Owned command batch and origin generation, or a typed error with no partial batch.
     * @note Queue the returned commands through RuntimeSceneService at its lifecycle safe point; headless owners may
     * commit them directly at an equivalent explicit boundary.
     */
    [[nodiscard]] Result<TransformTrackCommandBatch> BuildTransformTrackCommands(std::span<const TransformTrackView> tracks, CurveTime time,
                                                                                 const TransformOriginEpoch &origin,
                                                                                 Runtime::RuntimeSceneView scene,
                                                                                 TransformTrackOrderWorkspace workspace);
}  // namespace Horo::Cinematic
