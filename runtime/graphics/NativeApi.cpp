#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "../../ZGraphics/ZGraphics.hpp"
#include "../StandardLibrary.hpp"
#include "../Json.hpp"
#include "../Http.hpp"
#include "../SQLite.hpp"
#include "../NumericFormat.hpp"
#include "../installer/Platform.hpp"

#include <cstdint>
#include <cstdio>
#include <climits>
#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef ZPLUS_GRAPHICS_RUNTIME_EXPORTS
#define ZPLUS_GRAPHICS_API extern "C" __declspec(dllexport)
#else
#define ZPLUS_GRAPHICS_API extern "C" __declspec(dllimport)
#endif

namespace {

using zplus::graphics::Application;
using zplus::graphics::Event;

std::unique_ptr<Application> application;
std::unordered_map<int, Event> events;

struct ManagedNativeString {
    std::string value;
    std::size_t managedBytes = 0;
    bool marked = false;
};

struct ManagedNativeArray {
    std::vector<std::uintptr_t> elements;
    std::size_t managedBytes = 0;
    bool marked = false;
};

struct ManagedNativeStruct {
    std::size_t typeId = 0;
    std::vector<std::uintptr_t> fields;
    std::size_t managedBytes = 0;
    bool marked = false;
};

struct RootRange {
    const std::uintptr_t* slots;
    std::size_t count;
};

std::unordered_map<ManagedNativeString*, std::unique_ptr<ManagedNativeString>>
    managedStrings;
std::unordered_map<ManagedNativeArray*, std::unique_ptr<ManagedNativeArray>>
    managedArrays;
std::unordered_map<ManagedNativeStruct*, std::unique_ptr<ManagedNativeStruct>>
    managedStructs;
std::vector<RootRange> managedRoots;
std::size_t managedBytes = 0;
std::size_t collectionCount = 0;
std::size_t minimumCollectionThreshold = 64 * 1024;
std::size_t nextCollectionThreshold = 64 * 1024;
std::uintptr_t* nativeExceptionSlot = nullptr;
std::string nativeExceptionMessage;
constexpr std::uintptr_t managedStringTag = 1;
constexpr std::uintptr_t managedArrayTag = 2;
constexpr std::uintptr_t managedStructTag = 3;
constexpr std::uintptr_t managedTagMask = 7;
constexpr std::size_t minimumThreshold = 1024;
constexpr std::size_t maximumThreshold = 1ULL << 40;

void markManagedValue(
    std::uintptr_t value,
    std::vector<ManagedNativeArray*>& pendingArrays,
    std::vector<ManagedNativeStruct*>& pendingStructs
) {
    const std::uintptr_t tag = value & managedTagMask;
    auto* address = reinterpret_cast<void*>(value & ~managedTagMask);
    if (tag == managedStringTag) {
        const auto found = managedStrings.find(
            static_cast<ManagedNativeString*>(address));
        if (found != managedStrings.end()) found->second->marked = true;
    } else if (tag == managedArrayTag) {
        const auto found = managedArrays.find(
            static_cast<ManagedNativeArray*>(address));
        if (found != managedArrays.end() && !found->second->marked) {
            found->second->marked = true;
            pendingArrays.push_back(found->first);
        }
    } else if (tag == managedStructTag) {
        const auto found = managedStructs.find(
            static_cast<ManagedNativeStruct*>(address));
        if (found != managedStructs.end() && !found->second->marked) {
            found->second->marked = true;
            pendingStructs.push_back(found->first);
        }
    }
}

std::size_t collectManagedObjects() {
    std::vector<ManagedNativeArray*> pendingArrays;
    std::vector<ManagedNativeStruct*> pendingStructs;
    for (const RootRange& range : managedRoots) {
        for (std::size_t i = 0; i < range.count; ++i) {
            markManagedValue(range.slots[i], pendingArrays, pendingStructs);
        }
    }
    while (!pendingArrays.empty() || !pendingStructs.empty()) {
        if (!pendingArrays.empty()) {
            ManagedNativeArray* array = pendingArrays.back();
            pendingArrays.pop_back();
            for (const std::uintptr_t element : array->elements) {
                markManagedValue(element, pendingArrays, pendingStructs);
            }
        } else {
            ManagedNativeStruct* structure = pendingStructs.back();
            pendingStructs.pop_back();
            for (const std::uintptr_t field : structure->fields) {
                markManagedValue(field, pendingArrays, pendingStructs);
            }
        }
    }

    std::size_t reclaimed = 0;
    const auto sweep = [&reclaimed](auto& objects) {
        auto current = objects.begin();
        while (current != objects.end()) {
            auto& object = *current->second;
            if (!object.marked) {
                managedBytes -= object.managedBytes;
                current = objects.erase(current);
                ++reclaimed;
            } else {
                object.marked = false;
                ++current;
            }
        }
    };
    sweep(managedArrays);
    sweep(managedStructs);
    sweep(managedStrings);
    ++collectionCount;
    const std::size_t growth = managedBytes > maximumThreshold / 2
        ? maximumThreshold
        : managedBytes * 2;
    nextCollectionThreshold = std::max(minimumCollectionThreshold, growth);
    return reclaimed;
}

ManagedNativeStruct& checkedStruct(std::uintptr_t value) {
    if ((value & managedTagMask) != managedStructTag) {
        throw std::invalid_argument("member operation requires a struct value");
    }
    auto* address = reinterpret_cast<ManagedNativeStruct*>(
        value & ~managedTagMask);
    const auto found = managedStructs.find(address);
    if (found == managedStructs.end()) {
        throw std::runtime_error("native struct handle is no longer valid");
    }
    return *found->second;
}

template<typename T>
const char* allocateManagedString(T&& value) {
    if (managedBytes >= nextCollectionThreshold) {
        collectManagedObjects();
    }
    auto object = std::make_unique<ManagedNativeString>();
    object->value = std::forward<T>(value);
    object->managedBytes = sizeof(ManagedNativeString) + object->value.capacity();
    ManagedNativeString* address = object.get();
    managedBytes += object->managedBytes;
    managedStrings.emplace(address, std::move(object));
    const std::uintptr_t tagged =
        reinterpret_cast<std::uintptr_t>(address) | managedStringTag;
    return reinterpret_cast<const char*>(tagged);
}

class TemporaryRootScope {
public:
    TemporaryRootScope(const std::uintptr_t* values, std::size_t count) {
        managedRoots.push_back({values, count});
    }

    ~TemporaryRootScope() {
        managedRoots.pop_back();
    }

    TemporaryRootScope(const TemporaryRootScope&) = delete;
    TemporaryRootScope& operator=(const TemporaryRootScope&) = delete;
};

std::uintptr_t allocateManagedArray(
    const std::uintptr_t* elements,
    std::size_t count
) {
    if (count != 0 && !elements) {
        throw std::invalid_argument("array elements must not be null");
    }
    TemporaryRootScope roots(elements, count);
    if (managedBytes >= nextCollectionThreshold) {
        collectManagedObjects();
    }

    auto object = std::make_unique<ManagedNativeArray>();
    if (count > object->elements.max_size()) {
        throw std::length_error("array is too large");
    }
    if (count != 0) {
        object->elements.assign(elements, elements + count);
    }
    if (object->elements.capacity() >
        (std::numeric_limits<std::size_t>::max() - sizeof(ManagedNativeArray)) /
            sizeof(std::uintptr_t)) {
        throw std::overflow_error("array allocation size overflow");
    }
    object->managedBytes = sizeof(ManagedNativeArray) +
        object->elements.capacity() * sizeof(std::uintptr_t);
    if (object->managedBytes > std::numeric_limits<std::size_t>::max() - managedBytes) {
        throw std::overflow_error("managed heap byte count overflow");
    }
    ManagedNativeArray* address = object.get();
    managedBytes += object->managedBytes;
    managedArrays.emplace(address, std::move(object));
    return reinterpret_cast<std::uintptr_t>(address) | managedArrayTag;
}

ManagedNativeArray& checkedArray(std::uintptr_t value) {
    if ((value & managedTagMask) != managedArrayTag) {
        throw std::invalid_argument("array operation requires an array value");
    }
    auto* address = reinterpret_cast<ManagedNativeArray*>(
        value & ~managedTagMask);
    const auto found = managedArrays.find(address);
    if (found == managedArrays.end()) {
        throw std::runtime_error("native array handle is no longer valid");
    }
    return *found->second;
}

std::size_t checkedArrayIndex(std::int64_t index, const ManagedNativeArray& array) {
    if (index < 0) {
        throw std::out_of_range("array index must be a non-negative integer");
    }
    const auto unsignedIndex = static_cast<std::uint64_t>(index);
    if (unsignedIndex >= array.elements.size()) {
        throw std::out_of_range(
            "array index " + std::to_string(index) +
            " is out of bounds for array of length " +
            std::to_string(array.elements.size()));
    }
    return static_cast<std::size_t>(unsignedIndex);
}

std::size_t checkedInsertionIndex(
    std::int64_t index,
    const ManagedNativeArray& array
) {
    if (index < 0 ||
        static_cast<std::uint64_t>(index) > array.elements.size()) {
        throw std::out_of_range(
            "array insertion index " + std::to_string(index) +
            " is out of bounds for array of length " +
            std::to_string(array.elements.size()));
    }
    return static_cast<std::size_t>(index);
}

void ensureArrayCapacity(ManagedNativeArray& array, std::size_t required) {
    if (required <= array.elements.capacity()) return;
    if (required > array.elements.max_size()) {
        throw std::length_error("array capacity overflow");
    }

    const std::size_t current = array.elements.capacity();
    const std::size_t grown = current == 0
        ? std::min<std::size_t>(4, array.elements.max_size())
        : current > array.elements.max_size() / 2
            ? array.elements.max_size()
            : current * 2;
    const std::size_t newCapacity = std::max(required, grown);
    if (newCapacity >
        (std::numeric_limits<std::size_t>::max() - sizeof(ManagedNativeArray)) /
            sizeof(std::uintptr_t)) {
        throw std::overflow_error("array allocation size overflow");
    }
    array.elements.reserve(newCapacity);

    const std::size_t newBytes = sizeof(ManagedNativeArray) +
        array.elements.capacity() * sizeof(std::uintptr_t);
    if (newBytes > array.managedBytes &&
        newBytes - array.managedBytes >
            std::numeric_limits<std::size_t>::max() - managedBytes) {
        throw std::overflow_error("managed heap byte count overflow");
    }
    managedBytes += newBytes - array.managedBytes;
    array.managedBytes = newBytes;
}

bool managedValuesEqual(std::uintptr_t left, std::uintptr_t right) {
    const std::uintptr_t leftTag = left & managedTagMask;
    const std::uintptr_t rightTag = right & managedTagMask;
    if (leftTag == managedStringTag && rightTag == managedStringTag) {
        auto* leftAddress = reinterpret_cast<ManagedNativeString*>(
            left & ~managedTagMask);
        auto* rightAddress = reinterpret_cast<ManagedNativeString*>(
            right & ~managedTagMask);
        const auto leftString = managedStrings.find(leftAddress);
        const auto rightString = managedStrings.find(rightAddress);
        return leftString != managedStrings.end() &&
            rightString != managedStrings.end() &&
            leftString->second->value == rightString->second->value;
    }
    if (leftTag == managedArrayTag && rightTag == managedArrayTag) {
        return managedArrays.contains(reinterpret_cast<ManagedNativeArray*>(
                   left & ~managedTagMask)) &&
            managedArrays.contains(reinterpret_cast<ManagedNativeArray*>(
                   right & ~managedTagMask)) &&
            left == right;
    }
    if (leftTag == managedStructTag && rightTag == managedStructTag) {
        return managedStructs.contains(reinterpret_cast<ManagedNativeStruct*>(
                   left & ~managedTagMask)) &&
            managedStructs.contains(reinterpret_cast<ManagedNativeStruct*>(
                   right & ~managedTagMask)) &&
            left == right;
    }
    const bool leftIsManaged =
        (leftTag == managedStringTag &&
         managedStrings.contains(reinterpret_cast<ManagedNativeString*>(
             left & ~managedTagMask))) ||
        (leftTag == managedArrayTag &&
         managedArrays.contains(reinterpret_cast<ManagedNativeArray*>(
             left & ~managedTagMask))) ||
        (leftTag == managedStructTag &&
         managedStructs.contains(reinterpret_cast<ManagedNativeStruct*>(
             left & ~managedTagMask)));
    const bool rightIsManaged =
        (rightTag == managedStringTag &&
         managedStrings.contains(reinterpret_cast<ManagedNativeString*>(
             right & ~managedTagMask))) ||
        (rightTag == managedArrayTag &&
         managedArrays.contains(reinterpret_cast<ManagedNativeArray*>(
             right & ~managedTagMask))) ||
        (rightTag == managedStructTag &&
         managedStructs.contains(reinterpret_cast<ManagedNativeStruct*>(
             right & ~managedTagMask)));
    if (leftIsManaged || rightIsManaged) return false;
    return std::bit_cast<double>(static_cast<std::uint64_t>(left)) ==
        std::bit_cast<double>(static_cast<std::uint64_t>(right));
}

const char* decodeString(const char* value) {
    if (!value) return nullptr;
    const std::uintptr_t tagged = reinterpret_cast<std::uintptr_t>(value);
    if ((tagged & managedTagMask) != managedStringTag) return value;
    auto* object = reinterpret_cast<ManagedNativeString*>(
        tagged & ~managedStringTag);
    const auto found = managedStrings.find(object);
    if (found == managedStrings.end()) {
        throw std::runtime_error("native string handle is no longer valid");
    }
    return found->second->value.c_str();
}

std::int64_t checkedManagedBytes() {
    if (managedBytes > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("managed heap byte count exceeds the supported range");
    }
    return static_cast<std::int64_t>(managedBytes);
}

const char* checkedString(const char* value, const char* argument);

const char* allocateManagedResult(std::string value) {
    return allocateManagedString(std::move(value));
}

std::string checkedText(const char* value, const char* argument) {
    return decodeString(checkedString(value, argument));
}

std::vector<std::string> checkedStringArray(std::uintptr_t value) {
    const ManagedNativeArray& array = checkedArray(value);
    std::vector<std::string> result;
    result.reserve(array.elements.size());
    for (const std::uintptr_t element : array.elements) {
        result.push_back(checkedText(
            reinterpret_cast<const char*>(element), "process argument"));
    }
    return result;
}

std::int64_t checkedNativeInteger(double value, const char* operation) {
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        value >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::domain_error(
            std::string(operation) + " result is not representable by native integer numbers");
    }
    return static_cast<std::int64_t>(value);
}

double decodeNativeNumber(std::int64_t bits) {
    return std::bit_cast<double>(static_cast<std::uint64_t>(bits));
}

std::int64_t encodeNativeNumber(double value) {
    return std::bit_cast<std::int64_t>(value);
}

std::int64_t createManagedStringArray(const std::vector<std::string>& values) {
    std::vector<std::uintptr_t> handles(values.size(), 0);
    TemporaryRootScope roots(handles.data(), handles.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        handles[index] = reinterpret_cast<std::uintptr_t>(
            allocateManagedResult(values[index])) | managedStringTag;
    }
    return static_cast<std::int64_t>(
        allocateManagedArray(handles.data(), handles.size()));
}

std::vector<std::string> processCommandLineArguments() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot parse process command line");
    }

    std::vector<std::string> result;
    try {
        result.reserve(count > 1 ? static_cast<std::size_t>(count - 1) : 0);
        for (int index = 1; index < count; ++index) {
            const int required = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, raw[index], -1, nullptr, 0,
                nullptr, nullptr);
            if (required == 0) {
                throw std::system_error(
                    static_cast<int>(GetLastError()), std::system_category(),
                    "cannot convert process argument to UTF-8");
            }
            std::string argument(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, raw[index], -1,
                    argument.data(), required, nullptr, nullptr) == 0) {
                throw std::system_error(
                    static_cast<int>(GetLastError()), std::system_category(),
                    "cannot convert process argument to UTF-8");
            }
            argument.resize(static_cast<std::size_t>(required - 1));
            result.push_back(std::move(argument));
        }
    } catch (...) {
        LocalFree(raw);
        throw;
    }
    LocalFree(raw);
    return result;
}

std::vector<double> checkedNumberArray(std::uintptr_t handle) {
    const ManagedNativeArray& array = checkedArray(handle);
    std::vector<double> values;
    values.reserve(array.elements.size());
    for (const std::uintptr_t value : array.elements) {
        values.push_back(decodeNativeNumber(static_cast<std::int64_t>(value)));
    }
    return values;
}

std::int64_t createNumberArray(const std::vector<double>& values) {
    std::vector<std::uintptr_t> slots;
    slots.reserve(values.size());
    for (const double value : values) {
        slots.push_back(static_cast<std::uintptr_t>(encodeNativeNumber(value)));
    }
    return static_cast<std::int64_t>(
        allocateManagedArray(slots.data(), slots.size()));
}

Application& graphics() {
    if (!application) application = std::make_unique<Application>();
    return *application;
}

void fail(const char* message) noexcept {
    if (nativeExceptionSlot) {
        try {
            nativeExceptionMessage = message ? message : "unknown failure";
            *nativeExceptionSlot = reinterpret_cast<std::uintptr_t>(
                nativeExceptionMessage.c_str());
            return;
        } catch (...) {
            *nativeExceptionSlot = reinterpret_cast<std::uintptr_t>(
                "native runtime error");
            return;
        }
    }

    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error && error != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const char prefix[] = "native Z+ runtime error: ";
        WriteFile(error, prefix, static_cast<DWORD>(sizeof(prefix) - 1), &written, nullptr);
        WriteFile(error, message, static_cast<DWORD>(std::char_traits<char>::length(message)),
                  &written, nullptr);
        constexpr char newline[] = "\r\n";
        WriteFile(error, newline, 2, &written, nullptr);
    }
    ExitProcess(ERROR_INVALID_PARAMETER);
}

template<typename Result, typename Function>
Result guard(Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (const std::exception& error) {
        fail(error.what());
        return Result{};
    } catch (...) {
        fail("unknown failure");
        return Result{};
    }
}

template<typename Function>
std::int64_t guardVoid(Function&& function) noexcept {
    return guard<std::int64_t>([&]() {
        std::forward<Function>(function)();
        return 0;
    });
}

int checkedInt(std::int64_t value, const char* argument) {
    const double decoded = decodeNativeNumber(value);
    if (std::isfinite(decoded) && std::trunc(decoded) == decoded &&
        decoded >= static_cast<double>(INT_MIN) &&
        decoded <= static_cast<double>(INT_MAX) &&
        (value < INT_MIN || value > INT_MAX)) {
        return static_cast<int>(decoded);
    }
    if (value < static_cast<std::int64_t>(INT_MIN) ||
        value > static_cast<std::int64_t>(INT_MAX)) {
        throw std::out_of_range(std::string(argument) + " is outside the supported integer range");
    }
    return static_cast<int>(value);
}

const char* checkedString(const char* value, const char* argument) {
    if (!value) throw std::invalid_argument(std::string(argument) + " must not be null");
    return decodeString(value);
}

zplus::graphics::Color color(const char* value) {
    return zplus::graphics::parseColor(checkedString(value, "color"));
}

Event& currentEvent(std::int64_t window) {
    const auto found = events.find(checkedInt(window, "window"));
    if (found == events.end()) throw std::runtime_error("no graphics event is available");
    return found->second;
}

template<typename T>
const char* keepString(T&& value) {
    return allocateManagedString(std::forward<T>(value));
}

} // namespace

ZPLUS_GRAPHICS_API void zpg_setExceptionSlot(std::uintptr_t* slot) noexcept {
    nativeExceptionSlot = slot;
}

ZPLUS_GRAPHICS_API std::int64_t zpg_createWindow(
    const char* title, std::int64_t width, std::int64_t height
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().createWindow(checkedString(title, "title"),
                                       checkedInt(width, "width"),
                                       checkedInt(height, "height"));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_numberText(std::int64_t bits) noexcept {
    return guard<const char*>([&] {
        return keepString(zplus::runtime::formatNumber(
            std::bit_cast<double>(static_cast<std::uint64_t>(bits))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_numberRemainder(
    std::int64_t left, std::int64_t right
) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(std::fmod(
            decodeNativeNumber(left), decodeNativeNumber(right)));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_setPosition(
    std::int64_t window, std::int64_t x, std::int64_t y
) noexcept {
    return guardVoid([&] { graphics().setPosition(checkedInt(window, "window"),
                                                  checkedInt(x, "x"), checkedInt(y, "y")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_resize(
    std::int64_t window, std::int64_t width, std::int64_t height
) noexcept {
    return guardVoid([&] { graphics().resize(checkedInt(window, "window"),
                                             checkedInt(width, "width"),
                                             checkedInt(height, "height")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_setTitle(std::int64_t window, const char* title) noexcept {
    return guardVoid([&] { graphics().setTitle(checkedInt(window, "window"),
                                               checkedString(title, "title")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_setBackground(
    std::int64_t window, const char* value
) noexcept {
    return guardVoid([&] { graphics().setBackground(checkedInt(window, "window"), color(value)); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_show(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().show(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_hide(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().hide(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_minimize(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().minimize(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_maximize(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().maximize(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_restore(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().restore(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_focus(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().focus(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_close(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().close(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_isOpen(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] { return graphics().isOpen(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_run(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] { return graphics().run(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_drawText(
    std::int64_t window, const char* text, std::int64_t x, std::int64_t y,
    const char* value, std::int64_t size
) noexcept {
    return guardVoid([&] {
        graphics().drawText(checkedInt(window, "window"), checkedString(text, "text"),
                            checkedInt(x, "x"), checkedInt(y, "y"), color(value),
                            checkedInt(size, "size"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_drawImage(
    std::int64_t window, const char* path, std::int64_t x, std::int64_t y,
    std::int64_t width, std::int64_t height
) noexcept {
    return guardVoid([&] {
        graphics().drawImage(
            checkedInt(window, "window"), checkedString(path, "image path"),
            checkedInt(x, "x"), checkedInt(y, "y"),
            checkedInt(width, "width"), checkedInt(height, "height"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_drawLine(
    std::int64_t window, std::int64_t x1, std::int64_t y1, std::int64_t x2,
    std::int64_t y2, const char* value, std::int64_t thickness
) noexcept {
    return guardVoid([&] {
        graphics().drawLine(checkedInt(window, "window"), checkedInt(x1, "x1"),
                            checkedInt(y1, "y1"), checkedInt(x2, "x2"),
                            checkedInt(y2, "y2"), color(value),
                            checkedInt(thickness, "thickness"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_drawRectangle(
    std::int64_t window, std::int64_t x, std::int64_t y, std::int64_t width,
    std::int64_t height, const char* value, std::int64_t filled,
    std::int64_t thickness
) noexcept {
    return guardVoid([&] {
        graphics().drawRectangle(checkedInt(window, "window"), checkedInt(x, "x"),
                                 checkedInt(y, "y"), checkedInt(width, "width"),
                                 checkedInt(height, "height"), color(value), filled != 0,
                                 checkedInt(thickness, "thickness"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_drawCircle(
    std::int64_t window, std::int64_t x, std::int64_t y, std::int64_t radius,
    const char* value, std::int64_t filled, std::int64_t thickness
) noexcept {
    return guardVoid([&] {
        graphics().drawCircle(checkedInt(window, "window"), checkedInt(x, "x"),
                              checkedInt(y, "y"), checkedInt(radius, "radius"),
                              color(value), filled != 0, checkedInt(thickness, "thickness"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_clear(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().clear(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_pollEvent(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        const int id = checkedInt(window, "window");
        events[id] = graphics().pollEvent(id);
        return encodeNativeNumber(static_cast<double>(events[id].type));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_waitEvent(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        const int id = checkedInt(window, "window");
        events[id] = graphics().waitEvent(id);
        return encodeNativeNumber(static_cast<double>(events[id].type));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_update(std::int64_t window) noexcept {
    return guardVoid([&] { graphics().update(checkedInt(window, "window")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_setTargetFPS(
    std::int64_t window, std::int64_t fps
) noexcept {
    return guardVoid([&] { graphics().setTargetFPS(checkedInt(window, "window"),
                                                   checkedInt(fps, "fps")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_deltaTimeMs(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            graphics().deltaTimeMs(checkedInt(window, "window"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_framesPerSecond(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            graphics().framesPerSecond(checkedInt(window, "window"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_keyDown(
    std::int64_t window, std::int64_t key
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().keyDown(checkedInt(window, "window"), checkedInt(key, "key"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_keyPressed(
    std::int64_t window, std::int64_t key
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().keyPressed(checkedInt(window, "window"), checkedInt(key, "key"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_keyReleased(
    std::int64_t window, std::int64_t key
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().keyReleased(checkedInt(window, "window"), checkedInt(key, "key"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_mouseX(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            graphics().mouseX(checkedInt(window, "window"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_mouseY(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            graphics().mouseY(checkedInt(window, "window"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_mouseButtonDown(
    std::int64_t window, std::int64_t button
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().mouseButtonDown(checkedInt(window, "window"),
                                          checkedInt(button, "button"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_mouseButtonPressed(
    std::int64_t window, std::int64_t button
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().mouseButtonPressed(checkedInt(window, "window"),
                                             checkedInt(button, "button"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_mouseButtonReleased(
    std::int64_t window, std::int64_t button
) noexcept {
    return guard<std::int64_t>([&] {
        return graphics().mouseButtonReleased(checkedInt(window, "window"),
                                              checkedInt(button, "button"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_eventX(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(currentEvent(window).x));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_eventY(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(currentEvent(window).y));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_eventKey(std::int64_t window) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(currentEvent(window).key));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_eventText(std::int64_t window) noexcept {
    return guard<const char*>([&] { return keepString(currentEvent(window).text); });
}

ZPLUS_GRAPHICS_API const char* zpg_concat(const char* left, const char* right) noexcept {
    return guard<const char*>([&] {
        return keepString(std::string(checkedString(left, "left string")) +
                          checkedString(right, "right string"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_compareStrings(
    const char* left, const char* right
) noexcept {
    return guard<std::int64_t>([&] {
        const int result = std::string_view(checkedString(left, "left string")).compare(
            checkedString(right, "right string"));
        return result < 0 ? -1 : result > 0 ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stringData(const char* value) noexcept {
    return guard<const char*>([&] {
        const char* decoded = checkedString(value, "string");
        return decoded;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_json_valid(const char* source) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::json::valid(checkedText(source, "source")) ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_compact(const char* source) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::json::compact(checkedText(source, "source")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_quote(const char* value) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::json::quote(checkedText(value, "value")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_get(const char* source, const char* pointer) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::json::getRaw(
            checkedText(source, "source"), checkedText(pointer, "pointer")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_getString(
    const char* source, const char* pointer
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::json::getString(
            checkedText(source, "source"), checkedText(pointer, "pointer")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_json_getNumber(
    const char* source, const char* pointer
) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(zplus::json::getNumber(
            checkedText(source, "source"), checkedText(pointer, "pointer")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_json_getBoolean(
    const char* source, const char* pointer
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::json::getBoolean(
            checkedText(source, "source"), checkedText(pointer, "pointer")) ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_json_arrayLength(
    const char* source, const char* pointer
) noexcept {
    return guard<std::int64_t>([&] {
        const auto size = zplus::json::arrayLength(
            checkedText(source, "source"), checkedText(pointer, "pointer"));
        if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("JSON array is too large");
        }
        return encodeNativeNumber(static_cast<double>(size));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_arrayGet(
    const char* source, const char* pointer, std::int64_t index
) noexcept {
    return guard<const char*>([&] {
        const auto decodedIndex = checkedNativeInteger(
            decodeNativeNumber(index), "JSON array index");
        if (decodedIndex < 0) throw std::out_of_range("JSON array index must not be negative");
        return allocateManagedResult(zplus::json::arrayGet(
            checkedText(source, "source"), checkedText(pointer, "pointer"),
            static_cast<std::size_t>(decodedIndex)));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_json_kind(
    const char* source, const char* pointer
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::json::valueKind(
            checkedText(source, "source"), checkedText(pointer, "pointer")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverCreate(
    const char* address, std::int64_t port
) noexcept {
    return guard<std::int64_t>([&] {
        const auto decodedPort = checkedNativeInteger(
            decodeNativeNumber(port), "HTTP server port");
        return zplus::http::createServer(
            checkedText(address, "address"), decodedPort);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverStart(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::http::startServer(handle) ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverRunning(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::http::serverRunning(handle) ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverPort(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            zplus::http::serverPort(handle)));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_http_serverNext(
    std::int64_t handle, std::int64_t timeoutMilliseconds
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::http::nextRequest(
            handle, checkedNativeInteger(
                decodeNativeNumber(timeoutMilliseconds), "HTTP timeout")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverRespond(
    std::int64_t handle,
    std::int64_t requestId,
    std::int64_t status,
    const char* headersJson,
    const char* body
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::http::respond(
            handle,
            checkedNativeInteger(decodeNativeNumber(requestId), "HTTP request ID"),
            checkedNativeInteger(decodeNativeNumber(status), "HTTP response status"),
            checkedText(headersJson, "headers"),
            checkedText(body, "body")) ? 1 : 0;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverStop(std::int64_t handle) noexcept {
    return guardVoid([&] {
        zplus::http::stopServer(handle);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_serverConfigure(
    std::int64_t handle,
    std::int64_t maximumBodyBytes,
    std::int64_t timeoutMilliseconds
) noexcept {
    return guardVoid([&] {
        zplus::http::configureServer(
            handle,
            checkedNativeInteger(decodeNativeNumber(maximumBodyBytes), "HTTP body limit"),
            checkedNativeInteger(decodeNativeNumber(timeoutMilliseconds), "HTTP timeout"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_http_configureCors(
    std::int64_t handle,
    const char* origin,
    const char* methods,
    const char* headers
) noexcept {
    return guardVoid([&] {
        zplus::http::configureCors(
            handle,
            checkedText(origin, "origin"),
            checkedText(methods, "methods"),
            checkedText(headers, "headers"));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_http_routeParams(
    const char* path, const char* pattern
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::http::routeParams(
            checkedText(path, "path"), checkedText(pattern, "pattern")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_http_queryValue(
    const char* query, const char* name
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::http::queryValue(
            checkedText(query, "query"), checkedText(name, "name")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_http_requestHeader(
    const char* requestJson, const char* name
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::http::requestHeader(
            checkedText(requestJson, "request"), checkedText(name, "name")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_http_clientRequest(
    const char* method,
    const char* url,
    const char* headersJson,
    const char* body,
    std::int64_t timeoutMilliseconds
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::http::clientRequest(
            checkedText(method, "method"),
            checkedText(url, "url"),
            checkedText(headersJson, "headers"),
            checkedText(body, "body"),
            checkedNativeInteger(
                decodeNativeNumber(timeoutMilliseconds), "HTTP timeout")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_sqlite_open(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::sqlite::openDatabase(checkedText(path, "path"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_sqlite_close(std::int64_t handle) noexcept {
    return guardVoid([&] {
        zplus::sqlite::closeDatabase(handle);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_sqlite_execute(
    std::int64_t handle, const char* sql, const char* parametersJson
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::sqlite::execute(
            handle,
            checkedText(sql, "SQL"),
            checkedText(parametersJson, "parameters"));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_sqlite_query(
    std::int64_t handle, const char* sql, const char* parametersJson
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::sqlite::query(
            handle,
            checkedText(sql, "SQL"),
            checkedText(parametersJson, "parameters")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_sqlite_changes(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(zplus::sqlite::changes(handle)));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_pushRoots(
    const std::uintptr_t* slots,
    std::size_t count
) noexcept {
    return guardVoid([&] {
        if (count != 0 && !slots) {
            throw std::invalid_argument("root slots must not be null");
        }
        managedRoots.push_back({slots, count});
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_popRoots() noexcept {
    return guardVoid([&] {
        if (managedRoots.empty()) {
            throw std::logic_error("native managed root stack is unbalanced");
        }
        managedRoots.pop_back();
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_arrayCreate(
    const std::uintptr_t* elements,
    std::size_t count
) noexcept {
    return guard<std::uintptr_t>([&] {
        return allocateManagedArray(elements, count);
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_structCreate(
    std::uintptr_t typeId,
    const std::uintptr_t* fields,
    std::size_t count
) noexcept {
    return guard<std::uintptr_t>([&] {
        if (typeId == 0) {
            throw std::invalid_argument("struct type id must be positive");
        }
        if (count != 0 && !fields) {
            throw std::invalid_argument("struct fields must not be null");
        }
        TemporaryRootScope roots(fields, count);
        if (count > (std::numeric_limits<std::size_t>::max() -
                     sizeof(ManagedNativeStruct)) / sizeof(std::uintptr_t)) {
            throw std::length_error("struct allocation size overflow");
        }
        if (managedBytes >= nextCollectionThreshold) {
            collectManagedObjects();
        }
        auto object = std::make_unique<ManagedNativeStruct>();
        object->typeId = typeId;
        if (count != 0) object->fields.assign(fields, fields + count);
        object->managedBytes = sizeof(ManagedNativeStruct) +
            object->fields.capacity() * sizeof(std::uintptr_t);
        if (object->managedBytes >
            std::numeric_limits<std::size_t>::max() - managedBytes) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        ManagedNativeStruct* address = object.get();
        managedBytes += object->managedBytes;
        managedStructs.emplace(address, std::move(object));
        return reinterpret_cast<std::uintptr_t>(address) | managedStructTag;
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_structGet(
    std::uintptr_t structure,
    std::uintptr_t fieldIndex
) noexcept {
    return guard<std::uintptr_t>([&] {
        const ManagedNativeStruct& object = checkedStruct(structure);
        if (fieldIndex >= object.fields.size()) {
            throw std::out_of_range("struct field index is out of bounds");
        }
        return object.fields[fieldIndex];
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_structSet(
    std::uintptr_t structure,
    std::uintptr_t fieldIndex,
    std::uintptr_t value
) noexcept {
    return guardVoid([&] {
        std::uintptr_t roots[] = {structure, value};
        TemporaryRootScope temporaryRoots(roots, 2);
        ManagedNativeStruct& object = checkedStruct(structure);
        if (fieldIndex >= object.fields.size()) {
            throw std::out_of_range("struct field index is out of bounds");
        }
        object.fields[fieldIndex] = value;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_structIsType(
    std::uintptr_t structure,
    std::uintptr_t typeId
) noexcept {
    return guard<std::int64_t>([&] {
        if ((structure & managedTagMask) != managedStructTag) return 0;
        const auto* address = reinterpret_cast<ManagedNativeStruct*>(
            structure & ~managedTagMask);
        const auto found = managedStructs.find(
            const_cast<ManagedNativeStruct*>(address));
        return found != managedStructs.end() &&
                found->second->typeId == typeId
            ? 1
            : 0;
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_arrayGet(
    std::uintptr_t array,
    std::int64_t index
) noexcept {
    return guard<std::uintptr_t>([&] {
        ManagedNativeArray& object = checkedArray(array);
        const auto checkedIndex = checkedNativeInteger(
            decodeNativeNumber(index), "array index");
        return object.elements[checkedArrayIndex(checkedIndex, object)];
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arraySet(
    std::uintptr_t array,
    std::int64_t index,
    std::uintptr_t value
) noexcept {
    return guardVoid([&] {
        ManagedNativeArray& object = checkedArray(array);
        const auto checkedIndex = checkedNativeInteger(
            decodeNativeNumber(index), "array index");
        object.elements[checkedArrayIndex(checkedIndex, object)] = value;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arrayLength(
    std::uintptr_t array
) noexcept {
    return guard<std::int64_t>([&] {
        ManagedNativeArray& object = checkedArray(array);
        if (object.elements.size() > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("array length exceeds the supported range");
        }
        return encodeNativeNumber(static_cast<double>(object.elements.size()));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arrayPush(
    std::uintptr_t array,
    std::uintptr_t value
) noexcept {
    return guardVoid([&] {
        std::uintptr_t roots[] = {array, value};
        TemporaryRootScope temporaryRoots(roots, 2);
        ManagedNativeArray& object = checkedArray(array);
        if (object.elements.size() == object.elements.max_size()) {
            throw std::length_error("array capacity overflow");
        }
        const std::size_t required = object.elements.size() + 1;
        if (required > object.elements.capacity() &&
            managedBytes >= nextCollectionThreshold) {
            collectManagedObjects();
        }
        ensureArrayCapacity(object, required);
        object.elements.push_back(value);
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_arrayPop(
    std::uintptr_t array
) noexcept {
    return guard<std::uintptr_t>([&] {
        ManagedNativeArray& object = checkedArray(array);
        if (object.elements.empty()) {
            throw std::out_of_range("cannot pop from an empty array");
        }
        const std::uintptr_t value = object.elements.back();
        object.elements.pop_back();
        return value;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arrayClear(
    std::uintptr_t array
) noexcept {
    return guardVoid([&] {
        checkedArray(array).elements.clear();
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arrayInsert(
    std::uintptr_t array,
    std::int64_t index,
    std::uintptr_t value
) noexcept {
    return guardVoid([&] {
        std::uintptr_t roots[] = {array, value};
        TemporaryRootScope temporaryRoots(roots, 2);
        ManagedNativeArray& object = checkedArray(array);
        const auto checkedIndex = checkedNativeInteger(
            decodeNativeNumber(index), "array insertion index");
        const std::size_t insertionIndex = checkedInsertionIndex(checkedIndex, object);
        if (object.elements.size() == object.elements.max_size()) {
            throw std::length_error("array capacity overflow");
        }
        const std::size_t required = object.elements.size() + 1;
        if (required > object.elements.capacity() &&
            managedBytes >= nextCollectionThreshold) {
            collectManagedObjects();
        }
        ensureArrayCapacity(object, required);
        object.elements.insert(
            object.elements.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
            value);
    });
}

ZPLUS_GRAPHICS_API std::uintptr_t zpg_arrayRemove(
    std::uintptr_t array,
    std::int64_t index
) noexcept {
    return guard<std::uintptr_t>([&] {
        ManagedNativeArray& object = checkedArray(array);
        const auto checkedIndex = checkedNativeInteger(
            decodeNativeNumber(index), "array index");
        const std::size_t removalIndex = checkedArrayIndex(checkedIndex, object);
        const auto element =
            object.elements.begin() + static_cast<std::ptrdiff_t>(removalIndex);
        const std::uintptr_t value = *element;
        object.elements.erase(element);
        return value;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_arrayContains(
    std::uintptr_t array,
    std::uintptr_t value
) noexcept {
    return guard<std::int64_t>([&] {
        const ManagedNativeArray& object = checkedArray(array);
        return std::any_of(
            object.elements.begin(),
            object.elements.end(),
            [value](std::uintptr_t element) {
                return managedValuesEqual(element, value);
            }) ? std::int64_t{1} : std::int64_t{0};
    });
}

#define ZPG_STDLIB_STRING_UNARY(Name, Operation) \
ZPLUS_GRAPHICS_API const char* zpg_stdlib_string_##Name(const char* value) noexcept { \
    return guard<const char*>([&] { \
        return allocateManagedResult(zplus::stdlib::stringUnary( \
            Operation, checkedText(value, "value"))); \
    }); \
}

#define ZPG_STDLIB_MATH_UNARY(Name, Operation) \
ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_math_##Name(std::int64_t value) noexcept { \
    return guard<std::int64_t>([&] { \
        return encodeNativeNumber(zplus::stdlib::mathUnary( \
            Operation, decodeNativeNumber(value))); \
    }); \
}

#define ZPG_STDLIB_MATH_BINARY(Name, Operation) \
ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_math_##Name( \
    std::int64_t left, std::int64_t right) noexcept { \
    return guard<std::int64_t>([&] { \
        return encodeNativeNumber(zplus::stdlib::mathBinary( \
            Operation, decodeNativeNumber(left), decodeNativeNumber(right))); \
    }); \
}

ZPG_STDLIB_MATH_UNARY(abs, "abs")
ZPG_STDLIB_MATH_UNARY(round, "round")
ZPG_STDLIB_MATH_UNARY(floor, "floor")
ZPG_STDLIB_MATH_UNARY(ceil, "ceil")
ZPG_STDLIB_MATH_UNARY(sqrt, "sqrt")
ZPG_STDLIB_MATH_UNARY(sin, "sin")
ZPG_STDLIB_MATH_UNARY(cos, "cos")
ZPG_STDLIB_MATH_UNARY(tan, "tan")
ZPG_STDLIB_MATH_UNARY(asin, "asin")
ZPG_STDLIB_MATH_UNARY(acos, "acos")
ZPG_STDLIB_MATH_UNARY(atan, "atan")
ZPG_STDLIB_MATH_UNARY(log, "log")
ZPG_STDLIB_MATH_UNARY(log10, "log10")
ZPG_STDLIB_MATH_UNARY(exp, "exp")
ZPG_STDLIB_MATH_BINARY(min, "min")
ZPG_STDLIB_MATH_BINARY(max, "max")
ZPG_STDLIB_MATH_BINARY(pow, "pow")

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_math_pi() noexcept {
    return guard<std::int64_t>([] {
        return encodeNativeNumber(zplus::stdlib::mathConstant("pi"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_math_e() noexcept {
    return guard<std::int64_t>([] {
        return encodeNativeNumber(zplus::stdlib::mathConstant("e"));
    });
}

ZPG_STDLIB_STRING_UNARY(upper, "upper")
ZPG_STDLIB_STRING_UNARY(lower, "lower")
ZPG_STDLIB_STRING_UNARY(trim, "trim")
ZPG_STDLIB_STRING_UNARY(reverse, "reverse")

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_string_length(const char* value) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            zplus::stdlib::stringLength(checkedText(value, "value"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_string_contains(
    const char* value, const char* search) noexcept {
    return guard<std::int64_t>([&] {
        const auto result = zplus::stdlib::stringBinary(
            "contains", checkedText(value, "value"), checkedText(search, "search"));
        return result == "true" ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_string_startsWith(
    const char* value, const char* prefix) noexcept {
    return guard<std::int64_t>([&] {
        const auto result = zplus::stdlib::stringBinary(
            "startsWith", checkedText(value, "value"), checkedText(prefix, "prefix"));
        return result == "true" ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_string_endsWith(
    const char* value, const char* suffix) noexcept {
    return guard<std::int64_t>([&] {
        const auto result = zplus::stdlib::stringBinary(
            "endsWith", checkedText(value, "value"), checkedText(suffix, "suffix"));
        return result == "true" ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_string_replace(
    const char* value, const char* search, const char* replacement) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::stdlib::stringReplace(
            checkedText(value, "value"), checkedText(search, "search"),
            checkedText(replacement, "replacement")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_string_substring(
    const char* value, std::int64_t start, std::int64_t length) noexcept {
    return guard<const char*>([&] {
        const auto decodedStart = checkedNativeInteger(
            decodeNativeNumber(start), "substring start");
        const auto decodedLength = checkedNativeInteger(
            decodeNativeNumber(length), "substring length");
        return allocateManagedResult(zplus::stdlib::substring(
            checkedText(value, "value"), decodedStart, decodedLength));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_string_repeat(
    const char* value, std::int64_t count) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::stdlib::repeat(
            checkedText(value, "value"), checkedNativeInteger(
                decodeNativeNumber(count), "repeat count")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_string_split(
    const char* value, const char* delimiter) noexcept {
    return guard<std::int64_t>([&] {
        return createManagedStringArray(zplus::stdlib::split(
            checkedText(value, "value"), checkedText(delimiter, "delimiter")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_string_join(
    std::uintptr_t array, const char* separator) noexcept {
    return guard<const char*>([&] {
        const ManagedNativeArray& values = checkedArray(array);
        std::vector<std::string> strings;
        strings.reserve(values.elements.size());
        for (const std::uintptr_t value : values.elements) {
            strings.push_back(checkedText(
                reinterpret_cast<const char*>(value), "array element"));
        }
        return allocateManagedResult(
            zplus::stdlib::stringJoin(strings, checkedText(separator, "separator")));
    });
}

#undef ZPG_STDLIB_STRING_UNARY
#undef ZPG_STDLIB_MATH_UNARY
#undef ZPG_STDLIB_MATH_BINARY

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_sum(
    std::uintptr_t array) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(array);
        return encodeNativeNumber(std::accumulate(
            values.begin(), values.end(), 0.0));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_average(
    std::uintptr_t array) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(array);
        if (values.empty()) throw std::domain_error("average cannot be computed for an empty array");
        const double total = std::accumulate(values.begin(), values.end(), 0.0);
        return encodeNativeNumber(total / values.size());
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_min(
    std::uintptr_t array) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(array);
        if (values.empty()) throw std::domain_error("min cannot be computed for an empty array");
        return encodeNativeNumber(*std::min_element(values.begin(), values.end()));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_max(
    std::uintptr_t array) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(array);
        if (values.empty()) throw std::domain_error("max cannot be computed for an empty array");
        return encodeNativeNumber(*std::max_element(values.begin(), values.end()));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_reverse(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        auto values = checkedNumberArray(handle);
        std::reverse(values.begin(), values.end());
        return createNumberArray(values);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_sort(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        auto values = checkedNumberArray(handle);
        std::stable_sort(values.begin(), values.end());
        return createNumberArray(values);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_unique(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(handle);
        std::vector<double> unique;
        unique.reserve(values.size());
        for (const double value : values) {
            if (std::find(unique.begin(), unique.end(), value) == unique.end()) {
                unique.push_back(value);
            }
        }
        return createNumberArray(unique);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_contains(
    std::uintptr_t handle, std::int64_t value) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(handle);
        return std::find(values.begin(), values.end(), decodeNativeNumber(value)) != values.end()
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_count(
    std::uintptr_t handle, std::int64_t value) noexcept {
    return guard<std::int64_t>([&] {
        const auto values = checkedNumberArray(handle);
        return encodeNativeNumber(static_cast<double>(
            std::count(values.begin(), values.end(), decodeNativeNumber(value))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_first(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        const ManagedNativeArray& values = checkedArray(handle);
        if (values.elements.empty()) throw std::out_of_range("first cannot be used with an empty array");
        return encodeNativeNumber(decodeNativeNumber(
            static_cast<std::int64_t>(values.elements.front())));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_last(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        const ManagedNativeArray& values = checkedArray(handle);
        if (values.elements.empty()) throw std::out_of_range("last cannot be used with an empty array");
        return encodeNativeNumber(decodeNativeNumber(
            static_cast<std::int64_t>(values.elements.back())));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_collections_isEmpty(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return checkedArray(handle).elements.empty() ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_integer(
    std::int64_t minimum, std::int64_t maximum) noexcept {
    return guard<std::int64_t>([&] {
        const auto min = checkedNativeInteger(decodeNativeNumber(minimum), "random.integer minimum");
        const auto max = checkedNativeInteger(decodeNativeNumber(maximum), "random.integer maximum");
        return encodeNativeNumber(static_cast<double>(
            zplus::stdlib::randomInteger(min, max)));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_number(
    std::int64_t minimum, std::int64_t maximum) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(zplus::stdlib::randomNumber(
            decodeNativeNumber(minimum), decodeNativeNumber(maximum)));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_boolean() noexcept {
    return guard<std::int64_t>([] {
        return zplus::stdlib::randomBoolean() ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_choice(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        const ManagedNativeArray& values = checkedArray(handle);
        if (values.elements.empty()) throw std::out_of_range("choice cannot be used with an empty array");
        const auto index = static_cast<std::size_t>(
            zplus::stdlib::randomInteger(0, static_cast<std::int64_t>(values.elements.size() - 1)));
        return encodeNativeNumber(decodeNativeNumber(
            static_cast<std::int64_t>(values.elements[index])));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_shuffle(
    std::uintptr_t handle) noexcept {
    return guard<std::int64_t>([&] {
        ManagedNativeArray& values = checkedArray(handle);
        for (std::size_t index = values.elements.size(); index > 1; --index) {
            const auto selected = static_cast<std::size_t>(
                zplus::stdlib::randomInteger(0, static_cast<std::int64_t>(index - 1)));
            std::swap(values.elements[index - 1], values.elements[selected]);
        }
        return static_cast<std::int64_t>(handle);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_random_seed(std::int64_t seed) noexcept {
    return guardVoid([&] {
        const auto decoded = checkedNativeInteger(
            decodeNativeNumber(seed), "random.seed");
        if (decoded < 0) throw std::invalid_argument("random seed must be non-negative");
        zplus::stdlib::randomSeed(static_cast<std::uint64_t>(decoded));
    });
}

#define ZPG_STDLIB_PATH_UNARY(Name, Operation) \
ZPLUS_GRAPHICS_API const char* zpg_stdlib_path_##Name(const char* value) noexcept { \
    return guard<const char*>([&] { \
        return allocateManagedResult(zplus::stdlib::pathUnary( \
            Operation, checkedText(value, "path"))); \
    }); \
}

ZPG_STDLIB_PATH_UNARY(normalize, "normalize")
ZPG_STDLIB_PATH_UNARY(extension, "extension")
ZPG_STDLIB_PATH_UNARY(filename, "filename")
ZPG_STDLIB_PATH_UNARY(directory, "directory")
ZPG_STDLIB_PATH_UNARY(absolute, "absolute")

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_path_isAbsolute(const char* value) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::pathUnary(
            "isAbsolute", checkedText(value, "path")) == "true"
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_path_join(
    const char* left, const char* right) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::stdlib::pathJoin(
            checkedText(left, "left path"), checkedText(right, "right path")));
    });
}

#undef ZPG_STDLIB_PATH_UNARY

ZPLUS_GRAPHICS_API const char* zpg_stdlib_file_read(const char* path) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::stdlib::fileRead(checkedText(path, "path")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_write(
    const char* path, const char* contents) noexcept {
    return guardVoid([&] {
        zplus::stdlib::fileWrite(
            checkedText(path, "path"), checkedText(contents, "contents"), false);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_append(
    const char* path, const char* contents) noexcept {
    return guardVoid([&] {
        zplus::stdlib::fileWrite(
            checkedText(path, "path"), checkedText(contents, "contents"), true);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_exists(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::fileExists(checkedText(path, "path"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_delete(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::fileDelete(checkedText(path, "path"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_scheduleDeleteOnReboot(
    const char* path
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::scheduleFileRemovalOnReboot(
            checkedText(path, "path")) ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_createDirectory(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::fileCreateDirectory(checkedText(path, "path"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_listDirectory(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return createManagedStringArray(
            zplus::stdlib::fileListDirectory(checkedText(path, "path")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_size(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            zplus::stdlib::fileSize(checkedText(path, "path"))));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_isDirectory(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::fileIsDirectory(checkedText(path, "path"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_env_get(const char* name) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(
            zplus::stdlib::environmentGet(checkedText(name, "name")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_env_set(
    const char* name, const char* value) noexcept {
    return guardVoid([&] {
        zplus::stdlib::environmentSet(
            checkedText(name, "name"), checkedText(value, "value"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_env_exists(const char* name) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::stdlib::environmentExists(checkedText(name, "name"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_env_homeDirectory() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::stdlib::homeDirectory());
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_env_currentDirectory() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::stdlib::currentDirectory());
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_time_now() noexcept {
    return guard<std::int64_t>([] {
        return encodeNativeNumber(zplus::stdlib::timeNow());
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_time_format(
    std::int64_t timestamp, const char* format) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::stdlib::timeFormat(
            decodeNativeNumber(timestamp), checkedText(format, "format")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_time_parse(
    const char* value, const char* format) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(zplus::stdlib::timeParse(
            checkedText(value, "value"), checkedText(format, "format")));
    });
}

#define ZPG_STDLIB_TIME_PART(Name, Part) \
ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_time_##Name(std::int64_t timestamp) noexcept { \
    return guard<std::int64_t>([&] { \
    return encodeNativeNumber(zplus::stdlib::timePart( \
        Part, decodeNativeNumber(timestamp))); \
    }); \
}

ZPG_STDLIB_TIME_PART(year, "year")
ZPG_STDLIB_TIME_PART(month, "month")
ZPG_STDLIB_TIME_PART(day, "day")
ZPG_STDLIB_TIME_PART(hour, "hour")
ZPG_STDLIB_TIME_PART(minute, "minute")
ZPG_STDLIB_TIME_PART(second, "second")

#undef ZPG_STDLIB_TIME_PART

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_time_sleep(std::int64_t milliseconds) noexcept {
    return guardVoid([&] {
        zplus::stdlib::timeSleep(checkedNativeInteger(
            decodeNativeNumber(milliseconds), "sleep duration"));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_args_count() noexcept {
    return guard<std::int64_t>([] {
        const auto arguments = processCommandLineArguments();
        if (arguments.size() > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("argument count exceeds the supported range");
        }
        return encodeNativeNumber(static_cast<double>(arguments.size()));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_args_get(std::int64_t index) noexcept {
    return guard<const char*>([&] {
        const auto arguments = processCommandLineArguments();
        index = checkedNativeInteger(decodeNativeNumber(index), "argument index");
        if (index < 0 || static_cast<std::uint64_t>(index) >= arguments.size()) {
            throw std::out_of_range("command-line argument index is out of range");
        }
        return allocateManagedResult(arguments[static_cast<std::size_t>(index)]);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_args_has(const char* value) noexcept {
    return guard<std::int64_t>([&] {
        const auto arguments = processCommandLineArguments();
        return std::find(arguments.begin(), arguments.end(), checkedText(value, "value"))
            != arguments.end() ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_args_all() noexcept {
    return guard<std::int64_t>([] {
        return createManagedStringArray(processCommandLineArguments());
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_debug(const char* message) noexcept {
    return guardVoid([&] { zplus::stdlib::logWrite("debug", checkedText(message, "message")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_info(const char* message) noexcept {
    return guardVoid([&] { zplus::stdlib::logWrite("info", checkedText(message, "message")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_warning(const char* message) noexcept {
    return guardVoid([&] { zplus::stdlib::logWrite("warning", checkedText(message, "message")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_error(const char* message) noexcept {
    return guardVoid([&] { zplus::stdlib::logWrite("error", checkedText(message, "message")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_setLevel(const char* level) noexcept {
    return guardVoid([&] { zplus::stdlib::logSetLevel(checkedText(level, "level")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_log_setOutput(const char* path) noexcept {
    return guardVoid([&] { zplus::stdlib::logSetOutput(checkedText(path, "path")); });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_heapUsed() noexcept {
    return guard<std::int64_t>([] {
        return encodeNativeNumber(static_cast<double>(checkedManagedBytes()));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_heapObjects() noexcept {
    return guard<std::int64_t>([] {
        const std::size_t count =
            managedStrings.size() + managedArrays.size() + managedStructs.size();
        if (count > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("managed heap object count exceeds the supported range");
        }
        return encodeNativeNumber(static_cast<double>(count));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_collect() noexcept {
    return guard<std::int64_t>([] {
        collectManagedObjects();
        return std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_collectionCount() noexcept {
    return guard<std::int64_t>([] {
        if (collectionCount > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("collection count exceeds the supported range");
        }
        return encodeNativeNumber(static_cast<double>(collectionCount));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_setThreshold(
    std::int64_t bytes
) noexcept {
    return guardVoid([&] {
        const auto decodedBytes = checkedNativeInteger(
            decodeNativeNumber(bytes), "memory threshold");
        if (decodedBytes < static_cast<std::int64_t>(minimumThreshold) ||
            static_cast<std::uint64_t>(decodedBytes) > maximumThreshold) {
            throw std::invalid_argument(
                "memory threshold must be from 1024 through 1099511627776 bytes");
        }
        minimumCollectionThreshold = static_cast<std::size_t>(decodedBytes);
        nextCollectionThreshold = std::max(
            minimumCollectionThreshold, managedBytes);
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_memory_shutdown() noexcept {
    return guardVoid([] {
        managedRoots.clear();
        managedArrays.clear();
        managedStructs.clear();
        managedStrings.clear();
        managedBytes = 0;
        collectionCount = 0;
        minimumCollectionThreshold = 64 * 1024;
        nextCollectionThreshold = 64 * 1024;
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_shutdown() noexcept {
    return guardVoid([&] {
        zplus::http::shutdownServers();
        zplus::sqlite::shutdownDatabases();
        events.clear();
        application.reset();
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_copyFile(
    const char* source, const char* destination, std::int64_t overwrite
) noexcept {
    return guard<std::int64_t>([&] {
        if (overwrite != 0 && overwrite != 1) {
            throw std::invalid_argument("overwrite must be a boolean");
        }
        return zplus::installer::copyFile(
            checkedText(source, "source"), checkedText(destination, "destination"),
            overwrite != 0) ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_moveFile(
    const char* source, const char* destination, std::int64_t overwrite
) noexcept {
    return guard<std::int64_t>([&] {
        if (overwrite != 0 && overwrite != 1) {
            throw std::invalid_argument("overwrite must be a boolean");
        }
        return zplus::installer::moveFile(
            checkedText(source, "source"), checkedText(destination, "destination"),
            overwrite != 0) ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_file_removeEmptyDirectory(
    const char* path
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::removeEmptyDirectory(
            checkedText(path, "path")) ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_crypto_sha256File(
    const char* path
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::sha256File(
            checkedText(path, "path")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_process_run(
    const char* executable, std::uintptr_t arguments,
    const char* workingDirectory, std::int64_t timeout
) noexcept {
    return guard<const char*>([&] {
        const auto milliseconds = checkedNativeInteger(
            decodeNativeNumber(timeout), "process timeout");
        return allocateManagedResult(zplus::installer::runProcess(
            checkedText(executable, "executable"),
            checkedStringArray(arguments),
            checkedText(workingDirectory, "working directory"),
            milliseconds));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_platform() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::installer::platformName());
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_architecture() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::installer::architecture());
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_osVersion() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::installer::operatingSystemVersion());
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_executablePath() noexcept {
    return guard<const char*>([] {
        return allocateManagedResult(zplus::installer::executablePath());
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_system_availableSpace(
    const char* path
) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(
            zplus::installer::availableSpace(checkedText(path, "path"))));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_selectFolder(
    const char* title, const char* initialPath
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::selectFolder(
            checkedText(title, "title"), checkedText(initialPath, "initial path")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_system_messageBox(
    const char* title, const char* message, const char* buttons, const char* icon
) noexcept {
    return guard<std::int64_t>([&] {
        return encodeNativeNumber(static_cast<double>(zplus::installer::messageBox(
            checkedText(title, "title"), checkedText(message, "message"),
            checkedText(buttons, "buttons"), checkedText(icon, "icon"))));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_registry_read(
    const char* keyPath, const char* valueName
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::registryRead(
            checkedText(keyPath, "key path"), checkedText(valueName, "value name")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_registry_valueKind(
    const char* keyPath, const char* valueName
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::registryValueKind(
            checkedText(keyPath, "key path"), checkedText(valueName, "value name")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_registry_write(
    const char* keyPath, const char* valueName, const char* value, const char* kind
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::registryWrite(
            checkedText(keyPath, "key path"), checkedText(valueName, "value name"),
            checkedText(value, "value"), checkedText(kind, "kind"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_registry_deleteValue(
    const char* keyPath, const char* valueName
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::registryDeleteValue(
            checkedText(keyPath, "key path"), checkedText(valueName, "value name"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_system_createShortcut(
    const char* path, const char* target, const char* arguments,
    const char* workingDirectory
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::createShortcut(
            checkedText(path, "path"), checkedText(target, "target"),
            checkedText(arguments, "arguments"),
            checkedText(workingDirectory, "working directory"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_shortcutTarget(
    const char* path
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::shortcutTarget(
            checkedText(path, "path")));
    });
}

ZPLUS_GRAPHICS_API const char* zpg_stdlib_system_shortcutArguments(
    const char* path
) noexcept {
    return guard<const char*>([&] {
        return allocateManagedResult(zplus::installer::shortcutArguments(
            checkedText(path, "path")));
    });
}

ZPLUS_GRAPHICS_API std::int64_t zpg_stdlib_system_removeShortcutIfMatches(
    const char* path, const char* target, const char* arguments
) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::installer::removeShortcutIfMatches(
            checkedText(path, "path"), checkedText(target, "target"),
            checkedText(arguments, "arguments"))
            ? std::int64_t{1} : std::int64_t{0};
    });
}
