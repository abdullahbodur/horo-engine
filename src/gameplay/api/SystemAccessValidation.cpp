#include "Horo/Gameplay/GameplayErrors.h"
#include "SystemRegistryDetail.h"

#include <algorithm>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        using ComponentIndex = std::unordered_set<std::string_view>;

        struct IndexedAccess {
            ComponentIndex reads;
            ComponentIndex writes;
        };

        [[nodiscard]] ComponentIndex IndexComponents(const std::span<const ComponentTypeId> components) {
            ComponentIndex index;
            index.reserve(components.size());
            for (const ComponentTypeId &component : components)
                index.emplace(component.Value());
            return index;
        }

        [[nodiscard]] std::vector<IndexedAccess> IndexAccess(const std::span<const GameplaySystemRegistration> registrations) {
            std::vector<IndexedAccess> indexed;
            indexed.reserve(registrations.size());
            for (const GameplaySystemRegistration &registration : registrations)
                indexed.emplace_back(IndexComponents(registration.descriptor.access.reads),
                                     IndexComponents(registration.descriptor.access.writes));
            return indexed;
        }

        [[nodiscard]] bool Intersects(const ComponentIndex &left, const ComponentIndex &right) {
            const ComponentIndex &smaller = left.size() <= right.size() ? left : right;
            const ComponentIndex &larger = left.size() <= right.size() ? right : left;
            return std::ranges::any_of(smaller, [&larger](const std::string_view id) {
                return larger.contains(id);
            });
        }

        [[nodiscard]] bool Conflicts(const IndexedAccess &left, const IndexedAccess &right) {
            return Intersects(left.writes, right.reads) || Intersects(left.writes, right.writes) || Intersects(left.reads, right.writes);
        }
    }  // namespace

    Result<void> ValidateSystemAccess(const std::span<const GameplaySystemRegistration> registrations, const SystemEdges &edges) {
        const std::vector<IndexedAccess> access = IndexAccess(registrations);
        const std::vector<std::vector<bool>> reachable = BuildSystemReachability(edges);
        for (std::size_t left = 0; left < registrations.size(); ++left) {
            for (std::size_t right = left + 1; right < registrations.size(); ++right) {
                if (registrations[left].descriptor.phase != registrations[right].descriptor.phase ||
                    !Conflicts(access[left], access[right]))
                    continue;
                if (!reachable[left][right] && !reachable[right][left])
                    return Result<void>::Failure(MakeError(GameplayErrors::SystemAccessConflict));
            }
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay::Detail
