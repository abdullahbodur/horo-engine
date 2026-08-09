#pragma once

#include "Horo/Editor/EditorDataBus.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
/** @brief Visual importance of a user-facing editor notification. */
enum class NotificationSeverity : std::uint8_t
{
    Info,
    Success,
    Warning,
    Error,
};

/** @brief User-executable action button descriptor within a notification snackbar. */
struct NotificationAction
{
    std::string label;
    std::string actionId;
};

/** @brief Generic editor notification delivered to transient UI surfaces. */
struct NotificationEvent
{
    static constexpr auto HoroEventTypeName = "EditorNotificationEvent";

    std::uint64_t id{0};
    std::string source;
    NotificationSeverity severity = NotificationSeverity::Info;
    std::string title;
    std::string message;
    std::string deduplicationKey;
    float durationSeconds{5.0f};
    bool dismissible{true};
    std::vector<NotificationAction> actions;
};

/**
 * @brief Publishes user-facing notifications on one editor session bus.
 * @note The service does not own UI, logging, or notification lifetime.
 */
class NotificationService final
{
  public:
    explicit NotificationService(EditorDataBus &events) noexcept : events_(&events) {}

    /**
     * @brief Publishes one generic notification for any editor module.
     * @param source Stable producer name, such as `gameplay`, `asset`, or `plugin:example`.
     * @param severity Visual notification severity.
     * @param message User-facing message.
     * @param title Optional short heading.
     * @param deduplicationKey Optional key used by consumers to coalesce repeats.
     * @param durationSeconds Auto-dismiss duration in seconds (0.0f = sticky until dismissed).
     * @param actions Optional action buttons.
     */
    void Publish(std::string_view source, NotificationSeverity severity, std::string message,
                 std::string title = {}, std::string deduplicationKey = {}, float durationSeconds = 5.0f,
                 std::vector<NotificationAction> actions = {}) const;

    /** @brief Helper to publish a full NotificationEvent structure. */
    void Publish(NotificationEvent event) const;

  private:
    EditorDataBus *events_;
};
} // namespace Horo::Editor
