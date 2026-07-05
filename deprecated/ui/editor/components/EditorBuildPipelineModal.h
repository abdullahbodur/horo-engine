/** @file EditorBuildPipelineModal.h
 *  @brief Build & Release pipeline modal for configuring, triggering, and
 *         monitoring release builds with code signing support.
 *
 *  The modal owns a draft build configuration (single target OS per run,
 *  build type, signing settings, version tag) and owns an ExternalProcessRunner
 *  used to execute one local build child process at a time.  Build history is
 *  persisted to @c ~/.horo/build_history.json.
 *
 *  Integration follows the same pattern as EditorSettingsModal:
 *  - Open() seeds the draft from defaults and the current BuildToolchainSettings.
 *  - Draw() is called each frame; safe as a no-op when closed.
 *  - IsOpen() lets the caller skip drawing.
 *  - Dependencies are injected as non-owning pointers before Open().
 */
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "core/pipeline/ReleaseTypes.h"
#include "core/pipeline/ToolchainSettings.h"
#include "ui/launcher/ExternalProcessRunner.h"

// Forward declaration for editor theme (avoids imgui.h in header).
namespace Horo::Ui { struct EditorTheme; struct RecentRunEntry; }

namespace Horo::Editor {

using Build::BuildArch;
using Build::BuildConfig;
using Build::BuildHistoryEntry;
using Build::BuildJob;
using Build::BuildJobStatus;


using Build::BuildPipelineDraft;
using Build::BuildPipelineState;
using Build::BuildTargetOS;
using Build::PlatformSelection;
using Build::GetBuildArchLabel;
using Build::GetBuildConfigLabel;
using Build::GetBuildJobStatusLabel;
using Build::GetBuildTargetOSLabel;
using Build::SigningConfig;



/** @brief Identifies the active panel in the build pipeline modal tab bar. */
enum class BuildPanelTab {
    Platforms,     /**< Target OS / platform selection. */
    Build,         /**< Build configuration, version tag, output. */
    Signing,       /**< Code signing and notarization settings. */
    Log,           /**< Build progress, output, and history. */
};

/** @brief Modal component for configuring and executing release builds. */
class EditorBuildPipelineModal {
public:
    /** @brief Callback invoked when a build session finishes (all jobs terminal). */
    using BuildCompleteCallback = std::function<void(const std::vector<BuildJob> &)>;

    /** @brief Returns true while the modal should be rendered. */
    bool IsOpen() const { return m_open; }

    /** @brief Opens the modal with a fresh draft seeded from defaults.
     *
     * The host platform is selected when the matching BuildToolchainSettings
     * entry is enabled; if the host platform is disabled the first enabled
     * platform is selected.  Falls back to host-platform selection when no
     * toolchain settings pointer has been injected.
     */
    void Open();

    /** @brief Force-closes the modal without cancelling running builds. */
    void Close();

    /**
     * @brief Injects the toolchain settings store for evaluating target capabilities.
     * @param store Non-owning pointer to the toolchain registry.
     */
    void SetToolchainStore(const Build::ToolchainSettingsStore *store) {
        m_toolchainStore = store;
    }

    /**
     * @brief Draws the modal for one frame; safe to call when IsOpen() is false.
     *
     * Renders the tab bar (Platform / Signing / History), the active tab
     * content, the progress section (when a build is running), and the
     * footer action buttons.
     */
    void Draw();

    /**
     * @brief Sets the project root directory used to resolve default output paths.
     * @param root Absolute path to the project root.
     */
    void SetProjectRoot(std::string root) { m_projectRoot = std::move(root); }

    /** @brief Installs a callback invoked when a build session finishes. */
    void SetBuildCompleteCallback(BuildCompleteCallback cb) {
        m_buildCompleteCallback = std::move(cb);
    }

    /** @brief Polled once per frame to update build job progress and status. */
    void PollBuilds();

    /**
     * @brief Starts a release build from a prepared draft without requiring UI input.
     * @param draft Prepared build jobs and package settings.
     * @param projectRoot Absolute or relative project root used for the build.
     * @param outError Optional destination for a human-readable start failure.
     * @return True when the first build job was started or queued successfully.
     */
    bool StartRelease(BuildPipelineDraft&& draft, std::string projectRoot,
                      std::string *outError = nullptr);

    /** @brief Returns the current draft for test inspection. */
    const BuildPipelineDraft &DraftForTest() const { return m_draft; }

    /** @brief Returns the current pipeline state. */
    BuildPipelineState GetState() const { return m_state; }

    /** @brief Returns the persisted build history entries. */
    const std::vector<BuildHistoryEntry> &HistoryForTest() const { return m_history; }

    /** @brief Test-only: returns the current history viewing index (-1 = live). */
    int ViewingHistoryIndexForTest() const { return m_viewingHistoryIndex; }

    /** @brief Test-only: returns the cached historical log text. */
    const std::string &HistoricalLogTextForTest() const { return m_historicalLogText; }

    /** @brief Test-only: replaces the current draft with a deterministic state. */
    void SetDraftForTest(BuildPipelineDraft draft) { m_draft = std::move(draft); }

    /** @brief Test-only: directly sets the pipeline state without guard-rail checks. */
    void SetStateForTest(BuildPipelineState state) { m_state = state; }

    /** @brief Test-only: starts the next pending job through the production path. */
    void StartNextPendingJobForTest() { StartNextPendingJob(); }

    /** @brief Test-only: cancels running and pending jobs through the production path. */
    void CancelAllBuildsForTest() { CancelAllBuilds(); }

    /** @brief Test-only: builds the display command string through the production helper. */
    std::string BuildCommandForJobForTest(const BuildJob &job) const {
        return BuildCommandForJob(job);
    }

    /** @brief Test-only: builds the signing command string through the production helper. */
    std::string SignCommandForJobForTest(const BuildJob &job) const {
        return SignCommandForJob(job);
    }

    /** @brief Test-only: finalizes a deterministic draft through the production path. */
    bool FinalizeIfAllJobsTerminalForTest() { return FinalizeIfAllJobsTerminal(); }


    /** @brief Test-only: exposes platform enablement lookup via target capability model. */
    bool IsPlatformEnabledForTest(BuildTargetOS os) const;

    /** @brief Test-only: exposes selected-target buildability check.
     *
     *  @return True when the current single-selected target is buildable
     *          in the active build mode, given the injected BuildToolchainSettings. */
    bool IsSelectedTargetBuildable() const;

    /** @brief Returns a human-readable reason when the selected target is unavailable. */
    std::string SelectedTargetUnavailableReason() const;

    /** @brief Test-only: simulates a mock draft creation (avoids real API calls). */
    void MockCreateDraftForTest();

private:
    /** @brief Builds the shell command string for a single build job. */
    std::string BuildCommandForJob(const BuildJob &job) const;

    /** @brief Builds the shell command string for code-signing a job's output. */
    std::string SignCommandForJob(const BuildJob &job) const;

    /** @brief Starts the next pending build job, if any. */
    void StartNextPendingJob();

    /** @brief Cancels all running and pending builds. */
    void CancelAllBuilds();

    /** @brief Resolves the default output root from the project root. */
    std::string ResolveDefaultOutputRoot() const;

    /** @brief Generates the default version tag suggestion. */
    std::string DefaultVersionTag() const;

    /** @brief Bumps the patch version of @c m_draft.versionTag by one. */
    void BumpVersionPatch();

    /** @brief Validates version tag and game version; updates error members. */
    void ValidateVersionFields();

    /** @brief Loads build history from disk. */
    void LoadHistory();

    /** @brief Persists the current build history to disk. */
    void SaveHistory() const;

    /** @brief Appends the completed build session to history and saves. */
    void AppendHistoryEntry();

    /** @brief Finalizes the session if every job has reached a terminal state. */
    bool FinalizeIfAllJobsTerminal();

    /** @brief Centralized state transition with guard-rail validation.
     *
     *  In debug builds, asserts that @p to is a valid transition from the
     *  current state.  Production builds return false on invalid transitions
     *  without crashing.  Same-state is always a no-op.
     *
     *  @param to  Target pipeline state.
     *  @return    True if the transition was accepted and applied. */
    bool TransitionTo(BuildPipelineState to);

    // -- Draw helpers --

    /** @brief Renders target platform cards and build preset shortcuts. */
    void DrawPlatformColumn(float width, float height);

    /** @brief Renders the single-select platform cards. */
    void DrawPlatformCards();

    /** @brief Renders the build preset selector. */
    void DrawBuildPresets();

    /** @brief Applies a platform-card selection by card index. */
    void SelectPlatformCard(int index);

    /** @brief Renders build configuration, version, output, and job summary. */
    void DrawConfigurationColumn(float width, float height);

    /** @brief Renders signing configuration and recent build history. */
    void DrawSigningColumn(float width, float height);

    /** @brief Renders the signing identity card contents. */
    void DrawSigningIdentityCard();

    /** @brief Renders the signing security card contents. */
    void DrawSigningSecurityCard();

    /** @brief Renders the signing status card contents. */
    void DrawSigningStatusCard();

    /** @brief Renders build progress, log output, and recent runs. */
    void DrawLogColumn(float width, float height);

    /** @brief Renders the build progress section (visible during builds). */
    void DrawProgressSection() const;

    /** @brief Renders structured diagnostics for one build job. */
    void DrawJobDiagnostic(const BuildJob &job) const;

    /** @brief Renders the footer action buttons (Build All / Cancel / Close). */
    void DrawActionsSection();

    /** @brief Exports the currently visible redacted build log. */
    void ExportCurrentLog() const;

    /** @brief Resets the current draft and starts building from the first pending job. */
    void RestartBuildFromDraft();

    /** @brief Builds the live in-memory log text for the active draft. */
    std::string BuildLiveLogText() const;

    /** @brief Renders the banner shown while inspecting a historical log. */
    void DrawHistoryLogBanner(const BuildHistoryEntry &entry);

    /** @brief Loads historical log text for @p entryIndex into @c m_historicalLogText. */
    void LoadHistoricalLog(size_t entryIndex);

    /** @brief Builds clickable recent-run card entries from persisted history. */
    std::vector<Ui::RecentRunEntry> BuildRecentRunEntries();

    /** @brief Renders the custom icon+text tab bar and returns the active panel. */
    BuildPanelTab DrawTabBar(float availableWidth);



    // -- State --

    bool m_open = false;
    bool m_openRequested = false;
    BuildPanelTab m_activePanel = BuildPanelTab::Platforms;

    BuildPipelineDraft m_draft;
    BuildPipelineDraft m_original;          /**< State at modal open; used for dirty tracking. */

    BuildPipelineState m_state = BuildPipelineState::Idle;  /**< Current pipeline workflow state. */

    std::vector<BuildHistoryEntry> m_history;  /**< Persisted build history. */
    bool m_historyLoaded = false;
    int m_viewingHistoryIndex = -1;             /**< -1 = live draft logs; >=0 = index into m_history for inspection. */
    std::string m_historicalLogText;            /**< Log text loaded from disk for a historical entry being inspected. */

    std::string m_projectRoot;

    /** @brief Version field validation error; cleared on valid input. */
    std::string m_versionTagError;

    /** @brief Game version field validation error; cleared on valid input. */
    std::string m_gameVersionError;

    Launcher::ExternalProcessRunner m_processRunner;  /**< Owns the active build child process. */
    size_t m_lastProcessOutputSize = 0; /**< Bytes of active process output already appended to the visible job log. */
    BuildCompleteCallback m_buildCompleteCallback;

    /** @brief Non-owning pointer to the toolchain registry for target capability evaluation. */
    const Build::ToolchainSettingsStore *m_toolchainStore = nullptr;


    std::array<char, 128> m_buildNameBuf{};      /**< Input buffer for the build name field. */
    std::array<char, 64> m_versionTagBuf{};      /**< Input buffer for the version tag field. */
    std::array<char, 512> m_outputRootBuf{};     /**< Input buffer for the output root field. */
    std::array<char, 64> m_gameVersionBuf{};     /**< Input buffer for the game version field. */
    std::array<char, 32> m_buildNumberBuf{};     /**< Input buffer for the build number field. */
    std::array<char, 128> m_packageIdBuf{};      /**< Input buffer for the package identifier. */
    std::array<char, 512> m_certPathBuf{};       /**< Input buffer for the certificate path field. */
    std::array<char, 128> m_certPassBuf{};       /**< Input buffer for the certificate password. */
    std::array<char, 128> m_appleIdBuf{};        /**< Input buffer for Apple ID. */
    std::array<char, 64> m_teamIdBuf{};          /**< Input buffer for Apple Team ID. */
    std::array<char, 64> m_keychainProfileBuf{}; /**< Input buffer for keychain profile. */
};

} // namespace Horo::Editor
