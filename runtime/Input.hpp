#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <istream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zplus::runtime::input {

inline void ensureUtf8ConsoleInput() {
#if defined(_WIN32)
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (input && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode)) {
            SetConsoleCP(CP_UTF8);
        }
    });
#endif
}

inline std::optional<std::string> readLine(std::istream& stream) {
    std::string line;
    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }
    if (stream.bad()) {
        throw std::runtime_error("standard input read failed");
    }
    if (stream.eof()) return std::nullopt;
    throw std::runtime_error("standard input read failed");
}

inline std::string readRequiredLine(std::istream& stream, const char* function) {
    std::optional<std::string> line = readLine(stream);
    if (!line) {
        throw std::runtime_error(std::string(function) + ": end of input");
    }
    return std::move(*line);
}

inline std::string_view trimAsciiWhitespace(std::string_view value) {
    const auto isWhitespace = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' ||
            ch == '\r' || ch == '\f' || ch == '\v';
    };
    while (!value.empty() && isWhitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && isWhitespace(value.back())) value.remove_suffix(1);
    return value;
}

inline double parseNumber(std::string_view text) {
    text = trimAsciiWhitespace(text);
    if (text.empty()) {
        throw std::runtime_error("read_number: invalid numeric input");
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '+' || text.front() == '-') {
            throw std::runtime_error("read_number: invalid numeric input");
        }
    }
    if (text.empty()) {
        throw std::runtime_error("read_number: invalid numeric input");
    }

    double value = 0.0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(
        begin, end, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) {
        throw std::runtime_error("read_number: invalid numeric input");
    }
    return value;
}

inline double parseInteger(std::string_view text) {
    text = trimAsciiWhitespace(text);
    if (text.empty()) {
        throw std::runtime_error("read_integer: invalid integer input");
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '+' || text.front() == '-') {
            throw std::runtime_error("read_integer: invalid integer input");
        }
    }
    if (text.empty()) {
        throw std::runtime_error("read_integer: invalid integer input");
    }

    std::int64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("read_integer: invalid integer input");
    }
    constexpr std::int64_t maxExactlyRepresentable = 9007199254740991LL;
    if (value < -maxExactlyRepresentable || value > maxExactlyRepresentable) {
        throw std::runtime_error(
            "read_integer: value is outside the exactly representable number range");
    }
    return static_cast<double>(value);
}

inline bool parseBoolean(std::string_view text) {
    text = trimAsciiWhitespace(text);
    if (text.size() == 4 &&
        (text[0] == 't' || text[0] == 'T') &&
        (text[1] == 'r' || text[1] == 'R') &&
        (text[2] == 'u' || text[2] == 'U') &&
        (text[3] == 'e' || text[3] == 'E')) {
        return true;
    }
    if (text.size() == 5 &&
        (text[0] == 'f' || text[0] == 'F') &&
        (text[1] == 'a' || text[1] == 'A') &&
        (text[2] == 'l' || text[2] == 'L') &&
        (text[3] == 's' || text[3] == 'S') &&
        (text[4] == 'e' || text[4] == 'E')) {
        return false;
    }
    throw std::runtime_error("read_boolean: invalid boolean input");
}

} // namespace zplus::runtime::input
