# Editor Panel Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared `ui/` theme/component module and use it to align editor chrome with the refreshed launcher palette while preserving editor density.

**Architecture:** Create a small shared ImGui UI layer with palette/density tokens and scoped style helpers. Move launcher theme constants to the shared source without changing launcher layout, then apply compact editor variants to toolbar, status bar, hierarchy, assets, properties, bottom dock, command palette, quick-open, and settings/modal surfaces. Keep product behavior in `launcher/` and `editor/`; only common visual primitives live in `ui/`.

**Tech Stack:** C++20, Dear ImGui, CMake/Ninja, Catch2-style unit tests, Horo UI automation (`HoroEditorUiTest --run-ui-tests`).

---

## File Structure

- Create `ui/HoroTheme.h`: shared palette/density/rounding data types and getter functions.
- Create `ui/HoroTheme.cpp`: launcher-inspired token values and compact editor density values.
- Create `ui/UiComponents.h`: small scoped ImGui style helpers for panel, card, modal, input, and button styling.
- Create `ui/UiComponents.cpp`: helper implementations.
- Modify `CMakeLists.txt`: include `ui/*.cpp` in `ENGINE_SOURCES`.
- Modify `tests/CMakeLists.txt`: register `test_ui_theme`.
- Create `tests/test_ui_theme.cpp`: regression tests for shared theme tokens and style-scope stack balance.
- Modify `launcher/LauncherEditorShell.cpp`: replace local launcher theme struct/getter with shared `Horo::Ui` tokens and use shared component helpers where they are an exact behavior-preserving match.
- Modify `editor/EditorLayer.cpp`: apply shared editor chrome styles to toolbar/status/hierarchy/assets/bottom dock/modals.
- Modify `editor/EditorPropertiesPanel.cpp`: apply shared properties panel/section styling.

Do not move existing large files into `ui/` in this plan.

---

## Task 1: Add Shared Theme Token Module

**Files:**
- Create: `ui/HoroTheme.h`
- Create: `ui/HoroTheme.cpp`
- Create: `tests/test_ui_theme.cpp`
- Modify: `CMakeLists.txt:55-67`, `tests/CMakeLists.txt:286-290`

- [ ] **Step 1: Write the failing theme token test**

Create `tests/test_ui_theme.cpp` with this content:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "ui/HoroTheme.h"

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
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build --preset debug --target test_ui_theme --parallel
```

Expected: FAIL because `ui/HoroTheme.h` and `test_ui_theme` do not exist yet.

- [ ] **Step 3: Add shared theme header**

Create `ui/HoroTheme.h`:

```cpp
#pragma once

#include <imgui.h>

namespace Horo::Ui {

struct HoroPalette {
  ImVec4 backgroundTop{};
  ImVec4 backgroundBottom{};
  ImVec4 panel{};
  ImVec4 panelSoft{};
  ImVec4 card{};
  ImVec4 cardHover{};
  ImVec4 border{};
  ImVec4 text{};
  ImVec4 textMuted{};
  ImVec4 accent{};
  ImVec4 accentHover{};
  ImVec4 accentActive{};
  ImVec4 selection{};
  ImVec4 selectionHover{};
  ImVec4 input{};
  ImVec4 inputHover{};
  ImVec4 inputActive{};
  ImVec4 modal{};
  ImVec4 destructive{};
};

struct HoroRounding {
  float panel = 0.0f;
  float card = 0.0f;
  float button = 0.0f;
  float input = 0.0f;
  float tab = 0.0f;
};

struct HoroDensity {
  ImVec2 panelPadding{};
  ImVec2 cardPadding{};
  ImVec2 buttonPadding{};
  ImVec2 inputPadding{};
  float itemSpacing = 0.0f;
};

struct LauncherTheme {
  HoroPalette palette{};
  HoroRounding rounding{};
  HoroDensity density{};
};

struct EditorTheme {
  HoroPalette palette{};
  HoroRounding rounding{};
  HoroDensity density{};
};

const LauncherTheme &GetLauncherTheme();
const EditorTheme &GetEditorTheme();

} // namespace Horo::Ui
```

- [ ] **Step 4: Add shared theme implementation**

Create `ui/HoroTheme.cpp`:

```cpp
#include "ui/HoroTheme.h"

namespace Horo::Ui {
namespace {

constexpr HoroPalette kPalette{
    .backgroundTop = ImVec4(0.02f, 0.04f, 0.08f, 1.0f),
    .backgroundBottom = ImVec4(0.01f, 0.02f, 0.05f, 1.0f),
    .panel = ImVec4(0.06f, 0.10f, 0.16f, 0.82f),
    .panelSoft = ImVec4(0.08f, 0.13f, 0.20f, 0.64f),
    .card = ImVec4(0.05f, 0.10f, 0.17f, 0.76f),
    .cardHover = ImVec4(0.08f, 0.15f, 0.25f, 0.84f),
    .border = ImVec4(0.15f, 0.24f, 0.35f, 0.64f),
    .text = ImVec4(0.96f, 0.98f, 1.0f, 1.0f),
    .textMuted = ImVec4(0.62f, 0.69f, 0.78f, 1.0f),
    .accent = ImVec4(0.20f, 0.41f, 0.68f, 1.0f),
    .accentHover = ImVec4(0.24f, 0.46f, 0.75f, 1.0f),
    .accentActive = ImVec4(0.18f, 0.36f, 0.62f, 1.0f),
    .selection = ImVec4(0.14f, 0.31f, 0.52f, 0.92f),
    .selectionHover = ImVec4(0.18f, 0.37f, 0.60f, 0.96f),
    .input = ImVec4(0.04f, 0.08f, 0.13f, 1.0f),
    .inputHover = ImVec4(0.06f, 0.11f, 0.18f, 1.0f),
    .inputActive = ImVec4(0.08f, 0.15f, 0.25f, 1.0f),
    .modal = ImVec4(0.05f, 0.09f, 0.15f, 0.98f),
    .destructive = ImVec4(0.95f, 0.35f, 0.32f, 1.0f),
};

constexpr HoroRounding kLauncherRounding{
    .panel = 12.0f,
    .card = 8.0f,
    .button = 8.0f,
    .input = 6.0f,
    .tab = 6.0f,
};

constexpr HoroDensity kLauncherDensity{
    .panelPadding = ImVec2(18.0f, 18.0f),
    .cardPadding = ImVec2(16.0f, 14.0f),
    .buttonPadding = ImVec2(16.0f, 9.0f),
    .inputPadding = ImVec2(12.0f, 9.0f),
    .itemSpacing = 8.0f,
};

constexpr HoroRounding kEditorRounding{
    .panel = 7.0f,
    .card = 6.0f,
    .button = 5.0f,
    .input = 5.0f,
    .tab = 5.0f,
};

constexpr HoroDensity kEditorDensity{
    .panelPadding = ImVec2(10.0f, 8.0f),
    .cardPadding = ImVec2(10.0f, 8.0f),
    .buttonPadding = ImVec2(9.0f, 5.0f),
    .inputPadding = ImVec2(8.0f, 5.0f),
    .itemSpacing = 5.0f,
};

} // namespace

const LauncherTheme &GetLauncherTheme() {
  static const LauncherTheme theme{
      .palette = kPalette,
      .rounding = kLauncherRounding,
      .density = kLauncherDensity,
  };
  return theme;
}

const EditorTheme &GetEditorTheme() {
  static const EditorTheme theme{
      .palette = HoroPalette{
          .backgroundTop = kPalette.backgroundTop,
          .backgroundBottom = kPalette.backgroundBottom,
          .panel = ImVec4(kPalette.panel.x, kPalette.panel.y, kPalette.panel.z,
                          0.88f),
          .panelSoft = kPalette.panelSoft,
          .card = kPalette.card,
          .cardHover = kPalette.cardHover,
          .border = kPalette.border,
          .text = kPalette.text,
          .textMuted = kPalette.textMuted,
          .accent = kPalette.accent,
          .accentHover = kPalette.accentHover,
          .accentActive = kPalette.accentActive,
          .selection = kPalette.selection,
          .selectionHover = kPalette.selectionHover,
          .input = kPalette.input,
          .inputHover = kPalette.inputHover,
          .inputActive = kPalette.inputActive,
          .modal = kPalette.modal,
          .destructive = kPalette.destructive,
      },
      .rounding = kEditorRounding,
      .density = kEditorDensity,
  };
  return theme;
}

} // namespace Horo::Ui
```

- [ ] **Step 5: Add `ui/*.cpp` to engine sources**

Modify `CMakeLists.txt` `ENGINE_SOURCES` glob block to include `ui`:

```cmake
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
)
```

Add this block after `test_ui_automation_config` in `tests/CMakeLists.txt`:

```cmake
# ---- test_ui_theme ----
add_executable(test_ui_theme test_ui_theme.cpp)
target_link_libraries(test_ui_theme PRIVATE ${HORO_ENGINE_LINK} Catch2::Catch2WithMain)
horo_set_test_output_dir(test_ui_theme)
add_test(NAME test_ui_theme COMMAND $<TARGET_FILE:test_ui_theme>)
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build --preset debug --target test_ui_theme --parallel
ctest --preset debug --output-on-failure -R "^test_ui_theme$"
```

Expected: build succeeds and `test_ui_theme` passes.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt tests/CMakeLists.txt ui/HoroTheme.h ui/HoroTheme.cpp tests/test_ui_theme.cpp
git commit -m "feat(ui): add shared Horo theme tokens"
```

---

## Task 2: Add Shared ImGui Component Style Helpers

**Files:**
- Create: `ui/UiComponents.h`
- Create: `ui/UiComponents.cpp`
- Modify: `tests/test_ui_theme.cpp`

- [ ] **Step 1: Write failing style stack tests**

Append to `tests/test_ui_theme.cpp`:

```cpp
#include <imgui.h>

#include "ui/UiComponents.h"

TEST_CASE("style scopes restore ImGui style stacks") {
  ImGui::CreateContext();
  const int colorStackBefore = ImGui::GetCurrentContext()->ColorStack.Size;
  const int styleStackBefore = ImGui::GetCurrentContext()->StyleVarStack.Size;

  {
    Horo::Ui::ScopedPanelStyle panel(Horo::Ui::GetEditorTheme());
    CHECK(ImGui::GetCurrentContext()->ColorStack.Size > colorStackBefore);
    CHECK(ImGui::GetCurrentContext()->StyleVarStack.Size > styleStackBefore);
  }

  CHECK(ImGui::GetCurrentContext()->ColorStack.Size == colorStackBefore);
  CHECK(ImGui::GetCurrentContext()->StyleVarStack.Size == styleStackBefore);
  ImGui::DestroyContext();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build --preset debug --target test_ui_theme --parallel
```

Expected: FAIL because `ui/UiComponents.h` does not exist.

- [ ] **Step 3: Add component helper header**

Create `ui/UiComponents.h`:

```cpp
#pragma once

#include <imgui.h>

#include "ui/HoroTheme.h"

namespace Horo::Ui {

class ScopedPanelStyle {
public:
  explicit ScopedPanelStyle(const EditorTheme &theme);
  explicit ScopedPanelStyle(const LauncherTheme &theme);
  ~ScopedPanelStyle();

  ScopedPanelStyle(const ScopedPanelStyle &) = delete;
  ScopedPanelStyle &operator=(const ScopedPanelStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

class ScopedCardStyle {
public:
  explicit ScopedCardStyle(const EditorTheme &theme);
  explicit ScopedCardStyle(const LauncherTheme &theme);
  ~ScopedCardStyle();

  ScopedCardStyle(const ScopedCardStyle &) = delete;
  ScopedCardStyle &operator=(const ScopedCardStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

class ScopedInputStyle {
public:
  explicit ScopedInputStyle(const EditorTheme &theme);
  explicit ScopedInputStyle(const LauncherTheme &theme);
  ~ScopedInputStyle();

  ScopedInputStyle(const ScopedInputStyle &) = delete;
  ScopedInputStyle &operator=(const ScopedInputStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

void PushPrimaryButtonStyle(const EditorTheme &theme);
void PushPrimaryButtonStyle(const LauncherTheme &theme);
void PushSecondaryButtonStyle(const EditorTheme &theme);
void PushSecondaryButtonStyle(const LauncherTheme &theme);
void PopButtonStyle();

void TextMuted(const char *text);
void SectionHeader(const char *title);

} // namespace Horo::Ui
```

- [ ] **Step 4: Add component helper implementation**

Create `ui/UiComponents.cpp`:

```cpp
#include "ui/UiComponents.h"

namespace Horo::Ui {
namespace {

void PushPanelColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  *colorCount += 3;
}

void PushPanelVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.panel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.panelPadding);
  *styleCount += 3;
}

void PushCardColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.card);
  ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
  ImGui::PushStyleColor(ImGuiCol_Header, palette.selection);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.selectionHover);
  *colorCount += 4;
}

void PushCardVars(const HoroRounding &rounding, const HoroDensity &density,
                  int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding.card);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, density.cardPadding);
  *styleCount += 2;
}

void PushInputColors(const HoroPalette &palette, int *colorCount) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.input);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.inputHover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.inputActive);
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  *colorCount += 4;
}

void PushInputVars(const HoroRounding &rounding, const HoroDensity &density,
                   int *styleCount) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding.input);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, density.inputPadding);
  *styleCount += 2;
}

} // namespace

ScopedPanelStyle::ScopedPanelStyle(const EditorTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedPanelStyle::ScopedPanelStyle(const LauncherTheme &theme) {
  PushPanelColors(theme.palette, &m_colorCount);
  PushPanelVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedPanelStyle::~ScopedPanelStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

ScopedCardStyle::ScopedCardStyle(const EditorTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedCardStyle::ScopedCardStyle(const LauncherTheme &theme) {
  PushCardColors(theme.palette, &m_colorCount);
  PushCardVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedCardStyle::~ScopedCardStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

ScopedInputStyle::ScopedInputStyle(const EditorTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedInputStyle::ScopedInputStyle(const LauncherTheme &theme) {
  PushInputColors(theme.palette, &m_colorCount);
  PushInputVars(theme.rounding, theme.density, &m_styleCount);
}

ScopedInputStyle::~ScopedInputStyle() {
  ImGui::PopStyleVar(m_styleCount);
  ImGui::PopStyleColor(m_colorCount);
}

void PushPrimaryButtonStyle(const EditorTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushPrimaryButtonStyle(const LauncherTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.accentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushSecondaryButtonStyle(const EditorTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PushSecondaryButtonStyle(const LauncherTheme &theme) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
  ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.panelSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.selection);
  ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
}

void PopButtonStyle() {
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
}

void TextMuted(const char *text) {
  ImGui::TextColored(GetEditorTheme().palette.textMuted, "%s", text);
}

void SectionHeader(const char *title) {
  ImGui::TextColored(GetEditorTheme().palette.text, "%s", title);
  ImGui::Separator();
}

} // namespace Horo::Ui
```

- [ ] **Step 5: Run test to verify it passes**

Run:

```bash
cmake --build --preset debug --target test_ui_theme --parallel
ctest --preset debug --output-on-failure -R "^test_ui_theme$"
```

Expected: `test_ui_theme` passes and style stack counts are restored.

- [ ] **Step 6: Commit Task 2**

```bash
git add ui/UiComponents.h ui/UiComponents.cpp tests/test_ui_theme.cpp
git commit -m "feat(ui): add shared ImGui style helpers"
```

---

## Task 3: Wire Launcher to Shared Theme Without Visual Drift

**Files:**
- Modify: `launcher/LauncherEditorShell.cpp:261-554`, style helper call sites in launcher render helpers
- Test: existing `launcher-basic` UI automation

- [ ] **Step 1: Capture current launcher baseline screenshot**

Run:

```bash
mkdir -p ui_test_output/horo38-baseline
HORO_UI_TEST_SUITE=launcher-basic \
HORO_UI_TEST_FILTER='launcher/launcher_home_layout' \
HORO_UI_TEST_CAPTURE=1 \
HORO_UI_TEST_VIDEO=0 \
HORO_UI_TEST_RECORDING=1 \
HORO_UI_TEST_OUTPUT_DIR="$PWD/ui_test_output/horo38-baseline" \
HORO_LOG_LEVEL=info \
HORO_GLFW_SAMPLES=0 \
build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: PASS and a launcher home screenshot in `ui_test_output/horo38-baseline`.

- [ ] **Step 2: Replace local launcher theme type with shared include**

In `launcher/LauncherEditorShell.cpp`, add:

```cpp
#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
```

Remove the local `LauncherTheme` struct and local `GetLauncherTheme()` function. Replace references with:

```cpp
const Ui::LauncherTheme &theme = Ui::GetLauncherTheme();
```

If local code expects direct fields such as `theme.panel`, update to nested fields:

```cpp
theme.palette.panel
theme.palette.panelSoft
theme.palette.border
theme.palette.textMuted
theme.palette.accent
theme.palette.accentHover
theme.palette.accentActive
theme.rounding.panel
theme.rounding.card
```

- [ ] **Step 3: Replace exact-matching launcher style helpers**

Where launcher helper functions push the same primary/secondary button colors as the shared helpers, replace their style bodies with:

```cpp
bool RenderPrimaryButton(const char *label, const ImVec2 &size) {
  const Ui::LauncherTheme &theme = Ui::GetLauncherTheme();
  Ui::PushPrimaryButtonStyle(theme);
  const bool pressed = ImGui::Button(label, size);
  Ui::PopButtonStyle();
  return pressed;
}
```

For secondary buttons:

```cpp
bool RenderSecondaryButton(const char *label, const ImVec2 &size) {
  const Ui::LauncherTheme &theme = Ui::GetLauncherTheme();
  Ui::PushSecondaryButtonStyle(theme);
  const bool pressed = ImGui::Button(label, size);
  Ui::PopButtonStyle();
  return pressed;
}
```

Do not convert custom draw-list cards yet unless the shared helper is an exact match; keep custom launcher visuals stable.

- [ ] **Step 4: Build and run launcher checks**

Run:

```bash
cmake --build --preset debug --target HoroEditorUiTest test_launcher_core test_launcher_unit --parallel
ctest --preset debug --output-on-failure -R "launcher|ui_theme"
HORO_UI_TEST_SUITE=launcher-basic \
HORO_UI_TEST_CAPTURE=1 \
HORO_UI_TEST_VIDEO=0 \
HORO_UI_TEST_RECORDING=1 \
HORO_UI_TEST_OUTPUT_DIR="$PWD/ui_test_output/horo38-launcher" \
HORO_LOG_LEVEL=info \
HORO_GLFW_SAMPLES=0 \
build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: build succeeds; tests pass; launcher-basic reports `tests_succeeded=20`.

- [ ] **Step 5: Visually inspect launcher screenshot**

Open the baseline and new screenshots or use browser/image preview. Confirm:

- Home layout still uses the refreshed launcher design.
- Buttons and cards have no obvious color drift.
- Text contrast remains readable.

- [ ] **Step 6: Commit Task 3**

```bash
git add launcher/LauncherEditorShell.cpp
git commit -m "refactor(launcher): use shared Horo theme tokens"
```

---

## Task 4: Apply Shared Theme to Editor Global Style and Chrome

**Files:**
- Modify: `editor/EditorLayer.cpp:1760-1776`, `editor/EditorLayer.cpp:2195-2248`, `editor/EditorLayer.cpp:3210-3360`, `editor/EditorLayer.cpp:3753-4160`
- Test: existing UI automation suites

- [ ] **Step 1: Write a failing UI theme assertion test**

Append to `tests/test_ui_theme.cpp`:

```cpp
TEST_CASE("editor theme uses compact chrome tokens") {
  const Horo::Ui::EditorTheme &editor = Horo::Ui::GetEditorTheme();

  CHECK(editor.density.panelPadding.x == 10.0f);
  CHECK(editor.density.buttonPadding.y == 5.0f);
  CHECK(editor.rounding.tab == 5.0f);
  CHECK(editor.palette.selection.w >= 0.90f);
}
```

- [ ] **Step 2: Run test to verify it fails if tokens are not present**

Run:

```bash
ctest --preset debug --output-on-failure -R "^test_ui_theme$"
```

Expected: PASS because Task 1 introduced the compact tokens. Keep this as a regression test before changing editor rendering.

- [ ] **Step 3: Add editor theme include and local helper**

In `editor/EditorLayer.cpp`, add:

```cpp
#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
```

Near existing anonymous-namespace UI helpers, add:

```cpp
const Horo::Ui::EditorTheme &EditorUiTheme() {
  return Horo::Ui::GetEditorTheme();
}
```

- [ ] **Step 4: Style toolbar window**

In `EditorLayer::DrawToolbar()`, before `ImGui::Begin("##toolbar"...)`, push compact chrome colors and vars:

```cpp
const Horo::Ui::EditorTheme &theme = EditorUiTheme();
ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.palette.panel);
ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.panelSoft);
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.palette.selection);
ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.button);
ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, theme.density.buttonPadding);
```

After `ImGui::End()`, pop exactly:

```cpp
ImGui::PopStyleVar(3);
ImGui::PopStyleColor(5);
```

- [ ] **Step 5: Style status bar window**

In `EditorLayer::DrawStatusBar()`, apply the same window background/border text-muted tokens:

```cpp
const Horo::Ui::EditorTheme &theme = Horo::Ui::GetEditorTheme();
ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.palette.panel);
ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
```

After the status window ends:

```cpp
ImGui::PopStyleVar();
ImGui::PopStyleColor(3);
```

- [ ] **Step 6: Style hierarchy and assets windows**

In `DrawObjectList()` and `DrawAssetsPanel()`, wrap the main `ImGui::Begin(...)` windows with `Horo::Ui::ScopedPanelStyle`:

```cpp
const Horo::Ui::EditorTheme &theme = EditorUiTheme();
Horo::Ui::ScopedPanelStyle panelStyle(theme);
ImGui::Begin(kEditorHierarchyWindow, nullptr, kMainPanelWindowFlags);
```

For the hierarchy search input, change the existing `##object_search` block to:

```cpp
{
  Horo::Ui::ScopedInputStyle inputStyle(theme);
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  if (ImGui::InputTextWithHint("##object_search", "Search objects...",
                               searchBuf.data(), searchBuf.size()))
    m_objectSearchQuery = searchBuf.data();
  ImGui::PopItemFlag();
}
```

For the asset spotlight search input in `DrawAssetSpotlightPopup()`, wrap the existing `##asset_spotlight_input` call with `ScopedInputStyle` and keep its label and buffer unchanged.

- [ ] **Step 7: Style selection rows without changing selection logic**

Where `DrawObjectList()` pushes header colors for selectable rows, replace hard-coded values with:

```cpp
ImGui::PushStyleColor(ImGuiCol_Header, theme.palette.selection);
ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme.palette.selectionHover);
ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme.palette.accentActive);
```

Keep the existing selectable labels and IDs unchanged.

- [ ] **Step 8: Style command palette, quick-open, and settings modal**

Before each `ImGui::BeginPopupModal(...)` in `DrawCommandPalette()`, `DrawQuickOpenPopup()`, and `DrawSettingsModal()`, push modal style:

```cpp
const Horo::Ui::EditorTheme &theme = EditorUiTheme();
ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.palette.modal);
ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.rounding.panel);
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, theme.density.panelPadding);
```

After the popup modal block, pop:

```cpp
ImGui::PopStyleVar(2);
ImGui::PopStyleColor(2);
```

If a popup returns early, ensure pops occur before returning.

- [ ] **Step 9: Run editor chrome automation suites**

Run:

```bash
cmake --build --preset debug --target HoroEditorUiTest test_ui_theme --parallel
ctest --preset debug --output-on-failure -R "^test_ui_theme$"
HORO_UI_TEST_SUITE=launcher-basic HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=mcp-project HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=modals-mcp HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: all commands pass.

- [ ] **Step 10: Commit Task 4**

```bash
git add editor/EditorLayer.cpp tests/test_ui_theme.cpp
git commit -m "feat(editor): apply shared theme to editor chrome"
```

---

## Task 5: Theme Properties Panel Sections

**Files:**
- Modify: `editor/EditorPropertiesPanel.cpp:53-118`, section draw methods in `editor/EditorPropertiesPanel.cpp`
- Test: `properties-workflows`, `properties-close`

- [ ] **Step 1: Add shared UI include**

In `editor/EditorPropertiesPanel.cpp`, add:

```cpp
#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
```

- [ ] **Step 2: Apply panel style to properties window**

In `EditorLayer::DrawPropertiesPanel()`, before `ImGui::Begin(kEditorPropertiesWindow...)`, add:

```cpp
const Horo::Ui::EditorTheme &theme = Horo::Ui::GetEditorTheme();
Horo::Ui::ScopedPanelStyle panelStyle(theme);
```

Keep `ImGui::Begin(kEditorPropertiesWindow, nullptr, kMainPanelWindowFlags);` unchanged.

- [ ] **Step 3: Add compact section styling helper in file-local namespace**

In the anonymous namespace of `EditorPropertiesPanel.cpp`, add:

```cpp
struct PropertiesSectionStyle {
  explicit PropertiesSectionStyle(const Horo::Ui::EditorTheme &theme) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, theme.density.cardPadding);
  }

  ~PropertiesSectionStyle() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
  }

  PropertiesSectionStyle(const PropertiesSectionStyle &) = delete;
  PropertiesSectionStyle &operator=(const PropertiesSectionStyle &) = delete;
};
```

- [ ] **Step 4: Wrap section bodies without changing controls**

For each high-level section method such as `DrawPropertiesIdentitySection`, `DrawPropertiesTransformSection`, `DrawPropertiesAssetSection`, `DrawPropertiesSchemaFields`, and `DrawPropertiesComponentsList`, wrap the existing body in a child card:

```cpp
const Horo::Ui::EditorTheme &theme = Horo::Ui::GetEditorTheme();
PropertiesSectionStyle sectionStyle(theme);
ImGui::BeginChild("##properties_identity_section", ImVec2(0.0f, 0.0f), true,
                  ImGuiWindowFlags_AlwaysAutoResize);
Horo::Ui::SectionHeader("Identity");
// existing section controls stay here, with their existing labels/IDs
ImGui::EndChild();
ImGui::Dummy(ImVec2(0.0f, 6.0f));
```

Use unique child IDs per section:

- `##properties_identity_section`
- `##properties_transform_section`
- `##properties_asset_section`
- `##properties_schema_section`
- `##properties_components_section`

Use a fixed height that matches the section content instead of automatic child sizing. Start with these heights and adjust only when content is clipped during visual validation:

- Identity: `92.0f`
- Transform: `142.0f`
- Asset: `120.0f`
- Schema: `170.0f`
- Components: `150.0f`

- [ ] **Step 5: Style destructive delete button**

Before the existing `ImGui::Button("Delete")`, push destructive colors:

```cpp
ImGui::PushStyleColor(ImGuiCol_Button, theme.palette.destructive);
ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                      ImVec4(theme.palette.destructive.x, theme.palette.destructive.y,
                             theme.palette.destructive.z, 0.88f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                      ImVec4(theme.palette.destructive.x * 0.85f,
                             theme.palette.destructive.y * 0.85f,
                             theme.palette.destructive.z * 0.85f, 1.0f));
```

After the button, pop:

```cpp
ImGui::PopStyleColor(3);
```

- [ ] **Step 6: Run properties suites**

Run:

```bash
cmake --build --preset debug --target HoroEditorUiTest --parallel
HORO_UI_TEST_SUITE=properties-workflows HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=properties-close HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: both suites pass.

- [ ] **Step 7: Commit Task 5**

```bash
git add editor/EditorPropertiesPanel.cpp
git commit -m "feat(editor): theme properties panel sections"
```

---

## Task 6: Visual Validation and Final Hardening

**Files:**
- Modify: `ui/HoroTheme.cpp` only for token tuning found during visual validation.
- Modify: `editor/EditorLayer.cpp` or `editor/EditorPropertiesPanel.cpp` only for style-stack or clipping defects found during validation.

- [ ] **Step 1: Generate launcher and editor screenshots**

Run:

```bash
mkdir -p ui_test_output/horo38-final
HORO_UI_TEST_SUITE=launcher-basic \
HORO_UI_TEST_FILTER='launcher/launcher_home_layout,editor/properties_panel_visible' \
HORO_UI_TEST_CAPTURE=1 \
HORO_UI_TEST_VIDEO=0 \
HORO_UI_TEST_RECORDING=1 \
HORO_UI_TEST_OUTPUT_DIR="$PWD/ui_test_output/horo38-final" \
HORO_LOG_LEVEL=info \
HORO_GLFW_SAMPLES=0 \
build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: PASS and screenshots in `ui_test_output/horo38-final`.

- [ ] **Step 2: Inspect visuals in browser companion**

Use browser/image preview to inspect generated screenshots. Confirm:

- Launcher home still looks like the refreshed launcher.
- Editor chrome uses the same navy/blue palette.
- Editor spacing is still compact.
- Toolbar, status bar, hierarchy, assets, properties, bottom dock, and modals are readable.
- Selected, hovered, focused, disabled, and destructive states are distinct.

- [ ] **Step 3: Fix only concrete visual defects**

Fix only concrete defects found during visual inspection. Use these bounded fixes:

```cpp
// If selected rows are too close to hover rows, adjust only selectionHover alpha.
.selectionHover = ImVec4(0.18f, 0.37f, 0.60f, 0.96f),
```

```cpp
// If editor panels are too transparent, adjust editor panel alpha only.
.panel = ImVec4(kPalette.panel.x, kPalette.panel.y, kPalette.panel.z, 0.92f),
```

Do not add new layout features during hardening.

- [ ] **Step 4: Run full UI suite matrix locally for this branch**

Run:

```bash
cmake --build --preset debug --target HoroEditorUiTest --parallel
ctest --preset debug --output-on-failure -R "ui_theme|launcher"
HORO_UI_TEST_SUITE=launcher-basic HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=properties-workflows HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=mcp-project HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=modals-mcp HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
HORO_UI_TEST_SUITE=properties-close HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_VIDEO=0 HORO_UI_TEST_RECORDING=1 HORO_LOG_LEVEL=info HORO_GLFW_SAMPLES=0 build/debug/bin/HoroEditorUiTest --run-ui-tests
```

Expected: all commands pass.

- [ ] **Step 5: Run broader validation**

Run:

```bash
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
```

Expected: build succeeds and all tests pass. If `test_window` fails with a one-off local macOS `Bus error`, rerun only `ctest --preset debug --output-on-failure -R "^test_window$"` once and record both outputs in the final report.

- [ ] **Step 6: Request code review**

Dispatch an `oracle` review with this prompt:

```text
Review HORO-38 editor panel theme changes. Focus on UI style stack safety, theme/module boundaries, launcher visual drift, editor density/readability, and UI automation selector stability. Report Critical/Important/Minor findings.
```

Fix Critical and Important findings before proceeding.

- [ ] **Step 7: Commit final hardening changes when Step 3 or Step 6 changed code**

If Step 3 or Step 6 changed code:

```bash
git add ui editor launcher tests CMakeLists.txt
git commit -m "fix(ui): harden shared editor theme styling"
```

If there were no changes, do not create an empty commit.

---

## Final Verification Checklist

- [ ] Shared `ui/` module exists and owns theme tokens.
- [ ] Launcher reads shared tokens without visible launcher regression.
- [ ] Editor chrome uses launcher-inspired palette/rounding/borders.
- [ ] Editor density remains compact.
- [ ] UI automation selectors are unchanged unless explicitly covered by tests.
- [ ] `test_ui_theme` passes.
- [ ] Five UI suites pass locally or failures are documented with root cause.
- [ ] Visual inspection confirms launcher/editor consistency.
- [ ] Code review has no unresolved Critical or Important findings.
