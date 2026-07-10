#include "Horo/Editor/SettingsModalDraft.h"
#include "Horo/Editor/EditorSettingsService.h"

#include "Horo/Editor/EditorSettingsStore.h"

#include <algorithm>

namespace Horo::Editor
{
    namespace
    {
        /**
         * @brief Safe string copy into a fixed-size buffer.
         * @param dst Destination buffer.
         * @param dstSize Size of destination buffer in bytes.
         * @param src Source string to copy.
         */
        void CopyString(char *dst, const size_t dstSize, const std::string &src)
        {
            if (!dst || dstSize == 0)
            {
                return;
            }
            std::snprintf(dst, dstSize, "%s", src.c_str());
        }

    } // namespace

    /** @copydoc SettingsModal.h */
    [[nodiscard]] EditorSettings CollectDraftSettings(const SettingsState &st)
    {
        EditorSettings out{};
        out.startupBehavior = static_cast<EditorStartupBehavior>(std::clamp(st.general.startupAction, 0, 2));
        out.autoSaveIntervalMinutes = st.general.autoSaveInterval;
        out.confirmExitWithUnsavedChanges = st.general.confirmExit;
        out.restoreWorkspaceLayout = st.general.restoreWorkspace;
        out.defaultSceneOnProjectOpen = st.general.defaultScene;

        out.themePreset = static_cast<EditorThemePreset>(st.appearance.themeIndex);
        out.accentColorHex = st.appearance.accentHex;
        out.uiScalePercent = st.appearance.uiScale;
        out.codeFontSizePx = std::atoi(st.appearance.editorFontSize);

        out.orbitSensitivity = st.input.orbitSensitivity;
        out.panSensitivity = st.input.panSensitivity;
        out.invertOrbitY = st.input.invertOrbitY;

        out.viewportMode = static_cast<EditorViewportMode>(std::clamp(st.rendering.viewportMode, 0, 3));
        out.gridOverlay = st.rendering.gridOverlay;
        out.renderingTier = static_cast<EditorRenderingTier>(std::clamp(st.rendering.renderingTier, 0, 3));
        out.textureStreamingBudget = st.rendering.textureBudget;

        out.masterVolume = st.audio.masterVolume;
        out.audioOutputDevice = static_cast<EditorAudioOutputDevice>(std::clamp(st.audio.audioOutputDevice, 0, 2));
        out.audioEnabled = st.audio.audioEnabled;

        out.maxPreviewClients = st.network.maxPreviewClients;
        out.simulatedLatencyMs = st.network.simulatedLatencyMs;
        out.packageDownloadThreads = st.network.packageDownloadThreads;

        out.consoleLogLevel = static_cast<EditorConsoleLogLevel>(std::clamp(st.diagnostics.consoleLogLevel, 0, 3));
        out.writeLogToFile = st.diagnostics.writeLogToFile;
        out.autoCaptureOnStutter = st.diagnostics.autoCaptureStutter;
        out.stutterThresholdMs = st.diagnostics.stutterThresholdMs;

        out.horoMcpBridgeEnabled = st.plugins.horoMcpBridge;
        out.fmodIntegrationEnabled = st.plugins.fmodIntegration;
        out.steamworksSdkEnabled = st.plugins.steamworksSdk;
        out.pluginDiscoveryPath = st.runtime.discoveryPaths;

        // Serialise shortcut key bindings
        for (int i = 0; i < SettingsState::InputTab::kShortcutActionCount; ++i)
            std::snprintf(out.shortcutKeys[i], sizeof(out.shortcutKeys[i]),
                          "%s", st.input.shortcuts[i].keys);

        return out;
    }

    /** @copydoc SettingsModal.h */
    void ApplySettingsToDraft(SettingsState &st, const EditorSettings &settings)
    {
        st.general.startupAction = static_cast<int>(settings.startupBehavior);
        st.general.autoSaveInterval = settings.autoSaveIntervalMinutes;
        st.general.confirmExit = settings.confirmExitWithUnsavedChanges;
        st.general.restoreWorkspace = settings.restoreWorkspaceLayout;
        CopyString(st.general.defaultScene, sizeof(st.general.defaultScene), settings.defaultSceneOnProjectOpen);

        st.appearance.themeIndex = static_cast<int>(settings.themePreset);
        st.appearance.uiScale = settings.uiScalePercent;
        std::snprintf(st.appearance.editorFontSize, sizeof(st.appearance.editorFontSize), "%d", settings.codeFontSizePx);
        CopyString(st.appearance.accentHex, sizeof(st.appearance.accentHex), settings.accentColorHex);

        st.input.orbitSensitivity = settings.orbitSensitivity;
        st.input.panSensitivity = settings.panSensitivity;
        st.input.invertOrbitY = settings.invertOrbitY;

        st.rendering.viewportMode = static_cast<int>(settings.viewportMode);
        st.rendering.gridOverlay = settings.gridOverlay;
        st.rendering.renderingTier = static_cast<int>(settings.renderingTier);
        CopyString(st.rendering.textureBudget, sizeof(st.rendering.textureBudget), settings.textureStreamingBudget);

        st.audio.masterVolume = settings.masterVolume;
        st.audio.audioOutputDevice = static_cast<int>(settings.audioOutputDevice);
        st.audio.audioEnabled = settings.audioEnabled;

        st.network.maxPreviewClients = settings.maxPreviewClients;
        st.network.simulatedLatencyMs = settings.simulatedLatencyMs;
        st.network.packageDownloadThreads = settings.packageDownloadThreads;

        st.diagnostics.consoleLogLevel = static_cast<int>(settings.consoleLogLevel);
        st.diagnostics.writeLogToFile = settings.writeLogToFile;
        st.diagnostics.autoCaptureStutter = settings.autoCaptureOnStutter;
        st.diagnostics.stutterThresholdMs = settings.stutterThresholdMs;

        st.plugins.horoMcpBridge = settings.horoMcpBridgeEnabled;
        st.plugins.fmodIntegration = settings.fmodIntegrationEnabled;
        st.plugins.steamworksSdk = settings.steamworksSdkEnabled;
        CopyString(st.runtime.discoveryPaths, sizeof(st.runtime.discoveryPaths), settings.pluginDiscoveryPath);

        // Restore shortcut key bindings
        for (int i = 0; i < SettingsState::InputTab::kShortcutActionCount; ++i)
            std::snprintf(st.input.shortcuts[i].keys, sizeof(st.input.shortcuts[i].keys),
                          "%s", settings.shortcutKeys[i]);
    }

    /** @copydoc SettingsModal.h */
    void LoadSettingsForModal(SettingsState &st, const EditorSettingsService &settings)
    {
        const EditorSettingsSnapshot snapshot = settings.Snapshot();
        st.committed = snapshot.settings;
        st.settingsRevision = snapshot.revision;
        ApplySettingsToDraft(st, st.committed);
        st.dirty = false;
        st.initialized = true;
        st.statusIsError = false;
        st.statusMessage = "Loaded committed editor settings.";

        st.input.shortcutCount = SettingsState::InputTab::kShortcutActionCount;
    }

    /** @copydoc SettingsModal.h */
    [[nodiscard]] bool ApplySettings(SettingsState &st, EditorSettingsService &settings)
    {
        const EditorSettings draft = CollectDraftSettings(st);
        const Result<EditorSettingsSnapshot> result = settings.Commit(
            EditorSettingsDraft{.baseRevision = st.settingsRevision, .settings = draft});
        if (result.HasError())
        {
            st.statusMessage = result.ErrorValue().message;
            st.statusIsError = true;
            st.dirty = true;
            return false;
        }

        st.committed = result.Value().settings;
        st.settingsRevision = result.Value().revision;
        ApplySettingsToDraft(st, st.committed);
        st.dirty = false;
        st.statusMessage = "Settings applied.";
        st.statusIsError = false;
        return true;
    }

} // namespace Horo::Editor
