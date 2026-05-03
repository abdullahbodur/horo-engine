#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"

using Horo::Ui::EditorTheme;
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

  ImGui::DestroyContext(context);
}
