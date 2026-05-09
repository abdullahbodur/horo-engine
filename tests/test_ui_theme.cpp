#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
#include "ui/UiFonts.h"

using Horo::Ui::EditorTheme;
using Horo::Ui::FontFamilyConfig;
using Horo::Ui::FontResolutionResult;
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
