#include "Base64.hpp"

#include <array>
#include <stdexcept>

namespace zplus::encoding {
namespace {

constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeCharacter(char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+' || value == '-') return 62;
    if (value == '/' || value == '_') return 63;
    return -1;
}

} // namespace

std::string base64Encode(std::string_view value) {
    std::string result;
    result.reserve(((value.size() + 2) / 3) * 4);
    for (std::size_t offset = 0; offset < value.size(); offset += 3) {
        const auto first = static_cast<unsigned char>(value[offset]);
        const auto second = offset + 1 < value.size()
            ? static_cast<unsigned char>(value[offset + 1]) : 0;
        const auto third = offset + 2 < value.size()
            ? static_cast<unsigned char>(value[offset + 2]) : 0;
        const unsigned int block = (static_cast<unsigned int>(first) << 16) |
            (static_cast<unsigned int>(second) << 8) | third;
        result.push_back(alphabet[(block >> 18) & 0x3f]);
        result.push_back(alphabet[(block >> 12) & 0x3f]);
        result.push_back(offset + 1 < value.size()
            ? alphabet[(block >> 6) & 0x3f] : '=');
        result.push_back(offset + 2 < value.size()
            ? alphabet[block & 0x3f] : '=');
    }
    return result;
}

std::string base64Decode(std::string_view value) {
    if (value.size() % 4 != 0) {
        throw std::invalid_argument("base64 data must have a length divisible by four");
    }
    std::string result;
    result.reserve((value.size() / 4) * 3);
    for (std::size_t offset = 0; offset < value.size(); offset += 4) {
        const bool last = offset + 4 == value.size();
        const int first = decodeCharacter(value[offset]);
        const int second = decodeCharacter(value[offset + 1]);
        const int third = value[offset + 2] == '=' ? 0 : decodeCharacter(value[offset + 2]);
        const int fourth = value[offset + 3] == '=' ? 0 : decodeCharacter(value[offset + 3]);
        if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
            (!last && (value[offset + 2] == '=' || value[offset + 3] == '=')) ||
            (value[offset + 2] == '=' && value[offset + 3] != '=') ||
            (value[offset + 2] == '=' && (second & 0x0f) != 0) ||
            (value[offset + 3] == '=' && value[offset + 2] != '=' && (third & 0x03) != 0)) {
            throw std::invalid_argument("base64 data is malformed");
        }
        const unsigned int block = (static_cast<unsigned int>(first) << 18) |
            (static_cast<unsigned int>(second) << 12) |
            (static_cast<unsigned int>(third) << 6) |
            static_cast<unsigned int>(fourth);
        result.push_back(static_cast<char>((block >> 16) & 0xff));
        if (value[offset + 2] != '=') {
            result.push_back(static_cast<char>((block >> 8) & 0xff));
        }
        if (value[offset + 3] != '=') {
            result.push_back(static_cast<char>(block & 0xff));
        }
    }
    return result;
}

std::string base64UrlEncode(std::string_view value) {
    std::string result = base64Encode(value);
    for (char& ch : result) {
        if (ch == '+') ch = '-';
        else if (ch == '/') ch = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();
    return result;
}

} // namespace zplus::encoding
