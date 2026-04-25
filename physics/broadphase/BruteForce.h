#pragma once
#include <utility>
#include <vector>

namespace Monolith {
class RigidBody;

namespace BruteForce {
// Returns all pairs (i, j) with i < j whose AABBs overlap.
// Uses sphere/box AABB from collider, positioned at body.position.
std::vector<std::pair<int, int>>
FindOverlappingPairs(const std::vector<RigidBody *> &bodies);
} // namespace BruteForce
} // namespace Monolith
