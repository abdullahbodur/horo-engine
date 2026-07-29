#include "editor/screens/workspace/panels/inspector/InspectorPanel.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0F;

        /** @brief Maps the typed authored object kind to its localized Inspector label. */
        [[nodiscard]] const char *KindLocalizationKey(const SceneObjectKind kind) noexcept {
            switch (kind) {
                case SceneObjectKind::Mesh:
                    return "workspace.inspector.kind.mesh";
                case SceneObjectKind::Empty:
                    return "workspace.inspector.kind.empty";
                case SceneObjectKind::Camera:
                    return "workspace.inspector.kind.camera";
                case SceneObjectKind::Light:
                    return "workspace.inspector.kind.light";
                case SceneObjectKind::TriggerVolume:
                    return "workspace.inspector.kind.trigger_volume";
                case SceneObjectKind::AudioSource:
                    return "workspace.inspector.kind.audio_source";
            }
            return "workspace.inspector.kind.empty";
        }

        /** @brief Reports whether the Inspector's degree-based perspective draft is valid. */
        [[nodiscard]] bool IsValidFieldOfViewDegrees(const float value) noexcept {
            return std::isfinite(value) && value > 0.0F && value < 180.0F;
        }

        /** @brief Resolves the selected object without exposing iterator details to the panel flow. */
        [[nodiscard]] const SceneObject *FindSelectedObject(const EditorWorkspaceViewModel &viewModel) noexcept {
            if (!viewModel.primarySelection.has_value())
                return nullptr;
            const auto selected = std::ranges::find(viewModel.objects, *viewModel.primarySelection, &SceneObject::id);
            return selected == viewModel.objects.end() ? nullptr : &*selected;
        }

        /** @brief Draws one localized Inspector validation message with consistent spacing. */
        void DrawValidationMessageIfInvalid(const bool valid, const std::string &message, const float topSpacing,
                                            const float bottomSpacing = 0.0F) {
            if (valid)
                return;
            ImGui::SetCursorPosX(14.0F);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topSpacing);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
            ImGui::TextWrapped("%s", message.c_str());
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + bottomSpacing);
        }
    }  // namespace

    void InspectorPanel::OnAttach(PanelContext &ctx) {
        m_inputRouter = ctx.inputRouter;
    }

    void InspectorPanel::OnDetach() {
        m_nameInputContext.Reset();
        m_inputRouter = nullptr;
    }

    void InspectorPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color) {
        const float ox = pos.x + (size.x - 14.0f) * 0.5f;
        const float oy = pos.y + (size.y - 14.0f) * 0.5f;

        // Simple inspector icon (list with details)
        dl->AddRect(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 12), color, 0.0f, 0, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 5), ImVec2(ox + 10, oy + 5), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 8), ImVec2(ox + 10, oy + 8), color, 1.5f);
    }

    void InspectorPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                   EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) {
        static_cast<void>(pos);
        const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.inspector").c_str()};
        Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0f), false, ImGuiWindowFlags_NoSavedSettings);

        if (const SceneObject *selectedObject = FindSelectedObject(vm))
            DrawSelectedObject(*selectedObject, vm.documentRevision, cmd, ctx);
        else
            DrawEmptyState(cmd, ctx);

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void InspectorPanel::DrawSelectedObject(const SceneObject &object, const DocumentRevision revision,
                                            EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        AdoptCommand(command, m_editSession.BeginObject(object, revision));

        const InspectorNameEdit nameEdit = DrawObjectTitleWidgets(object, context);
        ApplyNameEdit(nameEdit, object, command);
        DrawValidationMessageIfInvalid(IsValidSceneObjectName(m_editSession.Draft().name),
                                       context.localization.Get("editor", "workspace.inspector.name_invalid"), 6.0F, 4.0F);

        const InspectorTransformEdit transformEdit = DrawTransformWidgets(context);
        ApplyTransformEdit(transformEdit, object, command);
        DrawValidationMessageIfInvalid(m_editSession.IsTransformValid(),
                                       context.localization.Get("editor", "workspace.inspector.transform_invalid"), 8.0F);

        if (object.components.camera.has_value()) {
            const InspectorCameraEdit cameraEdit = DrawCameraWidgets(context);
            ApplyCameraEdit(cameraEdit, object, command);
            DrawValidationMessageIfInvalid(m_editSession.IsCameraValid(),
                                           context.localization.Get("editor", "workspace.inspector.camera_invalid"), 8.0F);
        }
    }

    InspectorNameEdit InspectorPanel::DrawObjectTitleWidgets(const SceneObject &object, const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        constexpr auto badgeBackground = ImVec4(95.0F / 255.0F, 184.0F / 255.0F, 138.0F / 255.0F, 0.15F);
        const std::string &objectKind = context.localization.Get("editor", KindLocalizationKey(object.kind));
        const bool nameWasValid = IsValidSceneObjectName(draft.name);
        const Ui::TextEditResult edit = Ui::DrawEditableObjTitle("object_name", draft.name, MaximumSceneObjectNameBytes, objectKind.c_str(),
                                                                 badgeBackground, Theme::Ok(), context.theme.fonts, !nameWasValid);

        if (edit.active && !m_nameInputContext.IsActive() && m_inputRouter != nullptr) {
            m_nameInputContext = m_inputRouter->PushContext(Input::InputContextId{"editor.inspector.object_name"},
                                                            Input::InputContextKind::FocusedGuiWidget);
        } else if (!edit.active) {
            m_nameInputContext.Reset();
        }
        return {
            .cancelled = edit.cancelled,
            .committed = edit.committed,
        };
    }

    InspectorTransformEdit InspectorPanel::DrawTransformWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.transform").c_str(), context.theme.fonts);
        const Ui::Float3PropertyEditResult position =
            Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.position").c_str(), "position", draft.position,
                                  context.theme.fonts);
        const Ui::Float3PropertyEditResult rotation =
            Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.rotation").c_str(), "rotation",
                                  draft.rotationDegrees, context.theme.fonts, 0.25F);
        const Ui::Float3PropertyEditResult scale =
            Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.scale").c_str(), "scale", draft.scale,
                                  context.theme.fonts);

        return {
            .changed = position.changed || rotation.changed || scale.changed,
            .committed = position.committed || rotation.committed || scale.committed,
            .cancelRequested = m_editSession.HasTransformPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false),
        };
    }

    InspectorCameraEdit InspectorPanel::DrawCameraWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.camera.has_value())
            return {};

        Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.camera").c_str(), context.theme.fonts);

        const std::array<const char *, 2> projectionEntries{
            context.localization.Get("editor", "workspace.inspector.camera_projection_perspective").c_str(),
            context.localization.Get("editor", "workspace.inspector.camera_projection_orthographic").c_str(),
        };
        int projection = draft.camera->projection == Runtime::CameraProjection::Perspective ? 0 : 1;
        const bool projectionChanged =
            Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.camera_projection").c_str(), "camera_projection",
                                 projection, projectionEntries, context.theme.fonts);
        if (projectionChanged) {
            draft.camera->projection = projection == 0 ? Runtime::CameraProjection::Perspective : Runtime::CameraProjection::Orthographic;
        }

        bool committed = projectionChanged;
        if (draft.camera->projection == Runtime::CameraProjection::Perspective) {
            const bool fieldOfViewValid = IsValidFieldOfViewDegrees(draft.cameraFieldOfViewDegrees);
            const Ui::PropertyEditResult fieldOfView =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_field_of_view").c_str(),
                                     "camera_field_of_view", draft.cameraFieldOfViewDegrees, context.theme.fonts, 0.25F, 0.0F, 0.0F,
                                     !fieldOfViewValid, "%.1f°");
            if (fieldOfView.changed) {
                draft.camera->verticalFieldOfViewRadians = draft.cameraFieldOfViewDegrees * DegreesToRadians;
            }
            committed = committed || fieldOfView.committed;
        } else {
            const bool orthographicHeightValid = std::isfinite(draft.camera->orthographicHeight) && draft.camera->orthographicHeight > 0.0F;
            const Ui::PropertyEditResult orthographicHeight =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_orthographic_height").c_str(),
                                     "camera_orthographic_height", draft.camera->orthographicHeight, context.theme.fonts, 0.05F, 0.0F, 0.0F,
                                     !orthographicHeightValid);
            committed = committed || orthographicHeight.committed;
        }

        const bool nearPlaneValid = std::isfinite(draft.camera->nearPlane) && draft.camera->nearPlane > 0.0F &&
                                    std::isfinite(draft.camera->farPlane) && draft.camera->farPlane > draft.camera->nearPlane;
        const Ui::PropertyEditResult nearPlane =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_near_plane").c_str(), "camera_near_plane",
                                 draft.camera->nearPlane, context.theme.fonts, 0.01F, 0.0F, 0.0F, !nearPlaneValid);
        committed = committed || nearPlane.committed;

        const bool farPlaneValid = std::isfinite(draft.camera->farPlane) && draft.camera->farPlane > draft.camera->nearPlane;
        const Ui::PropertyEditResult farPlane =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_far_plane").c_str(), "camera_far_plane",
                                 draft.camera->farPlane, context.theme.fonts, 1.0F, 0.0F, 0.0F, !farPlaneValid);
        committed = committed || farPlane.committed;
        return {.committed = committed};
    }

    void InspectorPanel::ApplyNameEdit(const InspectorNameEdit &edit, const SceneObject &object, EditorWorkspaceViewCommandData &command) {
        AdoptCommand(command, m_editSession.ApplyNameEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyTransformEdit(const InspectorTransformEdit &edit, const SceneObject &object,
                                            EditorWorkspaceViewCommandData &command) {
        AdoptCommand(command, m_editSession.ApplyTransformEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyCameraEdit(const InspectorCameraEdit &edit, const SceneObject &object,
                                         EditorWorkspaceViewCommandData &command) {
        AdoptCommand(command, m_editSession.ApplyCameraEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::DrawEmptyState(EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        AdoptCommand(command, m_editSession.Clear());
        m_nameInputContext.Reset();

        ImGui::SetCursorPosX(14.0F);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextWrapped("%s", context.localization.Get("editor", "workspace.inspector.empty").c_str());
        ImGui::PopStyleColor();
    }

    void InspectorPanel::AdoptCommand(EditorWorkspaceViewCommandData &destination, EditorWorkspaceViewCommandData source) {
        if (destination.command != EditorWorkspaceViewCommand::None || source.command == EditorWorkspaceViewCommand::None) {
            return;
        }
        destination = std::move(source);
    }
}  // namespace Horo::Editor
