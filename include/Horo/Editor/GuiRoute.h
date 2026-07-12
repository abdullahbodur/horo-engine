#pragma once

#include <optional>
#include <string>
#include <variant>

namespace Horo::Editor {

/**
 * @file GuiRoute.h
 * @brief Typed top-level HoroEditor screen route contracts.
 */

/**
 * @brief Top-level screen route inside the single HoroEditor application.
 */
enum class GuiRouteKind {
    Welcome,
    ProjectBrowser,
    ProjectCreation,
    ProjectLoading,
    EditorWorkspace,
};

/**
 * @brief Route parameters for the welcome screen.
 */
struct WelcomeRouteParameters {
    bool operator==(const WelcomeRouteParameters&) const = default;
};

/**
 * @brief Route parameters for the project browser screen.
 */
struct ProjectBrowserRouteParameters {
    bool operator==(const ProjectBrowserRouteParameters&) const = default;
};

/**
 * @brief Route parameters for the project creation screen.
 */
struct ProjectCreationRouteParameters {
    std::optional<std::string> initialTemplate;
    bool operator==(const ProjectCreationRouteParameters&) const = default;
};

/**
 * @brief Route parameters for the project loading screen.
 */
struct ProjectLoadingRouteParameters {
    std::string projectRoot;
    std::string projectName;
    bool operator==(const ProjectLoadingRouteParameters&) const = default;
};

/**
 * @brief Route parameters for an editor workspace screen.
 */
struct EditorWorkspaceRouteParameters {
    std::string projectRoot;
    std::optional<std::string> initialScene;
    bool operator==(const EditorWorkspaceRouteParameters&) const = default;
};

/**
 * @brief Closed set of route-specific parameter payloads.
 */
using RouteParameters = std::variant<WelcomeRouteParameters,
                                     ProjectBrowserRouteParameters,
                                     ProjectCreationRouteParameters,
                                     ProjectLoadingRouteParameters,
                                     EditorWorkspaceRouteParameters>;

/**
 * @brief Typed route request validated by the screen host before navigation.
 */
struct GuiRoute {
    GuiRouteKind kind;
    RouteParameters parameters;
};

using GuiRouteRevision = std::uint64_t;

/**
 * @brief Process-level lifecycle notification published when the active route changes.
 */
struct GuiRouteChangedEvent {
    static constexpr auto HoroEventTypeName = "Horo.Editor.GuiRouteChangedEvent";
    GuiRouteKind previousKind;
    GuiRouteKind currentKind;
    GuiRouteRevision previousRevision;
    GuiRouteRevision currentRevision;
};

/**
 * @brief Returns true when the route kind and payload alternative match.
 * @param route Route request to validate.
 * @return True when the payload is structurally valid for the route kind.
 */
[[nodiscard]] bool IsRoutePayloadValid(const GuiRoute& route) noexcept;

/**
 * @brief Checks if two routes have the same kind and identical parameters.
 * @param lhs First route to compare.
 * @param rhs Second route to compare.
 * @return True when both routes have identical kind and parameters.
 */
[[nodiscard]] bool AreRoutesIdentical(const GuiRoute& lhs, const GuiRoute& rhs) noexcept;

} // namespace Horo::Editor
