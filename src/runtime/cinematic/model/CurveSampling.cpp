#include "Horo/Cinematic/CurveSampling.h"

#include "Horo/Cinematic/CinematicErrors.h"

#include <cmath>
#include <numeric>
#include <string>
#include <string_view>

namespace Horo::Cinematic {
    namespace {
        constexpr std::size_t CubicIterations = 12;

        template <typename T> [[nodiscard]] Result<T> Reject(const ErrorCodeDescriptor &descriptor, const std::string_view detail) {
            return Result<T>::Failure(MakeError(descriptor, std::string{detail}));
        }

        [[nodiscard]] bool IsKnown(const CurveInterpolation interpolation) noexcept {
            return interpolation < CurveInterpolation::Count;
        }

        [[nodiscard]] bool IsKnown(const CurveBoundaryMode mode) noexcept {
            return mode < CurveBoundaryMode::Count;
        }

        enum class BoundarySide : std::uint8_t {
            Before,
            After
        };

        struct MappedCurveTime final {
            CurveTime value;
            bool changed;
        };

        [[nodiscard]] CurveTime MapOutside(const CurveTime time, const CurveTime first, const CurveTime last, const CurveBoundaryMode mode,
                                           const BoundarySide side) noexcept {
            const bool before = side == BoundarySide::Before;
            if (mode == CurveBoundaryMode::Clamp)
                return before ? first : last;
            const auto duration = static_cast<std::uint64_t>(last - first);
            const auto distance = before ? static_cast<std::uint64_t>(first) + (std::uint64_t{} - static_cast<std::uint64_t>(time))
                                         : static_cast<std::uint64_t>(time) - static_cast<std::uint64_t>(last);
            const auto remainder = distance % duration;
            if (mode == CurveBoundaryMode::Repeat) {
                if (before)
                    return remainder == 0 ? first : last - static_cast<CurveTime>(remainder);
                return first + static_cast<CurveTime>(remainder);
            }
            const auto interval = distance / duration;
            const bool evenInterval = (interval & 1U) == 0U;
            if (before)
                return evenInterval ? first + static_cast<CurveTime>(remainder) : last - static_cast<CurveTime>(remainder);
            return evenInterval ? last - static_cast<CurveTime>(remainder) : first + static_cast<CurveTime>(remainder);
        }

        [[nodiscard]] double Bezier(const double p0, const double p1, const double p2, const double p3, const double parameter) noexcept {
            const double inverse = 1.0 - parameter;
            const double first = inverse * inverse * inverse * p0;
            const double second = 3.0 * inverse * inverse * parameter * p1;
            const double third = 3.0 * inverse * parameter * parameter * p2;
            const double fourth = parameter * parameter * parameter * p3;
            return ((first + second) + third) + fourth;
        }

        [[nodiscard]] float SampleBezier(const ScalarCurveKey &left, const ScalarCurveKey &right, const CurveTime time) noexcept {
            const auto x0 = static_cast<double>(left.time);
            const auto x1 = static_cast<double>(left.time + left.tangentOut.timeOffset);
            const auto x2 = static_cast<double>(right.time + right.tangentIn.timeOffset);
            const auto x3 = static_cast<double>(right.time);
            double lower = 0.0;
            double upper = 1.0;
            for (std::size_t iteration = 0; iteration < CubicIterations; ++iteration) {
                const double middle = std::midpoint(lower, upper);
                if (Bezier(x0, x1, x2, x3, middle) < static_cast<double>(time))
                    lower = middle;
                else
                    upper = middle;
            }
            const double parameter = std::midpoint(lower, upper);
            return static_cast<float>(Bezier(static_cast<double>(left.value), static_cast<double>(left.value + left.tangentOut.valueOffset),
                                             static_cast<double>(right.value + right.tangentIn.valueOffset),
                                             static_cast<double>(right.value), parameter));
        }

        [[nodiscard]] float SampleHermite(const ScalarCurveKey &left, const ScalarCurveKey &right, const CurveTime time) noexcept {
            const auto duration = static_cast<double>(right.time - left.time);
            const double parameter = static_cast<double>(time - left.time) / duration;
            const double squared = parameter * parameter;
            const double cubed = squared * parameter;
            const double outgoingSlope = static_cast<double>(left.tangentOut.valueOffset) / static_cast<double>(left.tangentOut.timeOffset);
            const double incomingSlope = static_cast<double>(right.tangentIn.valueOffset) / static_cast<double>(right.tangentIn.timeOffset);
            const double h00 = (2.0 * cubed) - (3.0 * squared) + 1.0;
            const double h10 = cubed - (2.0 * squared) + parameter;
            const double h01 = (-2.0 * cubed) + (3.0 * squared);
            const double h11 = cubed - squared;
            return static_cast<float>((h00 * left.value + h10 * duration * outgoingSlope) +
                                      (h01 * right.value + h11 * duration * incomingSlope));
        }

        [[nodiscard]] Result<void> ValidateSegment(const ScalarCurveKey &left, const ScalarCurveKey &right) {
            const CurveTime duration = right.time - left.time;
            if (left.interpolation == CurveInterpolation::CubicBezier) {
                if (left.tangentOut.timeOffset < 0 || left.tangentOut.timeOffset > duration || right.tangentIn.timeOffset > 0 ||
                    right.tangentIn.timeOffset < -duration ||
                    left.time + left.tangentOut.timeOffset > right.time + right.tangentIn.timeOffset)
                    return Reject<void>(CinematicErrors::CurveTangentInvalid,
                                        "Cubic time tangents must be monotonic inside their segment.");
            } else if (left.interpolation == CurveInterpolation::HermiteSpline &&
                       (left.tangentOut.timeOffset <= 0 || right.tangentIn.timeOffset >= 0)) {
                return Reject<void>(CinematicErrors::CurveTangentInvalid, "Hermite tangents require non-zero outward time offsets.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateKey(const ScalarCurveKey &key) {
            if (!IsKnown(key.interpolation))
                return Reject<void>(CinematicErrors::CurveMalformed, "The scalar curve uses an unknown interpolation mode.");
            if (!std::isfinite(key.value) || !std::isfinite(key.tangentIn.valueOffset) || !std::isfinite(key.tangentOut.valueOffset))
                return Reject<void>(CinematicErrors::CurveNonFinite, "A scalar key or tangent value is not finite.");
            if (key.time < 0)
                return Reject<void>(CinematicErrors::CurveMalformed, "Scalar curve key times cannot be negative.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateKeys(const std::span<const ScalarCurveKey> keys) {
            for (std::size_t index = 0; index < keys.size(); ++index) {
                if (auto keyValidation = ValidateKey(keys[index]); keyValidation.HasError())
                    return keyValidation;
                if (index == 0)
                    continue;
                if (keys[index].time <= keys[index - 1].time)
                    return Reject<void>(CinematicErrors::CurveMalformed, "Scalar curve key times must increase strictly.");
                if (auto segmentValidation = ValidateSegment(keys[index - 1], keys[index]); segmentValidation.HasError())
                    return segmentValidation;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] MappedCurveTime MapTime(const CurveTime time, const std::span<const ScalarCurveKey> keys,
                                              const CurveBoundaryPolicy boundaries) noexcept {
            const CurveTime first = keys.front().time;
            const CurveTime last = keys.back().time;
            if (time < first)
                return {MapOutside(time, first, last, boundaries.beforeFirst, BoundarySide::Before), true};
            if (time > last)
                return {MapOutside(time, first, last, boundaries.afterLast, BoundarySide::After), true};
            return {time, false};
        }

        [[nodiscard]] std::size_t FindLeftKey(const std::span<const ScalarCurveKey> keys, const CurveTime time) noexcept {
            std::size_t lower = 0;
            std::size_t upper = keys.size();
            while (lower < upper) {
                const std::size_t middle = std::midpoint(lower, upper);
                if (keys[middle].time <= time)
                    lower = middle + 1;
                else
                    upper = middle;
            }
            return lower - 1;
        }

        [[nodiscard]] float Interpolate(const ScalarCurveKey &left, const ScalarCurveKey &right, const CurveTime time) noexcept {
            using enum CurveInterpolation;
            switch (left.interpolation) {
                case Constant:
                    return left.value;
                case Linear: {
                    const double parameter = static_cast<double>(time - left.time) / static_cast<double>(right.time - left.time);
                    return static_cast<float>(static_cast<double>(left.value) +
                                              (static_cast<double>(right.value) - static_cast<double>(left.value)) * parameter);
                }
                case CubicBezier:
                    return SampleBezier(left, right, time);
                case HermiteSpline:
                    return SampleHermite(left, right, time);
                case Count:
                    return left.value;
            }
            return left.value;
        }
    }  // namespace

    /** @copydoc ScalarCurveView::Create */
    Result<ScalarCurveView> ScalarCurveView::Create(const std::span<const ScalarCurveKey> keys, const CurveBoundaryPolicy boundaries) {
        if (keys.empty())
            return Reject<ScalarCurveView>(CinematicErrors::CurveMalformed, "A scalar curve requires at least one key.");
        if (keys.size() > MaximumScalarCurveKeys)
            return Reject<ScalarCurveView>(CinematicErrors::CurveLimitExceeded, "The scalar curve exceeds the compiled key limit.");
        if (!IsKnown(boundaries.beforeFirst) || !IsKnown(boundaries.afterLast))
            return Reject<ScalarCurveView>(CinematicErrors::CurveMalformed, "The scalar curve uses an unknown boundary mode.");
        if (auto validation = ValidateKeys(keys); validation.HasError())
            return Result<ScalarCurveView>::Failure(validation.ErrorValue());
        return Result<ScalarCurveView>::Success(ScalarCurveView{keys, boundaries});
    }

    /** @copydoc ScalarCurveView::Sample */
    Result<ScalarCurveSample> ScalarCurveView::Sample(const CurveTime time) const {
        if (keys_.size() == 1)
            return Result<ScalarCurveSample>::Success({keys_.front().value, keys_.front().time, 0, time != keys_.front().time});

        const MappedCurveTime mapped = MapTime(time, keys_, boundaries_);
        if (mapped.value == keys_.back().time)
            return Result<ScalarCurveSample>::Success({keys_.back().value, mapped.value, keys_.size() - 1, mapped.changed});

        const std::size_t leftIndex = FindLeftKey(keys_, mapped.value);
        const ScalarCurveKey &left = keys_[leftIndex];
        const ScalarCurveKey &right = keys_[leftIndex + 1];
        if (left.time == mapped.value)
            return Result<ScalarCurveSample>::Success({left.value, mapped.value, leftIndex, mapped.changed});
        const float value = Interpolate(left, right, mapped.value);
        if (!std::isfinite(value))
            return Reject<ScalarCurveSample>(CinematicErrors::CurveNonFinite,
                                             "Curve interpolation produced a non-finite value at the requested time.");
        return Result<ScalarCurveSample>::Success({value, mapped.value, leftIndex, mapped.changed});
    }

    /** @copydoc ScalarCurveView::Keys */
    std::span<const ScalarCurveKey> ScalarCurveView::Keys() const noexcept {
        return keys_;
    }

    /** @copydoc ScalarCurveView::Boundaries */
    CurveBoundaryPolicy ScalarCurveView::Boundaries() const noexcept {
        return boundaries_;
    }

    ScalarCurveView::ScalarCurveView(const std::span<const ScalarCurveKey> keys, const CurveBoundaryPolicy boundaries) noexcept
        : keys_(keys), boundaries_(boundaries) {}
}  // namespace Horo::Cinematic
