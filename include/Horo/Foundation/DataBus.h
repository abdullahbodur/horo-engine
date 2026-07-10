#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

namespace Horo
{
    using EventTypeId = std::uint64_t;

    /** @brief Hashes an explicit, stable event name with FNV-1a. */
    constexpr EventTypeId HashEventTypeName(const std::string_view name) noexcept
    {
        EventTypeId hash = 14695981039346656037ull;
        for (const char character : name)
        {
            hash ^= static_cast<EventTypeId>(static_cast<unsigned char>(character));
            hash *= 1099511628211ull;
        }
        return hash;
    }

    template <typename EventT>
    [[nodiscard]] constexpr EventTypeId EventType() noexcept
    {
        return HashEventTypeName(EventT::HoroEventTypeName);
    }

    /** @brief Move-only RAII ownership of one data-bus handler. */
    class Subscription
    {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;
        Subscription(Subscription &&other) noexcept;
        Subscription &operator=(Subscription &&other) noexcept;
        void Reset() noexcept;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_release); }
    private:
        friend class EngineDataBus;
        explicit Subscription(std::function<void()> release) : m_release(std::move(release)) {}
        std::function<void()> m_release;
    };

    struct EngineDataBusConfig
    {
        std::size_t maxAsyncQueueSize = 1024;
        bool traceDispatch = true;
        const char *logCategory = "engine.data_bus";
    };

    /** @brief Process-scoped typed notification bus; commands and state ownership remain external. */
    class EngineDataBus
    {
    public:
        explicit EngineDataBus(EngineDataBusConfig config = {});
        ~EngineDataBus();
        EngineDataBus(const EngineDataBus &) = delete;
        EngineDataBus &operator=(const EngineDataBus &) = delete;
        EngineDataBus(EngineDataBus &&) noexcept;
        EngineDataBus &operator=(EngineDataBus &&) noexcept;

        template <typename EventT, typename Handler>
        Subscription Subscribe(Handler &&handler)
        {
            static_assert(requires { EventT::HoroEventTypeName; }, "Events require a stable HoroEventTypeName.");
            auto typed = [callback = std::forward<Handler>(handler)](const void *raw) { callback(*static_cast<const EventT *>(raw)); };
            return SubscribeErased(EventType<EventT>(), EventT::HoroEventTypeName, std::move(typed));
        }

        template <typename EventT>
        void Publish(const EventT &event)
        {
            static_assert(requires { EventT::HoroEventTypeName; }, "Events require a stable HoroEventTypeName.");
            auto replay = std::make_shared<EventT>(event);
            PublishErased(EventType<EventT>(), EventT::HoroEventTypeName, replay.get(),
                          [replay = std::move(replay)](EngineDataBus &bus) { bus.Publish(*replay); });
        }

        template <typename EventT>
        void PublishAsync(EventT event)
        {
            static_assert(requires { EventT::HoroEventTypeName; }, "Events require a stable HoroEventTypeName.");
            auto payload = std::make_shared<EventT>(std::move(event));
            QueueErased(EventType<EventT>(), EventT::HoroEventTypeName, std::move(payload),
                        [](EngineDataBus &bus, const void *raw) { bus.Publish(*static_cast<const EventT *>(raw)); });
        }

        /** @brief Delivers worker-thread notifications at the owner synchronization point. */
        void DispatchQueued();
        /** @brief Removes all handlers and queued notifications. */
        void Clear();

    private:
        using Handler = std::function<void(const void *)>;
        using DeferredPublisher = std::function<void(EngineDataBus &)>;
        using QueuedPublisher = std::function<void(EngineDataBus &, const void *)>;
        struct State;
        Subscription SubscribeErased(EventTypeId type, std::string_view name, Handler handler);
        void PublishErased(EventTypeId type, std::string_view name, const void *raw, DeferredPublisher retry);
        void QueueErased(EventTypeId type, std::string_view name, std::shared_ptr<void> payload, QueuedPublisher publish);
        std::shared_ptr<State> m_state;
    };
} // namespace Horo
