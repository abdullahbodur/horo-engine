#pragma once

/**
 * @file NativeBehavior.h
 * @brief Declarative native behavior annotation and generated-factory helpers.
 */

#include "Horo/Gameplay/Behavior.h"

#include <memory>
#include <type_traits>

namespace Horo::Gameplay {
    /** @brief Builds one module-owned factory registration for an annotated native behavior type. */
    template <typename Behavior> [[nodiscard]] BehaviorRegistration MakeNativeBehaviorRegistration(const char *stableTypeId) {
        static_assert(std::is_base_of_v<IBehaviorInstance, Behavior>);
        BehaviorDescriptor descriptor = Behavior::DescribeBehavior();
        descriptor.typeId = BehaviorTypeId::Parse(stableTypeId).Value();
        return BehaviorRegistration{
            std::move(descriptor),
            BehaviorFactoryBinding{
                nullptr,
                [](void *) -> IBehaviorInstance * {
            // The exporting gameplay module must allocate and destroy its own instances.
            return std::make_unique<Behavior>().release();
        },
                [](void *, IBehaviorInstance *instance) noexcept {
            const std::unique_ptr<Behavior> owned{static_cast<Behavior *>(instance)};
        },
            },
        };
    }
}  // namespace Horo::Gameplay

/**
 * @brief Marks one native behavior for generated complete-bundle discovery.
 *
 * Invoke at global namespace with a simple type identifier. The behavior type
 * provides `static BehaviorDescriptor DescribeBehavior()`; the stable ID remains
 * separate from its C++ class name.
 */
#define HORO_BEHAVIOR(Type, StableTypeId)                                                                                                  \
    Horo::Gameplay::BehaviorRegistration HoroGeneratedBehaviorRegistration_##Type() {                                                      \
        return Horo::Gameplay::MakeNativeBehaviorRegistration<Type>(StableTypeId);                                                         \
    }
