#pragma once
#include <cassert>
#include <memory>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "scene/ComponentPool.h"
#include "scene/Entity.h"
#include "scene/IComponentPoolBase.h"

namespace Monolith {
// Type-indexed ECS registry.  Adding a new component type requires zero
// Registry edits. Add<T>, Get<T>, Has<T>, Remove<T>, GetEntities<T> all work
// for any T.
class Registry {
public:
  Entity Create();

  void Destroy(Entity e);

  bool IsAlive(Entity e) const;

  void Clear(); // destroy all entities and component data

  template <typename T> T &Add(Entity e, T component = {}) {
    return GetOrCreatePool<T>().Add(e, std::move(component));
  }

  template <typename T> T &Get(Entity e) { return GetPool<T>().Get(e); }

  template <typename T> const T &Get(Entity e) const {
    return GetPool<T>().Get(e);
  }

  template <typename T> bool Has(Entity e) const {
    auto it = m_pools.find(std::type_index(typeid(T)));
    if (it == m_pools.end())
      return false;
    return static_cast<const ComponentPool<T> *>(it->second.get())->Has(e);
  }

  template <typename T> void Remove(Entity e) {
    auto it = m_pools.find(std::type_index(typeid(T)));
    if (it != m_pools.end())
      it->second->Remove(e);
  }

  template <typename T> const std::vector<Entity> &GetEntities() const {
    static const std::vector<Entity> empty;
    auto it = m_pools.find(std::type_index(typeid(T)));
    if (it == m_pools.end())
      return empty;
    return static_cast<const ComponentPool<T> *>(it->second.get())
        ->GetEntities();
  }

private:
  Entity m_nextId = 0;
  std::unordered_set<Entity> m_aliveSet;
  std::queue<Entity> m_freeList;

  std::unordered_map<std::type_index, std::unique_ptr<IComponentPoolBase>>
      m_pools;

  template <typename T> ComponentPool<T> &GetOrCreatePool() {
    auto key = std::type_index(typeid(T));
    auto it = m_pools.find(key);
    if (it == m_pools.end()) {
      auto pool = std::make_unique<ComponentPool<T>>();
      auto *ptr = pool.get();
      m_pools.emplace(key, std::move(pool));
      return *ptr;
    }
    return *static_cast<ComponentPool<T> *>(it->second.get());
  }

  template <typename T> ComponentPool<T> &GetPool() {
    auto it = m_pools.find(std::type_index(typeid(T)));
    assert(it != m_pools.end() &&
           "Component pool not found — call Add<T> first");
    return *static_cast<ComponentPool<T> *>(it->second.get());
  }

  template <typename T> const ComponentPool<T> &GetPool() const {
    auto it = m_pools.find(std::type_index(typeid(T)));
    assert(it != m_pools.end() &&
           "Component pool not found — call Add<T> first");
    return *static_cast<const ComponentPool<T> *>(it->second.get());
  }
};
} // namespace Monolith
