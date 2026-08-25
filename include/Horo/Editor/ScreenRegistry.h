#pragma once

#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Editor/GuiScreen.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Horo::Editor {

    /**
     * @file ScreenRegistry.h
     * @brief Registry mapping GUI route kinds to factory callbacks.
     */
    class ScreenRegistry {
    public:
        using ScreenFactory = std::function<std::unique_ptr<GuiScreen>(const EditorServiceRegistry &services, const GuiRoute &route)>;

        /**
         * @brief Registers or replaces the screen factory for one route kind.
         * @param kind Route kind owned by the
         * factory.
         * @param factory Factory invoked when that route is activated.
         */
        void Register(const GuiRouteKind kind, ScreenFactory factory) {
            factories_[kind] = std::move(factory);
        }

        /**
         * @brief Creates the screen registered for a route.
         * @param route Route and payload passed to the selected
         * factory.
         * @param services Editor services passed to the selected factory.
         * @return A screen instance, or null
         * when the route has no factory.
         */
        [[nodiscard]] std::unique_ptr<GuiScreen> CreateScreen(const GuiRoute &route, const EditorServiceRegistry &services) const {
            if (const auto it = factories_.find(route.kind); it != factories_.end()) {
                return it->second(services, route);
            }
            return nullptr;
        }

        /**
         * @brief Reports whether a route kind has a registered factory.
         * @param kind Route kind to query.
         *
         * @return True when a factory is registered.
         */
        [[nodiscard]] bool HasFactory(const GuiRouteKind kind) const noexcept {
            return factories_.contains(kind);
        }

    private:
        std::unordered_map<GuiRouteKind, ScreenFactory> factories_;
    };

}  // namespace Horo::Editor
