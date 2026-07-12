#include "EditorWorkspaceView.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <array>
#include <format>
#include <numbers>

namespace Horo::Editor
{
constexpr float kMenuBarH = 28.0F;
constexpr float kToolbarH = 38.0F;
constexpr float kStatusBarH = 26.0F;
constexpr float kActivityBarW = 36.0F;

EditorWorkspaceView::EditorWorkspaceView(const EditorGuiContext &context, const WorkspacePanelRegistry &panelRegistry)
    : m_context(context), m_panelRegistry(panelRegistry)
{
}

void EditorWorkspaceView::Draw(const EditorWorkspaceViewModel &viewModel,
                               EditorWorkspaceViewCommandData &outCommand) const
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    constexpr float menuH = kMenuBarH;
    constexpr float toolH = kToolbarH;
    constexpr float statusH = kStatusBarH;

    // Activity bars span from below the toolbar down to the status bar
    const float activityBarH = display.y - menuH - toolH - statusH;

    const float contentH = viewModel.activeBottomPanelId.empty() ? 0.0f : viewModel.bottomPanelHeight;

    // Main row height (Hierarchy, Viewport, Inspector)
    const float mainH = activityBarH - contentH;

    constexpr float leftActivityW = kActivityBarW;
    constexpr float rightActivityW = kActivityBarW;
    const float hierarchyW = viewModel.activeLeftPanelId.empty() ? 0.0f : viewModel.leftPanelWidth;
    const float inspectorW = viewModel.activeRightPanelId.empty() ? 0.0f : viewModel.rightPanelWidth;
    const float centerW = display.x - leftActivityW - hierarchyW - inspectorW - rightActivityW;
    const float bottomDockW = display.x - leftActivityW - rightActivityW;

    float curY = 0.0F;

    // ── Menu bar ────────────────────────────────────────────────────
    DrawMenuBar(display, viewModel, outCommand);
    curY += menuH;

    // ── Toolbar ─────────────────────────────────────────────────────
    DrawToolbar(ImVec2(0, curY), ImVec2(display.x, toolH));
    curY += toolH;

    // ── Left Activity Bar ───────────────────────────────────────────
    DrawActivityBarLeft(ImVec2(0, curY), ImVec2(leftActivityW, activityBarH), m_panelRegistry, 
                        viewModel.activeLeftPanelId, outCommand, WorkspaceDockArea::Left);

    // ── Middle Row ──────────────────────────────────────────────────
    float curX = leftActivityW;

    // Left Dock
    if (hierarchyW > 0.0f)
    {
        DrawDockArea(WorkspaceDockArea::Left, "##DockLeft", ImVec2(curX, curY), ImVec2(hierarchyW, mainH), 
                     viewModel.activeLeftPanelId, viewModel, outCommand);
        curX += hierarchyW;
    }

    // Document Dock
    DrawDockArea(WorkspaceDockArea::Document, "##DockDocument", ImVec2(curX, curY), ImVec2(centerW, mainH), 
                 viewModel.activeDocumentPanelId, viewModel, outCommand);
    curX += centerW;

    // Right Dock
    if (inspectorW > 0.0f)
    {
        DrawDockArea(WorkspaceDockArea::Right, "##DockRight", ImVec2(curX, curY), ImVec2(inspectorW, mainH), 
                     viewModel.activeRightPanelId, viewModel, outCommand);
        curX += inspectorW;
    }

    // ── Right Activity Bar ──────────────────────────────────────────
    DrawActivityBarRight(ImVec2(curX, curY), ImVec2(rightActivityW, activityBarH), m_panelRegistry, 
                         viewModel.activeRightPanelId, outCommand, WorkspaceDockArea::Right);

    // ── Content Browser ─────────────────────────────────────────────
    if (contentH > 0.0f)
    {
        DrawDockArea(WorkspaceDockArea::Bottom, "##DockBottom", ImVec2(leftActivityW, curY + mainH), ImVec2(bottomDockW, contentH), 
                     viewModel.activeBottomPanelId, viewModel, outCommand);
    }

    // ── Splitters ───────────────────────────────────────────────────
    auto DrawSplitterWindow = [](const char* id, const ImVec2& pos, const ImVec2& size, ImGuiMouseCursor cursor) -> float {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin(id, nullptr, 
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
                     
        ImGui::InvisibleButton("##split", size);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(cursor);
        float delta = 0.0f;
        if (ImGui::IsItemActive())
        {
            ImGui::SetMouseCursor(cursor);
            delta = cursor == ImGuiMouseCursor_ResizeEW ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
        }
        
        ImGui::End();
        ImGui::PopStyleVar(2);
        return delta;
    };

    if (hierarchyW > 0.0f)
    {
        if (float d = DrawSplitterWindow("##SplitLeft", ImVec2(leftActivityW + hierarchyW - 4.0f, curY), ImVec2(8.0f, mainH), ImGuiMouseCursor_ResizeEW); d != 0.0f)
        {
            outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
            outCommand.targetIndex = 0;
            outCommand.floatPayload = std::max(100.0f, std::min(display.x - leftActivityW - rightActivityW - inspectorW - 100.0f, viewModel.leftPanelWidth + d));
            outCommand.layoutPayload = WorkspaceLayoutSize{
                .leftWidth = *outCommand.floatPayload, .leftHeight = mainH,
                .rightWidth = inspectorW, .rightHeight = mainH,
                .bottomWidth = bottomDockW, .bottomHeight = contentH,
                .documentWidth = display.x - leftActivityW - *outCommand.floatPayload - inspectorW - rightActivityW, .documentHeight = mainH
            };
        }
    }

    if (inspectorW > 0.0f)
    {
        if (float d = DrawSplitterWindow("##SplitRight", ImVec2(display.x - rightActivityW - inspectorW - 4.0f, curY), ImVec2(8.0f, mainH), ImGuiMouseCursor_ResizeEW); d != 0.0f)
        {
            outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
            outCommand.targetIndex = 1;
            outCommand.floatPayload = std::max(100.0f, std::min(display.x - leftActivityW - rightActivityW - hierarchyW - 100.0f, viewModel.rightPanelWidth - d));
            outCommand.layoutPayload = WorkspaceLayoutSize{
                .leftWidth = hierarchyW, .leftHeight = mainH,
                .rightWidth = *outCommand.floatPayload, .rightHeight = mainH,
                .bottomWidth = bottomDockW, .bottomHeight = contentH,
                .documentWidth = display.x - leftActivityW - hierarchyW - *outCommand.floatPayload - rightActivityW, .documentHeight = mainH
            };
        }
    }

    if (contentH > 0.0f)
    {
        if (float d = DrawSplitterWindow("##SplitBottom", ImVec2(leftActivityW, curY + mainH - 4.0f), ImVec2(bottomDockW, 8.0f), ImGuiMouseCursor_ResizeNS); d != 0.0f)
        {
            outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
            outCommand.targetIndex = 2;
            outCommand.floatPayload = std::max(100.0f, std::min(activityBarH - 100.0f, viewModel.bottomPanelHeight - d));
            float newMainH = activityBarH - *outCommand.floatPayload;
            outCommand.layoutPayload = WorkspaceLayoutSize{
                .leftWidth = hierarchyW, .leftHeight = newMainH,
                .rightWidth = inspectorW, .rightHeight = newMainH,
                .bottomWidth = bottomDockW, .bottomHeight = *outCommand.floatPayload,
                .documentWidth = centerW, .documentHeight = newMainH
            };
        }
    }

    curY += activityBarH;

    // ── Status Bar ──────────────────────────────────────────────────
    DrawStatusBar(ImVec2(0.0F, curY), ImVec2(display.x, statusH), viewModel);
}

namespace
{

void DrawFileMenu(EditorWorkspaceViewCommandData &outCommand, const bool isDirty)
{
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, isDirty))
        {
            outCommand.command = EditorWorkspaceViewCommand::SaveScene;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Return to Welcome"))
        {
            outCommand.command = EditorWorkspaceViewCommand::ReturnToWelcome;
        }
        ImGui::EndMenu();
    }
}

void DrawEditMenu(EditorWorkspaceViewCommandData &outCommand, int selectedIndex)
{
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z"))
        {
            // TODO: Implement Undo
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y"))
        {
            // TODO: Implement Redo
        }
        ImGui::Separator();
        const bool hasSelection = (selectedIndex >= 0);
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
        {
            outCommand.command = EditorWorkspaceViewCommand::DuplicateObject;
            outCommand.targetIndex = selectedIndex;
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
        {
            outCommand.command = EditorWorkspaceViewCommand::DeleteObject;
            outCommand.targetIndex = selectedIndex;
        }
        ImGui::EndMenu();
    }
}

void DrawViewMenu()
{
    if (ImGui::BeginMenu("View"))
    {
        ImGui::TextDisabled("Panels");
        ImGui::Separator();
        ImGui::MenuItem("Hierarchy", nullptr, nullptr);
        ImGui::MenuItem("Inspector", nullptr, nullptr);
        ImGui::MenuItem("Viewport", nullptr, nullptr);
        ImGui::MenuItem("Content Browser", nullptr, nullptr);
        ImGui::EndMenu();
    }
}

} // namespace

void EditorWorkspaceView::DrawMenuBar(const ImVec2 &display, const EditorWorkspaceViewModel &viewModel,
                                      EditorWorkspaceViewCommandData &outCommand)
{
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(display.x, kMenuBarH));
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Bg2());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, 5.0F));

    ImGui::Begin("##MenuBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar())
    {
        // Logo badge
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent());
        ImGui::TextUnformatted("HORO");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0F, 10.0F);

        // Project path
        const char *projectLabel = viewModel.projectRoot.empty() ? "Untitled" : viewModel.projectRoot.c_str();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
        ImGui::TextUnformatted(projectLabel);
        ImGui::PopStyleColor();

        // Dirty indicator
        if (viewModel.isDirty)
        {
            ImGui::SameLine(0.0F, 6.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
            ImGui::TextUnformatted("●");
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(0.0F, 16.0F);

        DrawFileMenu(outCommand, viewModel.isDirty);
        DrawEditMenu(outCommand, viewModel.selectedIndex);
        DrawViewMenu();

        // Right-aligned FPS
        const std::string fps = std::format("{} fps", static_cast<int>(viewModel.fps));
        const float fpsW = ImGui::CalcTextSize(fps.c_str()).x + 12.0F;
        ImGui::SetCursorPosX(display.x - fpsW);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted(fps.c_str());
        ImGui::PopStyleColor();

        ImGui::EndMenuBar();
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

namespace
{

void DrawSelectIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    const std::array<ImVec2, 4> pts = {ImVec2(ox + 2.5f, oy + 2.5f), ImVec2(ox + 6.3f, oy + 12.0f),
                                       ImVec2(ox + 8.0f, oy + 8.0f), ImVec2(ox + 12.2f, oy + 6.2f)};
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_Closed, 1.5f);
}

void DrawMoveIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    dl->AddLine(ImVec2(ox + 7, oy + 2), ImVec2(ox + 7, oy + 12), col, 1.4f);
    dl->AddLine(ImVec2(ox + 2, oy + 7), ImVec2(ox + 12, oy + 7), col, 1.4f);
    const std::array<ImVec2, 3> p1 = {ImVec2(ox + 5, oy + 4), ImVec2(ox + 7, oy + 2), ImVec2(ox + 9, oy + 4)};
    dl->AddPolyline(p1.data(), static_cast<int>(p1.size()), col, 0, 1.4f);
    const std::array<ImVec2, 3> p2 = {ImVec2(ox + 5, oy + 10), ImVec2(ox + 7, oy + 12), ImVec2(ox + 9, oy + 10)};
    dl->AddPolyline(p2.data(), static_cast<int>(p2.size()), col, 0, 1.4f);
    const std::array<ImVec2, 3> p3 = {ImVec2(ox + 4, oy + 5), ImVec2(ox + 2, oy + 7), ImVec2(ox + 4, oy + 9)};
    dl->AddPolyline(p3.data(), static_cast<int>(p3.size()), col, 0, 1.4f);
    const std::array<ImVec2, 3> p4 = {ImVec2(ox + 10, oy + 5), ImVec2(ox + 12, oy + 7), ImVec2(ox + 10, oy + 9)};
    dl->AddPolyline(p4.data(), static_cast<int>(p4.size()), col, 0, 1.4f);
}

void DrawRotateIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    dl->PathArcTo(ImVec2(ox + 7, oy + 7), 4.0f, std::numbers::pi_v<float> * 1.5f, std::numbers::pi_v<float> * -0.2f);
    dl->PathStroke(col, 0, 1.4f);
    const std::array<ImVec2, 3> p1 = {ImVec2(ox + 12, oy + 1.5f), ImVec2(ox + 11, oy + 4.1f),
                                      ImVec2(ox + 8.4f, oy + 3)};
    dl->AddPolyline(p1.data(), static_cast<int>(p1.size()), col, 0, 1.4f);
}

void DrawScaleIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    dl->AddRect(ImVec2(ox + 4.5f, oy + 4.5f), ImVec2(ox + 9.5f, oy + 9.5f), col, 0.5f, 0, 1.4f);
    dl->AddLine(ImVec2(ox + 9.5f, oy + 4.5f), ImVec2(ox + 12, oy + 2), col, 1.4f);
    dl->AddLine(ImVec2(ox + 9.5f, oy + 9.5f), ImVec2(ox + 12, oy + 12), col, 1.4f);
    dl->AddLine(ImVec2(ox + 4.5f, oy + 9.5f), ImVec2(ox + 2, oy + 12), col, 1.4f);
    dl->AddLine(ImVec2(ox + 4.5f, oy + 4.5f), ImVec2(ox + 2, oy + 2), col, 1.4f);
}

void DrawLocalIcon(ImDrawList *dl, ImFont *font, const float x, const float y, const float w, const float h,
                   const ImU32 col)
{
    const ImVec2 ts = ImGui::CalcTextSize("L");
    dl->AddText(font, font->FontSize, ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), col, "L");
}

void DrawWorldIcon(ImDrawList *dl, ImFont *font, const float x, const float y, const float w, const float h)
{
    const ImVec2 ts = ImGui::CalcTextSize("W");
    dl->AddText(font, font->FontSize, ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), Theme::U32(Theme::Dim()),
                "W");
}

void DrawViewModeIcon(ImDrawList *dl, ImFont *font, const float x, const float y, const float w, const float h,
                      const ImU32 col)
{
    const ImVec2 ts = ImGui::CalcTextSize("Scene");
    dl->AddText(font, font->FontSize, ImVec2(x + 10.0f, y + (h - ts.y) * 0.5f), col, "Scene");
    const float ax = x + w - 14.0f;
    const float ay = y + h * 0.5f;
    dl->AddTriangleFilled(ImVec2(ax - 3, ay - 1.5f), ImVec2(ax + 3, ay - 1.5f), ImVec2(ax, ay + 2.5f), col);
}

void DrawSettingsIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    dl->AddCircle(ImVec2(ox + 7, oy + 7), 1.8f, col, 12, 1.35f);
    dl->AddLine(ImVec2(ox + 7, oy + 1.5f), ImVec2(ox + 7, oy + 2.7f), col, 1.35f);
    dl->AddLine(ImVec2(ox + 7, oy + 11.3f), ImVec2(ox + 7, oy + 12.5f), col, 1.35f);
    dl->AddLine(ImVec2(ox + 1.5f, oy + 7), ImVec2(ox + 2.7f, oy + 7), col, 1.35f);
    dl->AddLine(ImVec2(ox + 11.3f, oy + 7), ImVec2(ox + 12.5f, oy + 7), col, 1.35f);
    dl->AddLine(ImVec2(ox + 3.1f, oy + 3.1f), ImVec2(ox + 3.95f, oy + 3.95f), col, 1.35f);
    dl->AddLine(ImVec2(ox + 10.05f, oy + 10.05f), ImVec2(ox + 10.9f, oy + 10.9f), col, 1.35f);
    dl->AddLine(ImVec2(ox + 10.9f, oy + 3.1f), ImVec2(ox + 10.05f, oy + 3.95f), col, 1.35f);
    dl->AddLine(ImVec2(ox + 3.95f, oy + 10.05f), ImVec2(ox + 3.1f, oy + 10.9f), col, 1.35f);
}

void DrawHelpIcon(ImDrawList *dl, const float x, const float y, const float w, const float h, const ImU32 col)
{
    const float ox = x + (w - 14.0f) * 0.5f;
    const float oy = y + (h - 14.0f) * 0.5f;
    dl->AddCircle(ImVec2(ox + 7, oy + 7), 5.5f, col, 24, 1.4f);
    dl->PathArcTo(ImVec2(ox + 7, oy + 5.5f), 1.5f, std::numbers::pi_v<float>, 0);
    dl->PathLineTo(ImVec2(ox + 7, oy + 8.5f));
    dl->PathStroke(col, 0, 1.4f);
    dl->AddCircleFilled(ImVec2(ox + 7, oy + 10.5f), 0.8f, col);
}

template<typename DrawIconFunc>
void DrawToolButton(ImDrawList* dl, const float centerY, float& curX, const char* id, float width, const bool active, DrawIconFunc drawIcon)
{
    const float h = 26.0f;
    const float y = centerY - h * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(curX, y));
    if (ImGui::InvisibleButton(id, ImVec2(width, h)))
    { /* TODO: Handle tool click event */
    }
    const bool hovered = ImGui::IsItemHovered();
    if (active)
    {
        dl->AddRectFilled(ImVec2(curX + 2.0f, y + 2.0f), ImVec2(curX + width - 2.0f, y + h - 2.0f),
                          Theme::U32(Theme::Bg3()), 3.0f);
    }
    else if (hovered)
    {
        dl->AddRectFilled(ImVec2(curX + 2.0f, y + 2.0f), ImVec2(curX + width - 2.0f, y + h - 2.0f),
                          Theme::U32(Theme::Hover()), 3.0f);
    }
    drawIcon(curX, y, width, h, Theme::U32(active || hovered ? Theme::Text() : Theme::Muted()));
    curX += width;
}

} // namespace

void EditorWorkspaceView::DrawToolbar(const ImVec2 &pos, const ImVec2 &size) const
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 0.0F));

    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    float curX = pos.x + 10.0f;
    const float centerY = pos.y + size.y * 0.5f;

    auto drawToolGroupBg = [dl, centerY, &curX](const float width) {
        constexpr float h = 26.0f;
        const float y = centerY - h * 0.5f;
        dl->AddRectFilled(ImVec2(curX, y), ImVec2(curX + width, y + h), Theme::U32(Theme::Bg0()), 4.0f);
        dl->AddRect(ImVec2(curX, y), ImVec2(curX + width, y + h), Theme::U32(Theme::Border()), 4.0f);
        return y;
    };

    // Transform tools
    drawToolGroupBg(28.0f * 4);
    DrawToolButton(dl, centerY, curX, "##Select", 28.0f, true,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawSelectIcon(dl, x, y, w, h, col);
                   });
    DrawToolButton(dl, centerY, curX, "##Move", 28.0f, false,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawMoveIcon(dl, x, y, w, h, col);
                   });
    DrawToolButton(dl, centerY, curX, "##Rotate", 28.0f, false,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawRotateIcon(dl, x, y, w, h, col);
                   });
    DrawToolButton(dl, centerY, curX, "##Scale", 28.0f, false,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawScaleIcon(dl, x, y, w, h, col);
                   });

    curX += 16.0f; // Gap

    // Space
    drawToolGroupBg(28.0f * 2);
    DrawToolButton(dl, centerY, curX, "##Local", 28.0f, true,
                   [dl, this](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawLocalIcon(dl, m_context.theme.fonts.sans, x, y, w, h, col);
                   });
    DrawToolButton(dl, centerY, curX, "##World", 28.0f, false,
                   [dl, this](const float x, const float y, const float w, const float h, [[maybe_unused]] ImU32 col) {
                       DrawWorldIcon(dl, m_context.theme.fonts.sans, x, y, w, h);
                   });

    curX += 16.0f;

    // View mode
    constexpr float viewModeW = 80.0f;
    drawToolGroupBg(viewModeW);
    DrawToolButton(dl, centerY, curX, "##ViewMode", viewModeW, false,
                   [dl, this](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawViewModeIcon(dl, m_context.theme.fonts.sans, x, y, w, h, col);
                   });

    // Center Play Button
    constexpr float playW = 70.0f;
    const float playX = pos.x + (size.x - playW) * 0.5f;
    curX = playX;

    const float py = centerY - 13.0f;
    ImGui::SetCursorScreenPos(ImVec2(curX, py));
    if (ImGui::InvisibleButton("##Play", ImVec2(playW, 26.0f)))
    {
        // TODO: Handle play clicked
    }
    const bool playHovered = ImGui::IsItemHovered();
    dl->AddRectFilled(ImVec2(curX, py), ImVec2(curX + playW, py + 26.0f), Theme::U32(Theme::Bg0()), 4.0f);
    dl->AddRect(ImVec2(curX, py), ImVec2(curX + playW, py + 26.0f), Theme::U32(Theme::Border()), 4.0f);
    if (playHovered)
        dl->AddRectFilled(ImVec2(curX + 2, py + 2), ImVec2(curX + playW - 2, py + 24.0f), Theme::U32(Theme::Hover()),
                          3.0f);

    const ImU32 playCol = Theme::U32(Theme::Ok());
    const float tox = curX + 10.0f;
    const float toy = py + 8.0f;
    dl->AddTriangleFilled(ImVec2(tox, toy), ImVec2(tox + 8.5f, toy + 4.5f), ImVec2(tox, toy + 9.0f), playCol);
    dl->AddText(m_context.theme.fonts.sans, m_context.theme.fonts.sans->FontSize, ImVec2(curX + 24.0f, py + 5.0f),
                playCol, "Play");

    // Utility group (Right aligned)
    constexpr float utilW = 28.0f * 2;
    curX = pos.x + size.x - utilW - 10.0f;
    drawToolGroupBg(utilW);
    DrawToolButton(dl, centerY, curX, "##Settings", 28.0f, false,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawSettingsIcon(dl, x, y, w, h, col);
                   });
    DrawToolButton(dl, centerY, curX, "##Help", 28.0f, false,
                   [dl](const float x, const float y, const float w, const float h, const ImU32 col) {
                       DrawHelpIcon(dl, x, y, w, h, col);
                   });

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void EditorWorkspaceView::DrawDockArea(const WorkspaceDockArea area, const char* windowId, const ImVec2& pos, const ImVec2& size, const std::string_view activePanelId, const EditorWorkspaceViewModel& viewModel,
                                       EditorWorkspaceViewCommandData& outCommand) const
{
    const auto panels = m_panelRegistry.GetPanelsForArea(area);
    if (panels.empty())
    {
        return;
    }

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    ImGui::Begin(windowId, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    std::shared_ptr<IWorkspacePanel> activePanel = nullptr;
    for (const auto& p : panels)
    {
        if (p->GetId() == activePanelId)
        {
            activePanel = p;
            break;
        }
    }

    if (!activePanel)
    {
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        return;
    }

    // Render the active panel content inside a child view.
    // The panel itself is responsible for drawing its own tabs.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
    ImGui::BeginChild("##DockContent", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    activePanel->DrawPanel(ImGui::GetWindowPos(), ImGui::GetWindowSize(), viewModel, outCommand, m_context);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void EditorWorkspaceView::DrawActivityBarLeft(const ImVec2 &pos, const ImVec2 &size,
                                              const WorkspacePanelRegistry &registry,
                                              const std::string_view activePanelId, EditorWorkspaceViewCommandData& outCommand,
                                              WorkspaceDockArea area)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 8.0F));

    ImGui::Begin("##ActivityLeft", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const auto panels = registry.GetPanelsForArea(area);
    float currentY = pos.y + 10.0f;
    for (const auto &panel : panels)
    {
        const bool isActive = (panel->GetId() == activePanelId);
        
        ImGui::SetCursorPos(ImVec2(0, currentY - pos.y));
        ImGui::PushID(panel->GetId().c_str());
        if (ImGui::Selectable("##actbtn", isActive, 0, ImVec2(size.x, 36.0f)))
        {
            if (isActive)
            {
                // Toggle off
                outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                outCommand.targetIndex = static_cast<int>(area);
                outCommand.stringPayload = "";
            }
            else
            {
                // Switch to this panel
                outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                outCommand.targetIndex = static_cast<int>(area);
                outCommand.stringPayload = panel->GetId();
            }
        }
        ImGui::PopID();

        const ImU32 iconColor = isActive ? Theme::U32(Theme::Text()) : Theme::U32(Theme::Dim());
        panel->DrawIcon(dl, ImVec2(pos.x, currentY), ImVec2(size.x, 36.0f), iconColor);
        
        if (isActive)
        {
            // Active indicator line
            dl->AddLine(ImVec2(pos.x, currentY), ImVec2(pos.x, currentY + 36.0f), Theme::U32(Theme::Accent()), 2.0f);
        }

        currentY += 40.0f;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void EditorWorkspaceView::DrawActivityBarRight(const ImVec2 &pos, const ImVec2 &size,
                                               const WorkspacePanelRegistry &registry,
                                               const std::string_view activePanelId, EditorWorkspaceViewCommandData& outCommand,
                                               WorkspaceDockArea area)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 8.0F));

    ImGui::Begin("##ActivityRight", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const auto panels = registry.GetPanelsForArea(area);
    float currentY = pos.y + 10.0f;
    for (const auto &panel : panels)
    {
        const bool isActive = (panel->GetId() == activePanelId);

        ImGui::SetCursorPos(ImVec2(0, currentY - pos.y));
        ImGui::PushID(panel->GetId().c_str());
        if (ImGui::Selectable("##actbtn", isActive, 0, ImVec2(size.x, 36.0f)))
        {
            if (isActive)
            {
                // Toggle off
                outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                outCommand.targetIndex = static_cast<int>(area);
                outCommand.stringPayload = "";
            }
            else
            {
                // Switch to this panel
                outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                outCommand.targetIndex = static_cast<int>(area);
                outCommand.stringPayload = panel->GetId();
            }
        }
        ImGui::PopID();

        const ImU32 iconColor = isActive ? Theme::U32(Theme::Text()) : Theme::U32(Theme::Dim());
        panel->DrawIcon(dl, ImVec2(pos.x, currentY), ImVec2(size.x, 36.0f), iconColor);

        if (isActive)
        {
            // Active indicator line
            dl->AddLine(ImVec2(pos.x + size.x - 2.0f, currentY), ImVec2(pos.x + size.x - 2.0f, currentY + 36.0f), Theme::U32(Theme::Accent()), 2.0f);
        }

        currentY += 40.0f;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void EditorWorkspaceView::DrawStatusBar(const ImVec2 &pos, const ImVec2 &size,
                                        const EditorWorkspaceViewModel &viewModel)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F, 0.0F));

    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetCursorPosY((size.y - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());

    // Example placeholders for status pill
    ImGui::Text("Sel: %d", viewModel.selectedIndex >= 0 ? 1 : 0);
    ImGui::SameLine(0, 10.0f);
    if (viewModel.isDirty)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
        ImGui::TextUnformatted("Dirty: Yes");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted("Dirty: No");
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace Horo::Editor
