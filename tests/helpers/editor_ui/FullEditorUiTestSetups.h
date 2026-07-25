#pragma once

#include "EditorUiTestHarness.h"

#include <string>

namespace Horo::Tests
{
    class FullEditorUiTestHost;

    /** @brief Deterministic values consumed by the reusable project-creation setup pipeline. */
    struct FullEditorProjectSetup
    {
        std::string name{"UiAutomationProject"};
        std::string templateId{"empty"};
    };

    namespace FullEditorSetups
    {
        /** @brief Adds the Welcome-to-Project-Creation navigation setup step. */
        void OpenProjectCreation(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor);

        /** @brief Adds project template, identity, and creation submission setup steps. */
        void SubmitProjectCreation(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor,
                                   FullEditorProjectSetup setup);

        /** @brief Adds the asynchronous ProjectLoading-to-Workspace setup barrier. */
        void AwaitWorkspace(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor);

        /** @brief Composes the complete reusable project-creation setup pipeline. */
        void CreateProjectAndOpenWorkspace(UiScenarioPipe& pipeline, FullEditorUiTestHost& editor,
                                           FullEditorProjectSetup setup = {});
    } // namespace FullEditorSetups
} // namespace Horo::Tests
