#include "Input.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
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
thread_local std::string transientLine;
std::uintptr_t* exceptionSlot = nullptr;
std::string exceptionMessage;

struct ManagedInputString {
    std::string value;
    std::size_t managedBytes = 0;
    bool marked = false;
};

struct RootRange {
    const std::uintptr_t* slots = nullptr;
    std::size_t count = 0;
};

std::mutex managedMemoryMutex;
std::unordered_map<const char*, std::unique_ptr<ManagedInputString>> managedStrings;
std::vector<RootRange> managedRoots;
std::size_t managedBytes = 0;
std::size_t nextCollectionThreshold = 64 * 1024;
constexpr std::size_t minimumCollectionThreshold = 64 * 1024;

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

void writePrompt(const char* prompt) {
    if (prompt) {
        zplus::runtime::input::writePrompt(std::cout, std::string_view(prompt));
    }
}

std::string readRequiredLine(const char* function, const char* prompt) {
    writePrompt(prompt);
    return readRequiredLine(function);
}

void collectManagedStringsLocked() {
    for (const RootRange& range : managedRoots) {
        for (std::size_t index = 0; index < range.count; ++index) {
            const auto* value = reinterpret_cast<const char*>(range.slots[index]);
            if (const auto found = managedStrings.find(value);
                found != managedStrings.end()) {
                found->second->marked = true;
            }
        }
    }

    auto current = managedStrings.begin();
    while (current != managedStrings.end()) {
        ManagedInputString& value = *current->second;
        if (!value.marked) {
            managedBytes -= value.managedBytes;
            current = managedStrings.erase(current);
        } else {
            value.marked = false;
            ++current;
        }
    }
    nextCollectionThreshold = std::max(
        minimumCollectionThreshold,
        managedBytes > std::numeric_limits<std::size_t>::max() / 2
            ? std::numeric_limits<std::size_t>::max()
            : managedBytes * 2);
}

const char* allocateManagedString(std::string value) {
    std::lock_guard lock(managedMemoryMutex);
    if (managedBytes >= nextCollectionThreshold) {
        collectManagedStringsLocked();
    }

    auto object = std::make_unique<ManagedInputString>();
    object->value = std::move(value);
    if (object->value.capacity() >
        std::numeric_limits<std::size_t>::max() - sizeof(ManagedInputString)) {
        throw std::overflow_error("managed input string size overflow");
    }
    object->managedBytes = sizeof(ManagedInputString) + object->value.capacity();
    if (object->managedBytes >
        std::numeric_limits<std::size_t>::max() - managedBytes) {
        throw std::overflow_error("managed input heap size overflow");
    }
    const char* data = object->value.c_str();
    const std::size_t objectBytes = object->managedBytes;
    managedStrings.emplace(data, std::move(object));
    managedBytes += objectBytes;
    return data;
}

const char* readString(const char* prompt) {
    std::lock_guard lock(inputMutex);
    writePrompt(prompt);
    zplus::runtime::input::ensureUtf8ConsoleInput();
    std::optional<std::string> line = zplus::runtime::input::readLine(std::cin);
    return retain(line ? std::move(*line) : std::string{});
}

const char* readTransientString(const char* prompt) {
    std::lock_guard lock(inputMutex);
    writePrompt(prompt);
    zplus::runtime::input::ensureUtf8ConsoleInput();
    if (!zplus::runtime::input::readLine(std::cin, transientLine)) {
        transientLine.clear();
    }
    return transientLine.c_str();
}

const char* readManagedString(const char* prompt) {
    std::lock_guard lock(inputMutex);
    writePrompt(prompt);
    zplus::runtime::input::ensureUtf8ConsoleInput();
    std::string line;
    if (!zplus::runtime::input::readLine(std::cin, line)) line.clear();
    return allocateManagedString(std::move(line));
}

std::int64_t readNumber(const char* prompt) {
    std::lock_guard lock(inputMutex);
    return std::bit_cast<std::int64_t>(
        zplus::runtime::input::parseNumber(
            readRequiredLine("read_number", prompt)));
}

std::int64_t readInteger(const char* prompt) {
    std::lock_guard lock(inputMutex);
    return std::bit_cast<std::int64_t>(
        zplus::runtime::input::parseInteger(
            readRequiredLine("read_integer", prompt)));
}

std::int64_t readBoolean(const char* prompt) {
    std::lock_guard lock(inputMutex);
    return zplus::runtime::input::parseBoolean(
        readRequiredLine("read_boolean", prompt)) ? 1 : 0;
}

} // namespace

ZPLUS_INPUT_API void zpi_set_exception_slot(std::uintptr_t* slot) noexcept {
    exceptionSlot = slot;
}

ZPLUS_INPUT_API const char* zpi_read() noexcept {
    return guard<const char*>([] {
        return readString(nullptr);
    });
}

ZPLUS_INPUT_API const char* zpi_read_prompt(const char* prompt) noexcept {
    return guard<const char*>([prompt] { return readString(prompt); });
}

ZPLUS_INPUT_API const char* zpi_read_transient() noexcept {
    return guard<const char*>([] { return readTransientString(nullptr); });
}

ZPLUS_INPUT_API const char* zpi_read_transient_prompt(const char* prompt) noexcept {
    return guard<const char*>([prompt] { return readTransientString(prompt); });
}

ZPLUS_INPUT_API const char* zpi_read_managed() noexcept {
    return guard<const char*>([] { return readManagedString(nullptr); });
}

ZPLUS_INPUT_API const char* zpi_read_managed_prompt(const char* prompt) noexcept {
    return guard<const char*>([prompt] { return readManagedString(prompt); });
}

ZPLUS_INPUT_API const char* zpi_string_data(const char* value) noexcept {
    return guard<const char*>([value] {
        if (!value) throw std::invalid_argument("string must not be null");
        std::lock_guard lock(managedMemoryMutex);
        const auto found = managedStrings.find(value);
        return found == managedStrings.end()
            ? value
            : found->second->value.c_str();
    });
}

ZPLUS_INPUT_API std::int64_t zpi_push_roots(
    const std::uintptr_t* slots,
    std::size_t count
) noexcept {
    return guard<std::int64_t>([slots, count] {
        if (count != 0 && !slots) {
            throw std::invalid_argument("managed input root slots must not be null");
        }
        std::lock_guard lock(managedMemoryMutex);
        managedRoots.push_back({slots, count});
        return 0;
    });
}

ZPLUS_INPUT_API std::int64_t zpi_pop_roots() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(managedMemoryMutex);
        if (managedRoots.empty()) {
            throw std::logic_error("managed input root stack is unbalanced");
        }
        managedRoots.pop_back();
        return 0;
    });
}

#if defined(ZPLUS_INPUT_RUNTIME_TESTING)
ZPLUS_INPUT_API std::int64_t zpi_collect_strings() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(managedMemoryMutex);
        collectManagedStringsLocked();
        if (managedStrings.size() > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("managed input object count exceeds supported range");
        }
        return static_cast<std::int64_t>(managedStrings.size());
    });
}

ZPLUS_INPUT_API std::int64_t zpi_managed_string_count() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(managedMemoryMutex);
        if (managedStrings.size() > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("managed input object count exceeds supported range");
        }
        return static_cast<std::int64_t>(managedStrings.size());
    });
}
#endif

ZPLUS_INPUT_API std::int64_t zpi_memory_shutdown() noexcept {
    return guard<std::int64_t>([] {
        std::lock_guard lock(managedMemoryMutex);
        managedRoots.clear();
        managedStrings.clear();
        managedBytes = 0;
        nextCollectionThreshold = minimumCollectionThreshold;
        return 0;
    });
}

ZPLUS_INPUT_API std::int64_t zpi_read_number() noexcept {
    return guard<std::int64_t>([] { return readNumber(nullptr); });
}

ZPLUS_INPUT_API std::int64_t zpi_read_number_prompt(const char* prompt) noexcept {
    return guard<std::int64_t>([prompt] { return readNumber(prompt); });
}

ZPLUS_INPUT_API std::int64_t zpi_read_integer() noexcept {
    return guard<std::int64_t>([] { return readInteger(nullptr); });
}

ZPLUS_INPUT_API std::int64_t zpi_read_integer_prompt(const char* prompt) noexcept {
    return guard<std::int64_t>([prompt] { return readInteger(prompt); });
}

ZPLUS_INPUT_API std::int64_t zpi_read_boolean() noexcept {
    return guard<std::int64_t>([] { return readBoolean(nullptr); });
}

ZPLUS_INPUT_API std::int64_t zpi_read_boolean_prompt(const char* prompt) noexcept {
    return guard<std::int64_t>([prompt] { return readBoolean(prompt); });
}
