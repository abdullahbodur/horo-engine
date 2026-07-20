#pragma once

#include "EditorUiTestHarness.h"

namespace Horo::Tests
{
class FullEditorUiTestHost;

namespace FullEditorActions
{
/** @brief Adds the hierarchy interaction and cross-panel checks for creating one root Box. */
void CreateRootBox(UiScenarioPipe& pipeline);

/** @brief Adds the viewport interaction and shared-state check for orthographic projection. */
void SelectOrthographicProjection(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor);
} // namespace FullEditorActions
} // namespace Horo::Tests
