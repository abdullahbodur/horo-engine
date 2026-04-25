#pragma once
#include <vector>

#include "physics/constraints/ContactConstraint.h"

namespace Monolith {
class ConstraintSolver {
public:
  static constexpr int DEFAULT_ITERATIONS = 15;

  void Solve(std::vector<ContactConstraint> &constraints,
             int iterations = DEFAULT_ITERATIONS) const;
};
} // namespace Monolith
