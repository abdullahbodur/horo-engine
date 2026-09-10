#include "Horo/Cinematic/SequenceEvaluation.h"

#include "Horo/Cinematic/SequenceEvaluationErrors.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <utility>

namespace Horo::Cinematic {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failed(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        struct TraversalSegment final {
            SequenceTime from{};
            SequenceTime to{};
            std::uint64_t traversal{};
            SequenceTraversalDirection direction{SequenceTraversalDirection::Forward};
            bool includeFrom{};
        };

        struct AdvanceResult final {
            SequenceFrameCursor cursor;
            std::array<TraversalSegment, MaximumFrameLoopCrossings + 2> segments{};
            std::size_t segmentCount{};
        };

        [[nodiscard]] bool IdentityLess(const auto &left, const auto &right) noexcept {
            if (left.stableValue != right.stableValue)
                return left.stableValue < right.stableValue;
            return left.generation < right.generation;
        }

        [[nodiscard]] bool TrackLess(const SequenceFrameTrackDescriptor &left, const SequenceFrameTrackDescriptor &right) noexcept {
            if (left.stage != right.stage)
                return left.stage < right.stage;
            return IdentityLess(left.track, right.track);
        }

        [[nodiscard]] bool EventLess(const SequenceFrameEventKey &left, const SequenceFrameEventKey &right) noexcept {
            if (left.time != right.time)
                return left.time < right.time;
            if (left.track != right.track)
                return IdentityLess(left.track, right.track);
            return IdentityLess(left.key, right.key);
        }

        [[nodiscard]] bool CameraLess(const SequenceFrameCameraCutKey &left, const SequenceFrameCameraCutKey &right) noexcept {
            if (left.time != right.time)
                return left.time < right.time;
            if (left.track != right.track)
                return IdentityLess(left.track, right.track);
            return IdentityLess(left.key, right.key);
        }

        [[nodiscard]] bool PlayerLess(const SequenceFramePlayerOrder &left, const SequenceFramePlayerOrder &right) noexcept {
            if (left.priority != right.priority)
                return left.priority > right.priority;
            if (left.player.player != right.player.player)
                return IdentityLess(left.player.player, right.player.player);
            return IdentityLess(left.player.session, right.player.session);
        }

        [[nodiscard]] bool ValidTrack(const SequenceFrameTrackDescriptor &track) noexcept {
            return track.track.IsValid() && track.stage < SequenceApplyStage::Count && track.context != nullptr &&
                   track.sample != nullptr && track.apply != nullptr;
        }

        [[nodiscard]] bool ValidEvent(const SequenceFrameEventKey &event, const SequenceTime duration) noexcept {
            return event.track.IsValid() && event.key.IsValid() && event.time >= 0 && event.time <= duration;
        }

        [[nodiscard]] bool ValidCamera(const SequenceFrameCameraCutKey &cut, const SequenceTime duration) noexcept {
            return cut.track.IsValid() && cut.key.IsValid() && cut.camera.IsValid() && cut.time >= 0 && cut.time <= duration;
        }

        template <typename Value, typename SameIdentity>
        [[nodiscard]] bool HasIdentityCollision(const std::span<const Value> values, SameIdentity sameIdentity) noexcept {
            for (std::size_t left = 0; left < values.size(); ++left)
                for (std::size_t right = left + 1; right < values.size(); ++right)
                    if (sameIdentity(values[left], values[right]))
                        return true;
            return false;
        }

        [[nodiscard]] Result<std::int64_t> ScaleDelta(const SequenceTime sourceDelta, const SequencePlaybackRate rate,
                                                      const std::int64_t remainder) {
            if (sourceDelta < 0)
                return Failed<std::int64_t>(SequenceEvaluationErrors::DeltaInvalid);
            if (rate.denominator == 0 || remainder <= -static_cast<std::int64_t>(rate.denominator) ||
                remainder >= static_cast<std::int64_t>(rate.denominator))
                return Failed<std::int64_t>(SequenceEvaluationErrors::Malformed);
            const auto numerator = static_cast<std::int64_t>(rate.numerator);
            if (numerator != 0 && sourceDelta > std::numeric_limits<std::int64_t>::max() / std::max<std::int64_t>(1, std::abs(numerator)))
                return Failed<std::int64_t>(SequenceEvaluationErrors::ArithmeticOverflow);
            const std::int64_t product = sourceDelta * numerator;
            if ((remainder > 0 && product > std::numeric_limits<std::int64_t>::max() - remainder) ||
                (remainder < 0 && product < std::numeric_limits<std::int64_t>::min() - remainder))
                return Failed<std::int64_t>(SequenceEvaluationErrors::ArithmeticOverflow);
            return Result<std::int64_t>::Success(product + remainder);
        }

        [[nodiscard]] Result<void> AddSegment(AdvanceResult &result, const TraversalSegment segment) {
            if (result.segmentCount >= result.segments.size())
                return Failed<void>(SequenceEvaluationErrors::CapacityExceeded);
            result.segments[result.segmentCount++] = segment;
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AdvanceLoop(AdvanceResult &result, std::int64_t remaining, const SequenceTime duration,
                                               const std::size_t maximumCrossings) {
            bool includeFrom = false;
            std::size_t crossings = 0;
            while (remaining != 0) {
                if (remaining > 0) {
                    const SequenceTime distance = duration - result.cursor.position;
                    if (remaining <= distance) {
                        const SequenceTime next = result.cursor.position + remaining;
                        if (auto added = AddSegment(result, {result.cursor.position, next, result.cursor.traversal,
                                                             SequenceTraversalDirection::Forward, includeFrom});
                            added.HasError())
                            return added;
                        result.cursor.position = next;
                        break;
                    }
                    if (distance > 0) {
                        if (auto added = AddSegment(result, {result.cursor.position, duration, result.cursor.traversal,
                                                             SequenceTraversalDirection::Forward, includeFrom});
                            added.HasError())
                            return added;
                        remaining -= distance;
                    }
                    if (++crossings > maximumCrossings || result.cursor.traversal == std::numeric_limits<std::uint64_t>::max())
                        return Failed<void>(SequenceEvaluationErrors::CapacityExceeded);
                    ++result.cursor.traversal;
                    result.cursor.position = 0;
                    includeFrom = true;
                } else {
                    const SequenceTime distance = result.cursor.position;
                    if (-remaining <= distance) {
                        const SequenceTime next = result.cursor.position + remaining;
                        if (auto added = AddSegment(result, {result.cursor.position, next, result.cursor.traversal,
                                                             SequenceTraversalDirection::Reverse, includeFrom});
                            added.HasError())
                            return added;
                        result.cursor.position = next;
                        break;
                    }
                    if (distance > 0) {
                        if (auto added = AddSegment(result, {result.cursor.position, 0, result.cursor.traversal,
                                                             SequenceTraversalDirection::Reverse, includeFrom});
                            added.HasError())
                            return added;
                        remaining += distance;
                    }
                    if (++crossings > maximumCrossings || result.cursor.traversal == std::numeric_limits<std::uint64_t>::max())
                        return Failed<void>(SequenceEvaluationErrors::CapacityExceeded);
                    ++result.cursor.traversal;
                    result.cursor.position = duration;
                    includeFrom = true;
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AdvancePingPong(AdvanceResult &result, const std::int64_t step, const SequenceTime duration,
                                                   const std::size_t maximumCrossings) {
            std::uint64_t remaining = static_cast<std::uint64_t>(step < 0 ? -step : step);
            std::int8_t direction = step < 0 ? -result.cursor.pingPongDirection : result.cursor.pingPongDirection;
            std::size_t crossings = 0;
            while (remaining != 0) {
                const SequenceTime distance = direction > 0 ? duration - result.cursor.position : result.cursor.position;
                if (remaining <= static_cast<std::uint64_t>(distance)) {
                    const SequenceTime next = result.cursor.position + static_cast<SequenceTime>(remaining) * direction;
                    if (auto added =
                            AddSegment(result,
                                       {result.cursor.position, next, result.cursor.traversal,
                                        direction > 0 ? SequenceTraversalDirection::Forward : SequenceTraversalDirection::Reverse, false});
                        added.HasError())
                        return added;
                    result.cursor.position = next;
                    break;
                }
                if (distance > 0) {
                    const SequenceTime next = direction > 0 ? duration : 0;
                    if (auto added =
                            AddSegment(result,
                                       {result.cursor.position, next, result.cursor.traversal,
                                        direction > 0 ? SequenceTraversalDirection::Forward : SequenceTraversalDirection::Reverse, false});
                        added.HasError())
                        return added;
                    result.cursor.position = next;
                    remaining -= static_cast<std::uint64_t>(distance);
                }
                if (++crossings > maximumCrossings || result.cursor.traversal == std::numeric_limits<std::uint64_t>::max())
                    return Failed<void>(SequenceEvaluationErrors::CapacityExceeded);
                ++result.cursor.traversal;
                direction = static_cast<std::int8_t>(-direction);
                result.cursor.pingPongDirection = static_cast<std::int8_t>(-result.cursor.pingPongDirection);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<AdvanceResult> Advance(const SequenceFrameCursor &cursor, const SequencePlaybackRate rate,
                                                    const SequenceTime sourceDelta, const SequenceTime duration,
                                                    const SequenceLoopMode loopMode, const std::size_t maximumCrossings) {
            auto scaled = ScaleDelta(sourceDelta, rate, cursor.rateRemainder);
            if (scaled.HasError())
                return Result<AdvanceResult>::Failure(scaled.ErrorValue());
            AdvanceResult result{};
            result.cursor = cursor;
            const auto denominator = static_cast<std::int64_t>(rate.denominator);
            const std::int64_t step = scaled.Value() / denominator;
            result.cursor.rateRemainder = scaled.Value() % denominator;
            if (!cursor.eventCursorInitialized) {
                const auto direction = step < 0 ? SequenceTraversalDirection::Reverse : SequenceTraversalDirection::Forward;
                if (auto added = AddSegment(result, {cursor.position, cursor.position, cursor.traversal, direction, true});
                    added.HasError())
                    return Result<AdvanceResult>::Failure(added.ErrorValue());
                result.cursor.eventCursorInitialized = true;
            }
            if (step == 0)
                return Result<AdvanceResult>::Success(result);
            if (step == std::numeric_limits<std::int64_t>::min())
                return Failed<AdvanceResult>(SequenceEvaluationErrors::ArithmeticOverflow);
            Result<void> advanced = Result<void>::Success();
            if (loopMode == SequenceLoopMode::Loop) {
                advanced = AdvanceLoop(result, step, duration, maximumCrossings);
            } else if (loopMode == SequenceLoopMode::PingPong) {
                advanced = AdvancePingPong(result, step, duration, maximumCrossings);
            } else {
                const SequenceTime target = step > 0 ? (step > duration - cursor.position ? duration : cursor.position + step)
                                                     : (-step > cursor.position ? 0 : cursor.position + step);
                advanced =
                    AddSegment(result, {cursor.position, target, cursor.traversal,
                                        step > 0 ? SequenceTraversalDirection::Forward : SequenceTraversalDirection::Reverse, false});
                result.cursor.position = target;
            }
            if (advanced.HasError())
                return Result<AdvanceResult>::Failure(advanced.ErrorValue());
            return Result<AdvanceResult>::Success(result);
        }

        template <typename Key> [[nodiscard]] bool Crossed(const Key &key, const TraversalSegment &segment) noexcept {
            if (segment.from == segment.to)
                return segment.includeFrom && key.time == segment.from;
            if (segment.direction == SequenceTraversalDirection::Forward)
                return (segment.includeFrom ? key.time >= segment.from : key.time > segment.from) && key.time <= segment.to;
            return key.time >= segment.to && (segment.includeFrom ? key.time <= segment.from : key.time < segment.from);
        }

        template <typename Key, typename Emit>
        void VisitCrossed(const std::span<const Key> keys, const std::span<const TraversalSegment> segments, Emit emit) {
            for (const TraversalSegment &segment : segments) {
                if (segment.direction == SequenceTraversalDirection::Forward) {
                    for (const Key &key : keys)
                        if (Crossed(key, segment))
                            emit(key, segment);
                    continue;
                }
                std::size_t end = keys.size();
                while (end != 0) {
                    std::size_t begin = end - 1;
                    while (begin != 0 && keys[begin - 1].time == keys[end - 1].time)
                        --begin;
                    for (std::size_t index = begin; index < end; ++index)
                        if (Crossed(keys[index], segment))
                            emit(keys[index], segment);
                    end = begin;
                }
            }
        }

        [[nodiscard]] bool ValidCursor(const SequenceFrameCursor &cursor, const SequenceTime duration,
                                       const SequencePlaybackRate rate) noexcept {
            return cursor.controlFence.handle.IsValid() && cursor.controlFence.controlRevision != 0 && cursor.position >= 0 &&
                   cursor.position <= duration && cursor.traversal != 0 && cursor.evaluationRevision != 0 &&
                   (cursor.pingPongDirection == 1 || cursor.pingPongDirection == -1) && rate.denominator != 0;
        }
    }  // namespace

    /** @copydoc MakeSequenceFrameCursor */
    Result<SequenceFrameCursor> MakeSequenceFrameCursor(const SequencePlayerSnapshot &player, const SequenceCursorResetPolicy resetPolicy) {
        if (!player.handle.IsValid() || player.controlRevision == 0 || player.position < 0 || player.position > player.duration ||
            player.rate.denominator == 0 || resetPolicy >= SequenceCursorResetPolicy::Count)
            return Failed<SequenceFrameCursor>(SequenceEvaluationErrors::Malformed);
        return Result<SequenceFrameCursor>::Success({{player.handle, player.controlRevision},
                                                     player.position,
                                                     0,
                                                     1,
                                                     1,
                                                     1,
                                                     resetPolicy == SequenceCursorResetPolicy::SuppressCurrentBoundary});
    }

    /** @copydoc OrderSequenceFramePlayers */
    Result<std::size_t> OrderSequenceFramePlayers(const std::span<const SequenceFramePlayerOrder> unordered,
                                                  const std::span<SequenceFramePlayerOrder> ordered) {
        if (unordered.size() > MaximumFramePlayers || ordered.size() < unordered.size())
            return Failed<std::size_t>(SequenceEvaluationErrors::CapacityExceeded);
        if (HasIdentityCollision<SequenceFramePlayerOrder>(unordered, [](const auto &left, const auto &right) {
            return left.player == right.player;
        }))
            return Failed<std::size_t>(SequenceEvaluationErrors::Malformed);
        for (std::size_t index = 0; index < unordered.size(); ++index) {
            if (!unordered[index].player.IsValid())
                return Failed<std::size_t>(SequenceEvaluationErrors::Malformed);
            ordered[index] = unordered[index];
            std::size_t insertion = index;
            while (insertion > 0 && PlayerLess(ordered[insertion], ordered[insertion - 1])) {
                std::swap(ordered[insertion], ordered[insertion - 1]);
                --insertion;
            }
        }
        return Result<std::size_t>::Success(unordered.size());
    }

    /** @copydoc SequenceFrameEvaluationPlan::Create */
    Result<SequenceFrameEvaluationPlan> SequenceFrameEvaluationPlan::Create(const SequenceTime duration, const SequenceLoopMode loopMode,
                                                                            const std::size_t maximumLoopCrossings,
                                                                            const std::span<const SequenceFrameTrackDescriptor> tracks,
                                                                            const std::span<const SequenceFrameEventKey> events,
                                                                            const std::span<const SequenceFrameCameraCutKey> cameraCuts) {
        if (duration <= 0 || loopMode >= SequenceLoopMode::Count || maximumLoopCrossings == 0 ||
            maximumLoopCrossings > MaximumFrameLoopCrossings)
            return Failed<SequenceFrameEvaluationPlan>(SequenceEvaluationErrors::Malformed);
        if (tracks.size() > MaximumFrameEvaluationTracks || events.size() > MaximumFrameOccurrences ||
            cameraCuts.size() > MaximumFrameCameraCuts)
            return Failed<SequenceFrameEvaluationPlan>(SequenceEvaluationErrors::CapacityExceeded);
        if (!std::ranges::all_of(tracks, ValidTrack) ||
            !std::ranges::all_of(events,
                                 [&](const auto &event) {
            return ValidEvent(event, duration);
        }) ||
            !std::ranges::all_of(cameraCuts, [&](const auto &cut) {
            return ValidCamera(cut, duration);
        }))
            return Failed<SequenceFrameEvaluationPlan>(SequenceEvaluationErrors::Malformed);
        std::vector<SequenceFrameTrackDescriptor> orderedTracks(tracks.begin(), tracks.end());
        std::vector<SequenceFrameEventKey> orderedEvents(events.begin(), events.end());
        std::vector<SequenceFrameCameraCutKey> orderedCuts(cameraCuts.begin(), cameraCuts.end());
        std::ranges::sort(orderedTracks, TrackLess);
        std::ranges::sort(orderedEvents, EventLess);
        std::ranges::sort(orderedCuts, CameraLess);
        if (HasIdentityCollision<SequenceFrameTrackDescriptor>(orderedTracks,
                                                               [](const auto &left, const auto &right) {
            return left.track == right.track;
        }) ||
            HasIdentityCollision<SequenceFrameEventKey>(orderedEvents,
                                                        [](const auto &left, const auto &right) {
            return left.track == right.track && left.key == right.key;
        }) ||
            HasIdentityCollision<SequenceFrameCameraCutKey>(orderedCuts, [](const auto &left, const auto &right) {
            return left.track == right.track && left.key == right.key;
        }))
            return Failed<SequenceFrameEvaluationPlan>(SequenceEvaluationErrors::Malformed);
        return Result<SequenceFrameEvaluationPlan>::Success(SequenceFrameEvaluationPlan{duration, loopMode, maximumLoopCrossings,
                                                                                        std::move(orderedTracks), std::move(orderedEvents),
                                                                                        std::move(orderedCuts)});
    }

    /** @copydoc SequenceFrameEvaluationPlan::Evaluate */
    Result<SequenceFrameEvaluationResult> SequenceFrameEvaluationPlan::Evaluate(const SequencePlayerSnapshot &player,
                                                                                const SequenceTime sourceDelta, SequenceFrameCursor &cursor,
                                                                                const SequenceFrameScratch &scratch,
                                                                                const SequenceFrameHooks &hooks) const {
        if (!ValidCursor(cursor, duration_, player.rate) || player.duration != duration_)
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::Malformed);
        if (player.handle != cursor.controlFence.handle || player.controlRevision != cursor.controlFence.controlRevision)
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::Stale);
        if (player.state != SequencePlaybackState::Playing)
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::PlayerStateInvalid);
        if (cursor.evaluationRevision == std::numeric_limits<std::uint64_t>::max())
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::RevisionExhausted);
        if (scratch.values.size() < tracks_.size())
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::CapacityExceeded);

        auto advanced = Advance(cursor, player.rate, sourceDelta, duration_, loopMode_, maximumLoopCrossings_);
        if (advanced.HasError())
            return Result<SequenceFrameEvaluationResult>::Failure(advanced.ErrorValue());
        const SequenceTime previousPosition = cursor.position;
        const auto segments = std::span{advanced.Value().segments}.first(advanced.Value().segmentCount);
        std::size_t eventCount = 0;
        VisitCrossed<SequenceFrameEventKey>(events_, segments, [&](const auto &event, const auto &segment) {
            if (segment.direction == SequenceTraversalDirection::Forward || event.fireInReverse)
                ++eventCount;
        });
        std::size_t cameraCount = 0;
        VisitCrossed<SequenceFrameCameraCutKey>(cameraCuts_, segments, [&](const auto &, const auto &) {
            ++cameraCount;
        });
        if (eventCount > scratch.events.size() || cameraCount > scratch.cameraCuts.size())
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::CapacityExceeded);
        if ((eventCount != 0 && hooks.eventHook == nullptr) || (cameraCount != 0 && hooks.cameraHook == nullptr))
            return Failed<SequenceFrameEvaluationResult>(SequenceEvaluationErrors::HookUnavailable);

        for (std::size_t index = 0; index < tracks_.size(); ++index) {
            auto sampled = tracks_[index].sample(tracks_[index].context, advanced.Value().cursor.position);
            if (sampled.HasError())
                return Result<SequenceFrameEvaluationResult>::Failure(sampled.ErrorValue());
            scratch.values[index] = {tracks_[index].track, tracks_[index].stage, sampled.Value()};
        }

        std::size_t eventIndex = 0;
        VisitCrossed<SequenceFrameEventKey>(events_, segments, [&](const auto &event, const auto &segment) {
            if (segment.direction == SequenceTraversalDirection::Forward || event.fireInReverse)
                scratch.events[eventIndex++] = {player.handle, event.track, event.key, event.time, segment.traversal, segment.direction};
        });
        std::size_t cameraIndex = 0;
        VisitCrossed<SequenceFrameCameraCutKey>(cameraCuts_, segments, [&](const auto &cut, const auto &segment) {
            scratch.cameraCuts[cameraIndex++] = {player.handle, cut.track,         cut.key,          cut.camera,
                                                 cut.time,      segment.traversal, segment.direction};
        });

        SequenceFrameCursor committed = advanced.Value().cursor;
        ++committed.evaluationRevision;
        cursor = committed;
        for (std::size_t index = 0; index < tracks_.size(); ++index)
            tracks_[index].apply(tracks_[index].context, scratch.values[index].value);
        for (std::size_t index = 0; index < eventIndex; ++index)
            hooks.eventHook(hooks.eventContext, scratch.events[index]);
        for (std::size_t index = 0; index < cameraIndex; ++index)
            hooks.cameraHook(hooks.cameraContext, scratch.cameraCuts[index]);
        return Result<SequenceFrameEvaluationResult>::Success(
            {previousPosition, cursor.position, cursor.traversal, cursor.evaluationRevision, tracks_.size(), eventIndex, cameraIndex});
    }

    /** @copydoc SequenceFrameEvaluationPlan::TrackCount */
    std::size_t SequenceFrameEvaluationPlan::TrackCount() const noexcept {
        return tracks_.size();
    }

    SequenceFrameEvaluationPlan::SequenceFrameEvaluationPlan(const SequenceTime duration, const SequenceLoopMode loopMode,
                                                             const std::size_t maximumLoopCrossings,
                                                             std::vector<SequenceFrameTrackDescriptor> tracks,
                                                             std::vector<SequenceFrameEventKey> events,
                                                             std::vector<SequenceFrameCameraCutKey> cameraCuts) noexcept
        : duration_(duration), loopMode_(loopMode), maximumLoopCrossings_(maximumLoopCrossings), tracks_(std::move(tracks)),
          events_(std::move(events)), cameraCuts_(std::move(cameraCuts)) {}
}  // namespace Horo::Cinematic
