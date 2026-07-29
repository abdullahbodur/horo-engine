#pragma once

#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <array>
#include <optional>
#include <string>

namespace Horo::Editor {
    /** @brief UI-independent draft values owned by one Inspector edit session. */
    struct InspectorObjectDraft {
        std::optional<SceneObjectId> object;
        DocumentRevision revision;
        std::string name;
        std::array<float, 3> position{};
        std::array<float, 3> rotationDegrees{};
        std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
        std::optional<Runtime::CameraComponent> camera;
        float cameraFieldOfViewDegrees{60.0F};
    };

    /** @brief Semantic name-widget events consumed without an ImGui dependency. */
    struct InspectorNameEdit {
        bool cancelled{false};
        bool committed{false};
    };

    /** @brief Semantic transform-widget events consumed without an ImGui dependency. */
    struct InspectorTransformEdit {
        bool changed{false};
        bool committed{false};
        bool cancelRequested{false};
    };

    /** @brief Semantic Camera-widget events consumed without an ImGui dependency. */
    struct InspectorCameraEdit {
        bool committed{false};
    };

    /**
     * @brief Owns Inspector draft and preview lifecycle independently from rendering.
     *
     * Widget code may update the exposed draft values. Reducer methods translate
     * semantic widget events into at most one typed workspace command.
     */
    class InspectorEditSession {
    public:
        /** @brief Returns the mutable presentation draft used by Inspector widgets. */
        [[nodiscard]] InspectorObjectDraft &Draft() noexcept;

        /** @brief Returns the current presentation draft. */
        [[nodiscard]] const InspectorObjectDraft &Draft() const noexcept;

        /**
         * @brief Reconciles selection/revision changes and refreshes the draft.
         * @return A preview-cancellation command when the previous edit session owned one.
         */
        [[nodiscard]] EditorWorkspaceViewCommandData BeginObject(const SceneObject &object, DocumentRevision revision);

        /**
         * @brief Clears the current edit session.
         * @return A preview-cancellation command when a transient preview was active.
         */
        [[nodiscard]] EditorWorkspaceViewCommandData Clear();

        /** @brief Reduces one name edit into a typed workspace command. */
        [[nodiscard]] EditorWorkspaceViewCommandData ApplyNameEdit(const InspectorNameEdit &edit, const SceneObject &object,
                                                                   bool allowCommands);

        /** @brief Reduces one transform edit into preview, commit, or cancel. */
        [[nodiscard]] EditorWorkspaceViewCommandData ApplyTransformEdit(const InspectorTransformEdit &edit, const SceneObject &object,
                                                                        bool allowCommands);

        /** @brief Reduces one Camera edit into a typed component command. */
        [[nodiscard]] EditorWorkspaceViewCommandData ApplyCameraEdit(const InspectorCameraEdit &edit, const SceneObject &object,
                                                                     bool allowCommands) const;

        /** @brief Reports whether the current transform draft is valid. */
        [[nodiscard]] bool IsTransformValid() const noexcept;

        /** @brief Reports whether the session owns a transient transform preview. */
        [[nodiscard]] bool HasTransformPreview() const noexcept;

        /** @brief Reports whether the current Camera draft is valid. */
        [[nodiscard]] bool IsCameraValid() const noexcept;

    private:
        void SynchronizeDraft(const SceneObject &object, DocumentRevision revision);
        void ResetTransformDraft(const SceneObject &object);

        InspectorObjectDraft m_draft;
        std::optional<SceneObjectId> m_previewObject;
    };
}  // namespace Horo::Editor
