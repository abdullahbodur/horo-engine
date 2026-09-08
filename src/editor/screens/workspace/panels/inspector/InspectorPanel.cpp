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

        /** @brief Maps an authored object kind to its typed Inspector header icon. */
        [[nodiscard]] Ui::UiIcon KindIcon(const SceneObjectKind kind) noexcept {
            using enum SceneObjectKind;
            switch (kind) {
                case Mesh:
                    return Ui::UiIcon::HierarchyMesh;
                case Camera:
                    return Ui::UiIcon::Camera;
                case Light:
                    return Ui::UiIcon::Light;
                case TriggerVolume:
                    return Ui::UiIcon::TriggerVolume;
                case AudioSource:
                    return Ui::UiIcon::AudioSource;
                case GameObject:
                    return Ui::UiIcon::HierarchyGeneric;
            }
            return Ui::UiIcon::HierarchyGeneric;
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

        /** @brief Inspector-owned meaning attached to the generic card-title action surface. */
        struct ComponentTitleBarResult {
            bool resetRequested{false};
            bool toggleEnabledRequested{false};
            bool removeRequested{false};
        };

        /** @brief Component capabilities projected onto generic card actions for one frame. */
        struct ComponentTitleBarOptions {
            bool enabled{true};
            bool canReset{true};
            bool canToggleEnabled{true};
            bool canRemove{true};
        };

        /** @brief Binds Inspector semantics and localized copy to a generic card title bar. */
        [[nodiscard]] ComponentTitleBarResult DrawComponentTitleBar(Ui::Card &card, const char *title,
                                                                    const ComponentTitleBarOptions options,
                                                                    const EditorGuiContext &context) {
            ComponentTitleBarResult result;
            const std::string &resetLabel = context.localization.Get("editor", "workspace.inspector.component.reset");
            const std::string &enableLabel = context.localization.Get("editor", "workspace.inspector.component.enable");
            const std::string &disableLabel = context.localization.Get("editor", "workspace.inspector.component.disable");
            const std::string &settingsLabel = context.localization.Get("editor", "workspace.inspector.component.settings");
            const std::string &removeLabel = context.localization.Get("editor", "workspace.inspector.remove_component");
            const auto requestReset = [&result] {
                result.resetRequested = true;
            };
            const auto requestToggleEnabled = [&result] {
                result.toggleEnabledRequested = true;
            };
            const auto requestRemove = [&result] {
                result.removeRequested = true;
            };

            std::array<Ui::CardMenuAction, 3> menuItems;
            std::size_t menuItemCount = 0;
            if (options.canReset) {
                menuItems[menuItemCount++] = {
                    .id = "reset",
                    .label = resetLabel.c_str(),
                    .icon = Ui::UiIcon::Reset,
                    .onInvoke = requestReset,
                };
            }
            if (options.canToggleEnabled) {
                menuItems[menuItemCount++] = {
                    .id = "toggle_enabled",
                    .label = options.enabled ? disableLabel.c_str() : enableLabel.c_str(),
                    .icon = options.enabled ? Ui::UiIcon::Check : Ui::UiIcon::CheckboxUnchecked,
                    .onInvoke = requestToggleEnabled,
                };
            }
            if (options.canRemove) {
                const bool separatorBefore = menuItemCount > 0;
                menuItems[menuItemCount++] = {
                    .id = "remove",
                    .label = removeLabel.c_str(),
                    .icon = Ui::UiIcon::Delete,
                    .destructive = true,
                    .separatorBefore = separatorBefore,
                    .onInvoke = requestRemove,
                };
            }

            const std::span<const Ui::CardMenuAction> menu{menuItems.data(), menuItemCount};
            const std::array actions{
                Ui::CardTitleBarAction{
                    .id = "reset",
                    .icon = Ui::UiIcon::Reset,
                    .title = resetLabel.c_str(),
                    .enabled = options.canReset,
                    .onInvoke = requestReset,
                },
                Ui::CardTitleBarAction{
                    .id = "enabled",
                    .icon = options.enabled ? Ui::UiIcon::Check : Ui::UiIcon::CheckboxUnchecked,
                    .title = options.enabled ? disableLabel.c_str() : enableLabel.c_str(),
                    .enabled = options.canToggleEnabled,
                    .active = options.enabled,
                    .onInvoke = requestToggleEnabled,
                },
                Ui::CardTitleBarAction{
                    .id = "settings",
                    .icon = Ui::UiIcon::Settings,
                    .title = settingsLabel.c_str(),
                    .enabled = !menu.empty(),
                    .menuItems = menu,
                },
            };
            card.DrawTitleBar({.id = "title_bar", .title = title, .fonts = context.theme.fonts, .actions = actions});
            return result;
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
        const std::array tabNames{
            ctx.localization.Get("editor", "workspace.panel.inspector").c_str(),
            ctx.localization.Get("editor", "workspace.panel.scene").c_str(),
        };
        m_activeTab = Ui::DrawDockTabs(tabNames, m_activeTab, ctx.theme.fonts, 36.0F);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 8.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - 36.0F), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoSavedSettings);

        if (m_activeTab != 0) {
            Ui::Hint(ctx.localization.Get("editor", "workspace.inspector.scene_unavailable").c_str(), ctx.theme.fonts);
            ImGui::EndChild();
            ImGui::PopStyleVar();
            return;
        }

        std::array<SceneObjectId, 1> fallbackSelection{};
        std::span<const SceneObjectId> selectedObjects = vm.selectedObjects;
        if (selectedObjects.empty() && vm.primarySelection.has_value()) {
            fallbackSelection.front() = *vm.primarySelection;
            selectedObjects = fallbackSelection;
        }
        if (!selectedObjects.empty() && FindSelectedObject(vm) != nullptr) {
            DrawSelection(vm, selectedObjects, cmd, ctx);
            if (selectedObjects.size() == 1) {
                if (const auto *selected = FindSelectedObject(vm); selected != nullptr)
                    DrawAddComponent(*selected, vm, cmd, ctx);
            }
        } else {
            DrawEmptyState(cmd, ctx);
        }

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
            const InspectorNameEdit nameEdit = DrawObjectTitleWidgets(*primaryObject, command, context);
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
        const std::string addComponentLabel =
            "+  " + context.localization.Get("editor", "workspace.inspector.add_component") + "###InspectorAddComponent";
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
        const bool addComponentPressed = Ui::Button({.label = addComponentLabel.c_str(),
                                                     .size = {0.0F, 34.0F},
                                                     .variant = Ui::ButtonVariant::Secondary,
                                                     .font = context.theme.fonts.sans,
                                                     .componentSize = Ui::ComponentSize::Small,
                                                     .style = {.width = Ui::StyleWidth::FillAvailable}});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (addComponentPressed) {
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
            ImGui::PushID(static_cast<int>(attached.instanceId.value));
            Gameplay::BehaviorComponent edited = attached;
            bool committed = false;
            bool removeRequested = false;
            {
                Ui::Card card(Ui::CardProps{.id = "##BehaviorCard"});
                const ComponentTitleBarResult header =
                    DrawComponentTitleBar(card, sectionText.c_str(),
                                          {.enabled = edited.enabled, .canReset = false, .canToggleEnabled = true, .canRemove = true},
                                          context);
                removeRequested = header.removeRequested;
                if (header.toggleEnabledRequested) {
                    edited.enabled = !edited.enabled;
                    committed = true;
                }
                if (card.BeginBody()) {
                    ImGui::BeginDisabled(!edited.enabled);
                    if (int enabled = edited.enabled ? 1 : 0;
                        Ui::DrawComboPropRow(context.localization.Get("editor", "workspace.inspector.behavior_enabled").c_str(), "enabled",
                                             enabled, enabledEntries, context.theme.fonts)) {
                        edited.enabled = enabled != 0;
                        committed = true;
                    }
                    if (!missing) {
                        for (Gameplay::BehaviorField &field : edited.fields)
                            committed |= DrawBehaviorField(field, enabledEntries, context);
                    }
                    ImGui::EndDisabled();
                }
            }
            if (removeRequested && command.command == EditorWorkspaceViewCommand::None) {
                command.command = EditorWorkspaceViewCommand::RemoveBehaviorFromObject;
                command.objectPayload = object.id;
                command.behaviorInstancePayload = attached.instanceId;
                ImGui::PopID();
                continue;
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

    InspectorNameEdit InspectorPanel::DrawObjectTitleWidgets(const SceneObject &object, EditorWorkspaceViewCommandData &command,
                                                             const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        const std::string &staticLabel = context.localization.Get("editor", "workspace.inspector.static");
        const std::string &optionsLabel = context.localization.Get("editor", "workspace.inspector.object_options");
        const float uiScale = Theme::GetActiveTokens().sizes.uiScale;
        const float rowHeight = 38.0F * uiScale;
        const float checkboxSize = 14.0F * uiScale;
        const float checkboxGap = 4.0F * uiScale;
        const float controlGap = 6.0F * uiScale;
        const float optionsWidth = 30.0F * uiScale;
        const float staticFontSize = 11.0F * uiScale;
        const float staticTextWidth = context.theme.fonts.sansCompact->CalcTextSizeA(staticFontSize, 1000.0F, 0.0F, staticLabel.c_str()).x;
        const float staticWidth = checkboxSize + checkboxGap + staticTextWidth;
        const float trailingWidth = staticWidth + controlGap + optionsWidth;
        const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const bool nameWasValid = IsValidSceneObjectName(draft.name);
        const Ui::TextEditResult edit =
            Ui::DrawEditableTitle("object_name", draft.name, MaximumSceneObjectNameBytes, context.theme.fonts,
                                  {.leadingIcon = KindIcon(object.kind), .trailingWidth = trailingWidth, .error = !nameWasValid});
        const ImVec2 rowEnd = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos({rowOrigin.x + rowWidth - trailingWidth, rowOrigin.y + (rowHeight - checkboxSize) * 0.5F});
        bool isStatic = true;
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1.5F, 1.5F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {checkboxGap, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_CheckMark, Theme::Accent());
        ImGui::BeginDisabled();
        {
            Theme::ScopedTextStyle textStyle(context.theme.fonts.sansCompact, staticFontSize, Theme::FontPx::SansCompact);
            static_cast<void>(ImGui::Checkbox(staticLabel.c_str(), &isStatic));
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        const ImVec2 optionsPosition{rowOrigin.x + rowWidth - optionsWidth, rowOrigin.y + (rowHeight - optionsWidth) * 0.5F};
        ImGui::SetCursorScreenPos(optionsPosition);
        ImGui::PushID("object_options");
        const bool optionsPressed = ImGui::InvisibleButton("##button", {optionsWidth, optionsWidth});
        const bool optionsHovered = ImGui::IsItemHovered();
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        if (optionsHovered)
            drawList->AddRectFilled(optionsPosition, {optionsPosition.x + optionsWidth, optionsPosition.y + optionsWidth},
                                    Theme::U32(Theme::Hover()), 4.0F);
        const float iconInset = 6.0F * uiScale;
        const float iconSize = 18.0F * uiScale;
        Ui::DrawEditorIcon(drawList, Ui::UiIcon::MoreVertical, {optionsPosition.x + iconInset, optionsPosition.y + iconInset},
                           {iconSize, iconSize}, Theme::U32(optionsHovered ? Theme::Text() : Theme::Muted()), context.theme.fonts.icon);
        if (optionsHovered)
            ImGui::SetTooltip("%s", optionsLabel.c_str());
        if (optionsPressed)
            ImGui::OpenPopup("##menu");
        if (Ui::BeginMenuPopup("##menu")) {
            const bool canMutate = !object.effectivelyLocked && command.command == EditorWorkspaceViewCommand::None;
            if (Ui::ContextMenuItem(context.localization.Get("editor", "workspace.hierarchy.duplicate").c_str(), nullptr,
                                    context.theme.fonts, Ui::ContextMenuItemTone::Normal, Ui::UiIconRegistry::Token(Ui::UiIcon::Duplicate),
                                    canMutate)) {
                command.command = EditorWorkspaceViewCommand::DuplicateObject;
                command.objectPayload = object.id;
            }
            if (Ui::ContextMenuItem(context.localization.Get("editor", "workspace.hierarchy.delete").c_str(), nullptr, context.theme.fonts,
                                    Ui::ContextMenuItemTone::Danger, Ui::UiIconRegistry::Token(Ui::UiIcon::Delete), canMutate)) {
                command.command = EditorWorkspaceViewCommand::DeleteObject;
                command.objectPayload = object.id;
            }
            Ui::EndMenuPopup();
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(rowEnd);

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
        Ui::Float3PropertyEditResult position;
        Ui::Float3PropertyEditResult rotation;
        Ui::Float3PropertyEditResult scale;
        ComponentTitleBarResult header;
        {
            Ui::Card card(Ui::CardProps{.id = "##TransformCard"});
            header = DrawComponentTitleBar(card, context.localization.Get("editor", "workspace.inspector.transform").c_str(),
                                           {.enabled = true, .canReset = true, .canToggleEnabled = false, .canRemove = false}, context);
            if (card.BeginBody()) {
                position = Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.position").c_str(), "position",
                                                 draft.position, context.theme.fonts, 0.05F, draft.mixed.position);
                rotation = Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.rotation").c_str(), "rotation",
                                                 draft.rotationDegrees, context.theme.fonts, 0.25F, draft.mixed.rotation);
                scale = Ui::DrawFloat3PropRow(context.localization.Get("editor", "workspace.inspector.scale").c_str(), "scale", draft.scale,
                                              context.theme.fonts, 0.05F, draft.mixed.scale);
            }
        }

        return {
            .changed = position.changed || rotation.changed || scale.changed,
            .committed = position.committed || rotation.committed || scale.committed,
            .cancelRequested = m_editSession.HasTransformPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false),
            .resetRequested = header.resetRequested,
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

        Ui::Card card(Ui::CardProps{.id = "##CameraCard"});
        const ComponentTitleBarResult header =
            DrawComponentTitleBar(card, context.localization.Get("editor", "workspace.inspector.camera").c_str(),
                                  {.enabled = draft.camera->enabled}, context);
        if (header.resetRequested) {
            const bool enabled = draft.camera->enabled;
            draft.camera = Runtime::CameraComponent{};
            draft.camera->enabled = enabled;
            draft.cameraFieldOfViewDegrees = 60.0F;
        }
        if (header.toggleEnabledRequested)
            draft.camera->enabled = !draft.camera->enabled;
        if (!card.BeginBody())
            return {.committed = header.resetRequested || header.toggleEnabledRequested, .removeRequested = header.removeRequested};
        ImGui::BeginDisabled(!draft.camera->enabled);

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
        committed = committed || header.resetRequested || header.toggleEnabledRequested;
        ImGui::EndDisabled();
        card.Finish();
        return {.committed = committed, .removeRequested = header.removeRequested};
    }

    InspectorLightEdit InspectorPanel::DrawLightWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.light.has_value())
            return {};

        Ui::Card card(Ui::CardProps{.id = "##LightCard"});
        const ComponentTitleBarResult header =
            DrawComponentTitleBar(card, context.localization.Get("editor", "workspace.inspector.light").c_str(),
                                  {.enabled = draft.light->enabled}, context);
        if (header.resetRequested) {
            const bool enabled = draft.light->enabled;
            draft.light = Runtime::LightComponent{};
            draft.light->enabled = enabled;
            draft.lightInnerConeDegrees = 20.0F;
            draft.lightOuterConeDegrees = 45.0F;
        }
        if (header.toggleEnabledRequested)
            draft.light->enabled = !draft.light->enabled;
        if (!card.BeginBody())
            return {.committed = header.resetRequested || header.toggleEnabledRequested, .removeRequested = header.removeRequested};
        ImGui::BeginDisabled(!draft.light->enabled);

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
        bool committed =
            kindChanged || colorEdit.committed || intensity.committed || header.resetRequested || header.toggleEnabledRequested;
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
        ImGui::EndDisabled();
        card.Finish();
        return {
            .changed = changed,
            .committed = committed,
            .cancelRequested = m_editSession.HasLightPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false),
            .removeRequested = header.removeRequested,
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

        Ui::Card card(Ui::CardProps{.id = "##TriggerVolumeCard"});
        const ComponentTitleBarResult header =
            DrawComponentTitleBar(card, context.localization.Get("editor", "workspace.inspector.trigger_volume").c_str(),
                                  {.enabled = draft.triggerVolume->enabled}, context);
        if (header.resetRequested) {
            const bool enabled = draft.triggerVolume->enabled;
            draft.triggerVolume = Runtime::TriggerVolumeComponent{};
            draft.triggerVolume->enabled = enabled;
        }
        if (header.toggleEnabledRequested)
            draft.triggerVolume->enabled = !draft.triggerVolume->enabled;
        if (!card.BeginBody())
            return {.committed = header.resetRequested || header.toggleEnabledRequested, .removeRequested = header.removeRequested};
        ImGui::BeginDisabled(!draft.triggerVolume->enabled);

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

        ImGui::EndDisabled();
        card.Finish();
        return {.committed = shapeChanged || header.resetRequested || header.toggleEnabledRequested,
                .removeRequested = header.removeRequested};
    }

    InspectorAudioSourceEdit InspectorPanel::DrawAudioSourceWidgets(const EditorGuiContext &context) {
        InspectorObjectDraft &draft = m_editSession.Draft();
        if (!draft.audioSource.has_value())
            return {};

        Ui::Card card(Ui::CardProps{.id = "##AudioSourceCard"});
        const ComponentTitleBarResult header =
            DrawComponentTitleBar(card, context.localization.Get("editor", "workspace.inspector.audio_source").c_str(),
                                  {.enabled = draft.audioSource->enabled}, context);
        if (header.resetRequested) {
            const bool enabled = draft.audioSource->enabled;
            draft.audioSource = Runtime::AudioSourceComponent{};
            draft.audioSource->enabled = enabled;
        }
        if (header.toggleEnabledRequested)
            draft.audioSource->enabled = !draft.audioSource->enabled;
        if (!card.BeginBody())
            return {.committed = header.resetRequested || header.toggleEnabledRequested, .removeRequested = header.removeRequested};
        ImGui::BeginDisabled(!draft.audioSource->enabled);

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

        const bool committed =
            kindChanged || gainEdit.committed || spatialChanged || header.resetRequested || header.toggleEnabledRequested;
        ImGui::EndDisabled();
        card.Finish();
        return {.committed = committed, .removeRequested = header.removeRequested};
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
