#pragma once

#include "IR.hpp"

#include <cstddef>

namespace zplus::ir {

struct OptimizationStatistics {
    std::size_t foldedInstructions = 0;
    std::size_t propagatedConstants = 0;
    std::size_t simplifiedInstructions = 0;
    std::size_t commonSubexpressions = 0;
    std::size_t removedInstructions = 0;
    std::size_t removedBlocks = 0;
};

std::size_t eliminateUnreachableBlocks(Module& module);
std::size_t propagateConstants(Module& module);
std::size_t foldConstants(Module& module);
std::size_t simplifyAlgebraic(Module& module);
std::size_t eliminateCommonSubexpressions(Module& module);
std::size_t eliminateDeadCode(Module& module);
std::size_t simplifyControlFlow(Module& module);
std::size_t inlineSmallFunctions(Module& module);
OptimizationStatistics optimizeModule(Module& module);

} // namespace zplus::ir
