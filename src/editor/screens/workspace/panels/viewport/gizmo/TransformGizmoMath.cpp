#include "TransformGizmoMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace Horo::Editor {
    namespace {
        const ErrorDomainId TransformGizmoDomain{"horo.editor.transform_gizmo"};

        [[nodiscard]] bool IsTransformTool(const EditorTransformTool tool) noexcept {
            return tool == EditorTransformTool::Move || tool == EditorTransformTool::Rotate || tool == EditorTransformTool::Scale;
        }

        [[nodiscard]] bool IsAxisValid(const EditorTransformTool tool, const int axis) noexcept {
            return axis >= 0 && axis <= (tool == EditorTransformTool::Scale ? 3 : 2);
        }

        [[nodiscard]] constexpr Math::Vec3 LocalAxis(const int axis) noexcept {
            return axis == 0 ? Math::Vec3{1.0F, 0.0F, 0.0F} : (axis == 1 ? Math::Vec3{0.0F, 1.0F, 0.0F} : Math::Vec3{0.0F, 0.0F, 1.0F});
        }

        void SetTranslation(Math::Mat4 &matrix, const Math::Vec3 translation) noexcept {
            matrix.values[12] = translation.x;
            matrix.values[13] = translation.y;
            matrix.values[14] = translation.z;
        }

        [[nodiscard]] Math::Mat4 WorldAxisScaleMatrix(const Math::Vec3 axis, const float factor) noexcept {
            Math::Mat4 result = Math::Mat4::Identity();
            const float delta = factor - 1.0F;
            result.values[0] += delta * axis.x * axis.x;
            result.values[4] += delta * axis.x * axis.y;
            result.values[8] += delta * axis.x * axis.z;
            result.values[1] += delta * axis.y * axis.x;
            result.values[5] += delta * axis.y * axis.y;
            result.values[9] += delta * axis.y * axis.z;
            result.values[2] += delta * axis.z * axis.x;
            result.values[6] += delta * axis.z * axis.y;
            result.values[10] += delta * axis.z * axis.z;
            return result;
        }

        void PreserveScaleSigns(Math::Vec3 &value, const Math::Vec3 initial) noexcept {
            const auto preserve = [](const float candidate, const float original) {
                const float sign = original < 0.0F ? -1.0F : 1.0F;
                return sign * std::max(std::fabs(candidate), 0.0001F);
            };
            value = {preserve(value.x, initial.x), preserve(value.y, initial.y), preserve(value.z, initial.z)};
        }

        void MultiplyScaleComponent(Math::Vec3 &scale, const int axis, const float factor) noexcept {
            const auto apply = [factor](float &value) {
                const float sign = value < 0.0F ? -1.0F : 1.0F;
                value = sign * std::max(std::fabs(value) * factor, 0.0001F);
            };
            if (axis == 0 || axis == 3)
                apply(scale.x);
            if (axis == 1 || axis == 3)
                apply(scale.y);
            if (axis == 2 || axis == 3)
                apply(scale.z);
        }

        [[nodiscard]] Result<TransformGizmoMathOutcome> ValidateOutcome(Math::Transform transform, const Math::Vec3 worldPosition) {
            const Result<Math::Mat4> matrix = transform.TryToMatrix();
            if (matrix.HasError())
                return Result<TransformGizmoMathOutcome>::Failure(matrix.ErrorValue());
            if (!Math::IsFinite(worldPosition))
                return Result<TransformGizmoMathOutcome>::Failure(
                    MakeError(TransformGizmoErrors::InvalidUpdate, "Transform gizmo produced a non-finite world position."));
            return Result<TransformGizmoMathOutcome>::Success(TransformGizmoMathOutcome{std::move(transform), worldPosition});
        }
    }  // namespace

    namespace TransformGizmoErrors {
        const ErrorCodeDescriptor InvalidRequest{
            .domain = TransformGizmoDomain,
            .code = ErrorCode{"transform_gizmo.invalid_request"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Transform gizmo request is invalid.",
            .remediationHint = "Use a supported tool, axis, and finite transform inputs.",
            .retryable = false,
            .userActionable = false,
        };
        const ErrorCodeDescriptor InvalidUpdate{
            .domain = TransformGizmoDomain,
            .code = ErrorCode{"transform_gizmo.invalid_update"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Transform gizmo update is invalid.",
            .remediationHint = "Use finite pointer-derived values from the active gizmo session.",
            .retryable = false,
            .userActionable = false,
        };
        const ErrorCodeDescriptor RotationVectorRequired{
            .domain = TransformGizmoDomain,
            .code = ErrorCode{"transform_gizmo.rotation_vector_required"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Transform gizmo rotation vector is missing.",
            .remediationHint = "Project the pointer onto the active rotation plane before evaluating rotation.",
            .retryable = true,
            .userActionable = false,
        };
    }  // namespace TransformGizmoErrors

    /** @copydoc BeginTransformGizmoMath */
    Result<TransformGizmoMathSession> BeginTransformGizmoMath(const BeginTransformGizmoMathRequest &request) {
        if (!IsTransformTool(request.tool) || !IsAxisValid(request.tool, request.axis) || !std::isfinite(request.pixelsPerWorldUnit) ||
            request.pixelsPerWorldUnit <= 0.0F || !Math::IsFinite(request.initialWorldTransform) ||
            !Math::IsFinite(request.parentWorldTransform) || request.initialLocalTransform.TryToMatrix().HasError()) {
            return Result<TransformGizmoMathSession>::Failure(MakeError(TransformGizmoErrors::InvalidRequest));
        }

        Math::Vec3 worldAxis{};
        if (request.axis < 3) {
            const Result<Math::Vec3> normalizedAxis = Math::TryNormalize(request.worldAxis);
            if (normalizedAxis.HasError())
                return Result<TransformGizmoMathSession>::Failure(normalizedAxis.ErrorValue());
            worldAxis = normalizedAxis.Value();
        }

        Math::Vec3 startRotationVector{};
        if (request.tool == EditorTransformTool::Rotate) {
            if (!request.startRotationVector.has_value())
                return Result<TransformGizmoMathSession>::Failure(MakeError(TransformGizmoErrors::RotationVectorRequired));
            const Result<Math::Vec3> normalized = Math::TryNormalize(*request.startRotationVector);
            if (normalized.HasError())
                return Result<TransformGizmoMathSession>::Failure(normalized.ErrorValue());
            startRotationVector = normalized.Value();
        }

        const Result<Math::Mat4> parentInverse = Math::TryInverseAffine(request.parentWorldTransform);
        if (parentInverse.HasError())
            return Result<TransformGizmoMathSession>::Failure(parentInverse.ErrorValue());
        return Result<TransformGizmoMathSession>::Success(TransformGizmoMathSession{
            .tool = request.tool,
            .space = request.space,
            .axis = request.axis,
            .initialLocalTransform = request.initialLocalTransform,
            .initialWorldTransform = request.initialWorldTransform,
            .parentWorldInverse = parentInverse.Value(),
            .initialWorldPosition = Math::TransformPoint(request.initialWorldTransform, {}),
            .worldAxis = worldAxis,
            .startRotationVector = startRotationVector,
            .pixelsPerWorldUnit = request.pixelsPerWorldUnit,
        });
    }

    /** @copydoc EvaluateTransformGizmoMath */
    Result<TransformGizmoMathOutcome> EvaluateTransformGizmoMath(const TransformGizmoMathSession &session,
                                                                 const TransformGizmoMathUpdate &update) {
        if (!std::isfinite(update.projectedPixels))
            return Result<TransformGizmoMathOutcome>::Failure(MakeError(TransformGizmoErrors::InvalidUpdate));

        Math::Transform next = session.initialLocalTransform;
        Math::Vec3 worldPosition = session.initialWorldPosition;
        if (session.tool == EditorTransformTool::Move) {
            const float worldDistance = update.projectedPixels / session.pixelsPerWorldUnit;
            worldPosition = session.initialWorldPosition + session.worldAxis * worldDistance;
            next.translation = Math::TransformAffinePoint(session.parentWorldInverse, worldPosition);
        } else if (session.tool == EditorTransformTool::Rotate) {
            if (!update.currentRotationVector.has_value())
                return Result<TransformGizmoMathOutcome>::Failure(MakeError(TransformGizmoErrors::RotationVectorRequired));
            const Result<Math::Vec3> current = Math::TryNormalize(*update.currentRotationVector);
            if (current.HasError())
                return Result<TransformGizmoMathOutcome>::Failure(current.ErrorValue());
            const float angle = std::atan2(Math::Dot(session.worldAxis, Math::Cross(session.startRotationVector, current.Value())),
                                           Math::Dot(session.startRotationVector, current.Value()));
            if (session.space == EditorTransformSpace::Local) {
                const Result<Math::Quaternion> delta = Math::Quaternion::TryFromAxisAngle(LocalAxis(session.axis), angle);
                if (delta.HasError())
                    return Result<TransformGizmoMathOutcome>::Failure(delta.ErrorValue());
                const Result<Math::Quaternion> rotation = (session.initialLocalTransform.rotation * delta.Value()).TryNormalized();
                if (rotation.HasError())
                    return Result<TransformGizmoMathOutcome>::Failure(rotation.ErrorValue());
                next.rotation = rotation.Value();
            } else {
                const Result<Math::Quaternion> delta = Math::Quaternion::TryFromAxisAngle(session.worldAxis, angle);
                if (delta.HasError())
                    return Result<TransformGizmoMathOutcome>::Failure(delta.ErrorValue());
                Math::Mat4 desiredWorld =
                    Math::Multiply(Math::Transform{.rotation = delta.Value()}.ToMatrix(), session.initialWorldTransform);
                SetTranslation(desiredWorld, session.initialWorldPosition);
                const Result<Math::Transform> local = Math::TryDecomposeAffineTRS(Math::Multiply(session.parentWorldInverse, desiredWorld));
                if (local.HasError())
                    return Result<TransformGizmoMathOutcome>::Failure(local.ErrorValue());
                next = local.Value();
            }
        } else if (session.tool == EditorTransformTool::Scale) {
            const float factor = std::clamp(std::exp(update.projectedPixels / 120.0F), 0.01F, 100.0F);
            if (session.space == EditorTransformSpace::Local) {
                MultiplyScaleComponent(next.scale, session.axis, factor);
            } else {
                const Math::Mat4 scaleMatrix = session.axis == 3 ? Math::Transform{.scale = {factor, factor, factor}}.ToMatrix()
                                                                 : WorldAxisScaleMatrix(session.worldAxis, factor);
                Math::Mat4 desiredWorld = Math::Multiply(scaleMatrix, session.initialWorldTransform);
                SetTranslation(desiredWorld, session.initialWorldPosition);
                const Result<Math::Transform> local = Math::TryDecomposeAffineTRS(Math::Multiply(session.parentWorldInverse, desiredWorld));
                if (local.HasError())
                    return Result<TransformGizmoMathOutcome>::Failure(local.ErrorValue());
                next = local.Value();
                PreserveScaleSigns(next.scale, session.initialLocalTransform.scale);
            }
        } else {
            return Result<TransformGizmoMathOutcome>::Failure(MakeError(TransformGizmoErrors::InvalidRequest));
        }
        return ValidateOutcome(std::move(next), worldPosition);
    }

    /** @copydoc ResolveTransformGizmoWorldAxes */
    Result<std::array<Math::Vec3, 3>> ResolveTransformGizmoWorldAxes(const Math::Mat4 &worldTransform, const EditorTransformSpace space) {
        if (!Math::IsFinite(worldTransform))
            return Result<std::array<Math::Vec3, 3>>::Failure(MakeError(TransformGizmoErrors::InvalidRequest));
        if (space == EditorTransformSpace::World) {
            return Result<std::array<Math::Vec3, 3>>::Success(std::array{LocalAxis(0), LocalAxis(1), LocalAxis(2)});
        }
        const Result<Math::Transform> worldTrs = Math::TryDecomposeAffineTRS(worldTransform);
        if (worldTrs.HasError())
            return Result<std::array<Math::Vec3, 3>>::Failure(worldTrs.ErrorValue());
        std::array<Math::Vec3, 3> axes{};
        for (int axis = 0; axis < 3; ++axis) {
            const Result<Math::Vec3> rotated = worldTrs.Value().rotation.TryRotate(LocalAxis(axis));
            if (rotated.HasError())
                return Result<std::array<Math::Vec3, 3>>::Failure(rotated.ErrorValue());
            axes[static_cast<std::size_t>(axis)] = rotated.Value();
        }
        return Result<std::array<Math::Vec3, 3>>::Success(std::move(axes));
    }
}  // namespace Horo::Editor
