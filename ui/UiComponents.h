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

enum class ButtonStyleVariant {
  Primary,
  Secondary,
};

class ScopedButtonStyle {
public:
  explicit ScopedButtonStyle(const EditorTheme &theme, ButtonStyleVariant variant);
  explicit ScopedButtonStyle(const LauncherTheme &theme, ButtonStyleVariant variant);
  ~ScopedButtonStyle();

  ScopedButtonStyle(const ScopedButtonStyle &) = delete;
  ScopedButtonStyle &operator=(const ScopedButtonStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

void TextMuted(const EditorTheme &theme, const char *text);
void TextMuted(const LauncherTheme &theme, const char *text);

void SectionHeader(const EditorTheme &theme, const char *title);
void SectionHeader(const LauncherTheme &theme, const char *title);

bool Button(const EditorTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));
bool Button(const LauncherTheme &theme, ButtonStyleVariant variant,
            const char *label, const ImVec2 &size = ImVec2(0, 0));

bool RenderPrimaryButton(const LauncherTheme &theme, const char *label,
                         const ImVec2 &size = ImVec2(0, 0));

bool RenderSecondaryButton(const LauncherTheme &theme, const char *label,
                           const ImVec2 &size = ImVec2(0, 0));

bool RenderRecentProjectButton(const LauncherTheme &theme, const char *title,
                               const ImVec2 &size = ImVec2(0, 0));

void RenderLabeledInput(const LauncherTheme &theme, const char *title,
                        const char *id, char *buffer, size_t bufferSize,
                        float inputWidth);

bool InputText(const EditorTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);
bool InputText(const LauncherTheme &theme, const char *id, char *buffer,
               size_t bufferSize, ImGuiInputTextFlags flags = 0);

class ScopedComboStyle {
public:
  explicit ScopedComboStyle(const EditorTheme &theme);
  explicit ScopedComboStyle(const LauncherTheme &theme);
  ~ScopedComboStyle();

  ScopedComboStyle(const ScopedComboStyle &) = delete;
  ScopedComboStyle &operator=(const ScopedComboStyle &) = delete;

private:
  int m_colorCount = 0;
  int m_styleCount = 0;
};

bool InputTextWithHint(const EditorTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

bool InputTextWithHint(const LauncherTheme &theme, const char *id,
                       const char *hint, char *buffer, size_t bufferSize,
                       ImGuiInputTextFlags flags = 0);

bool Combo(const EditorTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);
bool Combo(const LauncherTheme &theme, const char *label, int *currentItem,
           const char *const items[], int itemCount);

} // namespace Horo::Ui
