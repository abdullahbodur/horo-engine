#include "Horo/Editor/ScreenRegistry.h"

#include "Horo/Editor/GuiScreenHost.h"

#include <utility>

namespace Horo::Editor {
    /** @copydoc ScreenRegistry::Register */
    void ScreenRegistry::Register(const GuiRouteKind kind, ScreenFactory factory) {
        factories_[kind] = std::move(factory);
    }

    /** @copydoc ScreenRegistry::CreateScreen */
    std::unique_ptr<GuiScreen> ScreenRegistry::CreateScreen(const GuiRoute &route, const EditorServiceRegistry &services) const {
        if (const auto it = factories_.find(route.kind); it != factories_.end()) {
            return it->second(services, route);
        }
        return nullptr;
    }

    /** @copydoc ScreenRegistry::HasFactory */
    bool ScreenRegistry::HasFactory(const GuiRouteKind kind) const noexcept {
        return factories_.contains(kind);
    }
}  // namespace Horo::Editor
