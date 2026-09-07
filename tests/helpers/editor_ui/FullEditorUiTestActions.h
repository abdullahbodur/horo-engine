#pragma once

#include "EditorUiTestHarness.h"

namespace Horo::Tests {
    class FullEditorUiTestHost;

    namespace FullEditorActions {
        /** @brief Adds the hierarchy interaction and cross-panel checks for creating one root Box. */
        void CreateRootBox(UiScenarioPipe &pipeline);

        /** @brief Exercises hierarchy duplicate, rename, and delete commands. */
        void ExerciseHierarchyEdits(UiScenarioPipe &pipeline);

        /** @brief Adds the viewport interaction and shared-state check for orthographic projection. */
        void SelectOrthographicProjection(UiScenarioPipe &pipeline, FullEditorUiTestHost &editor);

        /** @brief Exercises the input mapping pages and every global dock pane. */
        void ExerciseWorkspacePanels(UiScenarioPipe &pipeline, FullEditorUiTestHost &editor);
    }  // namespace FullEditorActions
}  // namespace Horo::Tests
