#pragma once

#include "Horo/Foundation/DataBus.h"

#include <thread>

namespace Horo::Editor
{
/** @brief Session-scoped, editor-thread notification surface. */
class EditorDataBus
{
  public:
    EditorDataBus() = default;

    template <typename EventT, typename Handler> Subscription Subscribe(Handler &&handler)
    {
        AssertOwnerThread();
        return m_bus.Subscribe<EventT>(std::forward<Handler>(handler));
    }

    template <typename EventT> void Publish(const EventT &event)
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
    std::thread::id m_ownerThread = std::this_thread::get_id();
    EngineDataBus m_bus{EngineDataBusConfig{.logCategory = "editor.data_bus"}};
};
} // namespace Horo::Editor
