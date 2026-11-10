#pragma once

#include "NativeCodeGenerator.hpp"

#include <cstdint>
#include <vector>

namespace zplus::backend::native {

std::vector<std::uint8_t> writeELF(const NativeImage& image);

} // namespace zplus::backend::native
