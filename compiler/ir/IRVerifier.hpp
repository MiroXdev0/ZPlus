#pragma once

#include "../Diagnostic.hpp"
#include "IR.hpp"

#include <vector>

namespace zplus::ir {

std::vector<zplus::Diagnostic> verifyModule(const Module& module);

} // namespace zplus::ir
