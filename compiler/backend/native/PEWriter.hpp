#pragma once

#include "NativeCodeGenerator.hpp"

#include <cstdint>
#include <vector>

namespace zplus::backend::native {

std::vector<std::uint8_t> writePE(const NativeImage& image);

} // namespace zplus::backend::native