#pragma once

#include "Horo/PCG/PCGGraphAsset.h"

namespace Horo::PCG::Detail {
    [[nodiscard]] Result<std::size_t> ValidateAndCanonicalizeGraph(PCGGraphSourceData &candidate, const PCGGraphSourceContext &context,
                                                                   const PCGGraphSourceLimits &limits);
}  // namespace Horo::PCG::Detail
