#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <string>
#include <string_view>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
#include "ui/UiFonts.h"

using Horo::Ui::EditorTheme;
using Horo::Ui::FontFamilyConfig;
using Horo::Ui::FontResolutionResult;
using Horo::Ui::HoroPalette;
using Horo::Ui::LauncherTheme;

namespace {

void CheckVec4Near(const ImVec4 &actual, const ImVec4 &expected) {
  constexpr float kTolerance = 0.0001f;
  CHECK(std::abs(actual.x - expected.x) < kTolerance);
  CHECK(std::abs(actual.y - expected.y) < kTolerance);
  CHECK(std::abs(actual.z - expected.z) < kTolerance);
  CHECK(std::abs(actual.w - expected.w) < kTolerance);
}

} // namespace

TEST_CASE("shared Horo theme exposes launcher and editor density variants") {
  const LauncherTheme &launcher = Horo::Ui::GetLauncherTheme();
  const EditorTheme &editor = Horo::Ui::GetEditorTheme();

  CheckVec4Near(launcher.palette.backgroundTop,
                ImVec4(1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f));
  CheckVec4Near(launcher.palette.backgroundBottom,
                ImVec4(1.0f / 255.0f, 7.0f / 255.0f, 17.0f / 255.0f, 1.0f));
  CheckVec4Near(launcher.palette.panel, ImVec4(0.05f, 0.09f, 0.15f, 0.94f));
  CheckVec4Near(launcher.palette.panelSoft, ImVec4(0.07f, 0.11f, 0.18f, 0.90f));
  CheckVec4Near(launcher.palette.border, ImVec4(0.16f, 0.27f, 0.42f, 0.68f));
  CheckVec4Near(launcher.palette.textMuted, ImVec4(0.68f, 0.74f, 0.84f, 1.0f));
  CheckVec4Near(launcher.palette.accent, ImVec4(0.23f, 0.54f, 0.93f, 1.0f));
  CheckVec4Near(launcher.palette.accentHover, ImVec4(0.28f, 0.60f, 0.99f, 1.0f));
  CheckVec4Near(launcher.palette.accentActive, ImVec4(0.18f, 0.46f, 0.82f, 1.0f));
  CHECK(editor.palette.panel.w == 0.88f);
  CHECK(editor.density.panelPadding.x < launcher.density.panelPadding.x);
  CHECK(editor.rounding.panel < launcher.rounding.window);
  CHECK(launcher.rounding.window == 16.0f);
  CHECK(launcher.rounding.panel == 12.0f);
  CHECK(launcher.rounding.card == 10.0f);
  CHECK(launcher.rounding.button == 8.0f);
  CHECK(launcher.density.buttonPadding.x == 16.0f);
  CHECK(launcher.density.buttonPadding.y == 9.0f);
}

TEST_CASE("shared Horo theme preserves launcher accent hierarchy") {
  const LauncherTheme &launcher = Horo::Ui::GetLauncherTheme();

  CHECK(launcher.palette.accent.x > launcher.palette.panel.x);
  CHECK(launcher.palette.accentHover.x >= launcher.palette.accent.x);
  CHECK(launcher.palette.accentActive.z <= launcher.palette.accent.z);
  CHECK(launcher.palette.textMuted.w == 1.0f);
  CHECK(launcher.palette.border.w == 0.68f);
}

TEST_CASE("style scopes restore ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  {
    Horo::Ui::ScopedPanelStyle panel(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedCardStyle card(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedInputStyle input(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedButtonStyle primary(Horo::Ui::GetEditorTheme(), Horo::Ui::ButtonStyleVariant::Primary);
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedButtonStyle secondary(Horo::Ui::GetEditorTheme(), Horo::Ui::ButtonStyleVariant::Secondary);
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedComboStyle combo(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedComboStyle combo(Horo::Ui::GetLauncherTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  {
    Horo::Ui::ScopedEditorTreeRowStyle treeRows(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size > colorStackBefore);
    CHECK(context->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::DestroyContext(context);
}

TEST_CASE("combo style scope pushes expected style counts") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  {
    Horo::Ui::ScopedComboStyle combo(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size == colorStackBefore + 5);
    CHECK(context->StyleVarStack.Size == styleStackBefore + 4);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::DestroyContext(context);
}

TEST_CASE("editor tree row style scope pushes expected style counts") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  {
    Horo::Ui::ScopedEditorTreeRowStyle treeRows(Horo::Ui::GetEditorTheme());
    CHECK(context->ColorStack.Size == colorStackBefore + 3);
    CHECK(context->StyleVarStack.Size == styleStackBefore + 3);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::DestroyContext(context);
}

TEST_CASE("DrawEditorTreeItem restores ImGui style stacks (node)") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("tree-node-test");

  {
    Horo::Ui::ScopedEditorTreeRowStyle rowStyle(Horo::Ui::GetEditorTheme());
    const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    Horo::Ui::EditorTreeItemSpec spec;
    spec.id = "##test_node";
    spec.label = "Folder";
    spec.prefixIcon = "";
    spec.kind = Horo::Ui::EditorTreeItemKind::Node;
    spec.normalTextColor = &white;
    const auto res = Horo::Ui::DrawEditorTreeItem(Horo::Ui::GetEditorTheme(), spec);
    if (res.open)
      ImGui::TreePop();
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("DrawEditorTreeItem restores ImGui style stacks (leaf with suffix)") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("tree-leaf-test");

  {
    Horo::Ui::ScopedEditorTreeRowStyle rowStyle(Horo::Ui::GetEditorTheme());
    static const ImVec4 kFileColor(0.65f, 0.75f, 0.95f, 1.0f);
    Horo::Ui::EditorTreeItemSpec spec;
    spec.id = "##test_leaf";
    spec.label = "main.cpp";
    spec.prefixIcon = "";
    spec.suffixIcon = "";
    spec.kind = Horo::Ui::EditorTreeItemKind::Leaf;
    spec.normalTextColor = &kFileColor;
    spec.hoveredTextColor = &Horo::Ui::GetEditorTheme().palette.text;
    Horo::Ui::DrawEditorTreeItem(Horo::Ui::GetEditorTheme(), spec);
  }

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorTreeSearchSlot restores ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("search-slot-test");

  char buf[64] = {};
  Horo::Ui::EditorTreeSearchSlotConfig cfg;
  cfg.enabled = true;
  cfg.id = "##search_slot_test";
  cfg.placeholder = "Search...";
  cfg.buffer = buf;
  cfg.bufferSize = sizeof(buf);
  cfg.width = 200.0f;
  Horo::Ui::RenderEditorTreeSearchSlot(Horo::Ui::GetEditorTheme(), cfg);

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("shared widget wrappers restore ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("wrapper-test");

  Horo::Ui::Button(Horo::Ui::GetEditorTheme(),
                   Horo::Ui::ButtonStyleVariant::Primary,
                   "Apply##wrapper_test_button", ImVec2(120.0f, 0.0f));
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  char buffer[32] = "value";
  Horo::Ui::InputText(Horo::Ui::GetLauncherTheme(), "##wrapper_test_input",
                      buffer, sizeof(buffer));
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  Horo::Ui::InputTextWithHint(Horo::Ui::GetEditorTheme(),
                              "##wrapper_test_hint", "Search", buffer,
                              sizeof(buffer));
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  int currentItem = 0;
  const char *items[] = {"OpenGL", "Vulkan"};
  Horo::Ui::Combo(Horo::Ui::GetLauncherTheme(), "Renderer##wrapper_test_combo",
                  &currentItem, items, 2);
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("font path resolution returns empty for empty config") {
  FontFamilyConfig config{.relativePath = "", .size = 16.0f};
  FontResolutionResult result = Horo::Ui::ResolveFontPath(config);
  CHECK_FALSE(result.found);
  CHECK(result.resolvedPath.empty());
}

TEST_CASE("font path resolution returns not found for nonexistent font") {
  FontFamilyConfig config{.relativePath = "assets/fonts/NonExistentFont.ttf",
                          .size = 16.0f};
  FontResolutionResult result = Horo::Ui::ResolveFontPath(config);
  CHECK_FALSE(result.found);
}

TEST_CASE("font config stores relative path and size") {
  FontFamilyConfig config{.relativePath = "assets/fonts/InterVariable.ttf",
                          .size = 18.0f};
  CHECK(config.relativePath == "assets/fonts/InterVariable.ttf");
  CHECK(config.size == 18.0f);
}

TEST_CASE("font loading falls back to ImGui default font") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  FontFamilyConfig config{.relativePath = "assets/fonts/NonExistentFont.ttf",
                           .size = 16.0f};
  Horo::Ui::LoadFonts(ImGui::GetIO(), config);
  CHECK(ImGui::GetIO().FontDefault != nullptr);
  ImGui::DestroyContext(context);
}

TEST_CASE("font path resolution rejects absolute paths") {
  FontFamilyConfig config{.relativePath = "/etc/passwd", .size = 16.0f};
  FontResolutionResult result = Horo::Ui::ResolveFontPath(config);
  CHECK_FALSE(result.found);
  CHECK(result.resolvedPath.empty());
}

TEST_CASE("font path resolution rejects path traversal") {
  FontFamilyConfig config{.relativePath = "../etc/passwd", .size = 16.0f};
  FontResolutionResult result = Horo::Ui::ResolveFontPath(config);
  CHECK_FALSE(result.found);
  CHECK(result.resolvedPath.empty());

  FontFamilyConfig nestedConfig{.relativePath = "assets/../etc/passwd",
                                 .size = 16.0f};
  FontResolutionResult nestedResult =
      Horo::Ui::ResolveFontPath(nestedConfig);
  CHECK_FALSE(nestedResult.found);
  CHECK(nestedResult.resolvedPath.empty());
}

TEST_CASE("secondary and recent project button wrappers restore ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("button-test");

  Horo::Ui::RenderSecondaryButton(Horo::Ui::GetLauncherTheme(), "Secondary##test",
                                   ImVec2(120.0f, 0.0f));
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  Horo::Ui::RenderRecentProjectButton(Horo::Ui::GetLauncherTheme(),
                                      "Recent##test", ImVec2(120.0f, 0.0f));
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("EditorPanelTabLabel returns correct names for all tabs") {
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Scene)) == "Scene");
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Project)) == "Project");
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Viewport)) == "Viewport");
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Assets)) == "Assets");
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Console)) == "Console");
  CHECK(std::string(Horo::Ui::EditorPanelTabLabel(
            Horo::Ui::EditorPanelTab::Animation)) == "Animation");
}

TEST_CASE("RenderEditorPanelTopBar restores ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("topbar-test");

  const Horo::Ui::EditorPanelTabItem tabs[] = {
      {Horo::Ui::EditorPanelTab::Project, true},
  };
  const Horo::Ui::EditorPanelActionItem actions[] = {
      {"+"},
      {"..."},
  };
  Horo::Ui::RenderEditorPanelTopBar(
      Horo::Ui::GetEditorTheme(), "test_topbar", tabs, actions);

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorPanelTopBar returns -1 indices when nothing is clicked") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  ImGui::NewFrame();
  ImGui::Begin("topbar-no-click-test");

  const Horo::Ui::EditorPanelTabItem tabs[] = {
      {Horo::Ui::EditorPanelTab::Project, true},
  };
  const Horo::Ui::EditorPanelActionItem actions[] = {
      {"+"},
  };
  const Horo::Ui::EditorPanelTopBarResult result =
      Horo::Ui::RenderEditorPanelTopBar(
          Horo::Ui::GetEditorTheme(), "no_click", tabs, actions);

  CHECK(result.clickedTabIndex == -1);
  CHECK(result.clickedActionIndex == -1);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorPanelTopBar disabled actions are excluded") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  ImGui::NewFrame();
  ImGui::Begin("disabled-action-test");

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  const Horo::Ui::EditorPanelTabItem tabs[] = {
      {Horo::Ui::EditorPanelTab::Scene, true},
  };
  // Two actions: one enabled, one disabled.
  const Horo::Ui::EditorPanelActionItem actions[] = {
      {"+",   nullptr, {}, true},
      {"...", nullptr, {}, false},
  };
  const auto result = Horo::Ui::RenderEditorPanelTopBar(
      Horo::Ui::GetEditorTheme(), "disabled_action_topbar", tabs, actions);

  // Stack must be clean regardless of which buttons were skipped.
  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);
  // No action was clicked (nothing was interacted with).
  CHECK(result.clickedActionIndex == -1);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorPanelTopBar restores stacks with dropdown action") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  const int colorStackBefore = context->ColorStack.Size;
  const int styleStackBefore = context->StyleVarStack.Size;

  ImGui::NewFrame();
  ImGui::Begin("topbar-dropdown-test");

  bool triggered = false;
  const Horo::Ui::EditorPanelDropdownItem dropItems[] = {
      {nullptr, "Alpha", [&triggered] { triggered = true; }},
      {nullptr, "Beta",  [&triggered] { triggered = true; }},
  };
  const Horo::Ui::EditorPanelTabItem tabs[] = {
      {Horo::Ui::EditorPanelTab::Scene, true},
  };
  const Horo::Ui::EditorPanelActionItem actions[] = {
      {"+", nullptr, dropItems},
  };
  Horo::Ui::RenderEditorPanelTopBar(
      Horo::Ui::GetEditorTheme(), "dropdown_topbar", tabs, actions);

  CHECK(context->ColorStack.Size == colorStackBefore);
  CHECK(context->StyleVarStack.Size == styleStackBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

// ─── Group A: Modal & picker primitives ───────────────────────────────────────

TEST_CASE("BeginEditorModal returns false when not opened") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("modal-test-window");

  // Not opened this frame → BeginEditorModal must return false (popup not open)
  Horo::Ui::EditorModalConfig cfg{"modal_test", 400.0f, true};
  const bool open = Horo::Ui::BeginEditorModal(cfg, false);
  CHECK_FALSE(open);
  // EndEditorModal must NOT be called when BeginEditorModal returns false

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorModalFooter result defaults to all-false") {
  // Verify the result struct default state
  Horo::Ui::EditorModalFooterResult r;
  CHECK_FALSE(r.confirmed);
  CHECK_FALSE(r.cancelled);
  CHECK_FALSE(r.alternate);
}

TEST_CASE("RenderEditorModalFooter renders without stack leak (OkCancel)") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("footer-test-window");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  const auto &theme = Horo::Ui::GetEditorTheme();
  Horo::Ui::RenderEditorModalFooter(theme, "OK");

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorModalFooter renders without stack leak (DestructiveCancel)") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("footer-destructive-window");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  const auto &theme = Horo::Ui::GetEditorTheme();
  Horo::Ui::RenderEditorModalFooter(
      theme, "Delete",
      Horo::Ui::EditorModalFooterStyle::DestructiveCancel);

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorModalFooter renders without stack leak (ThreeWay)") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("footer-threeway-window");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  const auto &theme = Horo::Ui::GetEditorTheme();
  Horo::Ui::RenderEditorModalFooter(
      theme, "Save & Continue",
      Horo::Ui::EditorModalFooterStyle::ThreeWay, "Discard");

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

// ─── Group B: Input field primitives ──────────────────────────────────────────

TEST_CASE("RenderEditorLabeledInput does not crash with empty buf") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("labeled-input-test");

  char buf[64] = "initial";
  const bool changed = Horo::Ui::RenderEditorLabeledInput(
      "Asset ID", "##test_labeled", buf, sizeof(buf));
  // Not interacted → not changed
  CHECK_FALSE(changed);
  // Buffer should be untouched
  CHECK(std::string(buf) == "initial");

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorLabeledInput with hint does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("labeled-input-hint-test");

  char buf[64] = {};
  REQUIRE_NOTHROW(Horo::Ui::RenderEditorLabeledInput(
      "Name", "##test_hint", buf, sizeof(buf), 0.0f, "Enter name..."));

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorCheckbox does not crash and restores stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("checkbox-test");

  const int colorsBefore = context->ColorStack.Size;
  const auto &theme = Horo::Ui::GetEditorTheme();
  bool value = false;
  Horo::Ui::RenderEditorCheckbox(theme, "Enable Feature", value, "Tooltip text");

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK_FALSE(value); // not interacted

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorToggle does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("toggle-test");

  const auto &theme = Horo::Ui::GetEditorTheme();
  bool value = false;
  REQUIRE_NOTHROW(
      Horo::Ui::RenderEditorToggle(theme, "##mcp_toggle", "Enable MCP", value));
  CHECK_FALSE(value);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorDragFloat does not crash and returns false without interaction") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("drag-float-test");

  float value = 3.14f;
  const bool changed = Horo::Ui::RenderEditorDragFloat(
      "Mass", "##mass", value, 0.1f, 0.0f, 100.0f);
  CHECK_FALSE(changed);
  CHECK(value == 3.14f);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorDragFloat3 does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("drag-float3-test");

  float vec[3] = {1.0f, 2.0f, 3.0f};
  const bool changed = Horo::Ui::RenderEditorDragFloat3(
      "Position", "##pos", vec);
  CHECK_FALSE(changed);
  CHECK(vec[0] == 1.0f);
  CHECK(vec[1] == 2.0f);
  CHECK(vec[2] == 3.0f);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorSliderFloat does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("slider-float-test");

  float fov = 60.0f;
  REQUIRE_NOTHROW(Horo::Ui::RenderEditorSliderFloat(
      "FOV", "##fov", fov, 1.0f, 179.0f));
  CHECK(fov == 60.0f);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorColorEdit3 does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("color-edit-test");

  float color[3] = {1.0f, 0.5f, 0.0f};
  REQUIRE_NOTHROW(Horo::Ui::RenderEditorColorEdit3("Color", "##col", color));
  CHECK(color[0] == 1.0f);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("BeginEditorPropertyRow and EndEditorPropertyRow do not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("property-row-test");

  REQUIRE_NOTHROW([&] {
    Horo::Ui::BeginEditorPropertyRow("Mesh", 120.0f);
    ImGui::TextUnformatted("value");
    Horo::Ui::EndEditorPropertyRow();
  }());

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

// ─── Group C: Card and status primitives ──────────────────────────────────────

TEST_CASE("BeginEditorCard and EndEditorCard do not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("card-test");

  const auto &theme = Horo::Ui::GetEditorTheme();
  Horo::Ui::EditorCardConfig cfg{"##testcard", 120.0f, 80.0f, false, false};
  if (Horo::Ui::BeginEditorCard(theme, cfg))
      Horo::Ui::EndEditorCard();

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("BeginEditorCard with selected=true does not crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("card-selected-test");

  const auto &theme = Horo::Ui::GetEditorTheme();
  Horo::Ui::EditorCardConfig cfg{"##selcard", 120.0f, 80.0f, true, false};
  if (Horo::Ui::BeginEditorCard(theme, cfg))
      Horo::Ui::EndEditorCard();

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorStatusText renders each level without crash") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();
  ImGui::NewFrame();
  ImGui::Begin("status-text-test");

  const auto &theme = Horo::Ui::GetEditorTheme();
  REQUIRE_NOTHROW([&] {
    Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Info,    "info %d", 1);
    Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Warning, "warn %s", "x");
    Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Error,   "err");
    Horo::Ui::RenderEditorStatusText(theme, Horo::Ui::EditorStatusLevel::Success, "ok");
  }());

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}


// ─── Editor theme presets ─────────────────────────────────────────────────────

TEST_CASE("editor theme preset ids are stable and match parser") {
  using Horo::Ui::EditorThemePreset;
  CHECK(std::string_view(Horo::Ui::EditorThemePresetId(
            EditorThemePreset::DarkBlue)) == "darkBlue");
  CHECK(std::string_view(Horo::Ui::EditorThemePresetId(
            EditorThemePreset::Graphite)) == "graphite");
  CHECK(std::string_view(Horo::Ui::EditorThemePresetId(
            EditorThemePreset::HighContrast)) == "highContrast");

  CHECK(std::string_view(Horo::Ui::EditorThemePresetLabel(
            EditorThemePreset::DarkBlue)) == "Dark Blue");
  CHECK(std::string_view(Horo::Ui::EditorThemePresetLabel(
            EditorThemePreset::Graphite)) == "Graphite");
  CHECK(std::string_view(Horo::Ui::EditorThemePresetLabel(
            EditorThemePreset::HighContrast)) == "High Contrast");

  bool ok = false;
  CHECK(Horo::Ui::ParseEditorThemePreset("darkBlue", &ok) ==
        EditorThemePreset::DarkBlue);
  CHECK(ok);
  CHECK(Horo::Ui::ParseEditorThemePreset("graphite", &ok) ==
        EditorThemePreset::Graphite);
  CHECK(ok);
  CHECK(Horo::Ui::ParseEditorThemePreset("highContrast", &ok) ==
        EditorThemePreset::HighContrast);
  CHECK(ok);
}

TEST_CASE("editor theme preset parser rejects unknown ids and falls back to DarkBlue") {
  using Horo::Ui::EditorThemePreset;
  bool ok = true;
  CHECK(Horo::Ui::ParseEditorThemePreset("unknownTheme", &ok) ==
        EditorThemePreset::DarkBlue);
  CHECK_FALSE(ok);

  // Empty string must not throw and must still return DarkBlue.
  ok = true;
  CHECK(Horo::Ui::ParseEditorThemePreset("", &ok) ==
        EditorThemePreset::DarkBlue);
  CHECK_FALSE(ok);

  // Case sensitivity: "DarkBlue" is not a valid id.
  ok = true;
  CHECK(Horo::Ui::ParseEditorThemePreset("DarkBlue", &ok) ==
        EditorThemePreset::DarkBlue);
  CHECK_FALSE(ok);
}

TEST_CASE("EditorThemePresets() enumerates all three presets in display order") {
  using Horo::Ui::EditorThemePreset;
  const auto presets = Horo::Ui::EditorThemePresets();
  REQUIRE(presets.size() == 3);
  CHECK(presets[0] == EditorThemePreset::DarkBlue);
  CHECK(presets[1] == EditorThemePreset::Graphite);
  CHECK(presets[2] == EditorThemePreset::HighContrast);
}

TEST_CASE("SetEditorThemePreset/GetEditorThemePreset round-trip") {
  using Horo::Ui::EditorThemePreset;
  // Snapshot and restore to avoid leaking state to later tests.
  const EditorThemePreset previous = Horo::Ui::GetEditorThemePreset();
  Horo::Ui::SetEditorThemePreset(EditorThemePreset::Graphite);
  CHECK(Horo::Ui::GetEditorThemePreset() == EditorThemePreset::Graphite);
  Horo::Ui::SetEditorThemePreset(EditorThemePreset::HighContrast);
  CHECK(Horo::Ui::GetEditorThemePreset() == EditorThemePreset::HighContrast);
  Horo::Ui::SetEditorThemePreset(EditorThemePreset::DarkBlue);
  CHECK(Horo::Ui::GetEditorThemePreset() == EditorThemePreset::DarkBlue);
  Horo::Ui::SetEditorThemePreset(previous);
}

TEST_CASE("DarkBlue preset preserves the canonical editor palette") {
  using Horo::Ui::EditorThemePreset;
  const EditorThemePreset previous = Horo::Ui::GetEditorThemePreset();
  Horo::Ui::SetEditorThemePreset(EditorThemePreset::DarkBlue);
  const auto &theme = Horo::Ui::GetEditorTheme();
  CheckVec4Near(theme.palette.accent, ImVec4(0.23f, 0.54f, 0.93f, 1.0f));
  CheckVec4Near(theme.palette.textMuted, ImVec4(0.68f, 0.74f, 0.84f, 1.0f));
  CheckVec4Near(theme.palette.border, ImVec4(0.16f, 0.27f, 0.42f, 0.68f));
  CHECK(theme.palette.panel.w == 0.88f);
  Horo::Ui::SetEditorThemePreset(previous);
}

TEST_CASE("alternative presets differ from DarkBlue on key tokens") {
  using Horo::Ui::EditorThemePreset;
  const EditorThemePreset previous = Horo::Ui::GetEditorThemePreset();

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::DarkBlue);
  const HoroPalette darkBlue = Horo::Ui::GetEditorTheme().palette;

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::Graphite);
  const HoroPalette graphite = Horo::Ui::GetEditorTheme().palette;

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::HighContrast);
  const HoroPalette hc = Horo::Ui::GetEditorTheme().palette;

  const auto differs = [](const ImVec4 &a, const ImVec4 &b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) >
           0.05f;
  };

  CHECK(differs(graphite.panel, darkBlue.panel));
  CHECK(differs(graphite.card, darkBlue.card));
  CHECK(differs(graphite.border, darkBlue.border));
  CHECK(differs(graphite.accent, darkBlue.accent));
  CHECK(differs(graphite.selection, darkBlue.selection));

  CHECK(differs(hc.panel, darkBlue.panel));
  CHECK(differs(hc.card, darkBlue.card));
  CHECK(differs(hc.border, darkBlue.border));
  CHECK(differs(hc.accent, darkBlue.accent));
  CHECK(differs(hc.selection, darkBlue.selection));

  Horo::Ui::SetEditorThemePreset(previous);
}

TEST_CASE("ApplyEditorTheme maps the selected preset palette into ImGuiStyle") {
  using Horo::Ui::EditorThemePreset;
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);

  const EditorThemePreset previous = Horo::Ui::GetEditorThemePreset();

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::Graphite);
  Horo::Ui::ApplyEditorTheme(ImGui::GetStyle());
  const auto graphite = Horo::Ui::GetEditorTheme().palette;
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Button], graphite.card);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Header], graphite.selection);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], graphite.accent);

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::HighContrast);
  Horo::Ui::ApplyEditorTheme(ImGui::GetStyle());
  const auto hc = Horo::Ui::GetEditorTheme().palette;
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Button], hc.card);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Header], hc.selection);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], hc.accent);

  Horo::Ui::SetEditorThemePreset(EditorThemePreset::DarkBlue);
  Horo::Ui::ApplyEditorTheme(ImGui::GetStyle());
  const auto db = Horo::Ui::GetEditorTheme().palette;
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Button], db.card);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_Header], db.selection);
  CheckVec4Near(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], db.accent);

  Horo::Ui::SetEditorThemePreset(previous);
  ImGui::DestroyContext(context);
}


// ─── Settings modal primitives ────────────────────────────────────────────────

TEST_CASE("RenderEditorVerticalTabs restores ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  ImGui::NewFrame();
  ImGui::Begin("vtab-test");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  const Horo::Ui::EditorVerticalTabItem items[] = {
      {"mcp",        nullptr, "MCP",        "Built-in server",    true},
      {"appearance", nullptr, "Appearance", "Theme presets",      false},
  };
  const auto result = Horo::Ui::RenderEditorVerticalTabs(
      Horo::Ui::GetEditorTheme(), "##vtabs", items, 220.0f);
  CHECK(result.clickedIndex == -1);

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("BeginEditorSettingsCard restores ImGui style stacks") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  ImGui::NewFrame();
  ImGui::Begin("settings-card-test");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  if (Horo::Ui::BeginEditorSettingsCard(
          Horo::Ui::GetEditorTheme(), "##server_card", "Server")) {
    Horo::Ui::RenderEditorSettingText(
        Horo::Ui::GetEditorTheme(), "Host", "127.0.0.1");
  }
  Horo::Ui::EndEditorSettingsCard();

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}

TEST_CASE("RenderEditorSettingsFooter restores ImGui style stacks and does not close popup") {
  ImGuiContext *context = ImGui::CreateContext();
  REQUIRE(context != nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);
  io.Fonts->Build();

  ImGui::NewFrame();
  ImGui::Begin("settings-footer-test");

  const int colorsBefore = context->ColorStack.Size;
  const int stylesBefore = context->StyleVarStack.Size;

  // canApply == true (Apply is enabled)
  const auto enabledResult = Horo::Ui::RenderEditorSettingsFooter(
      Horo::Ui::GetEditorTheme(), /*canApply=*/true);
  CHECK_FALSE(enabledResult.cancelled);
  CHECK_FALSE(enabledResult.applied);
  CHECK_FALSE(enabledResult.accepted);

  // canApply == false (Apply is disabled)
  const auto disabledResult = Horo::Ui::RenderEditorSettingsFooter(
      Horo::Ui::GetEditorTheme(), /*canApply=*/false);
  CHECK_FALSE(disabledResult.cancelled);
  CHECK_FALSE(disabledResult.applied);
  CHECK_FALSE(disabledResult.accepted);

  CHECK(context->ColorStack.Size == colorsBefore);
  CHECK(context->StyleVarStack.Size == stylesBefore);

  // Settings footer must NOT have called ImGui::CloseCurrentPopup(). We verify
  // by checking that the popup stack is empty: if the footer had called
  // CloseCurrentPopup on a non-existent popup it would assert.
  CHECK(context->OpenPopupStack.Size == 0);

  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext(context);
}
