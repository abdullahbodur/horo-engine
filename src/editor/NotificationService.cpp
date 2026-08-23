#include "Horo/Editor/NotificationService.h"

#include <atomic>

namespace Horo::Editor {
    namespace {
        std::atomic<std::uint64_t> g_notificationIdCounter{1};  // NOSONAR(cpp:S5421)
    }

    void NotificationService::Publish(std::string_view source, NotificationSeverity severity, std::string message, std::string title,
                                      std::string deduplicationKey, float durationSeconds, std::vector<NotificationAction> actions) const {
        Publish(NotificationEvent{.id = g_notificationIdCounter.fetch_add(1),
                                  .source = std::string(source),
                                  .severity = severity,
                                  .title = std::move(title),
                                  .message = std::move(message),
                                  .deduplicationKey = std::move(deduplicationKey),
                                  .durationSeconds = durationSeconds,
                                  .dismissible = true,
                                  .actions = std::move(actions)});
    }

    void NotificationService::Publish(NotificationEvent event) const {
        if (events_ == nullptr)
            return;

        if (event.id == 0)
            event.id = g_notificationIdCounter.fetch_add(1);

        if (!event.deduplicationKey.empty() && event.deduplicationKey.find("::") == std::string::npos) {
            event.deduplicationKey = event.source + "::" + event.deduplicationKey;
        }

        events_->Publish(event);
    }
}  // namespace Horo::Editor
