/** @file EditorToolbar.cpp
 *  @brief Implements the editor menu bar and icon-toolbar rendering logic. */
#include "EditorToolbar.h"

#include <imgui.h>
#include "ui/IconsFontAwesome6.h"
#include "ui/HoroTheme.h"
#include <GLFW/glfw3.h>

#include "renderer/Renderer.h"
#include "ui/editor/TransformGizmo.h"

namespace Horo::Editor {

void EditorToolbar::Draw(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));

#ifdef __APPLE__
    constexpr float toolbarH = 38.0f;
    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
#else
    constexpr float toolbarH = 48.0f;  // kEditorToolbarH
    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
#endif

    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbarH));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 0));
    ImGui::Begin("##toolbar", nullptr, toolbarFlags);

#ifndef __APPLE__
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "EDITOR");
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    if (ImGui::BeginMenuBar()) {
        const bool hasSelectedAsset = state.selectedAssetId && !state.selectedAssetId->empty();
        const bool hasSelection = state.selectedIndices && !state.selectedIndices->empty();
        const int primaryIdx = hasSelection && !state.selectedIndices->empty()
                                   ? state.selectedIndices->front()
                                   : -1;
        const bool hasSingleSelection =
            hasSelection && state.selectedIndices && state.selectedIndices->size() == 1;

        DrawFileMenu(callbacks, state);
        DrawAddMenu(callbacks, state);
        DrawEditMenu(callbacks, state);
        DrawViewMenu(callbacks, state);
        ImGui::EndMenuBar();
    }
#else
    DrawIconToolbar(callbacks, state);
#endif

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorToolbar::DrawFileMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene"))
            if (callbacks.requestSceneAction)
                callbacks.requestSceneAction("NewScene");

        if (ImGui::MenuItem("Open Scene..."))
            if (callbacks.requestSceneAction)
                callbacks.requestSceneAction("OpenSceneFile");

        if (ImGui::MenuItem("Reset Layout"))
            if (state.resetDockLayoutRequested)
                *state.resetDockLayoutRequested = true;

        ImGui::Separator();

        if (ImGui::MenuItem("Settings...")) {
            if (callbacks.openSettings)
                callbacks.openSettings();
        }

        if (callbacks.fileMenuRenderCallback) {
            ImGui::Separator();
            callbacks.fileMenuRenderCallback();
        }

        ImGui::EndMenu();
    }
}

void EditorToolbar::DrawAddMenu(const EditorToolbarCallbacks& callbacks,
                                const EditorToolbarState& state) {
    if (ImGui::BeginMenu("Add")) {
        const bool hasSelectedAsset = state.selectedAssetId && !state.selectedAssetId->empty();

        if (ImGui::MenuItem("Panel"))
            if (callbacks.addObject)
                callbacks.addObject(SceneObjectType::Panel);

        if (ImGui::MenuItem("Prop"))
            if (callbacks.addObject)
                callbacks.addObject(SceneObjectType::Prop);

        if (ImGui::MenuItem("Light"))
            if (callbacks.addObject)
                callbacks.addObject(SceneObjectType::Light);

        if (ImGui::MenuItem("Camera"))
            if (callbacks.addObject)
                callbacks.addObject(SceneObjectType::Camera);

        ImGui::Separator();

        if (!hasSelectedAsset)
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Prop from Selected Asset"))
            if (callbacks.addObjectFromSelectedAsset)
                callbacks.addObjectFromSelectedAsset();

        if (!hasSelectedAsset)
            ImGui::EndDisabled();

        ImGui::EndMenu();
    }
}

void EditorToolbar::DrawEditMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) {
    if (ImGui::BeginMenu("Edit")) {
        const bool hasSelection = state.selectedIndices && !state.selectedIndices->empty();
        const int primaryIdx = hasSelection && !state.selectedIndices->empty()
                                   ? state.selectedIndices->front()
                                   : -1;
        const bool hasSingleSelection = hasSelection && state.selectedIndices && state.selectedIndices->size() == 1;

        DrawEditMenuItems(callbacks, state);
        ImGui::EndMenu();
    }
}

void EditorToolbar::DrawEditMenuItems(const EditorToolbarCallbacks& callbacks,
                                      const EditorToolbarState& state) {
    bool canUndo = callbacks.canUndoHistory ? callbacks.canUndoHistory() : false;
    bool canRedo = callbacks.canRedoHistory ? callbacks.canRedoHistory() : false;

    const bool hasSelection = state.selectedIndices && !state.selectedIndices->empty();
    const int primaryIdx = hasSelection && !state.selectedIndices->empty()
                               ? state.selectedIndices->front()
                               : -1;
    const bool hasSingleSelection = hasSelection && state.selectedIndices && state.selectedIndices->size() == 1;

    if (ImGui::MenuItem("Undo", "Ctrl/Cmd+Z", false, canUndo))
        if (callbacks.undoHistory)
            callbacks.undoHistory();

    if (ImGui::MenuItem("Redo", "Ctrl/Cmd+Shift+Z / Ctrl+Y", false, canRedo))
        if (callbacks.redoHistory)
            callbacks.redoHistory();

    ImGui::Separator();

    if (ImGui::MenuItem("Rename...", nullptr, false, hasSingleSelection))
        if (callbacks.openRenameObjectModal)
            callbacks.openRenameObjectModal(primaryIdx);

    if (ImGui::MenuItem("Create Prefab", nullptr, false, hasSingleSelection))
        if (callbacks.createPrefabFromSelection)
            callbacks.createPrefabFromSelection();

    if (ImGui::MenuItem("Duplicate", nullptr, false, hasSelection))
        if (callbacks.duplicateSelectedObjects)
            callbacks.duplicateSelectedObjects();

    if (ImGui::MenuItem("Delete", nullptr, false, hasSelection))
        if (callbacks.requestDeleteSelectedObjects)
            callbacks.requestDeleteSelectedObjects();

    ImGui::Separator();

    if (ImGui::MenuItem("Copy Ref", "Ctrl/Cmd+Shift+C", false, hasSingleSelection)) {
        if (callbacks.buildSelectionRefCode && primaryIdx >= 0) {
            const std::string ref = callbacks.buildSelectionRefCode(primaryIdx);
            ImGui::SetClipboardText(ref.c_str());
        }
    }
}

void EditorToolbar::DrawViewMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) {
    if (ImGui::BeginMenu("View")) {
        const bool flyBefore = state.flyMode ? *state.flyMode : false;

        if (ImGui::MenuItem("Fly Mode", "Tab", state.flyMode ? *state.flyMode : false)) {
            if (callbacks.setFlyMode) {
                bool newFlyMode = !(state.flyMode ? *state.flyMode : false);
                callbacks.setFlyMode(newFlyMode);
            }
        }

        if ((flyBefore || (state.flyMode && *state.flyMode)))
            ImGui::TextDisabled("WASD + mouse");

        if (ImGui::MenuItem("Help", "? / F1"))
            if (callbacks.openHelpPopup)
                callbacks.openHelpPopup();

        if (ImGui::MenuItem("Quick Open", "Ctrl/Cmd+P"))
            if (state.quickOpenOpen)
                *state.quickOpenOpen = true;

        if (ImGui::MenuItem("Command Palette", "Ctrl/Cmd+Shift+P")) {
            if (state.commandPaletteOpen)
                *state.commandPaletteOpen = true;
            if (state.commandPaletteQuery)
                state.commandPaletteQuery->clear();
        }

        if (ImGui::MenuItem("Reset Layout"))
            if (state.resetDockLayoutRequested)
                *state.resetDockLayoutRequested = true;

        ImGui::EndMenu();
    }
}

void EditorToolbar::DrawIconToolbar(const EditorToolbarCallbacks& callbacks,
                                    const EditorToolbarState& state) {
    const ImVec2 buttonSize(28, 24);
    auto GetCenteredY = [&]() { return (ImGui::GetWindowHeight() - buttonSize.y) * 0.5f; };
    constexpr float buttonGap = 10.0f;
    constexpr float rightButtonGap = 8.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    const float g1Width = 4.0f * buttonSize.x + 3.0f * buttonGap;
    const float g2Width = 2.0f * buttonSize.x + buttonGap;
    const float g3Width = 100.0f;
    const float divPad = 6.0f;
    const float divTotal = 2.0f * divPad + 1.0f;
    const float mainBlockWidth = g1Width + divTotal + g2Width + divTotal + g3Width;

    const float rightGroupWidth = 180.0f;
    const float rightMargin = 16.0f;
    const float availableWidth = ImGui::GetWindowWidth() - rightGroupWidth - rightMargin;
    const float startX = (availableWidth - mainBlockWidth) * 0.5f;

    const float centerY = GetCenteredY();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 divColor = ImGui::GetColorU32(ImGuiCol_Separator);
    const float divTop = ImGui::GetWindowPos().y + centerY + 3.0f;
    const float divBot = ImGui::GetWindowPos().y + centerY + buttonSize.y - 3.0f;

    float curX = startX;

    // Gizmo mode buttons (requires state.currentGizmoMode)
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    {
        // Cast void* to GizmoMode* to avoid circular header dependency
        GizmoMode* gizmoMode = static_cast<GizmoMode*>(state.currentGizmoMode);

        const bool isSelect = !gizmoMode || *gizmoMode == GizmoMode::None;
        if (isSelect)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(ICON_FA_MOUSE_POINTER, buttonSize)) {
            if (gizmoMode)
                *gizmoMode = GizmoMode::None;
        }
        if (isSelect)
            ImGui::PopStyleColor();
        ImGui::SameLine(0, buttonGap);

        const bool isMove = gizmoMode && *gizmoMode == GizmoMode::Translate;
        if (isMove)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, buttonSize)) {
            if (gizmoMode)
                *gizmoMode = GizmoMode::Translate;
        }
        if (isMove)
            ImGui::PopStyleColor();
        ImGui::SameLine(0, buttonGap);

        const bool isRotate = gizmoMode && *gizmoMode == GizmoMode::Rotate;
        if (isRotate)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(ICON_FA_ROTATE, buttonSize)) {
            if (gizmoMode)
                *gizmoMode = GizmoMode::Rotate;
        }
        if (isRotate)
            ImGui::PopStyleColor();
        ImGui::SameLine(0, buttonGap);

        const bool isScale = gizmoMode && *gizmoMode == GizmoMode::Scale;
        if (isScale)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(ICON_FA_EXPAND, buttonSize)) {
            if (gizmoMode)
                *gizmoMode = GizmoMode::Scale;
        }
        if (isScale)
            ImGui::PopStyleColor();
    }
    curX += g1Width;

    {
        const float divX = ImGui::GetWindowPos().x + curX + divPad;
        dl->AddLine(ImVec2(divX, divTop), ImVec2(divX, divBot), divColor);
        curX += divTotal;
    }

    // Play / Pause / Stop buttons
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    {
        const bool playMode = state.playMode ? *state.playMode : false;

        if (!playMode) {
            if (ImGui::Button(ICON_FA_PLAY, buttonSize)) {
                if (state.playMode)
                    *state.playMode = true;
                if (state.playModeEscPresses)
                    *state.playModeEscPresses = 0;
                if (state.flyMode && *state.flyMode && state.window) {
                    *state.flyMode = false;
                    if (state.flyCamInitialized)
                        *state.flyCamInitialized = false;
                    if (state.prevCursorInit)
                        *state.prevCursorInit = false;
                    glfwSetInputMode(state.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }
        } else {
            if (ImGui::Button(ICON_FA_STOP, buttonSize)) {
                if (state.playMode)
                    *state.playMode = false;
                if (state.playModeEscPresses)
                    *state.playModeEscPresses = 0;
                if (state.window)
                    glfwSetInputMode(state.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        ImGui::SameLine(0, buttonGap);
        if (ImGui::Button(ICON_FA_PAUSE, buttonSize)) {}
    }
    curX += g2Width;

    {
        const float divX = ImGui::GetWindowPos().x + curX + divPad;
        dl->AddLine(ImVec2(divX, divTop), ImVec2(divX, divBot), divColor);
        curX += divTotal;
    }

    auto drawToolbarComboRow = [](const char* label, bool selected) {
        const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, 24.0f);
        const bool clicked = ImGui::InvisibleButton("##combo_row", rowSize);
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        if (hovered || selected) {
            const auto& palette = Ui::GetEditorTheme().palette;
            const ImVec4 fill = selected ? palette.selection : palette.selectionHover;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(rowMin.x + 1.0f, rowMin.y + 1.0f),
                ImVec2(rowMax.x - 1.0f, rowMax.y - 1.0f),
                ImGui::ColorConvertFloat4ToU32(fill), 5.0f);
        }
        ImGui::GetWindowDrawList()->AddText(ImVec2(rowMin.x + 5.0f, rowMin.y + 3.0f),
                                            ImGui::GetColorU32(ImGuiCol_Text), label);
        if (selected)
            ImGui::SetItemDefaultFocus();
        return clicked;
    };

    // Preset dropdown
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    {
        const char* presets[] = {"Default", "Layout", "Animation"};
        static int presetIdx = 0;
        const bool supportsWireframeOverlay =
            Renderer::GetBackendCapabilities().supportsWireframeOverlay;

        if (!supportsWireframeOverlay && state.wireframeMode)
            *state.wireframeMode = false;

        ImGui::SetNextItemWidth(100);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

        if (ImGui::BeginCombo("##preset", presets[presetIdx])) {
            for (int i = 0; i < 3; i++) {
                ImGui::PushID(i);
                if (drawToolbarComboRow(presets[i], presetIdx == i)) {
                    presetIdx = i;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::Separator();

            if (!supportsWireframeOverlay)
                ImGui::BeginDisabled();

            ImGui::PushID("wireframe");
            const bool wireframeMode = state.wireframeMode ? *state.wireframeMode : false;
            if (drawToolbarComboRow("Wireframe", wireframeMode)) {
                if (state.wireframeMode)
                    *state.wireframeMode = !wireframeMode;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();

            if (!supportsWireframeOverlay)
                ImGui::EndDisabled();

            ImGui::EndCombo();
        }
        ImGui::PopStyleVar(3);
    }

    // Right-aligned group: Scene / Help / Settings
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetWindowWidth() - rightGroupWidth - rightMargin, centerY));
    {
        const char* scenes[] = {"Scene 1", "Scene 2"};
        static int sceneIdx = 0;

        float maxSceneWidth = 0;
        for (const auto* scene : scenes) {
            maxSceneWidth = std::max(maxSceneWidth, ImGui::CalcTextSize(scene).x);
        }
        float comboWidth = maxSceneWidth + 24.0f;
        ImGui::SetNextItemWidth(comboWidth);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

        if (ImGui::BeginCombo("##scene", scenes[sceneIdx])) {
            for (int i = 0; i < 2; i++) {
                ImGui::PushID(i);
                if (drawToolbarComboRow(scenes[i], sceneIdx == i)) {
                    sceneIdx = i;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar(3);

        ImGui::SameLine(0, rightButtonGap);
        if (ImGui::Button(ICON_FA_CIRCLE_QUESTION, buttonSize)) {
            if (callbacks.openHelpPopup)
                callbacks.openHelpPopup();
        }

        ImGui::SameLine(0, rightButtonGap);
        if (ImGui::Button(ICON_FA_GEAR, buttonSize)) {
            if (callbacks.openSettings)
                callbacks.openSettings();
        }
    }

    ImGui::PopStyleVar();
}

}  // namespace Horo::Editor
