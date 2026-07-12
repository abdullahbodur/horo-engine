#pragma once

#include <cassert>
#include <typeindex>
#include <unordered_map>

namespace Horo::Editor {

/**
 * @file EditorServiceRegistry.h
 * @brief Type-indexed registry for composition-time service provisioning.
 */
class EditorServiceRegistry {
public:
    EditorServiceRegistry() = default;

    template <typename T>
    void Register(T& serviceInstance) {
        services_[std::type_index(typeid(T))] = &serviceInstance;
    }

    template <typename T>
    void RegisterConst(const T& serviceInstance) {
        constServices_[std::type_index(typeid(T))] = &serviceInstance;
    }

    template <typename T>
    [[nodiscard]] T& Get() const {
        const auto it = services_.find(std::type_index(typeid(T)));
        assert(it != services_.end() && "Requested mutable service not found in EditorServiceRegistry!");
        return *static_cast<T*>(it->second);
    }

    template <typename T>
    [[nodiscard]] const T& GetConst() const {
        if (const auto it = constServices_.find(std::type_index(typeid(T))); it != constServices_.end()) {
            return *static_cast<const T*>(it->second);
        }
        const auto mutIt = services_.find(std::type_index(typeid(T)));
        assert(mutIt != services_.end() && "Requested const service not found in EditorServiceRegistry!");
        return *static_cast<const T*>(mutIt->second);
    }

    template <typename T>
    [[nodiscard]] T* TryGet() const noexcept {
        if (const auto it = services_.find(std::type_index(typeid(T))); it != services_.end()) {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* TryGetConst() const noexcept {
        if (const auto it = constServices_.find(std::type_index(typeid(T))); it != constServices_.end()) {
            return static_cast<const T*>(it->second);
        }
        if (const auto mutIt = services_.find(std::type_index(typeid(T))); mutIt != services_.end()) {
            return static_cast<const T*>(mutIt->second);
        }
        return nullptr;
    }

private:
    std::unordered_map<std::type_index, void*> services_;
    std::unordered_map<std::type_index, const void*> constServices_;
};

} // namespace Horo::Editor
