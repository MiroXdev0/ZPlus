#include "../ZGraphics.hpp"

#include <stdexcept>

namespace zplus::graphics {

Color parseColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        throw std::invalid_argument("color must use #RRGGBB format");
    }

    auto channel = [&value](std::size_t offset) {
        unsigned int result = 0;
        for (std::size_t i = offset; i < offset + 2; ++i) {
            const char ch = value[i];
            unsigned int digit;
            if (ch >= '0' && ch <= '9') digit = static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned int>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned int>(ch - 'A' + 10);
            else throw std::invalid_argument("color must use hexadecimal #RRGGBB channels");
            result = result * 16 + digit;
        }
        return static_cast<std::uint8_t>(result);
    };

    return {channel(1), channel(3), channel(5), 255};
}

} // namespace zplus::graphics
