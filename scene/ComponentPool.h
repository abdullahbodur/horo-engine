#pragma once
#include <cassert>
#include <unordered_map>
#include <vector>

#include "scene/Entity.h"
#include "scene/IComponentPoolBase.h"

namespace Monolith {
    // Dense SoA component pool.
    // Provides O(1) add/get/remove with packed storage.
    template<typename T>
    class ComponentPool : public IComponentPoolBase {
    public:
        bool Has(Entity e) const { return m_sparse.contains(e); }

        T &Add(Entity e, T component = {}) {
            assert(!Has(e) && "Entity already has component");
            m_sparse[e] = static_cast<uint32_t>(m_dense.size());
            m_denseEntities.push_back(e);
            m_dense.push_back(std::move(component));
            return m_dense.back();
        }

        T &Get(Entity e) {
            assert(Has(e) && "Entity does not have component");
            return m_dense[m_sparse.at(e)];
        }

        const T &Get(Entity e) const {
            assert(Has(e) && "Entity does not have component");
            return m_dense[m_sparse.at(e)];
        }

        void Remove(Entity e) override {
            if (!Has(e))
                return;

            uint32_t idx = m_sparse.at(e);

            if (const uint32_t last = static_cast<uint32_t>(m_dense.size()) - 1;
                idx != last) {
                // Swap with last
                m_dense[idx] = std::move(m_dense[last]);
                m_denseEntities[idx] = m_denseEntities[last];
                m_sparse[m_denseEntities[idx]] = idx;
            }

            m_dense.pop_back();
            m_denseEntities.pop_back();
            m_sparse.erase(e);
        }

        void ClearAll() override {
            m_sparse.clear();
            m_dense.clear();
            m_denseEntities.clear();
        }

        const std::vector<T> &GetAll() const { return m_dense; }
        const std::vector<Entity> &GetEntities() const { return m_denseEntities; }
        size_t Size() const { return m_dense.size(); }

    private:
        std::unordered_map<Entity, uint32_t> m_sparse;
        std::vector<T> m_dense;
        std::vector<Entity> m_denseEntities;
    };
} // namespace Monolith
