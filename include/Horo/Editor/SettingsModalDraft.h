#pragma once

#include "Horo/Editor/EditorSettingsStore.h"

#include <cstdint>
#include <iterator>
#include <string>

namespace Horo::Editor
{
    class EditorSettingsService;

    /** @brief Modal-owned mutable draft for the editor settings workflow. */
    struct SettingsState
    {
        bool initialized = false;
        bool wasOpen = false;
        bool dirty = false;
        EditorSettings committed{};
        std::string statusMessage;
        bool statusIsError = false;
        int activeTab = 0;
        std::uint64_t settingsRevision = 0;

        struct GeneralTab { int startupAction = 0; int autoSaveInterval = 5; bool confirmExit = true; bool restoreWorkspace = true; std::string defaultScene = "Assets/Scenes/Main"; } general;
        struct AppearanceTab { int themeIndex = 0; std::string customThemePath = "~/.horo/themes/my-theme.json"; int uiScale = 100; std::string editorFontSize = "15"; std::string accentHex = "#04A5FC"; int pendingThemeIndex = -1; } appearance;
        struct InputTab
        {
            int orbitSensitivity = 100;
            int panSensitivity = 100;
            bool invertOrbitY = false;
            static constexpr int kMaxShortcuts = 16;
            struct ShortcutBinding { std::string keys; bool conflict = false; };
            ShortcutBinding shortcuts[kMaxShortcuts];
            int shortcutCount = 0;
            int listeningShortcut = -1;
            static constexpr const char *kShortcutActions[] = {"Save Scene", "Undo", "Build & Release", "Find", "Replace", "Duplicate", "Delete", "Select All"};
            static constexpr int kShortcutActionCount = static_cast<int>(std::size(kShortcutActions));
        } input;
        struct RenderingTab { int viewportMode = 0; bool gridOverlay = true; int renderingTier = 0; std::string textureBudget = "2048 MB"; int renderBackend = 0; } rendering;
        struct AudioTab { int masterVolume = 80; int audioOutputDevice = 0; bool audioEnabled = true; } audio;
        struct NetworkTab { int maxPreviewClients = 4; int simulatedLatencyMs = 0; int packageDownloadThreads = 8; } network;
        struct DiagnosticsTab { int consoleLogLevel = 2; bool writeLogToFile = true; bool autoCaptureStutter = false; float stutterThresholdMs = 33.3F; bool showFps = false; bool anonymousTelemetry = false; } diagnostics;
        int pluginSectionTab = 0;
        int selectedPlugin = 0;
        int pluginDetailTab[3] = {};
        char pluginFilter[64]{};
        std::string modalFeedback;
        struct PluginToggles { bool horoMcpBridge = true; bool fmodIntegration = true; bool steamworksSdk = false; } plugins;
        struct McpSettings { int transportMode = 0; int port = 8080; bool requireToken = true; bool allowRemote = false; int toolScope = 0; std::string assetRoot = "Assets/Generated"; } mcp;
        struct FmodSettings { std::string studioPath = "/Applications/FMOD Studio.app"; std::string projectFile = "Audio/HoroAudio.fspro"; std::string bankPath = "Assets/Audio/Banks"; bool liveUpdate = true; bool failOnMissing = true; int targetPlatform = 0; } fmod;
        struct SteamSettings { std::string sdkPath = "ThirdParty/Steamworks"; int initMode = 0; bool overlay = true; bool achievements = true; bool networking = false; } steam;
        struct PluginRuntime { std::string discoveryPaths = "{project}/plugins; ~/.horo/plugins"; int loadOrder = 0; std::string devPath = "~/dev/horo-plugins"; bool sandbox = true; int unsignedPolicy = 0; int networkPolicy = 0; int updateCheck = 0; int compatMode = 0; } runtime;
    };

    [[nodiscard]] EditorSettings CollectDraftSettings(const SettingsState &state);
    void ApplySettingsToDraft(SettingsState &state, const EditorSettings &settings);
    void LoadSettingsForModal(SettingsState &state, const EditorSettingsService &settings);
    [[nodiscard]] bool ApplySettings(SettingsState &state, EditorSettingsService &settings);
} // namespace Horo::Editor
