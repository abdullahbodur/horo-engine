/** @file EditorToolbar.cpp
 *  @brief Implements the editor menu bar and icon-toolbar rendering logic. */
#include "EditorToolbar.h"

#include <imgui.h>
#include "ui/IconsFontAwesome6.h"
#include "ui/HoroTheme.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>

#include "renderer/Renderer.h"
#include "ui/editor/TransformGizmo.h"

namespace Horo::Editor {

namespace {
/** @brief Renders a selectable row inside the toolbar combo popup. Returns true when clicked. */
bool DrawToolbarComboRow(const char* label, bool selected) {
    const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, 24.0f);
    const bool clicked = ImGui::InvisibleButton("##combo_row", rowSize);
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    if (const bool hovered = ImGui::IsItemHovered(); hovered || selected) {
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
}

/** @brief Renders a single toolbar gizmo-mode button. */
void DrawGizmoModeButton(const char* icon, const ImVec2& buttonSize, bool active,
                         GizmoMode* gizmoMode, GizmoMode targetMode, float buttonGap) {
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button(icon, buttonSize) && gizmoMode)
        *gizmoMode = targetMode;
    if (active)
        ImGui::PopStyleColor();
    ImGui::SameLine(0, buttonGap);
}

/** @brief Renders the four gizmo-mode buttons (Select, Translate, Rotate, Scale). */
void DrawGizmoModeButtons(const EditorToolbarState& state, const ImVec2& buttonSize, float buttonGap) {
    auto* gizmoMode = static_cast<GizmoMode*>(state.currentGizmoMode);
    const bool isSelect = !gizmoMode || *gizmoMode == GizmoMode::None;
    const bool isMove = gizmoMode && *gizmoMode == GizmoMode::Translate;
    const bool isRotate = gizmoMode && *gizmoMode == GizmoMode::Rotate;
    const bool isScale = gizmoMode && *gizmoMode == GizmoMode::Scale;

    DrawGizmoModeButton(ICON_FA_MOUSE_POINTER, buttonSize, isSelect, gizmoMode, GizmoMode::None, buttonGap);
    DrawGizmoModeButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, buttonSize, isMove, gizmoMode, GizmoMode::Translate, buttonGap);
    DrawGizmoModeButton(ICON_FA_ROTATE, buttonSize, isRotate, gizmoMode, GizmoMode::Rotate, buttonGap);

    // Scale is the last button — skip the trailing SameLine by inlining.
    if (isScale)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button(ICON_FA_EXPAND, buttonSize) && gizmoMode)
        *gizmoMode = GizmoMode::Scale;
    if (isScale)
        ImGui::PopStyleColor();
}

/** @brief Handles the Play button click side-effects. */
void OnPlayClicked(const EditorToolbarState& state) {
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

/** @brief Handles the Stop button click side-effects. */
void OnStopClicked(const EditorToolbarState& state) {
    if (state.playMode)
        *state.playMode = false;
    if (state.playModeEscPresses)
        *state.playModeEscPresses = 0;
    if (state.window)
        glfwSetInputMode(state.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

/** @brief Renders the Play/Stop + Pause button group. */
void DrawPlayPauseStopButtons(const EditorToolbarState& state,
                              const ImVec2& buttonSize, float buttonGap) {
    if (const bool playMode = state.playMode && *state.playMode; !playMode) {
        if (ImGui::Button(ICON_FA_PLAY, buttonSize))
            OnPlayClicked(state);
    } else {
        if (ImGui::Button(ICON_FA_STOP, buttonSize))
            OnStopClicked(state);
    }

    ImGui::SameLine(0, buttonGap);
    if (ImGui::Button(ICON_FA_PAUSE, buttonSize)) {
        // Pause is a placeholder for future play-mode pause; intentionally no-op.
    }
}

/** @brief Renders the preset combo dropdown (Default/Layout/Animation + Wireframe toggle). */
void DrawPresetCombo(const EditorToolbarState& state) {
    static constexpr std::array<const char*, 3> kPresets = {"Default", "Layout", "Animation"};
    static int presetIdx = 0;
    const bool supportsWireframeOverlay =
        Renderer::GetBackendCapabilities().supportsWireframeOverlay;

    if (!supportsWireframeOverlay && state.wireframeMode)
        *state.wireframeMode = false;

    ImGui::SetNextItemWidth(100);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    if (ImGui::BeginCombo("##preset", kPresets[presetIdx])) {
        for (int i = 0; i < static_cast<int>(kPresets.size()); i++) {
            ImGui::PushID(i);
            if (DrawToolbarComboRow(kPresets[i], presetIdx == i)) {
                presetIdx = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::Separator();

        if (!supportsWireframeOverlay)
            ImGui::BeginDisabled();

        ImGui::PushID("wireframe");
        if (const bool wireframeMode = state.wireframeMode && *state.wireframeMode;
            DrawToolbarComboRow("Wireframe", wireframeMode)) {
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

/** @brief Renders the right-aligned Scene / Help / Settings group. */
void DrawRightAlignedGroup(const EditorToolbarCallbacks& callbacks,
                           const ImVec2& buttonSize, float rightButtonGap) {
    static constexpr std::array<const char*, 2> kScenes = {"Scene 1", "Scene 2"};
    static int sceneIdx = 0;

    float maxSceneWidth = 0;
    for (const auto* scene : kScenes)
        maxSceneWidth = std::max(maxSceneWidth, ImGui::CalcTextSize(scene).x);
    const float comboWidth = maxSceneWidth + 24.0f;
    ImGui::SetNextItemWidth(comboWidth);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    if (ImGui::BeginCombo("##scene", kScenes[sceneIdx])) {
        for (int i = 0; i < static_cast<int>(kScenes.size()); i++) {
            ImGui::PushID(i);
            if (DrawToolbarComboRow(kScenes[i], sceneIdx == i)) {
                sceneIdx = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(3);

    ImGui::SameLine(0, rightButtonGap);
    if (ImGui::Button(ICON_FA_CIRCLE_QUESTION, buttonSize) && callbacks.openHelpPopup)
        callbacks.openHelpPopup();

    ImGui::SameLine(0, rightButtonGap);
    if (ImGui::Button(ICON_FA_GEAR, buttonSize) && callbacks.openSettings)
        callbacks.openSettings();
}
}  // namespace

/** @copydoc EditorToolbar::Draw */
void EditorToolbar::Draw(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state) const {
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

/** @copydoc EditorToolbar::DrawFileMenu */
void EditorToolbar::DrawFileMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) const {
    if (!ImGui::BeginMenu("File"))
        return;

    if (ImGui::MenuItem("New Scene") && callbacks.requestSceneAction)
        callbacks.requestSceneAction("NewScene");

    if (ImGui::MenuItem("Open Scene...") && callbacks.requestSceneAction)
        callbacks.requestSceneAction("OpenSceneFile");

    if (ImGui::MenuItem("Reset Layout") && state.resetDockLayoutRequested)
        *state.resetDockLayoutRequested = true;

    ImGui::Separator();

    if (ImGui::MenuItem("Settings...") && callbacks.openSettings)
        callbacks.openSettings();

    if (callbacks.fileMenuRenderCallback) {
        ImGui::Separator();
        callbacks.fileMenuRenderCallback();
    }

    ImGui::EndMenu();
}

/** @copydoc EditorToolbar::DrawAddMenu */
void EditorToolbar::DrawAddMenu(const EditorToolbarCallbacks& callbacks,
                                const EditorToolbarState& state) const {
    if (!ImGui::BeginMenu("Add"))
        return;

    const bool hasSelectedAsset = state.selectedAssetId && !state.selectedAssetId->empty();

    if (ImGui::MenuItem("Panel") && callbacks.addObject)
        callbacks.addObject(SceneObjectType::Panel);

    if (ImGui::MenuItem("Prop") && callbacks.addObject)
        callbacks.addObject(SceneObjectType::Prop);

    if (ImGui::MenuItem("Light") && callbacks.addObject)
        callbacks.addObject(SceneObjectType::Light);

    if (ImGui::MenuItem("Camera") && callbacks.addObject)
        callbacks.addObject(SceneObjectType::Camera);

    ImGui::Separator();

    if (!hasSelectedAsset)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Prop from Selected Asset") && callbacks.addObjectFromSelectedAsset)
        callbacks.addObjectFromSelectedAsset();

    if (!hasSelectedAsset)
        ImGui::EndDisabled();

    ImGui::EndMenu();
}

/** @copydoc EditorToolbar::DrawEditMenu */
void EditorToolbar::DrawEditMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) const {
    if (ImGui::BeginMenu("Edit")) {
        DrawEditMenuItems(callbacks, state);
        ImGui::EndMenu();
    }
}

/** @copydoc EditorToolbar::DrawEditMenuItems */
void EditorToolbar::DrawEditMenuItems(const EditorToolbarCallbacks& callbacks,
                                      const EditorToolbarState& state) const {
    const bool canUndo = callbacks.canUndoHistory && callbacks.canUndoHistory();
    const bool canRedo = callbacks.canRedoHistory && callbacks.canRedoHistory();

    const bool hasSelection = state.selectedIndices && !state.selectedIndices->empty();
    const int primaryIdx = hasSelection ? state.selectedIndices->front() : -1;
    const bool hasSingleSelection = hasSelection && state.selectedIndices->size() == 1;

    if (ImGui::MenuItem("Undo", "Ctrl/Cmd+Z", false, canUndo) && callbacks.undoHistory)
        callbacks.undoHistory();

    if (ImGui::MenuItem("Redo", "Ctrl/Cmd+Shift+Z / Ctrl+Y", false, canRedo) && callbacks.redoHistory)
        callbacks.redoHistory();

    ImGui::Separator();

    if (ImGui::MenuItem("Rename...", nullptr, false, hasSingleSelection) && callbacks.openRenameObjectModal)
        callbacks.openRenameObjectModal(primaryIdx);

    if (ImGui::MenuItem("Create Prefab", nullptr, false, hasSingleSelection) && callbacks.createPrefabFromSelection)
        callbacks.createPrefabFromSelection();

    if (ImGui::MenuItem("Duplicate", nullptr, false, hasSelection) && callbacks.duplicateSelectedObjects)
        callbacks.duplicateSelectedObjects();

    if (ImGui::MenuItem("Delete", nullptr, false, hasSelection) && callbacks.requestDeleteSelectedObjects)
        callbacks.requestDeleteSelectedObjects();

    ImGui::Separator();

    if (ImGui::MenuItem("Copy Ref", "Ctrl/Cmd+Shift+C", false, hasSingleSelection) &&
        callbacks.buildSelectionRefCode && primaryIdx >= 0) {
        const std::string ref = callbacks.buildSelectionRefCode(primaryIdx);
        ImGui::SetClipboardText(ref.c_str());
    }
}

/** @copydoc EditorToolbar::DrawViewMenu */
void EditorToolbar::DrawViewMenu(const EditorToolbarCallbacks& callbacks,
                                 const EditorToolbarState& state) const {
    if (!ImGui::BeginMenu("View"))
        return;

    const bool flyBefore = state.flyMode && *state.flyMode;

    if (ImGui::MenuItem("Fly Mode", "Tab", flyBefore) && callbacks.setFlyMode)
        callbacks.setFlyMode(!flyBefore);

    if (flyBefore || (state.flyMode && *state.flyMode))
        ImGui::TextDisabled("WASD + mouse");

    if (ImGui::MenuItem("Help", "? / F1") && callbacks.openHelpPopup)
        callbacks.openHelpPopup();

    if (ImGui::MenuItem("Quick Open", "Ctrl/Cmd+P") && state.quickOpenOpen)
        *state.quickOpenOpen = true;

    if (ImGui::MenuItem("Command Palette", "Ctrl/Cmd+Shift+P")) {
        if (state.commandPaletteOpen)
            *state.commandPaletteOpen = true;
        if (state.commandPaletteQuery)
            state.commandPaletteQuery->clear();
    }

    if (ImGui::MenuItem("Reset Layout") && state.resetDockLayoutRequested)
        *state.resetDockLayoutRequested = true;

    ImGui::EndMenu();
}

/** @copydoc EditorToolbar::DrawIconToolbar */
void EditorToolbar::DrawIconToolbar(const EditorToolbarCallbacks& callbacks,
                                    const EditorToolbarState& state) const {
    const ImVec2 buttonSize(28, 24);
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

    const float centerY = (ImGui::GetWindowHeight() - buttonSize.y) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 divColor = ImGui::GetColorU32(ImGuiCol_Separator);
    const float divTop = ImGui::GetWindowPos().y + centerY + 3.0f;
    const float divBot = ImGui::GetWindowPos().y + centerY + buttonSize.y - 3.0f;

    float curX = startX;

    // Group 1: gizmo mode buttons
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    DrawGizmoModeButtons(state, buttonSize, buttonGap);
    curX += g1Width;

    {
        const float divX = ImGui::GetWindowPos().x + curX + divPad;
        dl->AddLine(ImVec2(divX, divTop), ImVec2(divX, divBot), divColor);
        curX += divTotal;
    }

    // Group 2: Play / Pause / Stop
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    DrawPlayPauseStopButtons(state, buttonSize, buttonGap);
    curX += g2Width;

    {
        const float divX = ImGui::GetWindowPos().x + curX + divPad;
        dl->AddLine(ImVec2(divX, divTop), ImVec2(divX, divBot), divColor);
        curX += divTotal;
    }

    // Group 3: preset combo
    ImGui::SetCursorPos(ImVec2(curX, centerY));
    DrawPresetCombo(state);

    // Right-aligned group: Scene / Help / Settings
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetWindowWidth() - rightGroupWidth - rightMargin, centerY));
    DrawRightAlignedGroup(callbacks, buttonSize, rightButtonGap);

    ImGui::PopStyleVar();
}

}  // namespace Horo::Editor
