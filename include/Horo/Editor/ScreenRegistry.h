#pragma once

#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/GuiRoute.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Horo::Editor
{

class GuiScreen;

/**
 * @file ScreenRegistry.h
 * @brief Registry mapping GUI route kinds to factory callbacks.
 */
class ScreenRegistry
{
  public:
    using ScreenFactory =
        std::function<std::unique_ptr<GuiScreen>(const EditorServiceRegistry &services, const GuiRoute &route)>;

    void Register(GuiRouteKind kind, ScreenFactory factory)
    {
        factories_[kind] = std::move(factory);
    }

    [[nodiscard]] std::unique_ptr<GuiScreen> CreateScreen(const GuiRoute &route,
                                                          const EditorServiceRegistry &services) const
    {
        auto it = factories_.find(route.kind);
        if (it != factories_.end())
        {
            return it->second(services, route);
        }
        return nullptr;
    }

    [[nodiscard]] bool HasFactory(GuiRouteKind kind) const noexcept
    {
        return factories_.contains(kind);
    }

  private:
    std::unordered_map<GuiRouteKind, ScreenFactory> factories_;
};

} // namespace Horo::Editor
