#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>

namespace zplus::numeric {

inline bool parseLiteral(std::string_view text, double& value) noexcept {
    if (text.empty()) return false;

    int base = 10;
    std::size_t prefixLength = 0;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'b' || text[1] == 'B')) {
        base = 2;
        prefixLength = 2;
    } else if (text.size() > 2 && text[0] == '0' &&
               (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        prefixLength = 2;
    }

    if (prefixLength != 0) {
        std::uint64_t integer = 0;
        const char* begin = text.data() + prefixLength;
        const char* end = text.data() + text.size();
        const auto result = std::from_chars(begin, end, integer, base);
        if (result.ec != std::errc{} || result.ptr != end) return false;
        value = static_cast<double>(integer);
        return true;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(
        begin, end, value, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == end;
}

} // namespace zplus::numeric
