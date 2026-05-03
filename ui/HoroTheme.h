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
  float window = 0.0f;
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
