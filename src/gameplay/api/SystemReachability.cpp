#include "SystemRegistryDetail.h"

namespace Horo::Gameplay::Detail {
    std::vector<std::vector<bool>> BuildSystemReachability(const SystemEdges &edges) {
        std::vector reachable(edges.size(), std::vector<bool>(edges.size()));
        for (std::size_t source = 0; source < edges.size(); ++source) {
            std::vector pending{source};
            while (!pending.empty()) {
                const std::size_t current = pending.back();
                pending.pop_back();
                if (reachable[source][current])
                    continue;
                reachable[source][current] = true;
                pending.insert(pending.end(), edges[current].begin(), edges[current].end());
            }
        }
        return reachable;
    }
}  // namespace Horo::Gameplay::Detail
