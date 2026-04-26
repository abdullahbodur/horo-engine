#include "scene/Registry.h"

namespace Horo {
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
        for (const auto &[key, pool]: m_pools)
            pool->Remove(e);
        m_aliveSet.erase(e);
        m_freeList.push(e);
    }

    bool Registry::IsAlive(Entity e) const { return m_aliveSet.contains(e); }

    void Registry::Clear() {
        for (const auto &[key, pool]: m_pools)
            pool->ClearAll();
        m_aliveSet.clear();
        while (!m_freeList.empty())
            m_freeList.pop();
        m_nextId = 0;
    }
} // namespace Horo
