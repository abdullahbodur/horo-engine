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
                case SceneObjectKind::GameObject:
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

        std::array<SceneObjectId, 1> fallbackSelection{};
        std::span<const SceneObjectId> selectedObjects = vm.selectedObjects;
        if (selectedObjects.empty() && vm.primarySelection.has_value()) {
            fallbackSelection.front() = *vm.primarySelection;
            selectedObjects = fallbackSelection;
        }
        if (!selectedObjects.empty() && FindSelectedObject(vm) != nullptr)
            DrawSelection(vm, selectedObjects, cmd, ctx);
        else
            DrawEmptyState(cmd, ctx);

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void InspectorPanel::DrawSelection(const EditorWorkspaceViewModel &viewModel, const std::span<const SceneObjectId> selectedObjects,
                                       EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        AdoptCommand(command, m_editSession.BeginSelection(viewModel.objects, selectedObjects, viewModel.primarySelection,
                                                           viewModel.documentRevision));
        const SceneObject *primaryObject = FindSelectedObject(viewModel);
        if (primaryObject == nullptr)
            return;

        if (m_editSession.Draft().selectedObjectCount == 1) {
            const InspectorNameEdit nameEdit = DrawObjectTitleWidgets(*primaryObject, context);
            ApplyNameEdit(nameEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(IsValidSceneObjectName(m_editSession.Draft().name),
                                           context.localization.Get("editor", "workspace.inspector.name_invalid"), 6.0F, 4.0F);
        } else {
            m_nameInputContext.Reset();
            DrawMultiSelectionTitle(m_editSession.Draft().selectedObjectCount, context);
        }

        const InspectorTransformEdit transformEdit = DrawTransformWidgets(context);
        ApplyTransformEdit(transformEdit, command);
        DrawValidationMessageIfInvalid(m_editSession.IsTransformValid(),
                                       context.localization.Get("editor", "workspace.inspector.transform_invalid"), 8.0F);

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.camera.has_value()) {
            const InspectorCameraEdit cameraEdit = DrawCameraWidgets(context);
            ApplyCameraEdit(cameraEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsCameraValid(),
                                           context.localization.Get("editor", "workspace.inspector.camera_invalid"), 8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.light.has_value()) {
            const InspectorLightEdit lightEdit = DrawLightWidgets(context);
            ApplyLightEdit(lightEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsLightValid(),
                                           context.localization.Get("editor", "workspace.inspector.light_invalid"), 8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.triggerVolume.has_value()) {
            const InspectorTriggerVolumeEdit triggerEdit = DrawTriggerVolumeWidgets(context);
            ApplyTriggerVolumeEdit(triggerEdit, *primaryObject, command);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.audioSource.has_value()) {
            const InspectorAudioSourceEdit audioEdit = DrawAudioSourceWidgets(context);
            ApplyAudioSourceEdit(audioEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsAudioSourceValid(),
                                           context.localization.Get("editor", "workspace.inspector.audio_source_invalid"), 8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::SetCursorPosX(14.0F);
            if (ImGui::Button(context.localization.Get("editor", "workspace.inspector.add_component").c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x - 14.0F, 0.0F))) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (Ui::BeginMenuPopup("AddComponentPopup")) {
                if (!primaryObject->components.camera.has_value() &&
                    Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.camera").c_str(), nullptr, context.theme.fonts)) {
                    command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                    command.objectPayload = primaryObject->id;
                    command.componentTypePayload = ComponentType::Camera;
                }
                if (!primaryObject->components.light.has_value() &&
                    Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.light").c_str(), nullptr, context.theme.fonts)) {
                    command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                    command.objectPayload = primaryObject->id;
                    command.componentTypePayload = ComponentType::Light;
                }
                if (!primaryObject->components.triggerVolume.has_value() &&
                    Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.trigger_volume").c_str(), nullptr, context.theme.fonts)) {
                    command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                    command.objectPayload = primaryObject->id;
                    command.componentTypePayload = ComponentType::TriggerVolume;
                }
                if (!primaryObject->components.audioSource.has_value() &&
                    Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.audio_source").c_str(), nullptr, context.theme.fonts)) {
                    command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                    command.objectPayload = primaryObject->id;
                    command.componentTypePayload = ComponentType::AudioSource;
                }
                Ui::EndMenuPopup();
            }
        }
    }

    void InspectorPanel::DrawMultiSelectionTitle(const std::size_t selectedObjectCount, const EditorGuiContext &context) {
        ImGui::SetCursorPos({14.0F, ImGui::GetCursorPosY() + 14.0F});
        const std::string label =
            std::to_string(selectedObjectCount) + " " + context.localization.Get("editor", "workspace.inspector.objects_selected");
        {
            Theme::ScopedTextStyle textStyle(context.theme.fonts.sansEmphasis, 16.0F, Theme::FontPx::SansEmphasis);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0F);
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
                                  context.theme.fonts, 0.05F, draft.mixed.position);
        const Ui::Float3PropertyEditResult rotation =
            Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.rotation").c_str(), "rotation",
                                  draft.rotationDegrees, context.theme.fonts, 0.25F, draft.mixed.rotation);
        const Ui::Float3PropertyEditResult scale =
            Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.scale").c_str(), "scale", draft.scale,
                                  context.theme.fonts, 0.05F, draft.mixed.scale);

        return {
            .changed = position.changed || rotation.changed || scale.changed,
            .committed = position.committed || rotation.committed || scale.committed,
            .cancelRequested = m_editSession.HasTransformPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false),
            .changedAxes =
                {
                    .position = position.changedAxes,
                    .rotation = rotation.changedAxes,
                    .scale = scale.changedAxes,
                },
        };
    }

    InspectorCameraEdit InspectorPanel::DrawCameraWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.camera.has_value())
            return {};

        const bool removeRequested = Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.camera").c_str(), context.theme.fonts, true);

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
        return {.committed = committed, .removeRequested = removeRequested};
    }

    InspectorLightEdit InspectorPanel::DrawLightWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.light.has_value())
            return {};

        const bool removeRequested = Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.light").c_str(), context.theme.fonts, true);

        const std::array<const char *, 3> kindEntries{
            context.localization.Get("editor", "workspace.inspector.light_kind_directional").c_str(),
            context.localization.Get("editor", "workspace.inspector.light_kind_point").c_str(),
            context.localization.Get("editor", "workspace.inspector.light_kind_spot").c_str(),
        };
        int kind = static_cast<int>(draft.light->kind);
        const bool kindChanged = Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.light_kind").c_str(),
                                                      "light_kind", kind, kindEntries, context.theme.fonts);
        if (kindChanged)
            draft.light->kind = static_cast<Runtime::LightKind>(kind);

        std::array color{draft.light->color.x, draft.light->color.y, draft.light->color.z};
        const bool colorValid = std::ranges::all_of(color, [](const float component) {
            return std::isfinite(component);
        });
        const Ui::PropertyEditResult colorEdit =
            Ui::DrawColor3PropRow(context.localization.Get("editor", "workspace.inspector.light_color").c_str(), "light_color", color,
                                  context.theme.fonts, !colorValid);
        if (colorEdit.changed)
            draft.light->color = {color[0], color[1], color[2]};

        const bool intensityValid = std::isfinite(draft.light->intensity) && draft.light->intensity >= 0.0F;
        const Ui::PropertyEditResult intensity =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_intensity").c_str(), "light_intensity",
                                 draft.light->intensity, context.theme.fonts, 0.05F, 0.0F, 0.0F, !intensityValid);

        bool changed = kindChanged || colorEdit.changed || intensity.changed;
        bool committed = kindChanged || colorEdit.committed || intensity.committed;
        if (draft.light->kind != Runtime::LightKind::Directional) {
            const bool rangeValid = std::isfinite(draft.light->range) && draft.light->range >= 0.0F;
            const Ui::PropertyEditResult range =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_range").c_str(), "light_range",
                                     draft.light->range, context.theme.fonts, 0.1F, 0.0F, 0.0F, !rangeValid);
            changed = changed || range.changed;
            committed = committed || range.committed;
        }

        if (draft.light->kind == Runtime::LightKind::Spot) {
            const bool innerValid = std::isfinite(draft.lightInnerConeDegrees) && draft.lightInnerConeDegrees >= 0.0F &&
                                    std::isfinite(draft.lightOuterConeDegrees) &&
                                    draft.lightOuterConeDegrees >= draft.lightInnerConeDegrees;
            const Ui::PropertyEditResult inner =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_inner_cone").c_str(), "light_inner_cone",
                                     draft.lightInnerConeDegrees, context.theme.fonts, 0.25F, 0.0F, 0.0F, !innerValid, "%.1f°");
            if (inner.changed)
                draft.light->innerConeRadians = draft.lightInnerConeDegrees * DegreesToRadians;

            const bool outerValid =
                std::isfinite(draft.lightOuterConeDegrees) && draft.lightOuterConeDegrees >= draft.lightInnerConeDegrees;
            const Ui::PropertyEditResult outer =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_outer_cone").c_str(), "light_outer_cone",
                                     draft.lightOuterConeDegrees, context.theme.fonts, 0.25F, 0.0F, 0.0F, !outerValid, "%.1f°");
            if (outer.changed)
                draft.light->outerConeRadians = draft.lightOuterConeDegrees * DegreesToRadians;
            changed = changed || inner.changed || outer.changed;
            committed = committed || inner.committed || outer.committed;
        }
        return {
            .changed = changed,
            .committed = committed,
            .cancelRequested = m_editSession.HasLightPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false),
            .removeRequested = removeRequested,
        };
    }

    void InspectorPanel::ApplyNameEdit(const InspectorNameEdit &edit, const SceneObject &object, EditorWorkspaceViewCommandData &command) {
        AdoptCommand(command, m_editSession.ApplyNameEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyTransformEdit(const InspectorTransformEdit &edit, EditorWorkspaceViewCommandData &command) {
        AdoptCommand(command, m_editSession.ApplyTransformEdit(edit, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyCameraEdit(const InspectorCameraEdit &edit, const SceneObject &object,
                                         EditorWorkspaceViewCommandData &command) {
        if (edit.removeRequested && command.command == EditorWorkspaceViewCommand::None) {
            command.command = EditorWorkspaceViewCommand::RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::Camera;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyCameraEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyLightEdit(const InspectorLightEdit &edit, const SceneObject &object,
                                        EditorWorkspaceViewCommandData &command) {
        if (edit.removeRequested && command.command == EditorWorkspaceViewCommand::None) {
            command.command = EditorWorkspaceViewCommand::RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::Light;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyLightEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    InspectorTriggerVolumeEdit InspectorPanel::DrawTriggerVolumeWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.triggerVolume.has_value())
            return {};

        const bool removeRequested = Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.trigger_volume").c_str(), context.theme.fonts, true);

        const std::array<const char *, 4> shapeEntries{
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_box").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_sphere").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_capsule").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_plane").c_str(),
        };
        int shape = static_cast<int>(draft.triggerVolume->shape);
        const bool shapeChanged = Ui::DrawComboPropRow(
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape").c_str(),
            "trigger_volume_shape", shape, shapeEntries, context.theme.fonts);
        if (shapeChanged)
            draft.triggerVolume->shape = static_cast<Runtime::ColliderShapeType>(shape);

        return {.committed = shapeChanged, .removeRequested = removeRequested};
    }

    InspectorAudioSourceEdit InspectorPanel::DrawAudioSourceWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.audioSource.has_value())
            return {};

        const bool removeRequested = Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.audio_source").c_str(), context.theme.fonts, true);

        const std::array<const char *, 2> kindEntries{
            context.localization.Get("editor", "workspace.inspector.audio_source_kind_native_clip").c_str(),
            context.localization.Get("editor", "workspace.inspector.audio_source_kind_middleware_event").c_str(),
        };
        int kind = static_cast<int>(draft.audioSource->kind);
        const bool kindChanged = Ui::DrawComboPropRow(
            context.localization.Get("editor", "workspace.inspector.audio_source_kind").c_str(),
            "audio_source_kind", kind, kindEntries, context.theme.fonts);
        if (kindChanged)
            draft.audioSource->kind = static_cast<Runtime::AudioSourceKind>(kind);

        const bool gainValid = std::isfinite(draft.audioSource->gain) && draft.audioSource->gain >= 0.0F;
        const Ui::PropertyEditResult gainEdit =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.audio_source_gain").c_str(),
                                 "audio_source_gain", draft.audioSource->gain, context.theme.fonts, 0.01F, 0.0F, 0.0F, !gainValid);

        bool spatialValue = draft.audioSource->spatial;
        const std::array<const char *, 2> spatialEntries{
            context.localization.Get("editor", "workspace.value.off").c_str(),
            context.localization.Get("editor", "workspace.value.on").c_str(),
        };
        int spatialInt = spatialValue ? 1 : 0;
        const bool spatialChanged = Ui::DrawComboPropRow(
            context.localization.Get("editor", "workspace.inspector.audio_source_spatial").c_str(),
            "audio_source_spatial", spatialInt, spatialEntries, context.theme.fonts);
        if (spatialChanged)
            draft.audioSource->spatial = spatialInt != 0;

        const bool committed = kindChanged || gainEdit.committed || spatialChanged;
        return {.committed = committed, .removeRequested = removeRequested};
    }

    void InspectorPanel::ApplyTriggerVolumeEdit(const InspectorTriggerVolumeEdit &edit, const SceneObject &object,
                                                EditorWorkspaceViewCommandData &command) {
        if (edit.removeRequested && command.command == EditorWorkspaceViewCommand::None) {
            command.command = EditorWorkspaceViewCommand::RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::TriggerVolume;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyTriggerVolumeEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
    }

    void InspectorPanel::ApplyAudioSourceEdit(const InspectorAudioSourceEdit &edit, const SceneObject &object,
                                              EditorWorkspaceViewCommandData &command) {
        if (edit.removeRequested && command.command == EditorWorkspaceViewCommand::None) {
            command.command = EditorWorkspaceViewCommand::RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::AudioSource;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyAudioSourceEdit(edit, object, command.command == EditorWorkspaceViewCommand::None));
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
