#pragma once

/**
 * @file NativeBehavior.h
 * @brief Declarative native behavior annotation and generated-factory helpers.
 */

#include "Horo/Gameplay/Behavior.h"

#include <memory>
#include <type_traits>

namespace Horo::Gameplay {
    namespace Detail {
        template <typename Behavior> [[nodiscard]] IBehaviorInstance *CreateNativeBehavior(void *) {
            return std::make_unique<Behavior>().release();
        }

        template <typename Behavior> void DestroyNativeBehavior(void *, IBehaviorInstance *instance) noexcept {
            const std::unique_ptr<Behavior> owned{static_cast<Behavior *>(instance)};
        }
    }  // namespace Detail

    /** @brief Builds host-copyable descriptor metadata for an annotated native behavior type. */
    template <typename Behavior> [[nodiscard]] BehaviorDescriptor MakeNativeBehaviorDescriptor(const char *stableTypeId) {
        static_assert(std::is_base_of_v<IBehaviorInstance, Behavior>);
        BehaviorDescriptor descriptor = Behavior::DescribeBehavior();
        descriptor.typeId = BehaviorTypeId::Parse(stableTypeId).Value();
        return descriptor;
    }

    /** @brief Builds the module-owned factory callbacks paired with generated descriptor metadata. */
    template <typename Behavior> [[nodiscard]] BehaviorFactoryBinding MakeNativeBehaviorFactoryBinding() {
        static_assert(std::is_base_of_v<IBehaviorInstance, Behavior>);
        return {
            .userData = nullptr,
            .create = &Detail::CreateNativeBehavior<Behavior>,
            .destroy = &Detail::DestroyNativeBehavior<Behavior>,
        };
    }

    /** @brief Builds one module-owned factory registration for an annotated native behavior type. */
    template <typename Behavior> [[nodiscard]] BehaviorRegistration MakeNativeBehaviorRegistration(const char *stableTypeId) {
        return {MakeNativeBehaviorDescriptor<Behavior>(stableTypeId), MakeNativeBehaviorFactoryBinding<Behavior>()};
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
    Horo::Gameplay::BehaviorDescriptor HoroGeneratedBehaviorDescriptor_##Type() {                                                          \
        return Horo::Gameplay::MakeNativeBehaviorDescriptor<Type>(StableTypeId);                                                           \
    }                                                                                                                                      \
    Horo::Gameplay::BehaviorFactoryBinding HoroGeneratedBehaviorFactory_##Type() {                                                         \
        return Horo::Gameplay::MakeNativeBehaviorFactoryBinding<Type>();                                                                   \
    }
