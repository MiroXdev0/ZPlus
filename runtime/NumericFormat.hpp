#pragma once

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

namespace zplus::runtime {

inline std::string formatNumber(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
    char buffer[128];
    const auto result = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("could not format numeric value");
    }
    return std::string(buffer, result.ptr);
}

} // namespace zplus::runtime
