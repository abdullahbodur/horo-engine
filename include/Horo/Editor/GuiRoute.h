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
struct WelcomeRouteParameters {};

/**
 * @brief Route parameters for the project browser screen.
 */
struct ProjectBrowserRouteParameters {};

/**
 * @brief Route parameters for the project creation screen.
 */
struct ProjectCreationRouteParameters {
    std::optional<std::string> initialTemplate;
};

/**
 * @brief Route parameters for the project loading screen.
 */
struct ProjectLoadingRouteParameters {
    std::string projectRoot;
    std::string projectName;
};

/**
 * @brief Route parameters for an editor workspace screen.
 */
struct EditorWorkspaceRouteParameters {
    std::string projectRoot;
    std::optional<std::string> initialScene;
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

/**
 * @brief Returns true when the route kind and payload alternative match.
 * @param route Route request to validate.
 * @return True when the payload is structurally valid for the route kind.
 */
[[nodiscard]] bool IsRoutePayloadValid(const GuiRoute& route) noexcept;

} // namespace Horo::Editor
