#include "StandardLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <ctime>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <locale.h>
#include <wctype.h>
#endif

namespace zplus::stdlib {
namespace {

struct Utf8Character {
    std::uint32_t value;
    std::size_t begin;
    std::size_t end;
};

Utf8Character decodeUtf8Character(const std::string& value, std::size_t offset) {
    const auto first = static_cast<unsigned char>(value[offset]);
    std::uint32_t codePoint = 0;
    std::size_t width = 0;
    if (first <= 0x7f) {
        codePoint = first;
        width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
        codePoint = first & 0x1f;
        width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        codePoint = first & 0x0f;
        width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
        codePoint = first & 0x07;
        width = 4;
    } else {
        throw std::invalid_argument("string is not valid UTF-8");
    }
    if (width > value.size() - offset) {
        throw std::invalid_argument("string is not valid UTF-8");
    }
    for (std::size_t index = 1; index < width; ++index) {
        const auto next = static_cast<unsigned char>(value[offset + index]);
        if ((next & 0xc0) != 0x80) {
            throw std::invalid_argument("string is not valid UTF-8");
        }
        codePoint = (codePoint << 6) | (next & 0x3f);
    }
    if ((width == 2 && codePoint < 0x80) ||
        (width == 3 && codePoint < 0x800) ||
        (width == 4 && codePoint < 0x10000) ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff) ||
        codePoint > 0x10ffff) {
        throw std::invalid_argument("string is not valid UTF-8");
    }
    return {codePoint, offset, offset + width};
}

std::vector<Utf8Character> decodeUtf8(const std::string& value) {
    std::vector<Utf8Character> characters;
    for (std::size_t offset = 0; offset < value.size();) {
        const Utf8Character character = decodeUtf8Character(value, offset);
        characters.push_back(character);
        offset = character.end;
    }
    return characters;
}

void validateUtf8(const std::string& value) {
    for (std::size_t offset = 0; offset < value.size();) {
        offset = decodeUtf8Character(value, offset).end;
    }
}

bool isUnicodeWhitespace(std::uint32_t value) {
    return (value >= 0x0009 && value <= 0x000d) ||
        value == 0x0020 || value == 0x0085 || value == 0x00a0 ||
        value == 0x1680 || (value >= 0x2000 && value <= 0x200a) ||
        value == 0x2028 || value == 0x2029 || value == 0x202f ||
        value == 0x205f || value == 0x3000;
}

std::string changeUnicodeCase(const std::string& value, bool uppercase) {
    const auto characters = decodeUtf8(value);
    if (characters.empty()) return {};
#ifdef _WIN32
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("string exceeds the supported Unicode conversion size");
    }
    const int inputLength = static_cast<int>(value.size());
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength, nullptr, 0);
    if (wideLength == 0) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert UTF-8 string to Unicode");
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength,
            wide.data(), wideLength) != wideLength) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert UTF-8 string to Unicode");
    }
    const DWORD flags = uppercase ? LCMAP_UPPERCASE : LCMAP_LOWERCASE;
    const int mappedLength = LCMapStringEx(
        LOCALE_NAME_INVARIANT, flags, wide.data(), wideLength, nullptr, 0,
        nullptr, nullptr, 0);
    if (mappedLength == 0) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unicode case conversion failed");
    }
    std::wstring mapped(static_cast<std::size_t>(mappedLength), L'\0');
    if (LCMapStringEx(
            LOCALE_NAME_INVARIANT, flags, wide.data(), wideLength,
            mapped.data(), mappedLength, nullptr, nullptr, 0) != mappedLength) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unicode case conversion failed");
    }
    if (mapped.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Unicode case conversion result is too large");
    }
    const int outputLength = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, mapped.data(),
        static_cast<int>(mapped.size()), nullptr, 0, nullptr, nullptr);
    if (outputLength == 0) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert Unicode string to UTF-8");
    }
    std::string result(static_cast<std::size_t>(outputLength), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, mapped.data(),
            static_cast<int>(mapped.size()), result.data(), outputLength,
            nullptr, nullptr) != outputLength) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert Unicode string to UTF-8");
    }
    return result;
#else
    locale_t locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
    if (!locale) locale = newlocale(LC_CTYPE_MASK, "C.utf8", nullptr);
    if (!locale) locale = newlocale(LC_CTYPE_MASK, "", nullptr);
    if (!locale) {
        throw std::runtime_error("cannot initialize a Unicode character locale");
    }
    struct LocaleGuard {
        locale_t value;
        ~LocaleGuard() { freelocale(value); }
    } localeGuard{locale};
    std::string result;
    result.reserve(value.size());
    for (const Utf8Character& character : characters) {
        const wint_t mapped = uppercase
            ? towupper_l(static_cast<wint_t>(character.value), locale)
            : towlower_l(static_cast<wint_t>(character.value), locale);
        const std::uint32_t codePoint = mapped == WEOF
            ? character.value
            : static_cast<std::uint32_t>(mapped);
        if (codePoint <= 0x7f) {
            result.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else if (codePoint <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else if (codePoint <= 0x10ffff) {
            result.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            throw std::runtime_error("Unicode case conversion produced an invalid code point");
        }
    }
    return result;
#endif
}

std::mt19937_64& randomEngine() {
    static std::mt19937_64 engine([] {
        std::random_device source;
        std::seed_seq seed{source(), source(), source(), source(), source(), source()};
        return std::mt19937_64(seed);
    }());
    return engine;
}

std::vector<std::string>& activeArguments() {
    static std::vector<std::string> arguments;
    return arguments;
}

std::mutex& loggerMutex() {
    static std::mutex mutex;
    return mutex;
}

int& loggerThreshold() {
    static int threshold = 0;
    return threshold;
}

std::ofstream& loggerFile() {
    static std::ofstream output;
    return output;
}

int logLevel(const std::string& value) {
    if (value == "debug") return 0;
    if (value == "info") return 1;
    if (value == "warning") return 2;
    if (value == "error") return 3;
    throw std::invalid_argument("log level must be debug, info, warning, or error");
}

void requireFinite(double value, const char* operation) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(operation) + " requires a finite number");
    }
}

std::filesystem::path checkedPath(const std::string& value, const char* operation) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(operation) + " path must not be empty");
    }
    if (value.find('\0') != std::string::npos) {
        throw std::invalid_argument(std::string(operation) + " path contains a null byte");
    }
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::string pathString(const std::filesystem::path& value) {
    const std::u8string encoded = value.u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::optional<std::string> environmentValue(const std::string& name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    const errno_t result = _dupenv_s(&value, &length, name.c_str());
    if (result != 0) {
        std::free(value);
        throw std::system_error(
            static_cast<int>(result), std::generic_category(),
            "cannot read environment variable '" + name + "'");
    }
    if (!value) return std::nullopt;
    std::string copied(value);
    std::free(value);
    return copied;
#else
    const char* value = std::getenv(name.c_str());
    if (!value) return std::nullopt;
    return std::string(value);
#endif
}

std::tm localTime(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    if (localtime_s(&result, &value) != 0) {
        throw std::out_of_range("timestamp is outside the supported calendar range");
    }
#else
    if (!localtime_r(&value, &result)) {
        throw std::out_of_range("timestamp is outside the supported calendar range");
    }
#endif
    return result;
}

std::time_t checkedTimestamp(double timestamp) {
    requireFinite(timestamp, "time operation");
    if (timestamp < static_cast<double>(std::numeric_limits<std::time_t>::min()) ||
        timestamp > static_cast<double>(std::numeric_limits<std::time_t>::max())) {
        throw std::out_of_range("timestamp is outside the supported range");
    }
    return static_cast<std::time_t>(timestamp);
}

} // namespace

double mathUnary(const std::string& operation, double value) {
    requireFinite(value, operation.c_str());
    double result = 0.0;
    if (operation == "abs") result = std::fabs(value);
    else if (operation == "round") result = std::round(value);
    else if (operation == "floor") result = std::floor(value);
    else if (operation == "ceil") result = std::ceil(value);
    else if (operation == "sqrt") {
        if (value < 0.0) throw std::domain_error("sqrt requires a non-negative number");
        result = std::sqrt(value);
    } else if (operation == "sin") result = std::sin(value);
    else if (operation == "cos") result = std::cos(value);
    else if (operation == "tan") result = std::tan(value);
    else if (operation == "asin") {
        if (value < -1.0 || value > 1.0) throw std::domain_error("asin input must be in [-1, 1]");
        result = std::asin(value);
    } else if (operation == "acos") {
        if (value < -1.0 || value > 1.0) throw std::domain_error("acos input must be in [-1, 1]");
        result = std::acos(value);
    } else if (operation == "atan") result = std::atan(value);
    else if (operation == "log") {
        if (value <= 0.0) throw std::domain_error("log requires a positive number");
        result = std::log(value);
    } else if (operation == "log10") {
        if (value <= 0.0) throw std::domain_error("log10 requires a positive number");
        result = std::log10(value);
    } else if (operation == "exp") result = std::exp(value);
    else throw std::invalid_argument("unknown math operation '" + operation + "'");
    if (!std::isfinite(result)) {
        throw std::overflow_error(operation + " result is outside the finite numeric range");
    }
    return result;
}

double mathBinary(const std::string& operation, double left, double right) {
    requireFinite(left, operation.c_str());
    requireFinite(right, operation.c_str());
    if (operation == "min") return std::min(left, right);
    if (operation == "max") return std::max(left, right);
    if (operation == "pow") {
        const double result = std::pow(left, right);
        if (!std::isfinite(result)) {
            throw std::domain_error("pow result is outside the finite numeric range");
        }
        return result;
    }
    throw std::invalid_argument("unknown binary math operation '" + operation + "'");
}

double mathConstant(const std::string& name) {
    if (name == "pi") return std::acos(-1.0);
    if (name == "e") return std::exp(1.0);
    throw std::invalid_argument("unknown math constant '" + name + "'");
}

std::string stringUnary(const std::string& operation, const std::string& value) {
    const auto characters = decodeUtf8(value);
    if (operation == "upper" || operation == "lower") {
        return changeUnicodeCase(value, operation == "upper");
    }
    if (operation == "trim") {
        std::size_t first = 0;
        std::size_t last = characters.size();
        while (first < last && isUnicodeWhitespace(characters[first].value)) ++first;
        while (last > first && isUnicodeWhitespace(characters[last - 1].value)) --last;
        if (first == last) return {};
        return value.substr(
            characters[first].begin,
            characters[last - 1].end - characters[first].begin);
    }
    if (operation == "reverse") {
        std::string result;
        result.reserve(value.size());
        for (auto current = characters.rbegin(); current != characters.rend(); ++current) {
            result.append(value, current->begin, current->end - current->begin);
        }
        return result;
    }
    throw std::invalid_argument("unknown string operation '" + operation + "'");
}

std::string stringBinary(
    const std::string& operation,
    const std::string& value,
    const std::string& other
) {
    validateUtf8(value);
    validateUtf8(other);
    if (operation == "contains") return value.find(other) != std::string::npos ? "true" : "false";
    if (operation == "startsWith") return value.starts_with(other) ? "true" : "false";
    if (operation == "endsWith") return value.ends_with(other) ? "true" : "false";
    throw std::invalid_argument("unknown binary string operation '" + operation + "'");
}

std::string stringReplace(
    const std::string& value,
    const std::string& search,
    const std::string& replacement
) {
    validateUtf8(value);
    validateUtf8(search);
    validateUtf8(replacement);
    if (search.empty()) throw std::invalid_argument("replace search text must not be empty");
    std::string result;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t position = value.find(search, begin);
        if (position == std::string::npos) {
            result.append(value, begin, std::string::npos);
            return result;
        }
        result.append(value, begin, position - begin);
        result += replacement;
        begin = position + search.size();
    }
}

std::string stringJoin(
    const std::vector<std::string>& values,
    const std::string& separator
) {
    validateUtf8(separator);
    std::size_t size = 0;
    for (const auto& value : values) {
        validateUtf8(value);
        if (value.size() > std::string{}.max_size() - size) {
            throw std::length_error("joined string exceeds the supported size");
        }
        size += value.size();
    }
    if (values.size() > 1) {
        const std::size_t separators = values.size() - 1;
        if (separator.size() != 0 &&
            separators > (std::string{}.max_size() - size) / separator.size()) {
            throw std::length_error("joined string exceeds the supported size");
        }
        size += separators * separator.size();
    }
    std::string result;
    result.reserve(size);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result += separator;
        result += values[i];
    }
    return result;
}

std::int64_t stringLength(const std::string& value) {
    std::int64_t length = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        if (length == std::numeric_limits<std::int64_t>::max()) {
            throw std::length_error("string length exceeds the supported numeric range");
        }
        offset = decodeUtf8Character(value, offset).end;
        ++length;
    }
    return length;
}

std::string substring(
    const std::string& value,
    std::int64_t start,
    std::int64_t length
) {
    const auto characters = decodeUtf8(value);
    if (start < 0 || length < 0) {
        throw std::out_of_range("substring range is outside the string");
    }
    if (static_cast<std::uint64_t>(start) > characters.size()) {
        throw std::out_of_range("substring range is outside the string");
    }
    const std::size_t begin = static_cast<std::size_t>(start);
    if (static_cast<std::uint64_t>(length) > characters.size() - begin) {
        throw std::out_of_range("substring range is outside the string");
    }
    const std::size_t count = static_cast<std::size_t>(length);
    if (count == 0) {
        return value.substr(begin == characters.size() ? value.size() : characters[begin].begin, 0);
    }
    const std::size_t byteBegin = characters[begin].begin;
    const std::size_t byteEnd = characters[begin + count - 1].end;
    return value.substr(byteBegin, byteEnd - byteBegin);
}

std::string repeat(const std::string& value, std::int64_t count) {
    validateUtf8(value);
    if (count < 0) throw std::invalid_argument("repeat count must be non-negative");
    if (value.size() != 0 &&
        static_cast<std::uint64_t>(count) >
            std::string{}.max_size() / value.size()) {
        throw std::length_error("repeated string exceeds the supported size");
    }
    std::string result;
    result.reserve(value.size() * static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) result += value;
    return result;
}

std::vector<std::string> split(const std::string& value, const std::string& delimiter) {
    validateUtf8(value);
    validateUtf8(delimiter);
    if (delimiter.empty()) throw std::invalid_argument("split delimiter must not be empty");
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t position = value.find(delimiter, begin);
        if (position == std::string::npos) {
            result.push_back(value.substr(begin));
            break;
        }
        result.push_back(value.substr(begin, position - begin));
        begin = position + delimiter.size();
    }
    return result;
}

std::string pathUnary(const std::string& operation, const std::string& value) {
    const std::filesystem::path path = checkedPath(value, operation.c_str());
    if (operation == "normalize") return pathString(path.lexically_normal());
    if (operation == "extension") return pathString(path.extension());
    if (operation == "filename") return pathString(path.filename());
    if (operation == "directory") return pathString(path.parent_path());
    if (operation == "absolute") {
        std::error_code error;
        const auto result = std::filesystem::absolute(path, error);
        if (error) throw std::filesystem::filesystem_error("absolute path failed", path, error);
        return pathString(result.lexically_normal());
    }
    if (operation == "isAbsolute") return path.is_absolute() ? "true" : "false";
    throw std::invalid_argument("unknown path operation '" + operation + "'");
}

std::string pathJoin(const std::string& left, const std::string& right) {
    const auto first = checkedPath(left, "join");
    const auto second = checkedPath(right, "join");
    return pathString((first / second).lexically_normal());
}

std::string fileRead(const std::string& value) {
    const auto path = checkedPath(value, "read");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::system_error(errno, std::generic_category(), "cannot open file for reading: " + pathString(path));
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) throw std::runtime_error("failed while reading file: " + pathString(path));
    return contents.str();
}

void fileWrite(const std::string& value, const std::string& contents, bool append) {
    const auto path = checkedPath(value, append ? "append" : "write");
    std::ofstream output(path, std::ios::binary |
        (append ? std::ios::app : std::ios::trunc));
    if (!output) throw std::system_error(errno, std::generic_category(), "cannot open file for writing: " + pathString(path));
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) throw std::runtime_error("failed while writing file: " + pathString(path));
}

bool fileExists(const std::string& value) {
    std::error_code error;
    const bool exists = std::filesystem::exists(checkedPath(value, "exists"), error);
    if (error) throw std::filesystem::filesystem_error("exists failed", value, error);
    return exists;
}

bool fileDelete(const std::string& value) {
    std::error_code error;
    const bool removed = std::filesystem::remove(checkedPath(value, "delete"), error);
    if (error) throw std::filesystem::filesystem_error("delete failed", value, error);
    return removed;
}

bool fileCreateDirectory(const std::string& value) {
    std::error_code error;
    const bool created = std::filesystem::create_directories(
        checkedPath(value, "createDirectory"), error);
    if (error) throw std::filesystem::filesystem_error("createDirectory failed", value, error);
    return created;
}

std::vector<std::string> fileListDirectory(const std::string& value) {
    const auto path = checkedPath(value, "listDirectory");
    std::error_code error;
    std::filesystem::directory_iterator iterator(path, error);
    if (error) throw std::filesystem::filesystem_error("listDirectory failed", path, error);
    std::vector<std::string> result;
    for (const auto& entry : iterator) {
        result.push_back(pathString(entry.path().filename()));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::int64_t fileSize(const std::string& value) {
    std::error_code error;
    const auto size = std::filesystem::file_size(checkedPath(value, "size"), error);
    if (error) throw std::filesystem::filesystem_error("size failed", value, error);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("file size exceeds the supported range");
    }
    return static_cast<std::int64_t>(size);
}

bool fileIsDirectory(const std::string& value) {
    std::error_code error;
    const bool result = std::filesystem::is_directory(checkedPath(value, "isDirectory"), error);
    if (error) throw std::filesystem::filesystem_error("isDirectory failed", value, error);
    return result;
}

std::string environmentGet(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("environment variable name must not be empty");
    return environmentValue(name).value_or("");
}

void environmentSet(const std::string& name, const std::string& value) {
    if (name.empty() || name.find('=') != std::string::npos) {
        throw std::invalid_argument("environment variable name is invalid");
    }
#ifdef _WIN32
    if (_putenv_s(name.c_str(), value.c_str()) != 0) {
        throw std::runtime_error("failed to set environment variable '" + name + "'");
    }
#else
    if (setenv(name.c_str(), value.c_str(), 1) != 0) {
        throw std::system_error(errno, std::generic_category(), "failed to set environment variable '" + name + "'");
    }
#endif
}

bool environmentExists(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("environment variable name must not be empty");
    return environmentValue(name).has_value();
}

std::string homeDirectory() {
#ifdef _WIN32
    if (const auto profile = environmentValue("USERPROFILE");
        profile && !profile->empty()) return *profile;
#else
    if (const auto home = environmentValue("HOME");
        home && !home->empty()) return *home;
#endif
    throw std::runtime_error("home directory is not available in the environment");
}

std::string currentDirectory() {
    std::error_code error;
    const auto result = std::filesystem::current_path(error);
    if (error) throw std::filesystem::filesystem_error("currentDirectory failed", error);
    return pathString(result);
}

double timeNow() {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string timeFormat(double timestamp, const std::string& format) {
    const std::time_t seconds = checkedTimestamp(timestamp);
    const std::tm parts = localTime(seconds);
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::put_time(&parts, format.c_str());
    if (result.fail()) throw std::invalid_argument("time format string is invalid");
    return result.str();
}

double timeParse(const std::string& value, const std::string& format) {
    std::tm parts{};
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    input >> std::get_time(&parts, format.c_str());
    if (input.fail() || input.peek() != std::char_traits<char>::eof()) {
        throw std::invalid_argument("time value does not match the supplied format");
    }
    parts.tm_isdst = -1;
    const std::time_t result = std::mktime(&parts);
    if (result == static_cast<std::time_t>(-1)) {
        throw std::out_of_range("parsed time is outside the supported range");
    }
    return static_cast<double>(result);
}

double timePart(const std::string& part, double timestamp) {
    const std::tm value = localTime(checkedTimestamp(timestamp));
    if (part == "year") return value.tm_year + 1900;
    if (part == "month") return value.tm_mon + 1;
    if (part == "day") return value.tm_mday;
    if (part == "hour") return value.tm_hour;
    if (part == "minute") return value.tm_min;
    if (part == "second") return value.tm_sec;
    throw std::invalid_argument("unknown time component '" + part + "'");
}

void timeSleep(std::int64_t milliseconds) {
    if (milliseconds < 0) throw std::invalid_argument("sleep duration must be non-negative");
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

std::int64_t randomInteger(std::int64_t minimum, std::int64_t maximum) {
    if (minimum > maximum) {
        throw std::invalid_argument("random integer minimum must not exceed maximum");
    }
    return std::uniform_int_distribution<std::int64_t>(minimum, maximum)(randomEngine());
}

double randomNumber(double minimum, double maximum) {
    requireFinite(minimum, "random number");
    requireFinite(maximum, "random number");
    if (!(minimum < maximum)) {
        throw std::invalid_argument("random number requires minimum < maximum");
    }
    return std::uniform_real_distribution<double>(minimum, maximum)(randomEngine());
}

bool randomBoolean() {
    return std::bernoulli_distribution(0.5)(randomEngine());
}

void randomSeed(std::uint64_t seed) {
    randomEngine().seed(seed);
}

void setProcessArguments(std::vector<std::string> arguments) {
    activeArguments() = std::move(arguments);
}

const std::vector<std::string>& processArguments() {
    return activeArguments();
}

void logWrite(const std::string& level, const std::string& message) {
    const int severity = logLevel(level);
    std::lock_guard lock(loggerMutex());
    if (severity < loggerThreshold()) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const std::tm parts = localTime(timestamp);
    std::ostringstream line;
    line.imbue(std::locale::classic());
    line << '[' << std::put_time(&parts, "%Y-%m-%d %H:%M:%S") << "] "
         << level << ": " << message << '\n';

    std::ostream& output = loggerFile().is_open()
        ? static_cast<std::ostream&>(loggerFile())
        : static_cast<std::ostream&>(std::cout);
    output << line.str();
    output.flush();
    if (!output) throw std::runtime_error("failed to write log output");
}

void logSetLevel(const std::string& level) {
    const int threshold = logLevel(level);
    std::lock_guard lock(loggerMutex());
    loggerThreshold() = threshold;
}

void logSetOutput(const std::string& path) {
    std::lock_guard lock(loggerMutex());
    std::ofstream replacement;
    if (!path.empty()) {
        const auto outputPath = checkedPath(path, "log output");
        replacement.open(outputPath, std::ios::binary | std::ios::app);
        if (!replacement) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot open log output: " + outputPath.string());
        }
    }
    loggerFile() = std::move(replacement);
}

} // namespace zplus::stdlib
