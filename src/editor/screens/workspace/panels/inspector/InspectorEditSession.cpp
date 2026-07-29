#include "editor/screens/workspace/panels/inspector/InspectorEditSession.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Horo::Editor {
    namespace {
        constexpr float RadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
        constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0F;

        [[nodiscard]] std::array<float, 3> ToArray(const Math::Vec3 value) noexcept {
            return {value.x, value.y, value.z};
        }

        [[nodiscard]] Math::Vec3 ToVec3(const std::array<float, 3> &value) noexcept {
            return {value[0], value[1], value[2]};
        }

        [[nodiscard]] bool IsFinite(const std::array<float, 3> &value) noexcept {
            return std::ranges::all_of(value, [](const float component) {
                return std::isfinite(component);
            });
        }

        [[nodiscard]] std::optional<Math::Transform> MakeTransform(const InspectorObjectDraft &draft) noexcept {
            if (!IsFinite(draft.position) || !IsFinite(draft.rotationDegrees) || !IsFinite(draft.scale)) {
                return std::nullopt;
            }

            const Math::Vec3 rotationDegrees = ToVec3(draft.rotationDegrees);
            const Math::Vec3 rotationRadians{
                rotationDegrees.x * DegreesToRadians,
                rotationDegrees.y * DegreesToRadians,
                rotationDegrees.z * DegreesToRadians,
            };
            const Result<Math::Quaternion> rotation = Math::Quaternion::TryFromEulerRadians(rotationRadians);
            if (rotation.HasError())
                return std::nullopt;

            const Math::Transform transform{
                .translation = ToVec3(draft.position),
                .rotation = rotation.Value(),
                .scale = ToVec3(draft.scale),
            };
            if (!Math::IsFinite(transform.translation) || !Math::IsFinite(transform.rotation) || !Math::IsFinite(transform.scale)) {
                return std::nullopt;
            }
            return transform;
        }

        [[nodiscard]] EditorWorkspaceViewCommandData MakeObjectCommand(const EditorWorkspaceViewCommand command,
                                                                       const SceneObjectId object) {
            EditorWorkspaceViewCommandData result;
            result.command = command;
            result.objectPayload = object;
            return result;
        }
    }  // namespace

    /** @copydoc InspectorEditSession::Draft */
    InspectorObjectDraft &InspectorEditSession::Draft() noexcept {
        return m_draft;
    }

    /** @copydoc InspectorEditSession::Draft */
    const InspectorObjectDraft &InspectorEditSession::Draft() const noexcept {
        return m_draft;
    }

    /** @copydoc InspectorEditSession::BeginObject */
    EditorWorkspaceViewCommandData InspectorEditSession::BeginObject(const SceneObject &object, const DocumentRevision revision) {
        EditorWorkspaceViewCommandData command;
        if (m_previewObject.has_value() && (*m_previewObject != object.id || m_draft.revision != revision)) {
            command.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
            m_previewObject.reset();
        }
        SynchronizeDraft(object, revision);
        return command;
    }

    /** @copydoc InspectorEditSession::Clear */
    EditorWorkspaceViewCommandData InspectorEditSession::Clear() {
        EditorWorkspaceViewCommandData command;
        if (m_previewObject.has_value()) {
            command.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
            m_previewObject.reset();
        }
        m_draft = {};
        return command;
    }

    /** @copydoc InspectorEditSession::ApplyNameEdit */
    EditorWorkspaceViewCommandData InspectorEditSession::ApplyNameEdit(const InspectorNameEdit &edit, const SceneObject &object,
                                                                       const bool allowCommands) {
        if (!allowCommands)
            return {};
        if (edit.cancelled) {
            m_draft.name = object.name;
            return {};
        }
        if (!edit.committed || !IsValidSceneObjectName(m_draft.name) || m_draft.name == object.name) {
            return {};
        }

        EditorWorkspaceViewCommandData command = MakeObjectCommand(EditorWorkspaceViewCommand::UpdateObjectName, object.id);
        command.stringPayload = m_draft.name;
        return command;
    }

    /** @copydoc InspectorEditSession::ApplyTransformEdit */
    EditorWorkspaceViewCommandData InspectorEditSession::ApplyTransformEdit(const InspectorTransformEdit &edit, const SceneObject &object,
                                                                            const bool allowCommands) {
        if (!allowCommands)
            return {};

        const std::optional<Math::Transform> transform = MakeTransform(m_draft);
        if (edit.cancelRequested && m_previewObject.has_value()) {
            ResetTransformDraft(object);
            m_previewObject.reset();
            return MakeObjectCommand(EditorWorkspaceViewCommand::CancelObjectTransformPreview, object.id);
        }
        if (edit.committed) {
            EditorWorkspaceViewCommandData command;
            if (transform.has_value()) {
                command = MakeObjectCommand(EditorWorkspaceViewCommand::CommitObjectTransform, object.id);
                command.transformPayload = *transform;
            } else if (m_previewObject.has_value()) {
                command = MakeObjectCommand(EditorWorkspaceViewCommand::CancelObjectTransformPreview, object.id);
            }
            m_previewObject.reset();
            return command;
        }
        if (!edit.changed || !transform.has_value())
            return {};

        m_previewObject = object.id;
        EditorWorkspaceViewCommandData command = MakeObjectCommand(EditorWorkspaceViewCommand::PreviewObjectTransform, object.id);
        command.transformPayload = *transform;
        return command;
    }

    /** @copydoc InspectorEditSession::ApplyCameraEdit */
    EditorWorkspaceViewCommandData InspectorEditSession::ApplyCameraEdit(const InspectorCameraEdit &edit, const SceneObject &object,
                                                                         const bool allowCommands) const {
        if (!allowCommands || !edit.committed || !object.components.camera.has_value() || !m_draft.camera.has_value() ||
            !IsValidCameraComponent(*m_draft.camera) || *m_draft.camera == *object.components.camera) {
            return {};
        }

        EditorWorkspaceViewCommandData command = MakeObjectCommand(EditorWorkspaceViewCommand::UpdateCameraComponent, object.id);
        command.cameraPayload = *m_draft.camera;
        return command;
    }

    /** @copydoc InspectorEditSession::IsTransformValid */
    bool InspectorEditSession::IsTransformValid() const noexcept {
        return MakeTransform(m_draft).has_value();
    }

    /** @copydoc InspectorEditSession::HasTransformPreview */
    bool InspectorEditSession::HasTransformPreview() const noexcept {
        return m_previewObject.has_value();
    }

    /** @copydoc InspectorEditSession::IsCameraValid */
    bool InspectorEditSession::IsCameraValid() const noexcept {
        return m_draft.camera.has_value() && IsValidCameraComponent(*m_draft.camera);
    }

    void InspectorEditSession::SynchronizeDraft(const SceneObject &object, const DocumentRevision revision) {
        if (m_draft.object == object.id && m_draft.revision == revision)
            return;

        m_draft.object = object.id;
        m_draft.revision = revision;
        m_draft.name = object.name;
        ResetTransformDraft(object);
        m_draft.camera = object.components.camera;
        if (m_draft.camera.has_value()) {
            m_draft.cameraFieldOfViewDegrees = m_draft.camera->verticalFieldOfViewRadians * RadiansToDegrees;
        }
    }

    void InspectorEditSession::ResetTransformDraft(const SceneObject &object) {
        m_draft.position = ToArray(object.localTransform.translation);
        const Math::Vec3 eulerRadians = object.localTransform.rotation.ToEulerRadians();
        m_draft.rotationDegrees = {
            eulerRadians.x * RadiansToDegrees,
            eulerRadians.y * RadiansToDegrees,
            eulerRadians.z * RadiansToDegrees,
        };
        m_draft.scale = ToArray(object.localTransform.scale);
    }
}  // namespace Horo::Editor
