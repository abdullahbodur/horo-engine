#pragma once

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorSettingsStore.h"

#include <imgui.h>

#include <string>

namespace Horo::Editor
{

    /** @brief Mutable UI state for the Settings modal. */
    struct SettingsState
    {
        bool open = false;
        bool initialized = false;
        bool wasOpen = false;
        bool dirty = false;
        EditorSettings committed{};
        EditorSettingsDocument document{};
        std::string statusMessage;
        bool statusIsError = false;
        int activeTab = 0;
        int themeIndex = 0; // 0 = Horo Dark, 1 = Midnight, 2 = Light
        int startupAction = 0;
        int renderBackend = 0;
        int viewportMode = 0;
        int renderingTier = 0;
        int audioOutputDevice = 0;
        bool autoSave = true;
        int autoSaveInterval = 5;
        bool confirmExit = true;
        bool restoreWorkspace = true;
        int uiScale = 100;
        char editorFontSize[8] = "15";
        char defaultScene[64] = "Assets/Scenes/Main";
        char accentHex[16] = "#04A5FC";
        char textureBudget[32] = "2048 MB";
        char pluginPath[64] = "{project}/plugins";
        int orbitSensitivity = 100;
        int panSensitivity = 100;
        bool invertOrbitY = false;
        bool gridOverlay = true;
        int masterVolume = 80;
        bool audioEnabled = true;
        int maxPreviewClients = 4;
        int simulatedLatencyMs = 0;
        int packageDownloadThreads = 8;
        int consoleLogLevel = 2;
        bool writeLogToFile = true;
        bool autoCaptureStutter = false;
        float stutterThresholdMs = 33.3F;
        bool horoMcpBridge = true;
        bool fmodIntegration = true;
        bool steamworksSdk = false;
        bool showFps = false;
        bool anonymousTelemetry = false;
    };

    /**
     * @brief Draws the Settings modal when its state is open.
     * @param state Mutable modal state.
     * @param fonts Editor font handles.
     * @param logo Optional logo texture shown in the modal header.
     */
    void DrawSettingsModal(SettingsState &state, const Theme::Fonts &fonts, ::ImTextureID logo);

} // namespace Horo::Editor
