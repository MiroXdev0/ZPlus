#pragma once

#include "NativeCodeGenerator.hpp"

namespace zplus::backend::native {

NativeImage generateX64(const zplus::ir::Module& module, bool windowsAbi);

} // namespace zplus::backend::native
