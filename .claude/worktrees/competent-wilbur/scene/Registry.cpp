#include "scene/Registry.h"

namespace Monolith {

Entity Registry::Create() {
  Entity e;
  if (!m_freeList.empty()) {
    e = m_freeList.front();
    m_freeList.pop();
  } else {
    e = m_nextId++;
  }
  m_aliveSet.insert(e);
  return e;
}

void Registry::Destroy(Entity e) {
  for (auto& [key, pool] : m_pools)
    pool->Remove(e);
  m_aliveSet.erase(e);
  m_freeList.push(e);
}

bool Registry::IsAlive(Entity e) const {
  return m_aliveSet.count(e) != 0;
}

void Registry::Clear() {
  for (auto& [key, pool] : m_pools)
    pool->ClearAll();
  m_aliveSet.clear();
  while (!m_freeList.empty())
    m_freeList.pop();
  m_nextId = 0;
}

}  // namespace Monolith
