#include "editor/screens/workspace/panels/inspector/InspectorPanel.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <utility>
#include <variant>

namespace Horo::Editor {
    namespace {
        constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0F;

        /** @brief Maps the typed authored object kind to its localized Inspector label. */
        [[nodiscard]] const char *KindLocalizationKey(const SceneObjectKind kind) noexcept {
            using enum SceneObjectKind;
            switch (kind) {
                case Mesh:
                    return "workspace.inspector.kind.mesh";
                case GameObject:
                    return "workspace.inspector.kind.empty";
                case Camera:
                    return "workspace.inspector.kind.camera";
                case Light:
                    return "workspace.inspector.kind.light";
                case TriggerVolume:
                    return "workspace.inspector.kind.trigger_volume";
                case AudioSource:
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
            return selected == viewModel.objects.end() ? nullptr : std::to_address(selected);
        }

        /** @brief Draws one localized Inspector validation message with consistent spacing. */
        void DrawValidationMessageIfInvalid(const bool valid, const std::string &message, const Theme::Fonts &fonts, const float topSpacing,
                                            const float bottomSpacing = 0.0F) {
            if (valid)
                return;
            ImGui::SetCursorPosX(14.0F);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topSpacing);
            Ui::ErrorText(message.c_str(), fonts);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + bottomSpacing);
        }

        struct BehaviorFieldVisitor {
            Gameplay::BehaviorField &field;
            const std::span<const char *const, 2> enabledEntries;
            const EditorGuiContext &context;

            bool operator()(bool &fieldVal) const {
                if (int selected = fieldVal ? 1 : 0;
                    Ui::DrawComboPropRow(field.name.c_str(), "value", selected, enabledEntries, context.theme.fonts)) {
                    fieldVal = selected != 0;
                    return true;
                }
                return false;
            }

            bool operator()(double &fieldVal) const {
                auto draft = static_cast<float>(fieldVal);
                if (const auto [changed, committed] = Ui::DrawFloatPropRow(field.name.c_str(), "value", draft, context.theme.fonts);
                    committed) {
                    fieldVal = static_cast<double>(draft);
                    return true;
                }
                return false;
            }

            bool operator()(std::int64_t &fieldVal) const {
                auto draft = static_cast<float>(fieldVal);
                const auto [changed, committed] =
                    Ui::DrawFloatPropRow(field.name.c_str(), "value", draft, context.theme.fonts, Ui::FloatPropertyOptions{.speed = 1.0F});
                if (committed) {
                    fieldVal = static_cast<std::int64_t>(draft);
                    return true;
                }
                return false;
            }

            bool operator()(Math::Vec3 &fieldVal) const {
                std::array draft{fieldVal.x, fieldVal.y, fieldVal.z};
                if (const auto edit = Ui::DrawFloat3PropRow(field.name.c_str(), "value", draft, context.theme.fonts); edit.committed) {
                    fieldVal = {draft[0], draft[1], draft[2]};
                    return true;
                }
                return false;
            }

            bool operator()(const std::string &fieldVal) const {
                Ui::DrawPropRow(field.name.c_str(), fieldVal.c_str(), context.theme.fonts);
                return false;
            }

            template <typename T> bool operator()([[maybe_unused]] const T &) const noexcept {
                return false;
            }
        };

        /** @brief Renders a single dynamic behavior field using typed std::visit dispatch. */
        bool DrawBehaviorField(Gameplay::BehaviorField &field, const std::span<const char *const, 2> enabledEntries,
                               const EditorGuiContext &context) {
            ImGui::PushID(field.name.c_str());
            const bool fieldCommitted = std::visit(BehaviorFieldVisitor{field, enabledEntries, context}, field.value);
            ImGui::PopID();
            return fieldCommitted;
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

    void InspectorPanel::DrawPanel([[maybe_unused]] const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                   EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) {
        const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.inspector").c_str()};
        Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        constexpr float footerHeight = 48.0F;
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 28.0F - footerHeight), false, ImGuiWindowFlags_NoSavedSettings);

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
        if (selectedObjects.size() == 1) {
            if (const auto *selected = FindSelectedObject(vm); selected != nullptr)
                DrawAddComponent(*selected, vm, cmd, ctx);
        }
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
                                           context.localization.Get("editor", "workspace.inspector.name_invalid"), context.theme.fonts,
                                           6.0F, 4.0F);
        } else {
            m_nameInputContext.Reset();
            DrawMultiSelectionTitle(m_editSession.Draft().selectedObjectCount, context);
        }

        const InspectorTransformEdit transformEdit = DrawTransformWidgets(context);
        ApplyTransformEdit(transformEdit, command);
        DrawValidationMessageIfInvalid(m_editSession.IsTransformValid(),
                                       context.localization.Get("editor", "workspace.inspector.transform_invalid"), context.theme.fonts,
                                       8.0F);

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.camera.has_value()) {
            const InspectorCameraEdit cameraEdit = DrawCameraWidgets(context);
            ApplyCameraEdit(cameraEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsCameraValid(),
                                           context.localization.Get("editor", "workspace.inspector.camera_invalid"), context.theme.fonts,
                                           8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.light.has_value()) {
            const InspectorLightEdit lightEdit = DrawLightWidgets(context);
            ApplyLightEdit(lightEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsLightValid(),
                                           context.localization.Get("editor", "workspace.inspector.light_invalid"), context.theme.fonts,
                                           8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.triggerVolume.has_value()) {
            const InspectorTriggerVolumeEdit triggerEdit = DrawTriggerVolumeWidgets(context);
            ApplyTriggerVolumeEdit(triggerEdit, *primaryObject, command);
        }

        if (m_editSession.Draft().selectedObjectCount == 1 && primaryObject->components.audioSource.has_value()) {
            const InspectorAudioSourceEdit audioEdit = DrawAudioSourceWidgets(context);
            ApplyAudioSourceEdit(audioEdit, *primaryObject, command);
            DrawValidationMessageIfInvalid(m_editSession.IsAudioSourceValid(),
                                           context.localization.Get("editor", "workspace.inspector.audio_source_invalid"),
                                           context.theme.fonts, 8.0F);
        }

        if (m_editSession.Draft().selectedObjectCount == 1)
            DrawBehaviors(*primaryObject, viewModel, command, context);
    }

    void InspectorPanel::DrawAddComponent(const SceneObject &object, const EditorWorkspaceViewModel &viewModel,
                                          EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) const {
        ImGui::SetCursorPosX(14.0F);
        if (const std::string addComponentLabel =
                context.localization.Get("editor", "workspace.inspector.add_component") + "###InspectorAddComponent";
            Ui::Button({.label = addComponentLabel.c_str(),
                        .variant = Ui::ButtonVariant::Secondary,
                        .font = context.theme.fonts.sans,
                        .componentSize = Ui::ComponentSize::Medium,
                        .style = {.width = Ui::StyleWidth::FillAvailable}})) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (Ui::BeginMenuPopup("AddComponentPopup")) {
            if (!object.components.camera.has_value() &&
                Ui::ContextMenuItem((context.localization.Get("editor", "workspace.inspector.kind.camera") +
                                     "###inspector_component_camera")
                                        .c_str(),
                                    nullptr, context.theme.fonts)) {
                command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                command.objectPayload = object.id;
                command.componentTypePayload = ComponentType::Camera;
            }
            if (!object.components.light.has_value() &&
                Ui::ContextMenuItem((context.localization.Get("editor", "workspace.inspector.kind.light") + "###inspector_component_light")
                                        .c_str(),
                                    nullptr, context.theme.fonts)) {
                command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                command.objectPayload = object.id;
                command.componentTypePayload = ComponentType::Light;
            }
            if (!object.components.triggerVolume.has_value() &&
                Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.trigger_volume").c_str(), nullptr,
                                    context.theme.fonts)) {
                command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                command.objectPayload = object.id;
                command.componentTypePayload = ComponentType::TriggerVolume;
            }
            if (!object.components.audioSource.has_value() &&
                Ui::ContextMenuItem(context.localization.Get("editor", "workspace.inspector.kind.audio_source").c_str(), nullptr,
                                    context.theme.fonts)) {
                command.command = EditorWorkspaceViewCommand::AddComponentToObject;
                command.objectPayload = object.id;
                command.componentTypePayload = ComponentType::AudioSource;
            }
            for (const Gameplay::BehaviorDescriptor &descriptor : viewModel.availableBehaviors) {
                const bool alreadyAttached =
                    std::ranges::any_of(object.components.behaviors, [&descriptor](const Gameplay::BehaviorComponent &behavior) {
                    return behavior.typeId == descriptor.typeId;
                });
                const std::string behaviorLabel = descriptor.displayName + "###inspector_behavior_" + descriptor.typeId.Value();
                if ((!alreadyAttached || descriptor.allowMultiple) &&
                    Ui::ContextMenuItem(behaviorLabel.c_str(), nullptr, context.theme.fonts)) {
                    command.command = EditorWorkspaceViewCommand::AttachBehaviorToObject;
                    command.objectPayload = object.id;
                    command.behaviorTypePayload = descriptor.typeId;
                }
            }
            Ui::EndMenuPopup();
        }
    }

    void InspectorPanel::DrawBehaviors(const SceneObject &object, const EditorWorkspaceViewModel &viewModel,
                                       EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) const {
        const std::array<const char *, 2> enabledEntries{
            context.localization.Get("editor", "workspace.value.off").c_str(),
            context.localization.Get("editor", "workspace.value.on").c_str(),
        };
        for (const Gameplay::BehaviorComponent &attached : object.components.behaviors) {
            const auto descriptor = std::ranges::find(viewModel.availableBehaviors, attached.typeId, &Gameplay::BehaviorDescriptor::typeId);
            const bool missing = descriptor == viewModel.availableBehaviors.end();
            const std::string sectionText =
                missing ? context.localization.Get("editor", "workspace.inspector.behavior_missing") + " — " + attached.typeId.Value()
                        : descriptor->displayName;
            const std::string sectionLabel = sectionText + "###inspector_attached_behavior_" + attached.typeId.Value();
            ImGui::PushID(static_cast<int>(attached.instanceId.value));
            if (Ui::DrawPropSection(sectionLabel.c_str(), context.theme.fonts, true) &&
                command.command == EditorWorkspaceViewCommand::None) {
                command.command = EditorWorkspaceViewCommand::RemoveBehaviorFromObject;
                command.objectPayload = object.id;
                command.behaviorInstancePayload = attached.instanceId;
                ImGui::PopID();
                continue;
            }

            Gameplay::BehaviorComponent edited = attached;
            bool committed = false;
            if (int enabled = edited.enabled ? 1 : 0;
                Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.behavior_enabled").c_str(), "enabled", enabled,
                                     enabledEntries, context.theme.fonts)) {
                edited.enabled = enabled != 0;
                committed = true;
            }
            if (!missing) {
                for (Gameplay::BehaviorField &field : edited.fields) {
                    committed |= DrawBehaviorField(field, enabledEntries, context);
                }
            }
            if (committed && command.command == EditorWorkspaceViewCommand::None) {
                command.command = EditorWorkspaceViewCommand::UpdateBehaviorOnObject;
                command.objectPayload = object.id;
                command.behaviorPayload = std::move(edited);
            }
            ImGui::PopID();
        }
    }

    void InspectorPanel::DrawMultiSelectionTitle(const std::size_t selectedObjectCount, const EditorGuiContext &context) const {
        ImGui::SetCursorPos({14.0F, ImGui::GetCursorPosY() + 14.0F});
        const std::string label =
            std::format("{} {}", selectedObjectCount, context.localization.Get("editor", "workspace.inspector.objects_selected"));
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
        const Ui::TextEditResult edit =
            Ui::DrawEditableObjTitle("object_name", draft.name, MaximumSceneObjectNameBytes,
                                     Ui::EditableObjectTitleBadge{objectKind.c_str(), badgeBackground, Theme::Ok()}, context.theme.fonts,
                                     !nameWasValid);

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

        const bool removeRequested =
            Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.camera").c_str(), context.theme.fonts, true);

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
                                     "camera_field_of_view", draft.cameraFieldOfViewDegrees, context.theme.fonts,
                                     Ui::FloatPropertyOptions{.speed = 0.25F, .error = !fieldOfViewValid, .format = "%.1f°"});
            if (fieldOfView.changed) {
                draft.camera->verticalFieldOfViewRadians = draft.cameraFieldOfViewDegrees * DegreesToRadians;
            }
            committed = committed || fieldOfView.committed;
        } else {
            const bool orthographicHeightValid = std::isfinite(draft.camera->orthographicHeight) && draft.camera->orthographicHeight > 0.0F;
            const Ui::PropertyEditResult orthographicHeight =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_orthographic_height").c_str(),
                                     "camera_orthographic_height", draft.camera->orthographicHeight, context.theme.fonts,
                                     Ui::FloatPropertyOptions{.speed = 0.05F, .error = !orthographicHeightValid});
            committed = committed || orthographicHeight.committed;
        }

        const bool nearPlaneValid = std::isfinite(draft.camera->nearPlane) && draft.camera->nearPlane > 0.0F &&
                                    std::isfinite(draft.camera->farPlane) && draft.camera->farPlane > draft.camera->nearPlane;
        const Ui::PropertyEditResult nearPlane =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_near_plane").c_str(), "camera_near_plane",
                                 draft.camera->nearPlane, context.theme.fonts,
                                 Ui::FloatPropertyOptions{.speed = 0.01F, .error = !nearPlaneValid});
        committed = committed || nearPlane.committed;

        const bool farPlaneValid = std::isfinite(draft.camera->farPlane) && draft.camera->farPlane > draft.camera->nearPlane;
        const Ui::PropertyEditResult farPlane =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.camera_far_plane").c_str(), "camera_far_plane",
                                 draft.camera->farPlane, context.theme.fonts,
                                 Ui::FloatPropertyOptions{.speed = 1.0F, .error = !farPlaneValid});
        committed = committed || farPlane.committed;
        return {.committed = committed, .removeRequested = removeRequested};
    }

    InspectorLightEdit InspectorPanel::DrawLightWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.light.has_value())
            return {};

        const bool removeRequested =
            Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.light").c_str(), context.theme.fonts, true);

        const std::array<const char *, 3> kindEntries{
            context.localization.Get("editor", "workspace.inspector.light_kind_directional").c_str(),
            context.localization.Get("editor", "workspace.inspector.light_kind_point").c_str(),
            context.localization.Get("editor", "workspace.inspector.light_kind_spot").c_str(),
        };
        auto kind = static_cast<int>(draft.light->kind);
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
                                 draft.light->intensity, context.theme.fonts,
                                 Ui::FloatPropertyOptions{.speed = 0.05F, .error = !intensityValid});

        bool changed = kindChanged || colorEdit.changed || intensity.changed;
        bool committed = kindChanged || colorEdit.committed || intensity.committed;
        if (draft.light->kind != Runtime::LightKind::Directional) {
            const bool rangeValid = std::isfinite(draft.light->range) && draft.light->range >= 0.0F;
            const Ui::PropertyEditResult range =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_range").c_str(), "light_range",
                                     draft.light->range, context.theme.fonts,
                                     Ui::FloatPropertyOptions{.speed = 0.1F, .error = !rangeValid});
            changed = changed || range.changed;
            committed = committed || range.committed;
        }

        if (draft.light->kind == Runtime::LightKind::Spot) {
            const bool innerValid = std::isfinite(draft.lightInnerConeDegrees) && draft.lightInnerConeDegrees >= 0.0F &&
                                    std::isfinite(draft.lightOuterConeDegrees) &&
                                    draft.lightOuterConeDegrees >= draft.lightInnerConeDegrees;
            const Ui::PropertyEditResult inner =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_inner_cone").c_str(), "light_inner_cone",
                                     draft.lightInnerConeDegrees, context.theme.fonts,
                                     Ui::FloatPropertyOptions{.speed = 0.25F, .error = !innerValid, .format = "%.1f°"});
            if (inner.changed)
                draft.light->innerConeRadians = draft.lightInnerConeDegrees * DegreesToRadians;

            const bool outerValid =
                std::isfinite(draft.lightOuterConeDegrees) && draft.lightOuterConeDegrees >= draft.lightInnerConeDegrees;
            const Ui::PropertyEditResult outer =
                Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.light_outer_cone").c_str(), "light_outer_cone",
                                     draft.lightOuterConeDegrees, context.theme.fonts,
                                     Ui::FloatPropertyOptions{.speed = 0.25F, .error = !outerValid, .format = "%.1f°"});
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
                                         EditorWorkspaceViewCommandData &command) const {
        using enum EditorWorkspaceViewCommand;
        if (edit.removeRequested && command.command == None) {
            command.command = RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::Camera;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyCameraEdit(edit, object, command.command == None));
    }

    void InspectorPanel::ApplyLightEdit(const InspectorLightEdit &edit, const SceneObject &object,
                                        EditorWorkspaceViewCommandData &command) {
        using enum EditorWorkspaceViewCommand;
        if (edit.removeRequested && command.command == None) {
            command.command = RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::Light;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyLightEdit(edit, object, command.command == None));
    }

    InspectorTriggerVolumeEdit InspectorPanel::DrawTriggerVolumeWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.triggerVolume.has_value())
            return {};

        const bool removeRequested = Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.trigger_volume").c_str(),
                                                         context.theme.fonts, true);

        const std::array<const char *, 4> shapeEntries{
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_box").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_sphere").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_capsule").c_str(),
            context.localization.Get("editor", "workspace.inspector.trigger_volume_shape_plane").c_str(),
        };
        auto shape = static_cast<int>(draft.triggerVolume->shape);
        const bool shapeChanged =
            Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.trigger_volume_shape").c_str(),
                                 "trigger_volume_shape", shape, shapeEntries, context.theme.fonts);
        if (shapeChanged)
            draft.triggerVolume->shape = static_cast<Runtime::ColliderShapeType>(shape);

        return {.committed = shapeChanged, .removeRequested = removeRequested};
    }

    InspectorAudioSourceEdit InspectorPanel::DrawAudioSourceWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.audioSource.has_value())
            return {};

        const bool removeRequested =
            Ui::DrawPropSection(context.localization.Get("editor", "workspace.inspector.audio_source").c_str(), context.theme.fonts, true);

        const std::array<const char *, 2> kindEntries{
            context.localization.Get("editor", "workspace.inspector.audio_source_kind_native_clip").c_str(),
            context.localization.Get("editor", "workspace.inspector.audio_source_kind_middleware_event").c_str(),
        };
        auto kind = static_cast<int>(draft.audioSource->kind);
        const bool kindChanged = Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.audio_source_kind").c_str(),
                                                      "audio_source_kind", kind, kindEntries, context.theme.fonts);
        if (kindChanged)
            draft.audioSource->kind = static_cast<Runtime::AudioSourceKind>(kind);

        const bool gainValid = std::isfinite(draft.audioSource->gain) && draft.audioSource->gain >= 0.0F;
        const Ui::PropertyEditResult gainEdit =
            Ui::DrawFloatPropRow(context.localization.Get("editor", "workspace.inspector.audio_source_gain").c_str(), "audio_source_gain",
                                 draft.audioSource->gain, context.theme.fonts,
                                 Ui::FloatPropertyOptions{.speed = 0.01F, .error = !gainValid});

        bool spatialValue = draft.audioSource->spatial;
        const std::array<const char *, 2> spatialEntries{
            context.localization.Get("editor", "workspace.value.off").c_str(),
            context.localization.Get("editor", "workspace.value.on").c_str(),
        };
        int spatialInt = spatialValue ? 1 : 0;
        const bool spatialChanged =
            Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.audio_source_spatial").c_str(),
                                 "audio_source_spatial", spatialInt, spatialEntries, context.theme.fonts);
        if (spatialChanged)
            draft.audioSource->spatial = spatialInt != 0;

        const bool committed = kindChanged || gainEdit.committed || spatialChanged;
        return {.committed = committed, .removeRequested = removeRequested};
    }

    void InspectorPanel::ApplyTriggerVolumeEdit(const InspectorTriggerVolumeEdit &edit, const SceneObject &object,
                                                EditorWorkspaceViewCommandData &command) const {
        using enum EditorWorkspaceViewCommand;
        if (edit.removeRequested && command.command == None) {
            command.command = RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::TriggerVolume;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyTriggerVolumeEdit(edit, object, command.command == None));
    }

    void InspectorPanel::ApplyAudioSourceEdit(const InspectorAudioSourceEdit &edit, const SceneObject &object,
                                              EditorWorkspaceViewCommandData &command) const {
        using enum EditorWorkspaceViewCommand;
        if (edit.removeRequested && command.command == None) {
            command.command = RemoveComponentFromObject;
            command.objectPayload = object.id;
            command.componentTypePayload = ComponentType::AudioSource;
            return;
        }
        AdoptCommand(command, m_editSession.ApplyAudioSourceEdit(edit, object, command.command == None));
    }

    void InspectorPanel::DrawEmptyState(EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        AdoptCommand(command, m_editSession.Clear());
        m_nameInputContext.Reset();

        ImGui::SetCursorPosX(14.0F);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0F);
        Ui::Hint(context.localization.Get("editor", "workspace.inspector.empty").c_str(), context.theme.fonts);
    }

    void InspectorPanel::AdoptCommand(EditorWorkspaceViewCommandData &destination, EditorWorkspaceViewCommandData source) {
        if (destination.command != EditorWorkspaceViewCommand::None || source.command == EditorWorkspaceViewCommand::None) {
            return;
        }
        destination = std::move(source);
    }
}  // namespace Horo::Editor
