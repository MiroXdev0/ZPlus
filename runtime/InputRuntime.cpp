#include "Input.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define ZPLUS_INPUT_API extern "C" __declspec(dllexport)
#else
#define ZPLUS_INPUT_API extern "C" __attribute__((visibility("default")))
#endif

namespace {

std::mutex inputMutex;
std::vector<std::unique_ptr<std::string>> retainedStrings;
std::uintptr_t* exceptionSlot = nullptr;
std::string exceptionMessage;

#if defined(_WIN32)
void ensureUtf8ConsoleInput() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (input && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode)) {
            SetConsoleCP(CP_UTF8);
        }
    });
}
#endif

void reportFailure(const char* message) noexcept {
    if (exceptionSlot) {
        try {
            exceptionMessage = message ? message : "unknown terminal input failure";
            *exceptionSlot = reinterpret_cast<std::uintptr_t>(exceptionMessage.c_str());
            return;
        } catch (...) {
            *exceptionSlot = reinterpret_cast<std::uintptr_t>(
                "native Z+ terminal input failure");
            return;
        }
    }

    std::fprintf(stderr, "native Z+ terminal input error: %s\n",
                 message ? message : "unknown failure");
    std::fflush(stderr);
    std::exit(EXIT_FAILURE);
}

template<typename Result, typename Function>
Result guard(Function&& function) noexcept {
    try {
        return function();
    } catch (const std::exception& error) {
        reportFailure(error.what());
    } catch (...) {
        reportFailure("unknown terminal input failure");
    }
    return Result{};
}

const char* retain(std::string value) {
    auto string = std::make_unique<std::string>(std::move(value));
    const char* result = string->c_str();
    retainedStrings.push_back(std::move(string));
    return result;
}

std::string readRequiredLine(const char* function) {
    zplus::runtime::input::ensureUtf8ConsoleInput();
    std::optional<std::string> line = zplus::runtime::input::readLine(std::cin);
    if (!line) {
        throw std::runtime_error(std::string(function) + ": end of input");
    }
    return std::move(*line);
}

} // namespace

ZPLUS_INPUT_API void zpi_set_exception_slot(std::uintptr_t* slot) noexcept {
    exceptionSlot = slot;
}

ZPLUS_INPUT_API const char* zpi_read() noexcept {
    return guard<const char*>([] {
        std::lock_guard lock(inputMutex);
        zplus::runtime::input::ensureUtf8ConsoleInput();
        std::optional<std::string> line = zplus::runtime::input::readLine(std::cin);
        return retain(line ? std::move(*line) : std::string{});
    });
}

ZPLUS_INPUT_API std::int64_t zpi_read_number() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(inputMutex);
        const std::string line = readRequiredLine("read_number");
        return std::bit_cast<std::int64_t>(
            zplus::runtime::input::parseNumber(line));
    });
}

ZPLUS_INPUT_API std::int64_t zpi_read_integer() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(inputMutex);
        const std::string line = readRequiredLine("read_integer");
        return std::bit_cast<std::int64_t>(
            zplus::runtime::input::parseInteger(line));
    });
}

ZPLUS_INPUT_API std::int64_t zpi_read_boolean() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(inputMutex);
        const std::string line = readRequiredLine("read_boolean");
        return zplus::runtime::input::parseBoolean(line) ? 1 : 0;
    });
}
