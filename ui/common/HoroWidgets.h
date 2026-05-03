#pragma once

#include <cstddef>
#include <imgui.h>

namespace Horo::UI {

// Renders a primary (accent-colored) push button.
// Returns true when clicked.
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

// Renders a secondary (surface-dark) push button.
// Returns true when clicked.
bool SecondaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

// Renders a disabled text label followed by a styled InputText.
void LabeledInput(const char* title, const char* id,
                  char* buffer, size_t bufferSize,
                  float inputWidth);

// Renders centered, dimmed text in the middle of the current content region.
// Useful for empty-state messages in panels and lists.
void CenteredEmptyState(const char* text);

} // namespace Horo::UI
