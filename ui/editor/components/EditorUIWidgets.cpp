/** @file EditorUIWidgets.cpp
 *  @brief Implements transient editor overlays and modal widget behavior. */
#include "EditorUIWidgets.h"
#include "ui/editor/EditorLayer.h"
#include "ui/UiComponents.h"
#include "imgui.h"

namespace Horo::Editor {

void EditorUIWidgets::Initialize(EditorLayer* editor) {
    m_editor = editor;
}

void EditorUIWidgets::DrawClipboardToast() const {
    if (m_clipboardToastTime <= 0.0f)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const ImVec2 size(230.0f, 32.0f);
    const ImVec2 pos(io.DisplaySize.x - size.x - 14.0f,
                     io.DisplaySize.y - size.y - 14.0f);
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    draw->AddRectFilled(pos, max, IM_COL32(12, 18, 28, 215), 8.0f);
    draw->AddRect(pos, max, IM_COL32(90, 190, 255, 185), 8.0f, 0, 1.0f);
    const char* label = m_clipboardToastLabel.empty()
                            ? "Reference copied"
                            : m_clipboardToastLabel.c_str();
    draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 9.0f),
                  IM_COL32(220, 235, 255, 255), label);

    // Decrement timer for next frame
    m_clipboardToastTime -= ImGui::GetIO().DeltaTime;
}

void EditorUIWidgets::DrawHotReloadOverlay() const {
    if (!m_hotReloadOverlayActive)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const ImVec2 panelSize(280.0f, 74.0f);
    const ImVec2 panelPos((io.DisplaySize.x - panelSize.x) * 0.5f, 18.0f);
    const ImVec2 panelMax(panelPos.x + panelSize.x, panelPos.y + panelSize.y);

    draw->AddRectFilled(panelPos, panelMax, IM_COL32(10, 14, 22, 215), 10.0f);
    draw->AddRect(panelPos, panelMax, IM_COL32(70, 120, 190, 180), 10.0f, 0,
                  1.0f);

    const ImVec2 spinnerCenter(panelPos.x + 24.0f,
                               panelPos.y + panelSize.y * 0.5f);
    const float spinnerR = 10.0f;
    draw->AddCircle(spinnerCenter, spinnerR, IM_COL32(80, 90, 120, 200), 24,
                    2.0f);

    const float arcStart = m_hotReloadOverlaySpinner;
    const float arcEnd = arcStart + 2.5f;
    draw->PathArcTo(spinnerCenter, spinnerR, arcStart, arcEnd, 24);
    draw->PathStroke(IM_COL32(110, 210, 255, 255), false, 3.0f);

    const char* label = m_hotReloadOverlayLabel.empty()
                            ? "Hot Reload"
                            : m_hotReloadOverlayLabel.c_str();
    draw->AddText(ImVec2(panelPos.x + 44.0f, panelPos.y + 14.0f),
                  IM_COL32(230, 240, 255, 255), label);

    const ImVec2 barMin(panelPos.x + 44.0f, panelPos.y + 42.0f);
    const ImVec2 barMax(panelMax.x - 16.0f, panelPos.y + 56.0f);
    draw->AddRectFilled(barMin, barMax, IM_COL32(26, 32, 46, 255), 4.0f);

    const float w = (barMax.x - barMin.x) * m_hotReloadOverlayProgress;
    if (w > 1.0f)
        draw->AddRectFilled(barMin, ImVec2(barMin.x + w, barMax.y),
                            IM_COL32(90, 190, 255, 255), 4.0f);
}

void EditorUIWidgets::DrawStatusBar() const {
    const ImGuiIO& io = ImGui::GetIO();

    constexpr float kEditorStatusH = 22.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - kEditorStatusH));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kEditorStatusH));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##editor_statusbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (getStatusBarText) {
        ImGui::TextDisabled("%s", getStatusBarText().c_str());
    } else {
        ImGui::TextDisabled("Ready");
    }

    ImGui::End();
}

void EditorUIWidgets::DrawViewGimbal(const Camera& cam) {
    // Gimbal drawing is complex and needs viewport context from EditorLayer
    // For now, this is deferred - the actual implementation stays in EditorLayer
    // because it needs access to m_viewportPanelRect and other viewport state
    (void)cam;
}

// ---- Confirmation Modals ----

void EditorUIWidgets::DrawConfirmDeleteObjectsModal() {
    if (m_confirmDeleteObjectsOpen)
        ImGui::OpenPopup("Confirm Delete Objects");
    if (!Horo::Ui::BeginEditorModal({"Confirm Delete Objects", 400.0f, true}, false))
        return;

    int validCount = static_cast<int>(m_pendingDeleteObjectIndices.size());
    if (validCount <= 0) {
        m_confirmDeleteObjectsOpen = false;
        m_pendingDeleteObjectIndices.clear();
        ImGui::CloseCurrentPopup();
        Horo::Ui::EndEditorModal();
        return;
    }

    ImGui::Text("Delete %d selected object(s)?", validCount);
    ImGui::TextDisabled("This action cannot be undone.");
    ImGui::Separator();

    const Horo::Ui::EditorTheme& theme = Horo::Ui::GetEditorTheme();
    const auto result = Horo::Ui::RenderEditorModalFooter(
        theme, "Delete All", Horo::Ui::EditorModalFooterStyle::DestructiveCancel);
    if (result.confirmed) {
        if (onConfirmDeleteObjects) {
            onConfirmDeleteObjects(m_pendingDeleteObjectIndices);
        }
        m_confirmDeleteObjectsOpen = false;
        m_pendingDeleteObjectIndices.clear();
        ImGui::CloseCurrentPopup();
    }
    if (result.cancelled) {
        m_confirmDeleteObjectsOpen = false;
        m_pendingDeleteObjectIndices.clear();
    }
    Horo::Ui::EndEditorModal();
}

void EditorUIWidgets::DrawConfirmDeleteAssetModal() {
    if (m_confirmDeleteAssetOpen)
        ImGui::OpenPopup("Confirm Delete Asset");
    if (!Horo::Ui::BeginEditorModal({"Confirm Delete Asset", 400.0f, true}, false))
        return;

    if (m_pendingDeleteAssetId.empty()) {
        m_confirmDeleteAssetOpen = false;
        m_pendingDeleteAssetId.clear();
        m_pendingDeleteAssetError.clear();
        ImGui::CloseCurrentPopup();
        Horo::Ui::EndEditorModal();
        return;
    }

    ImGui::Text("Delete asset '%s'?", m_pendingDeleteAssetId.c_str());
    ImGui::TextDisabled("All object bindings to this asset will be cleared.");
    const Horo::Ui::EditorTheme& theme = Horo::Ui::GetEditorTheme();
    if (!m_pendingDeleteAssetError.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 360.0f);
        Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Error,
                                         "%s", m_pendingDeleteAssetError.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    const auto result = Horo::Ui::RenderEditorModalFooter(
        theme, "Delete", Horo::Ui::EditorModalFooterStyle::DestructiveCancel);
    if (result.confirmed) {
        if (onConfirmDeleteAsset) {
            onConfirmDeleteAsset(m_pendingDeleteAssetId);
        }
        m_confirmDeleteAssetOpen = false;
        m_pendingDeleteAssetId.clear();
        m_pendingDeleteAssetError.clear();
        ImGui::CloseCurrentPopup();
    }
    if (result.cancelled) {
        m_confirmDeleteAssetOpen = false;
        m_pendingDeleteAssetId.clear();
        m_pendingDeleteAssetError.clear();
    }
    Horo::Ui::EndEditorModal();
}

void EditorUIWidgets::DrawExitConfirmModal() {
    if (m_confirmExitOpen)
        ImGui::OpenPopup("Unsaved Changes");

    if (!Horo::Ui::BeginEditorModal({"Unsaved Changes", 480.0f, true}, false))
        return;

    ImGui::TextUnformatted("You have unsaved changes.");
    ImGui::TextDisabled("Save or discard them before you continue.");
    ImGui::Separator();

    const Horo::Ui::EditorTheme& theme = Horo::Ui::GetEditorTheme();
    if (!m_exitConfirmError.empty())
        Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Error,
                                         "%s", m_exitConfirmError.c_str());

    const auto result = Horo::Ui::RenderEditorModalFooter(
        theme, "Save", Horo::Ui::EditorModalFooterStyle::ThreeWay, "Discard");
    if (result.confirmed) {
        // This would need more sophisticated callback to handle save+continue
        // For now just close
        m_confirmExitOpen = false;
        m_exitConfirmError.clear();
        ImGui::CloseCurrentPopup();
    }
    if (result.alternate) {
        if (onConfirmExit) {
            onConfirmExit();
        }
        m_confirmExitOpen = false;
        m_exitConfirmError.clear();
        ImGui::CloseCurrentPopup();
    }
    if (result.cancelled) {
        m_confirmExitOpen = false;
        m_exitConfirmError.clear();
    }

    Horo::Ui::EndEditorModal();
}

// ---- Input Modals ----

void EditorUIWidgets::DrawRenameObjectModal() {
    if (m_renameObjectOpen) {
        ImGui::OpenPopup("Rename Object");
        m_renameObjectOpen = false;
    }
    if (!Horo::Ui::BeginEditorModal({"Rename Object", 400.0f, true}, false))
        return;

    std::string nameBuf(256, '\0');
    m_renameObjectDraft.copy(nameBuf.data(), nameBuf.size() - 1);
    if (ImGui::InputText("New ID", nameBuf.data(), nameBuf.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_renameObjectDraft = nameBuf.data();
    } else if (std::string_view(nameBuf.data()) != m_renameObjectDraft) {
        m_renameObjectDraft = nameBuf.data();
    }

    const Horo::Ui::EditorTheme& theme = Horo::Ui::GetEditorTheme();
    if (!m_renameObjectError.empty())
        Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Error,
                                         "%s", m_renameObjectError.c_str());

    const auto result = Horo::Ui::RenderEditorModalFooter(
        theme, "Rename", Horo::Ui::EditorModalFooterStyle::OkCancel);
    if (result.confirmed)
        ApplyRenameObject();
    if (result.cancelled)
        CancelRenameObject();

    Horo::Ui::EndEditorModal();
}

void EditorUIWidgets::ApplyRenameObject() {
    if (m_renameObjectIndex < 0) {
        CancelRenameObject();
        return;
    }

    bool success = false;
    if (onApplyRenameObject) {
        success = onApplyRenameObject(m_renameObjectIndex, m_renameObjectDraft);
    }

    if (success) {
        m_renameObjectError.clear();
        m_renameObjectIndex = -1;
        ImGui::CloseCurrentPopup();
    }
    // else: error message should be set by callback
}

void EditorUIWidgets::CancelRenameObject() {
    m_renameObjectError.clear();
    m_renameObjectIndex = -1;
    ImGui::CloseCurrentPopup();
}

// ---- Callbacks ----

void EditorUIWidgets::OnClipboardAction(const std::string& label, float duration) {
    m_clipboardToastTime = duration;
    m_clipboardToastLabel = label;
}

void EditorUIWidgets::OnHotReloadStart(float duration, const std::string& label) {
    m_hotReloadOverlayActive = true;
    m_hotReloadOverlayProgress = 0.0f;
    m_hotReloadOverlaySpinner = 0.0f;
    m_hotReloadOverlayLabel = label;
    (void)duration; // duration could be used for progress calculations
}

void EditorUIWidgets::OnHotReloadProgress(float progress, float spinner) {
    m_hotReloadOverlayProgress = progress;
    m_hotReloadOverlaySpinner = spinner;
}

void EditorUIWidgets::OnHotReloadEnd() {
    m_hotReloadOverlayActive = false;
}

void EditorUIWidgets::OpenConfirmDeleteObjects(const std::vector<int>& indices) {
    m_confirmDeleteObjectsOpen = true;
    m_pendingDeleteObjectIndices = indices;
}

void EditorUIWidgets::OpenConfirmDeleteAsset(const std::string& assetId) {
    m_confirmDeleteAssetOpen = true;
    m_pendingDeleteAssetId = assetId;
    m_pendingDeleteAssetError.clear();
}

void EditorUIWidgets::OpenConfirmExit() {
    m_confirmExitOpen = true;
}

void EditorUIWidgets::OpenRenameObject(int objectIndex) {
    m_renameObjectOpen = true;
    m_renameObjectIndex = objectIndex;
    m_renameObjectError.clear();
}

}  // namespace Horo::Editor
