#include "Horo/Cinematic/TransformTrack.h"

#include "Horo/Cinematic/CinematicErrors.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Cinematic {
    namespace {
        template <typename T> [[nodiscard]] Result<T> RejectTransform(const ErrorCodeDescriptor &code, const std::string_view detail) {
            return Result<T>::Failure(MakeError(code, std::string{detail}));
        }

        [[nodiscard]] const TransformBindingSnapshot *FindExact(const std::span<const TransformBindingSnapshot> bindings,
                                                                const TransformBindingId id) noexcept {
            const auto found = std::ranges::find(bindings, id, &TransformBindingSnapshot::binding);
            return found == bindings.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] bool HasStableValue(const std::span<const TransformBindingSnapshot> bindings, const TransformBindingId id) noexcept {
            return std::ranges::any_of(bindings, [id](const TransformBindingSnapshot &binding) {
                return binding.binding.stableValue == id.stableValue;
            });
        }

        [[nodiscard]] const ErrorCodeDescriptor &UnavailableBindingError(const std::span<const TransformBindingSnapshot> bindings,
                                                                         const TransformBindingId id) noexcept {
            if (HasStableValue(bindings, id))
                return CinematicErrors::TransformBindingStale;
            return CinematicErrors::TransformBindingMissing;
        }

        [[nodiscard]] const ErrorCodeDescriptor &ParentTopologyError(const TransformTrackDescriptor &track,
                                                                     const TransformBindingSnapshot &binding) noexcept {
            if (!track.parent || !binding.parent)
                return CinematicErrors::TransformMalformed;
            if (track.parent->stableValue == binding.parent->stableValue)
                return CinematicErrors::TransformBindingStale;
            return CinematicErrors::TransformMalformed;
        }

        [[nodiscard]] Result<void> ValidateBindingEntries(const std::span<const TransformBindingSnapshot> bindings) {
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                const TransformBindingSnapshot &binding = bindings[index];
                if (!binding.binding.IsValid())
                    return RejectTransform<void>(CinematicErrors::TransformMalformed, "A transform binding identity is invalid.");
                if (std::ranges::find(bindings.first(index), binding.binding, &TransformBindingSnapshot::binding) !=
                    bindings.first(index).end())
                    return RejectTransform<void>(CinematicErrors::TransformMalformed,
                                                 "The binding snapshot contains a duplicate identity.");
                if (!binding.parent)
                    continue;
                if (!binding.parent->IsValid())
                    return RejectTransform<void>(CinematicErrors::TransformMalformed, "A transform parent identity is invalid.");
                if (FindExact(bindings, *binding.parent) == nullptr) {
                    return RejectTransform<void>(UnavailableBindingError(bindings, *binding.parent),
                                                 "A transform parent is unavailable in the exact binding snapshot.");
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsParentEmitted(const std::span<const TransformBindingSnapshot> bindings,
                                           const std::span<const std::uint8_t> emitted, const std::size_t index) noexcept {
            if (const auto parent = bindings[index].parent; parent.has_value()) {
                const auto *parentBinding = FindExact(bindings, *parent);
                const auto parentIndex = static_cast<std::size_t>(parentBinding - bindings.data());
                return emitted[parentIndex] != 0;
            }
            return true;
        }

        [[nodiscard]] Result<void> ValidateBindingHierarchy(const std::span<const TransformBindingSnapshot> bindings) {
            std::vector<std::uint8_t> emitted(bindings.size());
            std::size_t emittedCount = 0;
            while (emittedCount < bindings.size()) {
                bool progressed = false;
                for (std::size_t index = 0; index < bindings.size(); ++index) {
                    if (emitted[index])
                        continue;
                    if (!IsParentEmitted(bindings, emitted, index))
                        continue;
                    emitted[index] = true;
                    ++emittedCount;
                    progressed = true;
                }
                if (!progressed)
                    return RejectTransform<void>(CinematicErrors::TransformHierarchyCycle,
                                                 "The transform binding snapshot contains a parent cycle.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateBindings(const std::span<const TransformBindingSnapshot> bindings) {
            if (auto entries = ValidateBindingEntries(bindings); entries.HasError())
                return entries;
            return ValidateBindingHierarchy(bindings);
        }

        [[nodiscard]] Result<void> ValidateTrack(const TransformTrackDescriptor &track,
                                                 const std::span<const TransformBindingSnapshot> bindings) {
            if (track.version != CurrentTransformTrackVersion)
                return RejectTransform<void>(CinematicErrors::TransformVersionUnsupported,
                                             "The transform track requires an explicit compatible-version migration.");
            if (!track.track.IsValid())
                return RejectTransform<void>(CinematicErrors::TransformMalformed, "A transform track identity is invalid.");
            if (!track.binding.IsValid())
                return RejectTransform<void>(CinematicErrors::TransformMalformed, "A transform binding identity is invalid.");
            const TransformBindingSnapshot *binding = FindExact(bindings, track.binding);
            if (binding == nullptr) {
                return RejectTransform<void>(UnavailableBindingError(bindings, track.binding),
                                             "The transform track does not resolve in the exact binding snapshot.");
            }
            if (track.parent != binding->parent) {
                return RejectTransform<void>(ParentTopologyError(track, *binding),
                                             "The transform track parent does not match the scene binding topology.");
            }
            if (track.parent.has_value() == track.rootAnchor.has_value())
                return RejectTransform<void>(CinematicErrors::TransformMalformed,
                                             "Exactly root transform tracks must declare a canonical world anchor.");
            return Result<void>::Success();
        }

        [[nodiscard]] bool TrackBefore(const TransformTrackDescriptor &left, const TransformTrackDescriptor &right) noexcept {
            if (left.track.stableValue != right.track.stableValue)
                return left.track.stableValue < right.track.stableValue;
            return left.track.generation < right.track.generation;
        }

        [[nodiscard]] bool ParentTrackEmitted(const TransformTrackDescriptor &track, const std::span<const TransformTrackDescriptor> tracks,
                                              const std::span<const std::uint8_t> emitted) noexcept {
            if (!track.parent)
                return true;
            for (std::size_t index = 0; index < tracks.size(); ++index) {
                if (tracks[index].binding == *track.parent)
                    return emitted[index];
            }
            return true;
        }

        [[nodiscard]] Result<std::vector<TransformTrackDescriptor>> CompileOrder(const std::span<const TransformTrackDescriptor> tracks) {
            std::vector<TransformTrackDescriptor> ordered;
            ordered.reserve(tracks.size());
            std::vector<std::uint8_t> emitted(tracks.size());
            while (ordered.size() < tracks.size()) {
                std::optional<std::size_t> selected;
                for (std::size_t index = 0; index < tracks.size(); ++index) {
                    if (emitted[index] || !ParentTrackEmitted(tracks[index], tracks, emitted))
                        continue;
                    if (!selected || TrackBefore(tracks[index], tracks[*selected]))
                        selected = index;
                }
                if (!selected.has_value())
                    return RejectTransform<std::vector<TransformTrackDescriptor>>(CinematicErrors::TransformHierarchyCycle,
                                                                                  "Transform tracks contain a parent cycle.");
                emitted[*selected] = true;
                ordered.push_back(tracks[*selected]);
            }
            return Result<std::vector<TransformTrackDescriptor>>::Success(std::move(ordered));
        }

        [[nodiscard]] Result<void> ValidateTracks(const std::span<const TransformTrackDescriptor> tracks,
                                                  const std::span<const TransformBindingSnapshot> bindings) {
            for (std::size_t index = 0; index < tracks.size(); ++index) {
                if (auto validated = ValidateTrack(tracks[index], bindings); validated.HasError())
                    return validated;
                for (std::size_t prior = 0; prior < index; ++prior) {
                    if (tracks[prior].track == tracks[index].track)
                        return RejectTransform<void>(CinematicErrors::TransformMalformed,
                                                     "Transform tracks contain a duplicate track identity.");
                    if (tracks[prior].binding == tracks[index].binding)
                        return RejectTransform<void>(CinematicErrors::TransformMalformed,
                                                     "Transform tracks contain a duplicate binding identity.");
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<Math::Transform> SampleTransform(const TransformTrackDescriptor &track, const CurveTime time) {
            std::array<float, 10> values{};
            std::size_t outputIndex = 0;
            const auto sampleChannels = [&values, &outputIndex, time](const auto &channels) {
                for (const ScalarCurveView &channel : channels) {
                    auto sample = channel.Sample(time);
                    if (sample.HasError())
                        return Result<void>::Failure(sample.ErrorValue());
                    values[outputIndex++] = sample.Value().value;
                }
                return Result<void>::Success();
            };
            if (auto sampled = sampleChannels(track.curves.translation); sampled.HasError())
                return Result<Math::Transform>::Failure(sampled.ErrorValue());
            if (auto sampled = sampleChannels(track.curves.rotation); sampled.HasError())
                return Result<Math::Transform>::Failure(sampled.ErrorValue());
            if (auto sampled = sampleChannels(track.curves.scale); sampled.HasError())
                return Result<Math::Transform>::Failure(sampled.ErrorValue());

            Math::Transform transform{{values[0], values[1], values[2]},
                                      {values[3], values[4], values[5], values[6]},
                                      {values[7], values[8], values[9]}};
            if (auto normalized = transform.rotation.TryNormalized(); normalized.HasValue())
                transform.rotation = normalized.Value();
            else
                return RejectTransform<Math::Transform>(CinematicErrors::TransformSampleInvalid,
                                                        "Sampled rotation channels form a zero or non-finite quaternion.");
            return Result<Math::Transform>::Success(transform);
        }

        [[nodiscard]] Result<void> ApplyRootOrigin(Math::Transform &transform, const Math::WorldCoordinate64 &anchor,
                                                   const Math::WorldCoordinate64 &origin) {
            const auto anchorMillimeters = anchor.Millimeters();
            const auto originMillimeters = origin.Millimeters();
            std::array<float, 3> offsetMeters{};
            for (std::size_t axis = 0; axis < offsetMeters.size(); ++axis) {
                const long double delta =
                    static_cast<long double>(anchorMillimeters[axis]) - static_cast<long double>(originMillimeters[axis]);
                offsetMeters[axis] = static_cast<float>(delta / 1'000.0L);
            }
            transform.translation += {offsetMeters[0], offsetMeters[1], offsetMeters[2]};
            if (!Math::IsFinite(transform.translation))
                return RejectTransform<void>(CinematicErrors::TransformSampleInvalid,
                                             "The root anchor cannot be represented in the active origin frame.");
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc TransformEvaluationPlan::Create */
    Result<TransformEvaluationPlan> TransformEvaluationPlan::Create(const TransformSceneVersion scene,
                                                                    const std::span<const TransformTrackDescriptor> tracks,
                                                                    const std::span<const TransformBindingSnapshot> bindings) {
        if (scene.sceneGeneration == 0 || scene.bindingRevision == 0)
            return RejectTransform<TransformEvaluationPlan>(CinematicErrors::TransformMalformed,
                                                            "Transform scene and binding generations must be non-zero.");
        if (tracks.empty() || tracks.size() > MaximumTransformTracks || bindings.size() > MaximumTransformTracks)
            return RejectTransform<TransformEvaluationPlan>(CinematicErrors::TransformLimitExceeded,
                                                            "Transform activation exceeds the compiled track or binding limit.");
        if (auto validated = ValidateBindings(bindings); validated.HasError())
            return Result<TransformEvaluationPlan>::Failure(validated.ErrorValue());
        if (auto validated = ValidateTracks(tracks, bindings); validated.HasError())
            return Result<TransformEvaluationPlan>::Failure(validated.ErrorValue());
        auto ordered = CompileOrder(tracks);
        if (ordered.HasError())
            return Result<TransformEvaluationPlan>::Failure(ordered.ErrorValue());
        return Result<TransformEvaluationPlan>::Success(TransformEvaluationPlan{scene, std::move(ordered).Value()});
    }

    /** @copydoc TransformEvaluationPlan::Evaluate */
    Result<std::size_t> TransformEvaluationPlan::Evaluate(const CurveTime time, const TransformEvaluationContext &context,
                                                          const std::span<TransformEvaluationValue> output) const {
        if (context.scene != scene_)
            return RejectTransform<std::size_t>(CinematicErrors::TransformBindingStale,
                                                "The evaluation plan does not belong to the active scene binding generation.");
        if (context.originEpoch == 0)
            return RejectTransform<std::size_t>(CinematicErrors::TransformMalformed, "The evaluation context has no active origin epoch.");
        if (output.size() < orderedTracks_.size())
            return RejectTransform<std::size_t>(CinematicErrors::TransformLimitExceeded,
                                                "Caller output storage is smaller than the admitted transform-track count.");

        for (std::size_t index = 0; index < orderedTracks_.size(); ++index) {
            const TransformTrackDescriptor &track = orderedTracks_[index];
            auto sampled = SampleTransform(track, time);
            if (sampled.HasError())
                return Result<std::size_t>::Failure(sampled.ErrorValue());
            Math::Transform transform = sampled.Value();
            if (track.rootAnchor) {
                if (auto rebased = ApplyRootOrigin(transform, *track.rootAnchor, context.origin); rebased.HasError())
                    return Result<std::size_t>::Failure(rebased.ErrorValue());
            }
            output[index] = {track.track, track.binding, track.parent, transform, track.rootAnchor.has_value()};
        }
        return Result<std::size_t>::Success(orderedTracks_.size());
    }

    /** @copydoc TransformEvaluationPlan::SceneVersion */
    TransformSceneVersion TransformEvaluationPlan::SceneVersion() const noexcept {
        return scene_;
    }

    /** @copydoc TransformEvaluationPlan::TrackCount */
    std::size_t TransformEvaluationPlan::TrackCount() const noexcept {
        return orderedTracks_.size();
    }

    TransformEvaluationPlan::TransformEvaluationPlan(const TransformSceneVersion scene,
                                                     std::vector<TransformTrackDescriptor> orderedTracks) noexcept
        : scene_(scene), orderedTracks_(std::move(orderedTracks)) {}
}  // namespace Horo::Cinematic
