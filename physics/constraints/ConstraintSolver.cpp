#include "physics/constraints/ConstraintSolver.h"

namespace Monolith {

void ConstraintSolver::Solve(std::vector<ContactConstraint>& constraints, int iterations) {
  for (int i = 0; i < iterations; i++)
    for (auto& c : constraints)
      c.Solve();
}

}  // namespace Monolith
