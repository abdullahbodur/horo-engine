#include "Horo/Cinematic/TransformTrack.h"

#include "Horo/Cinematic/CinematicErrors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace Horo::Cinematic {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Reject(const ErrorCodeDescriptor &descriptor, const std::string_view detail) {
            return Result<T>::Failure(MakeError(descriptor, std::string{detail}));
        }

        [[nodiscard]] Result<float> SampleChannel(const ScalarCurveView &curve, const CurveTime time) {
            Result<ScalarCurveSample> sampled = curve.Sample(time);
            if (sampled.HasError())
                return Result<float>::Failure(sampled.ErrorValue());
            return Result<float>::Success(sampled.Value().value);
        }

        [[nodiscard]] Result<Math::Vec3> SampleVector(const std::array<ScalarCurveView, 3> &curves, const CurveTime time) {
            const Result<float> x = SampleChannel(curves[0], time);
            if (x.HasError())
                return Result<Math::Vec3>::Failure(x.ErrorValue());
            const Result<float> y = SampleChannel(curves[1], time);
            if (y.HasError())
                return Result<Math::Vec3>::Failure(y.ErrorValue());
            const Result<float> z = SampleChannel(curves[2], time);
            if (z.HasError())
                return Result<Math::Vec3>::Failure(z.ErrorValue());
            return Result<Math::Vec3>::Success({x.Value(), y.Value(), z.Value()});
        }

        [[nodiscard]] Result<Math::Quaternion> SampleRotation(const std::array<ScalarCurveView, 4> &curves, const CurveTime time) {
            std::array<float, 4> value{};
            for (std::size_t channel = 0; channel < value.size(); ++channel) {
                const Result<float> sampled = SampleChannel(curves[channel], time);
                if (sampled.HasError())
                    return Result<Math::Quaternion>::Failure(sampled.ErrorValue());
                value[channel] = sampled.Value();
            }
            const Result<Math::Quaternion> normalized = Math::Quaternion{value[0], value[1], value[2], value[3]}.TryNormalized();
            if (normalized.HasError())
                return Reject<Math::Quaternion>(CinematicErrors::TransformRotationInvalid,
                                                "Sampled quaternion channels do not form a finite non-zero rotation.");
            return normalized;
        }

        [[nodiscard]] Result<void> ValidateRotationCurves(const std::array<ScalarCurveView, 4> &curves) {
            const std::span<const ScalarCurveKey> reference = curves[0].Keys();
            for (std::size_t channel = 1; channel < curves.size(); ++channel) {
                const std::span<const ScalarCurveKey> keys = curves[channel].Keys();
                if (keys.size() != reference.size())
                    return Reject<void>(CinematicErrors::TransformRotationInvalid,
                                        "Quaternion channels must use the same key count and timeline positions.");
                for (std::size_t key = 0; key < keys.size(); ++key) {
                    if (keys[key].time != reference[key].time)
                        return Reject<void>(CinematicErrors::TransformRotationInvalid,
                                            "Quaternion channels must use the same key count and timeline positions.");
                }
            }
            std::optional<Math::Quaternion> previous;
            for (std::size_t key = 0; key < reference.size(); ++key) {
                const Math::Quaternion value{curves[0].Keys()[key].value, curves[1].Keys()[key].value, curves[2].Keys()[key].value,
                                             curves[3].Keys()[key].value};
                const Result<Math::Quaternion> normalized = value.TryNormalized();
                if (normalized.HasError())
                    return Reject<void>(CinematicErrors::TransformRotationInvalid,
                                        "Every authored quaternion key must be finite and non-zero.");
                if (previous) {
                    const float dot = previous->x * normalized.Value().x + previous->y * normalized.Value().y +
                                      previous->z * normalized.Value().z + previous->w * normalized.Value().w;
                    if (dot < -Math::DefaultEpsilon)
                        return Reject<void>(CinematicErrors::TransformRotationInvalid,
                                            "Quaternion keys must use one continuous hemisphere before interpolation.");
                }
                previous = normalized.Value();
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<Math::Vec3> LocalizeAnchor(const Math::WorldCoordinate64 &anchor, const Math::WorldCoordinate64 &origin) {
            const auto anchorMillimeters = anchor.Millimeters();
            const auto originMillimeters = origin.Millimeters();
            Math::Vec3 localized{};
            float *components[] = {&localized.x, &localized.y, &localized.z};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const long double meters =
                    (static_cast<long double>(anchorMillimeters[axis]) - static_cast<long double>(originMillimeters[axis])) / 1000.0L;
                if (meters > static_cast<long double>(std::numeric_limits<float>::max()) ||
                    meters < -static_cast<long double>(std::numeric_limits<float>::max()))
                    return Reject<Math::Vec3>(CinematicErrors::TransformOriginInvalid,
                                              "Canonical anchor is outside the representable localized float range.");
                *components[axis] = static_cast<float>(meters);
            }
            return Result<Math::Vec3>::Success(localized);
        }

    }  // namespace

    /** @copydoc TransformTrackView::Create */
    Result<TransformTrackView> TransformTrackView::Create(TransformTrackBinding binding, TransformTrackCurves curves) {
        if (!binding.target.IsValid())
            return Reject<TransformTrackView>(CinematicErrors::TransformBindingInvalid,
                                              "A transform track requires a non-zero authored target.");
        if (binding.parent && (!binding.parent->IsValid() || *binding.parent == binding.target))
            return Reject<TransformTrackView>(CinematicErrors::TransformBindingInvalid,
                                              "A transform track parent must be a distinct non-zero authored object.");
        if (binding.parent.has_value() == binding.rootAnchor.has_value())
            return Reject<TransformTrackView>(CinematicErrors::TransformBindingInvalid,
                                              "A root track requires one canonical anchor and a child track requires one parent.");
        if (const Result<void> rotation = ValidateRotationCurves(curves.rotation); rotation.HasError())
            return Result<TransformTrackView>::Failure(rotation.ErrorValue());
        return Result<TransformTrackView>::Success(TransformTrackView{std::move(binding), std::move(curves)});
    }

    /** @copydoc TransformTrackView::Sample */
    Result<Math::Transform> TransformTrackView::Sample(const CurveTime time, const TransformOriginEpoch &origin) const {
        if (origin.generation == 0)
            return Reject<Math::Transform>(CinematicErrors::TransformOriginInvalid, "Transform sampling requires a non-zero origin epoch.");

        Result<Math::Vec3> position = SampleVector(curves_.position, time);
        if (position.HasError())
            return Result<Math::Transform>::Failure(position.ErrorValue());
        const Result<Math::Quaternion> rotation = SampleRotation(curves_.rotation, time);
        if (rotation.HasError())
            return Result<Math::Transform>::Failure(rotation.ErrorValue());
        const Result<Math::Vec3> scale = SampleVector(curves_.scale, time);
        if (scale.HasError())
            return Result<Math::Transform>::Failure(scale.ErrorValue());

        if (binding_.rootAnchor) {
            const Result<Math::Vec3> localized = LocalizeAnchor(*binding_.rootAnchor, origin.origin);
            if (localized.HasError())
                return Result<Math::Transform>::Failure(localized.ErrorValue());
            position = Result<Math::Vec3>::Success(localized.Value() + position.Value());
        }

        Math::Transform transform{position.Value(), rotation.Value(), scale.Value()};
        if (transform.TryToMatrix().HasError())
            return Reject<Math::Transform>(CinematicErrors::TransformSampleInvalid,
                                           "Sampled transform is not a finite representable local TRS value.");
        return Result<Math::Transform>::Success(transform);
    }

    /** @copydoc TransformTrackView::Binding */
    const TransformTrackBinding &TransformTrackView::Binding() const noexcept {
        return binding_;
    }

    /** @copydoc TransformTrackView::TransformTrackView */
    TransformTrackView::TransformTrackView(TransformTrackBinding binding, TransformTrackCurves curves) noexcept
        : binding_(std::move(binding)), curves_(std::move(curves)) {}

    /** @copydoc BuildTransformTrackOrder */
    Result<std::size_t> BuildTransformTrackOrder(const std::span<const TransformTrackView> tracks,
                                                 const TransformTrackOrderWorkspace workspace) {
        if (tracks.size() > MaximumTransformTracksPerEvaluation || workspace.states.size() < tracks.size() ||
            workspace.sortedIndices.size() < tracks.size() || workspace.traversalStack.size() < tracks.size() ||
            workspace.orderedIndices.size() < tracks.size())
            return Reject<std::size_t>(CinematicErrors::TransformCapacityExceeded,
                                       "Transform hierarchy input or caller-owned workspace exceeds its admitted capacity.");

        std::fill_n(workspace.states.begin(), tracks.size(), std::uint8_t{});
        for (std::size_t index = 0; index < tracks.size(); ++index)
            workspace.sortedIndices[index] = index;
        std::sort(workspace.sortedIndices.begin(), workspace.sortedIndices.begin() + static_cast<std::ptrdiff_t>(tracks.size()),
                  [&tracks](const std::size_t left, const std::size_t right) {
            return tracks[left].Binding().target.value < tracks[right].Binding().target.value;
        });
        for (std::size_t index = 1; index < tracks.size(); ++index) {
            if (tracks[workspace.sortedIndices[index - 1]].Binding().target == tracks[workspace.sortedIndices[index]].Binding().target)
                return Reject<std::size_t>(CinematicErrors::TransformBindingDuplicate,
                                           "Two transform tracks target the same authored scene object.");
        }

        const auto findSorted = [&tracks, &workspace](const Runtime::SceneObjectId target) -> std::optional<std::size_t> {
            const auto begin = workspace.sortedIndices.begin();
            const auto end = begin + static_cast<std::ptrdiff_t>(tracks.size());
            const auto found = std::lower_bound(begin, end, target.value, [&tracks](const std::size_t index, const std::uint64_t value) {
                return tracks[index].Binding().target.value < value;
            });
            if (found == end || tracks[*found].Binding().target != target)
                return std::nullopt;
            return *found;
        };

        std::size_t outputCount = 0;
        for (std::size_t sorted = 0; sorted < tracks.size(); ++sorted) {
            std::size_t current = workspace.sortedIndices[sorted];
            if (workspace.states[current] == 2)
                continue;
            std::size_t depth = 0;
            while (workspace.states[current] == 0) {
                workspace.states[current] = 1;
                workspace.traversalStack[depth++] = current;
                const std::optional<Runtime::SceneObjectId> parent = tracks[current].Binding().parent;
                const std::optional<std::size_t> parentTrack = parent ? findSorted(*parent) : std::nullopt;
                if (!parentTrack)
                    break;
                current = *parentTrack;
            }
            if (workspace.states[current] == 1 && (depth == 0 || workspace.traversalStack[depth - 1] != current))
                return Reject<std::size_t>(CinematicErrors::TransformHierarchyCycle, "Transform track parent bindings contain a cycle.");
            while (depth > 0) {
                const std::size_t track = workspace.traversalStack[--depth];
                workspace.states[track] = 2;
                workspace.orderedIndices[outputCount++] = track;
            }
        }
        return Result<std::size_t>::Success(outputCount);
    }

    /** @copydoc BuildTransformTrackCommands */
    Result<TransformTrackCommandBatch> BuildTransformTrackCommands(const std::span<const TransformTrackView> tracks, const CurveTime time,
                                                                   const TransformOriginEpoch &origin,
                                                                   const Runtime::RuntimeSceneView scene,
                                                                   const TransformTrackOrderWorkspace workspace) {
        const Result<std::size_t> ordered = BuildTransformTrackOrder(tracks, workspace);
        if (ordered.HasError())
            return Result<TransformTrackCommandBatch>::Failure(ordered.ErrorValue());
        if (tracks.empty())
            return Result<TransformTrackCommandBatch>::Success({{}, 0, origin.generation});

        Runtime::SceneCommandBuffer commands;
        for (std::size_t order = 0; order < ordered.Value(); ++order) {
            const TransformTrackView &track = tracks[workspace.orderedIndices[order]];
            const std::optional<Runtime::EntityRef> target = scene.Find(track.Binding().target);
            if (!target)
                return Reject<TransformTrackCommandBatch>(CinematicErrors::TransformTargetMissing,
                                                          "A transform track target is absent from the current runtime scene generation.");
            const Result<Runtime::RuntimeEntityView> targetView = scene.Get(*target);
            if (targetView.HasError())
                return Result<TransformTrackCommandBatch>::Failure(targetView.ErrorValue());

            if (track.Binding().parent) {
                const std::optional<Runtime::EntityRef> parent = scene.Find(*track.Binding().parent);
                if (!parent)
                    return Reject<
                        TransformTrackCommandBatch>(CinematicErrors::TransformParentMissing,
                                                    "A transform track parent is absent from the current runtime scene generation.");
                if (!targetView.Value().parent || *targetView.Value().parent != *parent)
                    return Reject<TransformTrackCommandBatch>(CinematicErrors::TransformParentMismatch,
                                                              "The current runtime hierarchy does not match the authored track parent.");
            } else if (targetView.Value().parent) {
                return Reject<TransformTrackCommandBatch>(CinematicErrors::TransformParentMismatch,
                                                          "An anchored root track currently resolves to a parented runtime entity.");
            }

            const Result<Math::Transform> sampled = track.Sample(time, origin);
            if (sampled.HasError())
                return Result<TransformTrackCommandBatch>::Failure(sampled.ErrorValue());
            commands.SetLocalTransform(*target, sampled.Value());
        }
        return Result<TransformTrackCommandBatch>::Success({std::move(commands), ordered.Value(), origin.generation});
    }
}  // namespace Horo::Cinematic
