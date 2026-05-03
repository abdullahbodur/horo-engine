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
