#pragma once

#include "Horo/Foundation/DataBus.h"

#include <thread>

namespace Horo::Editor
{
    /** @brief Session-scoped, editor-thread notification surface. */
    class EditorDataBus
    {
    public:
        EditorDataBus() : m_ownerThread(std::this_thread::get_id()), m_bus(EngineDataBusConfig{.logCategory = "editor.data_bus"}) {}

        template <typename EventT, typename Handler>
        Subscription Subscribe(Handler &&handler)
        {
            AssertOwnerThread();
            return m_bus.Subscribe<EventT>(std::forward<Handler>(handler));
        }

        template <typename EventT>
        void Publish(const EventT &event)
        {
            AssertOwnerThread();
            m_bus.Publish(event);
        }

        void Clear()
        {
            AssertOwnerThread();
            m_bus.Clear();
        }

    private:
        void AssertOwnerThread() const noexcept;
        std::thread::id m_ownerThread;
        EngineDataBus m_bus;
    };
} // namespace Horo::Editor
