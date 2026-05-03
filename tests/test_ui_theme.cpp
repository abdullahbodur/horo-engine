#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"

using Horo::Ui::EditorTheme;
using Horo::Ui::LauncherTheme;

TEST_CASE("shared Horo theme exposes launcher and editor density variants") {
  const LauncherTheme &launcher = Horo::Ui::GetLauncherTheme();
  const EditorTheme &editor = Horo::Ui::GetEditorTheme();

  CHECK(launcher.palette.panel.w == 0.82f);
  CHECK(editor.palette.panel.w == 0.88f);
  CHECK(editor.density.panelPadding.x < launcher.density.panelPadding.x);
  CHECK(editor.rounding.panel <= launcher.rounding.panel);
}

TEST_CASE("shared Horo theme preserves launcher accent hierarchy") {
  const LauncherTheme &launcher = Horo::Ui::GetLauncherTheme();

  CHECK(launcher.palette.accent.x > launcher.palette.panel.x);
  CHECK(launcher.palette.accentHover.x >= launcher.palette.accent.x);
  CHECK(launcher.palette.accentActive.z <= launcher.palette.accent.z);
  CHECK(launcher.palette.textMuted.w == 1.0f);
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
