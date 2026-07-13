#pragma once

#include "Horo/Editor/EditorMenuModel.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/EditorStatusBarModel.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Horo
{
class EngineDataBus;
} // namespace Horo

namespace Horo::Editor
{

struct EditorGuiContext;
class EditorModalHost;
class EditorSettingsService;
class LocalizationService;
class ProjectCreationService;
class EditorStatusBar;

/**
 * @file GuiScreenHost.h
 * @brief Top-level screen host, route navigation, and leave-guard contracts.
 */

/** @brief Reason why a screen requires resolution before leaving. */
enum class LeaveRequirementKind
{
    DirtyDocument,
    UnsavedDraft,
    RunningOperation,
    RecoveryDecision,
    NativeDialogCompletion,
};

/** @brief Allowed actions when resolving a leave requirement. */
enum class LeaveAction
{
    Save,
    Discard,
    CancelOperation,
    KeepRunning,
    Wait,
    Stay,
};

using LeaveSubjectId = std::uint64_t;
using LeaveRequirementRevision = std::uint32_t;
using ScreenId = std::uint32_t;
using ScreenInstanceId = std::uint64_t;
using NavigationAttemptId = std::uint64_t;
using ProjectCreationOperationId = std::uint64_t;

/** @brief Screen-space region reserved by the editor shell for active screen content. */
struct GuiContentRegion
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

/** @brief Description of a pending leave blocker requiring resolution. */
struct LeaveRequirement
{
    LeaveRequirementKind kind;
    LeaveSubjectId subject;
    LeaveRequirementRevision revision;
    std::vector<LeaveAction> allowedActions;
};

/** @brief Initial decision on whether a screen allows leaving. */
enum class LeaveDisposition
{
    Allow,
    Deny,
    RequireResolution,
};

/** @brief Result of querying CanLeave on a screen. */
struct LeaveDecision
{
    LeaveDisposition disposition;
    std::optional<LeaveRequirement> requirement;
};

/** @brief Target representing process application termination. */
struct ApplicationCloseTarget
{
    bool operator==(const ApplicationCloseTarget &) const = default;
};

/** @brief Destination target for a leave request. */
struct LeaveTarget
{
    std::variant<GuiRoute, ApplicationCloseTarget> value;
};

/** @brief Resolution selected by the user or host for a pending leave requirement. */
struct LeaveResolution
{
    LeaveSubjectId subject;
    LeaveRequirementRevision revision;
    LeaveAction action;
};

/** @brief Error codes produced when a leave resolution attempt fails. */
enum class LeaveErrorCode
{
    StaleSubject,
    ActionNotAllowed,
    OperationFailed,
};

/** @brief Diagnostic error when resolve-leave fails. */
struct LeaveError
{
    LeaveErrorCode code;
    std::vector<Diagnostic> diagnostics;
};

/** @brief Stable codes for navigation failures. */
enum class NavigationErrorCode
{
    InvalidRouteParameters,
    Busy,
    LeaveDenied,
    Cancelled,
    LeaveResolutionLimitExceeded,
    DestinationConstructionFailed,
    DestinationEntryFailed,
};

/** @brief Error details returned when navigation cannot commit. */
struct NavigationError
{
    NavigationErrorCode code;
    std::vector<Diagnostic> diagnostics;
};

/**
 * @brief Abstract contract for a top-level GUI screen inside HoroEditor.
 */
class GuiScreen
{
  public:
    virtual ~GuiScreen() = default;

    /** @brief Returns stable identifier for this screen class. */
    [[nodiscard]] virtual ScreenId Id() const = 0;

    /** @brief Called during navigation commit before becoming the active screen. */
    [[nodiscard]] virtual Result<void> OnEnter(const GuiRoute &route) = 0;

    /** @brief Updates screen-local simulation and time-dependent logic. */
    virtual void OnUpdate(float dt) = 0;

    /** @brief Renders the screen UI inside the shell-provided application content area. */
    virtual void Draw(const GuiContentRegion &contentRegion) = 0;

    /** @brief Appends active panel IDs used by panel-scoped status contribution visibility. */
    virtual void CollectActivePanelIds(std::vector<std::string_view> &output) const
    {
        static_cast<void>(output);
    }

    /**
     * @brief Gives the active screen an opportunity to handle an application menu action.
     * @param action Stable action emitted by a native or in-window menu renderer.
     * @return True when the screen consumed the action.
     */
    virtual bool HandleMenuAction(EditorMenuAction action)
    {
        static_cast<void>(action);
        return false;
    }

    /** @brief Queries whether navigation to target is permitted right now. */
    [[nodiscard]] virtual LeaveDecision CanLeave(const LeaveTarget &target) const = 0;

    /** @brief Resolves a pending leave requirement with a chosen action. */
    [[nodiscard]] virtual Result<LeaveDecision> ResolveLeave(const LeaveTarget &target,
                                                             const LeaveResolution &resolution) = 0;

    /** @brief Called immediately before screen destruction upon route change. */
    virtual void OnLeave() = 0;
};

/**
 * @brief Coordinates top-level screens, route transitions, and leave guards.
 */
class GuiScreenHost
{
  public:
    /**
     * @brief Constructs the screen host with required application service references.
     */
    explicit GuiScreenHost(const EditorGuiContext &context, EditorModalHost &modalHost,
                           EditorSettingsService &settingsService, LocalizationService &localization,
                           EngineDataBus &engineEvents, ProjectCreationService &creationService,
                           std::uintptr_t logoTexture = 0);

    ~GuiScreenHost();

    GuiScreenHost(const GuiScreenHost &) = delete;
    GuiScreenHost &operator=(const GuiScreenHost &) = delete;
    GuiScreenHost(GuiScreenHost &&) = delete;
    GuiScreenHost &operator=(GuiScreenHost &&) = delete;

    /**
     * @brief Requests navigation to a new top-level route.
     * @param destination Target route.
     * @return Success when accepted. Requests made from a screen callback are queued until that callback returns.
     */
    Result<void> Navigate(GuiRoute destination);

    /**
     * @brief Requests application close, passing through leave guards.
     * @return Success if allowed, or error if blocked/denied.
     */
    Result<void> RequestCloseApplication();

    /** @brief Returns the currently active route. */
    [[nodiscard]] const GuiRoute &ActiveRoute() const noexcept;

    /** @brief Reports whether application exit has been approved and requested. */
    [[nodiscard]] bool IsApplicationCloseRequested() const noexcept;

    /** @brief Sets active project creation operation ID being tracked across route transitions. */
    void SetActiveCreationId(std::optional<ProjectCreationOperationId> id) noexcept;

    /** @brief Returns active project creation operation ID if one is currently tracked. */
    [[nodiscard]] std::optional<ProjectCreationOperationId> GetActiveCreationId() const noexcept;

    /** @brief Updates the active screen and checks pending leave dialogs. */
    void OnUpdate(float dt);

    /** @brief Renders the active screen and any active leave-resolution modals. */
    void Draw();

    /**
     * @brief Routes a platform-neutral application menu action through host navigation and the active screen.
     * @param action Action selected by the user.
     */
    void DispatchMenuAction(EditorMenuAction action);

    /** @brief Returns mutable service registry used for dependency injection. */
    [[nodiscard]] EditorServiceRegistry &Services() noexcept;

    /** @brief Returns const service registry used for dependency injection. */
    [[nodiscard]] const EditorServiceRegistry &Services() const noexcept;

    /** @brief Returns mutable screen factory registry. */
    [[nodiscard]] ScreenRegistry &Screens() noexcept;

    /** @brief Returns const screen factory registry. */
    [[nodiscard]] const ScreenRegistry &Screens() const noexcept;

    /** @brief Returns the host-owned registry used by built-ins, modules, and plugin adapters. */
    [[nodiscard]] EditorStatusItemRegistry &StatusItems() noexcept;

    /** @brief Returns the host-owned status-item registry for read-only inspection. */
    [[nodiscard]] const EditorStatusItemRegistry &StatusItems() const noexcept;

  private:
    Result<void> ExecuteLeaveCheckAndCommit(const LeaveTarget &target);
    void FlushPendingNavigation();
    void CommitRoute(GuiRoute destination);
    void PresentLeaveDialog(const LeaveRequirement &requirement, const LeaveTarget &target);
    void ExecuteLeaveResolution(LeaveAction action, LeaveRequirement requirement, LeaveTarget target);
    std::unique_ptr<GuiScreen> CreateScreen(const GuiRoute &route);

    const EditorGuiContext *context_;
    EditorModalHost *modalHost_;
    EditorSettingsService *settingsService_;
    LocalizationService *localization_;
    EngineDataBus *engineEvents_;
    ProjectCreationService *creationService_;
    std::uintptr_t logoTexture_{0};

    EditorServiceRegistry services_;
    ScreenRegistry screenRegistry_;
    WorkspacePanelRegistry workspacePanelRegistry_;
    EditorStatusItemRegistry statusItemRegistry_;
    std::unique_ptr<EditorStatusBar> statusBar_;
    std::vector<std::string_view> activeStatusPanelIds_;

    GuiRoute activeRoute_;
    GuiRouteRevision activeRevision_{0};
    ScreenInstanceId nextScreenInstanceId_{1};
    NavigationAttemptId nextAttemptId_{1};

    std::unique_ptr<GuiScreen> activeScreen_;
    std::optional<GuiRoute> pendingNavigation_;
    std::optional<ProjectCreationOperationId> activeCreationId_;
    bool closeRequested_{false};
    bool navigationBusy_{false};
    bool isScreenCallbackActive_{false};

    std::optional<LeaveRequirement> pendingRequirement_;
    std::optional<LeaveTarget> pendingTarget_;
    std::size_t resolutionAttemptCount_{0};
};

} // namespace Horo::Editor
