#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/NotificationService.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace Horo::Editor
{
/** @brief Invocation data when a user clicks a snackbar action button. */
struct SnackbarActionInvokedEvent
{
    std::string source;
    std::string actionId;
    std::uint64_t notificationId{0};
};

/** @brief Active snackbar state tracked by the host. */
struct ActiveSnackbar
{
    NotificationEvent event;
    float elapsedSeconds{0.0f};
    std::size_t repeatCount{1};
};

/**
 * @brief Floating UI overlay host that renders active editor notification snackbars.
 */
class EditorSnackbarHost final
{
  public:
    explicit EditorSnackbarHost(EditorDataBus &events);
    ~EditorSnackbarHost();

    EditorSnackbarHost(const EditorSnackbarHost &) = delete;
    EditorSnackbarHost &operator=(const EditorSnackbarHost &) = delete;

    /**
     * @brief Updates lifetime state and renders active snackbar overlay.
     * @param context Editor GUI context (theme, localization, fonts).
     * @param elapsedSeconds Frame delta time in seconds.
     * @return Optional action invocation event if user clicked an action button.
     */
    std::optional<SnackbarActionInvokedEvent> Draw(const EditorGuiContext &context, float elapsedSeconds);

    /** @brief Clears all active and queued snackbars. */
    void Clear();

  private:
    void OnNotificationEvent(const NotificationEvent &event);
    void DismissAt(std::size_t index);

    EditorDataBus *events_;
    Subscription subscription_;
    std::deque<ActiveSnackbar> activeSnackbars_;
};
} // namespace Horo::Editor
