#include "Runtime.hpp"

#include "../compiler/NumericLiteral.hpp"
#include "../compiler/ir/IR.hpp"
#include "../compiler/ir/IRVerifier.hpp"
#include "Input.hpp"
#include "NumericFormat.hpp"
#include "../ZPGraphics/ZGraphics.hpp"
#include "audio/Audio.hpp"
#include "physics/Physics.hpp"
#include "StandardLibrary.hpp"
#include "Json.hpp"
#include "Http.hpp"
#include "Web.hpp"
#include "SQLite.hpp"
#include "installer/Platform.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zplus::runtime {

// ============================================================
// Runtime value
// ============================================================

struct RuntimeArray;
struct RuntimeString;
struct RuntimeStruct;
using RuntimeArrayPtr = RuntimeArray*;
using RuntimeStringPtr = RuntimeString*;
using RuntimeStructPtr = RuntimeStruct*;
using RuntimeValue =
    std::variant<double, std::string, bool, RuntimeArrayPtr, RuntimeStringPtr,
                 RuntimeStructPtr>;

struct RuntimeArray {
    std::vector<RuntimeValue> elements;
    bool marked = false;
    std::size_t managedBytes = 0;
};

struct RuntimeString {
    std::string value;
    bool marked = false;
    std::size_t managedBytes = 0;
};

struct RuntimeStruct {
    std::string typeName;
    std::unordered_map<std::string, RuntimeValue> fields;
    bool marked = false;
    std::size_t managedBytes = 0;
};

static zplus::graphics::Application& graphicsApplication() {
    static zplus::graphics::Application application;
    return application;
}

static std::unordered_map<int, zplus::graphics::Event>& graphicsEvents() {
    static std::unordered_map<int, zplus::graphics::Event> events;
    return events;
}

// ============================================================
// Execution result
// ============================================================

struct ExecutionResult {

    int status = 0;

    bool returned = false;

    RuntimeValue value = 0.0;
};


// ============================================================
// Runtime environment
// ============================================================

struct Environment : std::unordered_map<std::string, RuntimeValue> {
    static constexpr std::size_t inlineSlotCount = 16;

    Environment* parent = nullptr;
    Environment* globals = nullptr;
    std::array<RuntimeValue, inlineSlotCount> inlineSlots{};
    std::vector<RuntimeValue> overflowSlots;
    std::vector<RuntimeValue> deferredRoots;
    std::size_t slotCount = 0;

    void prepareSlots(std::size_t count) {
        slotCount = count;
        if (count > inlineSlots.size()) {
            overflowSlots.resize(count - inlineSlots.size());
        }
    }

    RuntimeValue& slot(std::size_t index) {
        if (index >= slotCount) {
            throw std::runtime_error("invalid runtime slot");
        }
        return index < inlineSlots.size()
            ? inlineSlots[index]
            : overflowSlots[index - inlineSlots.size()];
    }

    const RuntimeValue& slot(std::size_t index) const {
        if (index >= slotCount) {
            throw std::runtime_error("invalid runtime slot");
        }
        return index < inlineSlots.size()
            ? inlineSlots[index]
            : overflowSlots[index - inlineSlots.size()];
    }
};

thread_local std::vector<Environment*> activeRuntimeEnvironments;

class EnvironmentRootScope {
public:
    explicit EnvironmentRootScope(Environment& environment) {
        activeRuntimeEnvironments.push_back(&environment);
    }

    ~EnvironmentRootScope() {
        if (!activeRuntimeEnvironments.empty()) {
            activeRuntimeEnvironments.pop_back();
        }
    }

    EnvironmentRootScope(const EnvironmentRootScope&) = delete;
    EnvironmentRootScope& operator=(const EnvironmentRootScope&) = delete;
};

class ManagedHeap {
public:
    static constexpr std::size_t initialThreshold = 64 * 1024;
    static constexpr std::size_t minimumThreshold = 1024;
    static constexpr std::size_t maximumThreshold = 1ULL << 40;

    RuntimeArrayPtr makeArray(std::vector<RuntimeValue> elements) {
        collectIfNeeded(std::span<const RuntimeValue>(elements));
        auto object = std::make_unique<RuntimeArray>();
        object->elements = std::move(elements);
        object->managedBytes = checkedArrayBytes(object->elements.capacity());
        if (object->managedBytes >
            std::numeric_limits<std::size_t>::max() - bytesUsed_) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        RuntimeArrayPtr result = object.get();
        bytesUsed_ += object->managedBytes;
        arrays_.push_back(std::move(object));
        return result;
    }

    void push(RuntimeArray& array, RuntimeValue value) {
        std::array<RuntimeValue, 2> roots{RuntimeArrayPtr{&array}, value};
        ensureCapacity(array, checkedRequiredSize(array, 1), roots);
        array.elements.push_back(std::move(value));
        updateArrayBytes(array);
    }

    RuntimeValue pop(RuntimeArray& array) {
        if (array.elements.empty()) {
            throw std::out_of_range("cannot pop from an empty array");
        }
        RuntimeValue value = std::move(array.elements.back());
        array.elements.pop_back();
        return value;
    }

    void clear(RuntimeArray& array) noexcept {
        array.elements.clear();
    }

    void insert(RuntimeArray& array, std::size_t index, RuntimeValue value) {
        if (index > array.elements.size()) {
            throw std::out_of_range(
                "array insertion index " + std::to_string(index) +
                " is out of bounds for array of length " +
                std::to_string(array.elements.size()));
        }
        std::array<RuntimeValue, 2> roots{RuntimeArrayPtr{&array}, value};
        ensureCapacity(array, checkedRequiredSize(array, 1), roots);
        array.elements.insert(
            array.elements.begin() + static_cast<std::ptrdiff_t>(index),
            std::move(value));
        updateArrayBytes(array);
    }

    RuntimeValue remove(RuntimeArray& array, std::size_t index) {
        if (index >= array.elements.size()) {
            throw std::out_of_range(
                "array index " + std::to_string(index) +
                " is out of bounds for array of length " +
                std::to_string(array.elements.size()));
        }
        auto element = array.elements.begin() + static_cast<std::ptrdiff_t>(index);
        RuntimeValue value = std::move(*element);
        array.elements.erase(element);
        return value;
    }

    RuntimeStringPtr makeString(
        std::string value,
        std::span<const RuntimeValue> temporaryRoots = {}
    ) {
        collectIfNeeded(temporaryRoots);
        auto object = std::make_unique<RuntimeString>();
        object->value = std::move(value);
        object->managedBytes = sizeof(RuntimeString) + object->value.capacity();
        if (object->managedBytes >
            std::numeric_limits<std::size_t>::max() - bytesUsed_) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        RuntimeStringPtr result = object.get();
        bytesUsed_ += object->managedBytes;
        strings_.push_back(std::move(object));
        return result;
    }

    RuntimeStructPtr makeStruct(
        std::string typeName,
        std::unordered_map<std::string, RuntimeValue> fields,
        std::span<const RuntimeValue> temporaryRoots = {}
    ) {
        collectIfNeeded(temporaryRoots);
        auto object = std::make_unique<RuntimeStruct>();
        object->typeName = std::move(typeName);
        object->fields = std::move(fields);
        object->managedBytes = sizeof(RuntimeStruct) +
            object->typeName.capacity() +
            object->fields.bucket_count() * sizeof(void*);
        for (const auto& [name, _] : object->fields) {
            object->managedBytes +=
                sizeof(std::pair<const std::string, RuntimeValue>) +
                name.capacity();
        }
        if (object->managedBytes >
            std::numeric_limits<std::size_t>::max() - bytesUsed_) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        RuntimeStructPtr result = object.get();
        bytesUsed_ += object->managedBytes;
        structs_.push_back(std::move(object));
        return result;
    }

    std::size_t collect(std::span<const RuntimeValue> temporaryRoots = {}) {
        std::vector<RuntimeArrayPtr> pendingArrays;
        std::vector<RuntimeStructPtr> pendingStructs;

        const auto markValue = [&](const RuntimeValue& value) {
            if (const auto* array = std::get_if<RuntimeArrayPtr>(&value);
                array && *array && !(*array)->marked) {
                (*array)->marked = true;
                pendingArrays.push_back(*array);
            } else if (const auto* string = std::get_if<RuntimeStringPtr>(&value);
                       string && *string && !(*string)->marked) {
                (*string)->marked = true;
            } else if (const auto* structure =
                           std::get_if<RuntimeStructPtr>(&value);
                       structure && *structure && !(*structure)->marked) {
                (*structure)->marked = true;
                pendingStructs.push_back(*structure);
            }
        };

        for (const Environment* environment : activeRuntimeEnvironments) {
            for (const auto& [_, value] : *environment) {
                markValue(value);
            }
            for (std::size_t i = 0; i < environment->slotCount; ++i) {
                markValue(environment->slot(i));
            }
            for (const RuntimeValue& value : environment->deferredRoots) {
                markValue(value);
            }
        }
        for (const RuntimeValue& value : temporaryRoots) {
            markValue(value);
        }

        while (!pendingArrays.empty() || !pendingStructs.empty()) {
            if (!pendingArrays.empty()) {
                RuntimeArrayPtr array = pendingArrays.back();
                pendingArrays.pop_back();
                for (const RuntimeValue& value : array->elements) {
                    markValue(value);
                }
            } else {
                RuntimeStructPtr structure = pendingStructs.back();
                pendingStructs.pop_back();
                for (const auto& [_, value] : structure->fields) {
                    markValue(value);
                }
            }
        }
        std::size_t reclaimed = 0;
        auto sweep = [this, &reclaimed](auto& objects) {
            auto current = objects.begin();
            while (current != objects.end()) {
                if (!(*current)->marked) {
                    bytesUsed_ -= (*current)->managedBytes;
                    current = objects.erase(current);
                    ++reclaimed;
                } else {
                    (*current)->marked = false;
                    ++current;
                }
            }
        };
        sweep(arrays_);
        sweep(strings_);
        sweep(structs_);

        ++collectionCount_;
        const std::size_t liveGrowth =
            bytesUsed_ > (maximumThreshold / 2)
                ? maximumThreshold
                : bytesUsed_ * 2;
        nextCollectionThreshold_ =
            std::max(minimumCollectionThreshold_, liveGrowth);
        return reclaimed;
    }

    void setThreshold(std::size_t bytes) {
        minimumCollectionThreshold_ = bytes;
        nextCollectionThreshold_ = std::max(bytes, bytesUsed_);
    }

    std::size_t bytesUsed() const noexcept { return bytesUsed_; }
    std::size_t objectCount() const noexcept {
        return arrays_.size() + strings_.size() + structs_.size();
    }
    std::size_t collectionCount() const noexcept { return collectionCount_; }

private:
    static std::size_t checkedRequiredSize(
        const RuntimeArray& array,
        std::size_t additional
    ) {
        if (additional > array.elements.max_size() - array.elements.size()) {
            throw std::length_error("array capacity overflow");
        }
        return array.elements.size() + additional;
    }

    static std::size_t checkedArrayBytes(std::size_t capacity) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max() - sizeof(RuntimeArray)) /
                sizeof(RuntimeValue)) {
            throw std::length_error("array capacity overflow");
        }
        return sizeof(RuntimeArray) + capacity * sizeof(RuntimeValue);
    }

    void updateArrayBytes(RuntimeArray& array) {
        const std::size_t updated = checkedArrayBytes(array.elements.capacity());
        if (updated > array.managedBytes &&
            updated - array.managedBytes >
                std::numeric_limits<std::size_t>::max() - bytesUsed_) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        if (updated >= array.managedBytes) {
            bytesUsed_ += updated - array.managedBytes;
        } else {
            bytesUsed_ -= array.managedBytes - updated;
        }
        array.managedBytes = updated;
    }

    void ensureCapacity(
        RuntimeArray& array,
        std::size_t required,
        std::span<const RuntimeValue> temporaryRoots
    ) {
        if (required <= array.elements.capacity()) return;
        collectIfNeeded(temporaryRoots);
        if (required > array.elements.max_size()) {
            throw std::length_error("array capacity overflow");
        }
        const std::size_t capacity = array.elements.capacity();
        const std::size_t doubled = capacity == 0
            ? std::min<std::size_t>(4, array.elements.max_size())
            : capacity > array.elements.max_size() / 2
                ? array.elements.max_size()
                : capacity * 2;
        const std::size_t newCapacity = std::max(required, doubled);
        const std::size_t newBytes = checkedArrayBytes(newCapacity);
        if (newBytes > array.managedBytes &&
            newBytes - array.managedBytes >
                std::numeric_limits<std::size_t>::max() - bytesUsed_) {
            throw std::overflow_error("managed heap byte count overflow");
        }
        array.elements.reserve(newCapacity);
        updateArrayBytes(array);
    }

    void collectIfNeeded(std::span<const RuntimeValue> temporaryRoots = {}) {
        if (bytesUsed_ >= nextCollectionThreshold_) {
            collect(temporaryRoots);
        }
    }

    std::vector<std::unique_ptr<RuntimeArray>> arrays_;
    std::vector<std::unique_ptr<RuntimeString>> strings_;
    std::vector<std::unique_ptr<RuntimeStruct>> structs_;
    std::size_t bytesUsed_ = 0;
    std::size_t collectionCount_ = 0;
    std::size_t minimumCollectionThreshold_ = initialThreshold;
    std::size_t nextCollectionThreshold_ = initialThreshold;
};

thread_local ManagedHeap* activeManagedHeap = nullptr;

static ManagedHeap& managedHeap() {
    if (!activeManagedHeap) {
        throw std::runtime_error("managed heap is unavailable");
    }
    return *activeManagedHeap;
}

class ManagedHeapScope {
public:
    explicit ManagedHeapScope(ManagedHeap& heap) : previous_(activeManagedHeap) {
        activeManagedHeap = &heap;
    }

    ~ManagedHeapScope() { activeManagedHeap = previous_; }

    ManagedHeapScope(const ManagedHeapScope&) = delete;
    ManagedHeapScope& operator=(const ManagedHeapScope&) = delete;

private:
    ManagedHeap* previous_;
};

thread_local zplus::ast::Location activeRuntimeLocation;
thread_local std::vector<const zplus::ir::Function*> runtimeFrames;

class RuntimeFrameScope {
public:
    explicit RuntimeFrameScope(const zplus::ir::Function& function)
        : uncaughtExceptions_(std::uncaught_exceptions()) {
        runtimeFrames.push_back(&function);
    }

    ~RuntimeFrameScope() {
        if (std::uncaught_exceptions() == uncaughtExceptions_ &&
            !runtimeFrames.empty()) {
            runtimeFrames.pop_back();
        }
    }

private:
    int uncaughtExceptions_;
};

class RuntimeContextScope {
public:
    ~RuntimeContextScope() {
        runtimeFrames.clear();
        activeRuntimeEnvironments.clear();
        activeRuntimeLocation = {};
    }
};

static std::string formatRuntimeError(const std::exception& error) {
    std::ostringstream message;
    if (!activeRuntimeLocation.sourceFile.empty() &&
        activeRuntimeLocation.line > 0 &&
        activeRuntimeLocation.column > 0) {
        message << activeRuntimeLocation.sourceFile << ':'
                << activeRuntimeLocation.line << ':'
                << activeRuntimeLocation.column << ": ";
    }
    message << error.what();

    for (auto frame = runtimeFrames.rbegin(); frame != runtimeFrames.rend(); ++frame) {
        const auto& function = **frame;
        message << "\n  at " << function.name;
        if (!function.location.sourceFile.empty() &&
            function.location.line > 0 &&
            function.location.column > 0) {
            message << " (" << function.location.sourceFile << ':'
                    << function.location.line << ':'
                    << function.location.column << ')';
        }
    }
    return message.str();
}

// ============================================================
// Type helpers
// ============================================================

static bool isNumber(
    const RuntimeValue& value
) noexcept {

    return std::holds_alternative<double>(
        value
    );
}


static bool isString(
    const RuntimeValue& value
) noexcept {

    if (std::holds_alternative<std::string>(value)) {
        return true;
    }
    const auto* string = std::get_if<RuntimeStringPtr>(&value);
    return string && *string;
}

static const std::string& stringValue(const RuntimeValue& value) {
    if (const auto* string = std::get_if<std::string>(&value)) {
        return *string;
    }
    if (const auto* string = std::get_if<RuntimeStringPtr>(&value);
        string && *string) {
        return (*string)->value;
    }
    throw std::runtime_error("runtime value is not a string");
}

static bool isBoolean(
    const RuntimeValue& value
) noexcept {

    return std::holds_alternative<bool>(
        value
    );
}

static bool isArray(const RuntimeValue& value) noexcept {
    const auto* array = std::get_if<RuntimeArrayPtr>(&value);
    return array && *array;
}

static bool isStruct(const RuntimeValue& value) noexcept {
    const auto* structure = std::get_if<RuntimeStructPtr>(&value);
    return structure && *structure;
}

static std::size_t checkedArrayIndex(
    const RuntimeValue& value,
    const RuntimeArray& array
) {
    if (!isNumber(value)) {
        throw std::runtime_error("array index must be a number");
    }

    const double index = std::get<double>(value);
    if (!std::isfinite(index) || std::floor(index) != index || index < 0.0) {
        throw std::out_of_range("array index must be a non-negative integer");
    }
    if (index >= static_cast<double>(array.elements.size())) {
        throw std::out_of_range(
            "array index " + std::to_string(index) +
            " is out of bounds for array of length " +
            std::to_string(array.elements.size())
        );
    }
    return static_cast<std::size_t>(index);
}


// ============================================================
// Conversion helpers
// ============================================================

static double toNumber(
    const RuntimeValue& value
) {

    if (isNumber(value)) {
        return std::get<double>(value);
    }

    if (isBoolean(value)) {

        return std::get<bool>(value)
            ? 1.0
            : 0.0;
    }

    if (isString(value)) {

        const std::string& text = stringValue(value);

        try {

            std::size_t consumed = 0;

            const double number =
                std::stod(
                    text,
                    &consumed
                );

            if (consumed == text.size()) {
                return number;
            }
        }
        catch (...) {
        }
    }

    return 0.0;
}

static std::int64_t toBitwiseInteger(const RuntimeValue& value) {
    const double number = toNumber(value);
    constexpr double minimum =
        static_cast<double>(std::numeric_limits<std::int64_t>::min());
    constexpr double limit = -minimum;
    if (!std::isfinite(number) || number < minimum || number >= limit) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(number);
}

static std::int64_t applyBitwise(
    const std::string& opcode,
    std::int64_t left,
    std::int64_t right
) {
    if (opcode == "bitand" || opcode == "and_assign") return left & right;
    if (opcode == "bitor" || opcode == "or_assign") return left | right;
    if (opcode == "bitxor" || opcode == "xor_assign") return left ^ right;

    const unsigned shift =
        static_cast<unsigned>(static_cast<std::uint64_t>(right) & 63U);
    if (opcode == "shl" || opcode == "shl_assign") {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(left) << shift;
        return std::bit_cast<std::int64_t>(bits);
    }
    return left >> shift;
}


static bool toBool(
    const RuntimeValue& value
) {

    if (isBoolean(value)) {
        return std::get<bool>(value);
    }

    if (isNumber(value)) {
        return std::get<double>(value) != 0.0;
    }

    if (isString(value)) {
        return !stringValue(value).empty();
    }
    if (isArray(value)) {
        return true;
    }

    return false;
}


static std::string valueToString(
    const RuntimeValue& value
) {

    if (isString(value)) {
        return stringValue(value);
    }

    if (isBoolean(value)) {

        return std::get<bool>(value)
            ? "true"
            : "false";
    }

    if (isNumber(value)) {
        return zplus::runtime::formatNumber(std::get<double>(value));
    }

    if (isArray(value)) {
        const auto& elements = std::get<RuntimeArrayPtr>(value)->elements;
        std::string result = "[";
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i != 0) result += ", ";
            result += valueToString(elements[i]);
        }
        result += "]";
        return result;
    }
    if (isStruct(value)) {
        return std::get<RuntimeStructPtr>(value)->typeName;
    }

    return "";
}


// ============================================================
// Console
// ============================================================

void initialize() {

#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

#endif
}


// ============================================================
// Literal helpers
// ============================================================

static std::string unquote(
    const std::string& value
) {

    if (value.size() >= 2) {

        const char first =
            value.front();

        const char last =
            value.back();

        if (
            (
                first == '"' &&
                last == '"'
            ) ||
            (
                first == '\'' &&
                last == '\''
            )
        ) {

            return value.substr(
                1,
                value.size() - 2
            );
        }
    }

    return value;
}


// ============================================================
// Resolve IR value
// ============================================================

static RuntimeValue resolveValue(
    const zplus::ir::Value& operand,
    const Environment& environment
) {

    const std::string& name =
        operand.name;

    if (operand.runtimeSlot != zplus::ir::kInvalidRuntimeSlot) {
        const Environment* storage = operand.globalSlot
            ? environment.globals
            : &environment;
        if (storage) {
            return storage->slot(operand.runtimeSlot);
        }
    }

    if (operand.hasNumericValue) {
        return operand.numericValue;
    }

    if (operand.kind == zplus::ir::Value::Kind::Boolean) {
        return name == "true" || name == "True";
    }

    if (operand.kind == zplus::ir::Value::Kind::Null) {
        return false;
    }

    if (operand.kind == zplus::ir::Value::Kind::String ||
        operand.kind == zplus::ir::Value::Kind::Character) {
        return unquote(name);
    }

    // --------------------------------------------------------
    // Variables / temporaries
    // --------------------------------------------------------

    for (const Environment* scope = &environment; scope; scope = scope->parent) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            return found->second;
        }
    }


    // --------------------------------------------------------
    // Boolean
    // --------------------------------------------------------

    if (
        name == "true" ||
        name == "True"
    ) {
        return true;
    }

    if (
        name == "false" ||
        name == "False"
    ) {
        return false;
    }


    // --------------------------------------------------------
    // Null
    // --------------------------------------------------------

    if (
        name == "null" ||
        name == "Null"
    ) {
        return false;
    }


    // --------------------------------------------------------
    // Quoted string
    // --------------------------------------------------------

    if (
        name.size() >= 2 &&
        (
            (
                name.front() == '"' &&
                name.back() == '"'
            ) ||
            (
                name.front() == '\'' &&
                name.back() == '\''
            )
        )
    ) {

        return unquote(name);
    }


    // --------------------------------------------------------
    // Number
    // --------------------------------------------------------

    double number = 0.0;
    if (zplus::numeric::parseLiteral(name, number)) return number;


    // --------------------------------------------------------
    // Raw string
    // --------------------------------------------------------

    return name;
}


// ============================================================
// Store result
// ============================================================

static void storeResult(
    const zplus::ir::Instruction& instruction,
    RuntimeValue value,
    Environment& environment
) {

    if (instruction.result.empty()) {
        return;
    }

    if (instruction.resultSlot != zplus::ir::kInvalidRuntimeSlot) {
        environment.slot(instruction.resultSlot) = std::move(value);
    } else {
        environment[instruction.result] = std::move(value);
    }
}

static void storeVariable(
    Environment& environment,
    const zplus::ir::Value& target,
    RuntimeValue value
) {
    if (target.runtimeSlot != zplus::ir::kInvalidRuntimeSlot) {
        Environment* storage = target.globalSlot
            ? environment.globals
            : &environment;
        if (storage) {
            storage->slot(target.runtimeSlot) = std::move(value);
            return;
        }
    }

    const std::string& name = target.name;
    for (Environment* scope = &environment; scope; scope = scope->parent) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            found->second = std::move(value);
            return;
        }
    }
    environment[name] = std::move(value);
}


// ============================================================
// Find basic block
// ============================================================

static const zplus::ir::BasicBlock* findBlock(
    const zplus::ir::Function& function,
    const std::string& label
) {

    for (const auto& block :
         function.blocks) {

        if (block.label == label) {
            return &block;
        }
    }

    return nullptr;
}


// ============================================================
// Forward declaration
// ============================================================

static ExecutionResult executeFunction(
    const zplus::ir::Module& module,
    const zplus::ir::Function& function,
    std::span<RuntimeValue> arguments,
    Environment* parentEnvironment = nullptr
);


// ============================================================
// Built-in say
// ============================================================

static RuntimeValue builtinSay(
    const zplus::ir::Instruction& instruction,
    const Environment& environment
) {

    for (
        std::size_t i = 1;
        i < instruction.operands.size();
        ++i
    ) {

        const RuntimeValue value =
            resolveValue(
                instruction.operands[i],
                environment
            );

        std::cout <<
            valueToString(value);

        if (
            i + 1 <
            instruction.operands.size()
        ) {
            std::cout << ' ';
        }
    }

    std::cout << '\n';

    return false;
}


// ============================================================
// Built-in output without a newline
// ============================================================

static RuntimeValue builtinPrint(
    const zplus::ir::Instruction& instruction,
    const Environment& environment
) {

    for (
        std::size_t i = 1;
        i < instruction.operands.size();
        ++i
    ) {

        const RuntimeValue value =
            resolveValue(
                instruction.operands[i],
                environment
            );

        std::cout <<
            valueToString(value);
    }

    return false;
}


// ============================================================
// Built-in time
// ============================================================

static RuntimeValue builtinTime() {

    static const auto start =
        std::chrono::steady_clock::now();

    const auto now =
        std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            now - start
        ).count();

    return static_cast<double>(
        elapsed
    );
}

static RuntimeValue executeMemoryBuiltin(
    const std::string& callee,
    const zplus::ir::Instruction& instruction,
    const Environment& environment
) {
    const std::size_t argumentCount = instruction.operands.size() - 1;
    if (callee == "__zmemory_heapUsed" ||
        callee == "__zmemory_heapObjects" ||
        callee == "__zmemory_collect" ||
        callee == "__zmemory_collectionCount") {
        if (argumentCount != 0) {
            throw std::runtime_error(callee + " expects no arguments");
        }
        ManagedHeap& heap = managedHeap();
        if (callee == "__zmemory_heapUsed") {
            return static_cast<double>(heap.bytesUsed());
        }
        if (callee == "__zmemory_heapObjects") {
            return static_cast<double>(heap.objectCount());
        }
        if (callee == "__zmemory_collectionCount") {
            return static_cast<double>(heap.collectionCount());
        }
        heap.collect();
        return false;
    }

    if (callee == "__zmemory_setThreshold") {
        if (argumentCount != 1) {
            throw std::runtime_error("__zmemory_setThreshold expects one argument");
        }
        const double requested = toNumber(resolveValue(instruction.operands[1], environment));
        if (!std::isfinite(requested) ||
            std::floor(requested) != requested ||
            requested < static_cast<double>(ManagedHeap::minimumThreshold) ||
            requested > static_cast<double>(ManagedHeap::maximumThreshold)) {
            throw std::runtime_error(
                "memory threshold must be an integer from 1024 through 1099511627776 bytes"
            );
        }
        managedHeap().setThreshold(static_cast<std::size_t>(requested));
        return false;
    }

    throw std::runtime_error("unknown memory-management function '" + callee + "'");
}

static std::int64_t standardInteger(const RuntimeValue& value, const char* argument) {
    if (!isNumber(value)) {
        throw std::invalid_argument(std::string(argument) + " must be a number");
    }
    const double number = std::get<double>(value);
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        number >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::out_of_range(std::string(argument) + " must be a supported integer");
    }
    return static_cast<std::int64_t>(number);
}

static RuntimeValue executeStandardLibraryBuiltin(
    const std::string& callee,
    const zplus::ir::Instruction& instruction,
    Environment& environment
) {
    const std::size_t count = instruction.operands.size() - 1;
    const auto failArity = [&] {
        throw std::runtime_error(callee + " received an invalid number of arguments");
    };
    const auto value = [&](std::size_t index) {
        if (index >= count) failArity();
        return resolveValue(instruction.operands[index + 1], environment);
    };
    const auto text = [&](std::size_t index) {
        RuntimeValue argument = value(index);
        if (!isString(argument)) {
            throw std::invalid_argument(callee + " argument " +
                std::to_string(index + 1) + " must be a string");
        }
        return stringValue(argument);
    };
    const auto number = [&](std::size_t index) {
        RuntimeValue argument = value(index);
        if (!isNumber(argument)) {
            throw std::invalid_argument(callee + " argument " +
                std::to_string(index + 1) + " must be a number");
        }
        return std::get<double>(argument);
    };
    const auto integer = [&](std::size_t index) {
        const double argument = number(index);
        if (!std::isfinite(argument) || std::trunc(argument) != argument ||
            argument < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            argument >= -static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
            throw std::invalid_argument(callee + " argument " +
                std::to_string(index + 1) + " must be a finite integer");
        }
        return static_cast<std::int64_t>(argument);
    };
    const auto array = [&](std::size_t index) -> RuntimeArray& {
        RuntimeValue argument = value(index);
        if (!isArray(argument)) {
            throw std::invalid_argument(callee + " argument " +
                std::to_string(index + 1) + " must be an array");
        }
        return *std::get<RuntimeArrayPtr>(argument);
    };
    const std::string prefix = "__zstdlib_";
    const std::string audioPrefix = "__zaudio_";
    if (callee.rfind(audioPrefix, 0) == 0) {
        const std::string name = callee.substr(audioPrefix.size());
        if (name == "initialize") {
            if (count != 0) failArity();
            zplus::audio::initialize();
            return false;
        }
        if (name == "initialized") {
            if (count != 0) failArity();
            return zplus::audio::initialized();
        }
        if (name == "shutdown" || name == "unloadAll" ||
            name == "mute" || name == "unmute") {
            if (count != 0) failArity();
            if (name == "shutdown") zplus::audio::shutdown();
            else if (name == "unloadAll") zplus::audio::unloadAll();
            else if (name == "mute") zplus::audio::mute();
            else zplus::audio::unmute();
            return false;
        }
        if (name == "loadMusic" || name == "loadSound") {
            if (count != 1) failArity();
            return static_cast<double>(name == "loadMusic"
                ? zplus::audio::loadMusic(text(0))
                : zplus::audio::loadSound(text(0)));
        }
        if (name == "unload" || name == "play" || name == "pause" ||
            name == "resume" || name == "stop" || name == "isPlaying" ||
            name == "isPaused" || name == "isStopped" ||
            name == "getVolume" || name == "isLooping" ||
            name == "getPlaybackPosition" || name == "getDuration") {
            if (count != 1) failArity();
            const std::int64_t handle = integer(0);
            if (name == "unload") {
                zplus::audio::unload(handle);
                return false;
            }
            if (name == "play") {
                zplus::audio::play(handle);
                return false;
            }
            if (name == "pause") {
                zplus::audio::pause(handle);
                return false;
            }
            if (name == "resume") {
                zplus::audio::resume(handle);
                return false;
            }
            if (name == "stop") {
                zplus::audio::stop(handle);
                return false;
            }
            if (name == "isPlaying") return zplus::audio::isPlaying(handle);
            if (name == "isPaused") return zplus::audio::isPaused(handle);
            if (name == "isStopped") return zplus::audio::isStopped(handle);
            if (name == "getVolume") return zplus::audio::getVolume(handle);
            if (name == "isLooping") return zplus::audio::isLooping(handle);
            if (name == "getPlaybackPosition") {
                return zplus::audio::getPlaybackPosition(handle);
            }
            return zplus::audio::getDuration(handle);
        }
        if (name == "setVolume" || name == "setLooping" ||
            name == "setPlaybackPosition") {
            if (count != 2) failArity();
            const std::int64_t handle = integer(0);
            if (name == "setVolume") {
                zplus::audio::setVolume(handle, number(1));
            } else if (name == "setLooping") {
                RuntimeValue looping = value(1);
                if (!std::holds_alternative<bool>(looping)) {
                    throw std::invalid_argument(
                        callee + " argument 2 must be a boolean");
                }
                zplus::audio::setLooping(handle, std::get<bool>(looping));
            } else {
                zplus::audio::setPlaybackPosition(handle, number(1));
            }
            return false;
        }
        if (name == "setMasterVolume") {
            if (count != 1) failArity();
            zplus::audio::setMasterVolume(number(0));
            return false;
        }
        if (name == "getMasterVolume" || name == "isMuted" ||
            name == "deviceCount" || name == "defaultDevice") {
            if (count != 0) failArity();
            if (name == "getMasterVolume") return zplus::audio::getMasterVolume();
            if (name == "isMuted") return zplus::audio::isMuted();
            if (name == "deviceCount") {
                return static_cast<double>(zplus::audio::deviceCount());
            }
            return managedHeap().makeString(zplus::audio::defaultDevice());
        }
        if (name == "deviceName") {
            if (count != 1) failArity();
            return managedHeap().makeString(
                zplus::audio::deviceName(integer(0)));
        }
        if (name == "selectDevice") {
            if (count != 1) failArity();
            zplus::audio::selectDevice(text(0));
            return false;
        }
        throw std::invalid_argument("unknown audio operation '" + name + "'");
    }
    const std::string physicsPrefix = "__zphysics_";
    if (callee.rfind(physicsPrefix, 0) == 0) {
        const std::string name = callee.substr(physicsPrefix.size());
        const auto boolean = [&](std::size_t index) {
            RuntimeValue argument = value(index);
            if (!std::holds_alternative<bool>(argument)) {
                throw std::invalid_argument(callee + " argument " +
                    std::to_string(index + 1) + " must be a boolean");
            }
            return std::get<bool>(argument);
        };
        if (name == "createWorld") {
            if (count != 2) failArity();
            return static_cast<double>(zplus::physics::createWorld(number(0), number(1)));
        }
        if (name == "destroyWorld" || name == "destroyBody" ||
            name == "destroyShape" || name == "destroyJoint") {
            if (count != 1) failArity();
            const auto handle = integer(0);
            if (name == "destroyWorld") zplus::physics::destroyWorld(handle);
            else if (name == "destroyBody") zplus::physics::destroyBody(handle);
            else if (name == "destroyShape") zplus::physics::destroyShape(handle);
            else zplus::physics::destroyJoint(handle);
            return false;
        }
        if (name == "getGravityX" || name == "getGravityY" ||
            name == "isPaused" || name == "bodyCount" ||
            name == "shapeCount" || name == "contactCount" ||
            name == "getPositionX" || name == "getPositionY" ||
            name == "getRotation" || name == "getVelocityX" ||
            name == "getVelocityY" || name == "getAngularVelocity" ||
            name == "getMass" || name == "raycastFraction" ||
            name == "raycastPointX" || name == "raycastPointY" ||
            name == "raycastNormalX" || name == "raycastNormalY") {
            if (count != 1) failArity();
            const auto handle = integer(0);
            if (name == "getGravityX") return zplus::physics::getGravityX(handle);
            if (name == "getGravityY") return zplus::physics::getGravityY(handle);
            if (name == "isPaused") return zplus::physics::isPaused(handle);
            if (name == "bodyCount") return static_cast<double>(zplus::physics::bodyCount(handle));
            if (name == "shapeCount") return static_cast<double>(zplus::physics::shapeCount(handle));
            if (name == "contactCount") return static_cast<double>(zplus::physics::contactCount(handle));
            if (name == "getPositionX") return zplus::physics::getPositionX(handle);
            if (name == "getPositionY") return zplus::physics::getPositionY(handle);
            if (name == "getRotation") return zplus::physics::getRotation(handle);
            if (name == "getVelocityX") return zplus::physics::getVelocityX(handle);
            if (name == "getVelocityY") return zplus::physics::getVelocityY(handle);
            if (name == "getAngularVelocity") return zplus::physics::getAngularVelocity(handle);
            if (name == "getMass") return zplus::physics::getMass(handle);
            if (name == "raycastFraction") return zplus::physics::raycastFraction(handle);
            if (name == "raycastPointX") return zplus::physics::raycastPointX(handle);
            if (name == "raycastPointY") return zplus::physics::raycastPointY(handle);
            if (name == "raycastNormalX") return zplus::physics::raycastNormalX(handle);
            return zplus::physics::raycastNormalY(handle);
        }
        if (name == "setGravity" || name == "setPosition" ||
            name == "setVelocity" || name == "applyForce" ||
            name == "applyImpulse") {
            if (count != 3) failArity();
            const auto handle = integer(0);
            if (name == "setGravity") zplus::physics::setGravity(handle, number(1), number(2));
            else if (name == "setPosition") zplus::physics::setPosition(handle, number(1), number(2));
            else if (name == "setVelocity") zplus::physics::setVelocity(handle, number(1), number(2));
            else if (name == "applyForce") zplus::physics::applyForce(handle, number(1), number(2));
            else zplus::physics::applyImpulse(handle, number(1), number(2));
            return false;
        }
        if (name == "setFixedTimeStep" || name == "setIterations" ||
            name == "setPaused" || name == "update" ||
            name == "setRotation" || name == "setAngularVelocity" ||
            name == "setMass" || name == "setLinearDamping" ||
            name == "setAngularDamping" || name == "applyTorque") {
            if (count != 2) failArity();
            const auto handle = integer(0);
            if (name == "setFixedTimeStep") zplus::physics::setFixedTimeStep(handle, number(1));
            else if (name == "setIterations") zplus::physics::setIterations(handle, integer(1));
            else if (name == "setPaused") zplus::physics::setPaused(handle, boolean(1));
            else if (name == "update") zplus::physics::update(handle, number(1));
            else if (name == "setRotation") zplus::physics::setRotation(handle, number(1));
            else if (name == "setAngularVelocity") zplus::physics::setAngularVelocity(handle, number(1));
            else if (name == "setMass") zplus::physics::setMass(handle, number(1));
            else if (name == "setLinearDamping") zplus::physics::setLinearDamping(handle, number(1));
            else if (name == "setAngularDamping") zplus::physics::setAngularDamping(handle, number(1));
            else zplus::physics::applyTorque(handle, number(1));
            return false;
        }
        if (name == "createBody") {
            if (count != 4) failArity();
            return static_cast<double>(zplus::physics::createBody(
                integer(0), text(1), number(2), number(3)));
        }
        if (name == "createCircle" || name == "createBox" ||
            name == "createPolygon") {
            if ((name == "createBox" && count != 4) ||
                (name != "createBox" && count != 3)) failArity();
            const auto world = integer(0);
            const auto body = integer(1);
            if (name == "createCircle") {
                return static_cast<double>(zplus::physics::createCircle(world, body, number(2)));
            }
            if (name == "createBox") {
                return static_cast<double>(zplus::physics::createBox(
                    world, body, number(2), number(3)));
            }
            return static_cast<double>(zplus::physics::createPolygon(world, body, text(2)));
        }
        if (name == "createSegment") {
            if (count != 6) failArity();
            return static_cast<double>(zplus::physics::createSegment(
                integer(0), integer(1), number(2), number(3), number(4), number(5)));
        }
        if (name == "setCollisionCategory" || name == "setCollisionMask" ||
            name == "setSensor" || name == "setFriction" ||
            name == "setRestitution" || name == "setDensity" ||
            name == "setMouseJointTarget") {
            const std::size_t expected = name == "setMouseJointTarget" ? 3 : 2;
            if (count != expected) failArity();
            const auto handle = integer(0);
            if (name == "setCollisionCategory")
                zplus::physics::setCollisionCategory(handle, integer(1));
            else if (name == "setCollisionMask")
                zplus::physics::setCollisionMask(handle, integer(1));
            else if (name == "setSensor")
                zplus::physics::setSensor(handle, boolean(1));
            else if (name == "setFriction")
                zplus::physics::setFriction(handle, number(1));
            else if (name == "setRestitution")
                zplus::physics::setRestitution(handle, number(1));
            else if (name == "setDensity")
                zplus::physics::setDensity(handle, number(1));
            else zplus::physics::setMouseJointTarget(handle, number(1), number(2));
            return false;
        }
        if (name == "isTouching") {
            if (count != 2) failArity();
            return zplus::physics::isTouching(integer(0), integer(1));
        }
        if (name == "collisionStarted" || name == "collisionEnded") {
            if (count != 3) failArity();
            return name == "collisionStarted"
                ? zplus::physics::collisionStarted(integer(0), integer(1), integer(2))
                : zplus::physics::collisionEnded(integer(0), integer(1), integer(2));
        }
        if (name == "createDistanceJoint") {
            if (count != 4) failArity();
            return static_cast<double>(zplus::physics::createDistanceJoint(
                integer(0), integer(1), integer(2), number(3)));
        }
        if (name == "createRevoluteJoint" || name == "createWeldJoint") {
            if (count != 5) failArity();
            const auto world = integer(0);
            const auto bodyA = integer(1);
            const auto bodyB = integer(2);
            return static_cast<double>(name == "createRevoluteJoint"
                ? zplus::physics::createRevoluteJoint(
                    world, bodyA, bodyB, number(3), number(4))
                : zplus::physics::createWeldJoint(
                    world, bodyA, bodyB, number(3), number(4)));
        }
        if (name == "createMouseJoint") {
            if (count != 5) failArity();
            return static_cast<double>(zplus::physics::createMouseJoint(
                integer(0), integer(1), number(2), number(3), number(4)));
        }
        if (name == "raycast" || name == "pointQuery" ||
            name == "areaQuery" || name == "queryBodyAt") {
            if ((name == "raycast" && count != 5) ||
                (name == "pointQuery" && count != 3) ||
                (name == "areaQuery" && count != 5) ||
                (name == "queryBodyAt" && count != 2)) failArity();
            if (name == "raycast") return static_cast<double>(
                zplus::physics::raycast(integer(0), number(1), number(2), number(3), number(4)));
            if (name == "pointQuery") return static_cast<double>(
                zplus::physics::pointQuery(integer(0), number(1), number(2)));
            if (name == "areaQuery") return static_cast<double>(
                zplus::physics::areaQuery(integer(0), number(1), number(2), number(3), number(4)));
            return static_cast<double>(zplus::physics::queryBodyAt(integer(0), integer(1)));
        }
        if (name == "setQueryFilter") {
            if (count != 3) failArity();
            zplus::physics::setQueryFilter(integer(0), integer(1), integer(2));
            return false;
        }
        throw std::invalid_argument("unknown physics operation '" + name + "'");
    }
    const std::string httpPrefix = "__zhttp_";
    if (callee.rfind(httpPrefix, 0) == 0) {
        const std::string name = callee.substr(httpPrefix.size());
        if (name == "serverCreate") {
            if (count != 2) failArity();
            return static_cast<double>(zplus::http::createServer(
                text(0), integer(1)));
        }
        if (name == "serverStart" || name == "serverRunning" ||
            name == "serverPort" || name == "serverStop") {
            if (count != 1) failArity();
            const auto handle = integer(0);
            if (name == "serverStart") return zplus::http::startServer(handle);
            if (name == "serverRunning") return zplus::http::serverRunning(handle);
            if (name == "serverPort") return static_cast<double>(
                zplus::http::serverPort(handle));
            zplus::http::stopServer(handle);
            return 0.0;
        }
        if (name == "serverNext") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::http::nextRequest(
                integer(0), integer(1)));
        }
        if (name == "serverRespond") {
            if (count != 5) failArity();
            return zplus::http::respond(
                integer(0), integer(1), integer(2), text(3), text(4));
        }
        if (name == "serverRespondBinary") {
            if (count != 5) failArity();
            return zplus::http::respondBinary(
                integer(0), integer(1), integer(2), text(3), text(4));
        }
        if (name == "serverConfigure") {
            if (count != 3) failArity();
            zplus::http::configureServer(integer(0), integer(1), integer(2));
            return 0.0;
        }
        if (name == "configureCors") {
            if (count != 4) failArity();
            zplus::http::configureCors(
                integer(0), text(1), text(2), text(3));
            return 0.0;
        }
        if (name == "routeParams") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::http::routeParams(
                text(0), text(1)));
        }
        if (name == "queryValue") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::http::queryValue(
                text(0), text(1)));
        }
        if (name == "requestHeader") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::http::requestHeader(
                text(0), text(1)));
        }
        if (name == "clientRequest") {
            if (count != 5) failArity();
            return managedHeap().makeString(zplus::http::clientRequest(
                text(0), text(1), text(2), text(3), integer(4)));
        }
        if (name == "clientRequestBinary") {
            if (count != 5) failArity();
            return managedHeap().makeString(zplus::http::clientRequestBinary(
                text(0), text(1), text(2), text(3), integer(4)));
        }
        throw std::invalid_argument("unknown HTTP operation '" + name + "'");
    }
    const std::string webPrefix = "__zweb_";
    if (callee.rfind(webPrefix, 0) == 0) {
        const std::string name = callee.substr(webPrefix.size());
        if (name == "urlEncode" || name == "urlDecode" || name == "parseUrl" ||
            name == "parseForm" || name == "parseCookies" || name == "securityHeaders" ||
            name == "mimeType") {
            if (count != 1) failArity();
            if (name == "urlEncode") return managedHeap().makeString(
                zplus::web::urlEncode(text(0)));
            if (name == "urlDecode") return managedHeap().makeString(
                zplus::web::urlDecode(text(0)));
            if (name == "parseUrl") return managedHeap().makeString(
                zplus::web::parseUrl(text(0)));
            if (name == "parseForm") return managedHeap().makeString(
                zplus::web::parseForm(text(0)));
            if (name == "parseCookies") return managedHeap().makeString(
                zplus::web::parseCookies(text(0)));
            if (name == "securityHeaders") return managedHeap().makeString(
                zplus::web::securityHeaders(text(0)));
            return managedHeap().makeString(zplus::web::mimeType(text(0)));
        }
        if (name == "buildUrl") {
            if (count != 6) failArity();
            return managedHeap().makeString(zplus::web::buildUrl(
                text(0), text(1), text(2), text(3), text(4), text(5)));
        }
        if (name == "cookieHeader") {
            if (count != 8) failArity();
            return managedHeap().makeString(zplus::web::cookieHeader(
                text(0), text(1), integer(2), text(3), text(4),
                toBool(value(5)), toBool(value(6)), text(7)));
        }
        if (name == "cookieHeaderExpires") {
            if (count != 9) failArity();
            return managedHeap().makeString(zplus::web::cookieHeaderExpires(
                text(0), text(1), integer(2), text(3), text(4), text(5),
                toBool(value(6)), toBool(value(7)), text(8)));
        }
        if (name == "routeAdd") {
            if (count != 4) failArity();
            return managedHeap().makeString(zplus::web::routeAdd(
                text(0), text(1), text(2), text(3)));
        }
        if (name == "routeGroup") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::routeGroup(
                text(0), text(1)));
        }
        if (name == "routeMatch") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::routeMatch(
                text(0), text(1), text(2)));
        }
        if (name == "routeAddConfigured") {
            if (count != 6) failArity();
            return managedHeap().makeString(zplus::web::routeAddConfigured(
                text(0), text(1), text(2), text(3), text(4), text(5)));
        }
        if (name == "routeUrl") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::routeUrl(
                text(0), text(1), text(2)));
        }
        if (name == "response" || name == "jsonResponse") {
            if (count != 3) failArity();
            return managedHeap().makeString(name == "response"
                ? zplus::web::response(integer(0), text(1), text(2))
                : zplus::web::jsonResponse(integer(0), text(1), text(2)));
        }
        if (name == "parseJsonBody") {
            if (count != 1) failArity();
            return managedHeap().makeString(zplus::web::parseJsonBody(text(0)));
        }
        if (name == "notFoundResponse") {
            if (count != 1) failArity();
            return managedHeap().makeString(zplus::web::notFoundResponse(text(0)));
        }
        if (name == "methodNotAllowedResponse") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::methodNotAllowedResponse(
                text(0), text(1)));
        }
        if (name == "staticFile") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::staticFile(
                text(0), text(1), integer(2)));
        }
        if (name == "respondFile") {
            if (count != 7) failArity();
            return zplus::web::respondFile(
                integer(0), integer(1), integer(2), text(3),
                text(4), text(5), integer(6));
        }
        if (name == "respondStatic") {
            if (count != 9) failArity();
            return zplus::web::respondStatic(
                integer(0), integer(1), integer(2), text(3), text(4),
                text(5), text(6), integer(7), text(8));
        }
        if (name == "parseMultipart") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::parseMultipart(
                text(0), integer(1)));
        }
        if (name == "saveUpload") {
            if (count != 5) failArity();
            return managedHeap().makeString(zplus::web::saveUpload(
                text(0), integer(1), text(2), text(3), integer(4)));
        }
        if (name == "validateForm") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::validateForm(
                text(0), text(1)));
        }
        if (name == "formHas") {
            if (count != 2) failArity();
            return zplus::web::formHas(text(0), text(1));
        }
        if (name == "formNumber") {
            if (count != 2) failArity();
            return zplus::web::formNumber(text(0), text(1));
        }
        if (name == "loadTemplate") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::loadTemplate(
                text(0), text(1), integer(2)));
        }
        if (name == "renderTemplate") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::renderTemplate(
                text(0), text(1)));
        }
        if (name == "middlewareUse") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::middlewareUse(
                text(0), text(1), text(2)));
        }
        if (name == "middlewareBefore") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::middlewareBefore(
                text(0), text(1)));
        }
        if (name == "middlewareAfter") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::middlewareAfter(
                text(0), text(1), text(2)));
        }
        if (name == "sessionStoreCreate") {
            if (count != 0) failArity();
            return static_cast<double>(zplus::web::sessionStoreCreate());
        }
        if (name == "sessionStoreClose") {
            if (count != 1) failArity();
            zplus::web::sessionStoreClose(integer(0));
            return 0.0;
        }
        if (name == "sessionCreate") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::sessionCreate(
                integer(0), integer(1)));
        }
        if (name == "sessionSet") {
            if (count != 4) failArity();
            return zplus::web::sessionSet(
                integer(0), text(1), text(2), text(3));
        }
        if (name == "sessionGet") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::sessionGet(
                integer(0), text(1), text(2)));
        }
        if (name == "sessionHas") {
            if (count != 3) failArity();
            return zplus::web::sessionHas(
                integer(0), text(1), text(2));
        }
        if (name == "sessionInvalidate") {
            if (count != 2) failArity();
            return zplus::web::sessionInvalidate(integer(0), text(1));
        }
        if (name == "sessionRotate") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::web::sessionRotate(
                integer(0), text(1), integer(2)));
        }
        if (name == "sessionCsrfToken") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::web::sessionCsrfToken(
                integer(0), text(1)));
        }
        if (name == "sessionValidateCsrf") {
            if (count != 3) failArity();
            return zplus::web::sessionValidateCsrf(
                integer(0), text(1), text(2));
        }
        if (name == "sessionCookie") {
            if (count != 5) failArity();
            return managedHeap().makeString(zplus::web::sessionCookie(
                text(0), text(1), integer(2), toBool(value(3)), text(4)));
        }
        throw std::invalid_argument("unknown ZWeb operation '" + name + "'");
    }
    const std::string sqlitePrefix = "__zsqlite_";
    if (callee.rfind(sqlitePrefix, 0) == 0) {
        const std::string name = callee.substr(sqlitePrefix.size());
        if (name == "open") {
            if (count != 1) failArity();
            return static_cast<double>(zplus::sqlite::openDatabase(text(0)));
        }
        if (name == "close") {
            if (count != 1) failArity();
            zplus::sqlite::closeDatabase(integer(0));
            return 0.0;
        }
        if (name == "execute") {
            if (count != 3) failArity();
            return static_cast<double>(zplus::sqlite::execute(
                integer(0), text(1), text(2)));
        }
        if (name == "query") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::sqlite::query(
                integer(0), text(1), text(2)));
        }
        if (name == "changes") {
            if (count != 1) failArity();
            return static_cast<double>(zplus::sqlite::changes(integer(0)));
        }
        throw std::invalid_argument("unknown SQLite operation '" + name + "'");
    }
    const std::string jsonPrefix = "__zjson_";
    if (callee.rfind(jsonPrefix, 0) == 0) {
        const std::string name = callee.substr(jsonPrefix.size());
        if (name == "valid") {
            if (count != 1) failArity();
            return zplus::json::valid(text(0));
        }
        if (name == "compact") {
            if (count != 1) failArity();
            return managedHeap().makeString(zplus::json::compact(text(0)));
        }
        if (name == "quote") {
            if (count != 1) failArity();
            return managedHeap().makeString(zplus::json::quote(text(0)));
        }
        if (name == "get" || name == "getString" || name == "getNumber" ||
            name == "getBoolean" || name == "arrayLength" || name == "kind") {
            if (count != 2) failArity();
            const std::string source = text(0);
            const std::string pointer = text(1);
            if (name == "get") return managedHeap().makeString(zplus::json::getRaw(source, pointer));
            if (name == "getString") {
                return managedHeap().makeString(zplus::json::getString(source, pointer));
            }
            if (name == "getNumber") {
                return zplus::json::getNumber(source, pointer);
            }
            if (name == "getBoolean") {
                return zplus::json::getBoolean(source, pointer);
            }
            if (name == "arrayLength") {
                return static_cast<double>(zplus::json::arrayLength(source, pointer));
            }
            return managedHeap().makeString(zplus::json::valueKind(source, pointer));
        }
        if (name == "arrayGet") {
            if (count != 3) failArity();
            const double indexValue = number(2);
            if (!std::isfinite(indexValue) || std::trunc(indexValue) != indexValue ||
                indexValue < 0 ||
                indexValue > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
                throw std::invalid_argument("JSON array index must be a non-negative integer");
            }
            return managedHeap().makeString(zplus::json::arrayGet(
                text(0), text(1), static_cast<std::size_t>(indexValue)));
        }
        throw std::invalid_argument("unknown JSON operation '" + name + "'");
    }
    if (callee.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("invalid standard-library builtin '" + callee + "'");
    }
    const std::string operation = callee.substr(prefix.size());

    if (operation.rfind("math_", 0) == 0) {
        const std::string name = operation.substr(5);
        if (name == "pi" || name == "e") {
            if (count != 0) failArity();
            return zplus::stdlib::mathConstant(name);
        }
        if (name == "min" || name == "max" || name == "pow") {
            if (count != 2) failArity();
            return zplus::stdlib::mathBinary(name, number(0), number(1));
        }
        if (count != 1) failArity();
        return zplus::stdlib::mathUnary(name, number(0));
    }
    if (operation.rfind("string_", 0) == 0) {
        const std::string name = operation.substr(7);
        if (name == "length") {
            if (count != 1) failArity();
            return static_cast<double>(zplus::stdlib::stringLength(text(0)));
        }
        if (name == "split") {
            if (count != 2) failArity();
            const auto parts = zplus::stdlib::split(text(0), text(1));
            std::vector<RuntimeValue> values;
            values.reserve(parts.size());
            ManagedHeap& heap = managedHeap();
            for (const auto& part : parts) {
                values.emplace_back(heap.makeString(
                    part, std::span<const RuntimeValue>(values)));
            }
            return heap.makeArray(std::move(values));
        }
        if (name == "join") {
            if (count != 2) failArity();
            RuntimeArray& values = array(0);
            std::vector<std::string> strings;
            strings.reserve(values.elements.size());
            for (const RuntimeValue& item : values.elements) {
                if (!isString(item)) {
                    throw std::invalid_argument("join expects an array of strings");
                }
                strings.push_back(stringValue(item));
            }
            return managedHeap().makeString(
                zplus::stdlib::stringJoin(strings, text(1)));
        }
        if (name == "replace") {
            if (count != 3) failArity();
            return managedHeap().makeString(
                zplus::stdlib::stringReplace(text(0), text(1), text(2)));
        }
        if (name == "substring") {
            if (count != 3) failArity();
            return managedHeap().makeString(zplus::stdlib::substring(
                text(0), standardInteger(value(1), "substring start"),
                standardInteger(value(2), "substring length")));
        }
        if (name == "repeat") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::stdlib::repeat(
                text(0), standardInteger(value(1), "repeat count")));
        }
        if (name == "contains" || name == "startsWith" || name == "endsWith") {
            if (count != 2) failArity();
            const std::string result =
                zplus::stdlib::stringBinary(name, text(0), text(1));
            return result == "true";
        }
        if (count != 1) failArity();
        return managedHeap().makeString(zplus::stdlib::stringUnary(name, text(0)));
    }
    if (operation.rfind("collections_", 0) == 0) {
        const std::string name = operation.substr(12);
        if (count != (name == "contains" || name == "count" ? 2 : 1)) failArity();
        RuntimeArray& items = array(0);
        const auto numericItems = [&] {
            std::vector<double> numbers;
            numbers.reserve(items.elements.size());
            for (const RuntimeValue& item : items.elements) {
                if (!isNumber(item)) {
                    throw std::invalid_argument("numeric collection operation requires number[]");
                }
                numbers.push_back(std::get<double>(item));
            }
            return numbers;
        };
        if (name == "isEmpty") return items.elements.empty();
        if (name == "contains" || name == "count") {
            const double sought = number(1);
            const auto matches = [&sought](const RuntimeValue& item) {
                return isNumber(item) && std::get<double>(item) == sought;
            };
            const std::size_t occurrences = static_cast<std::size_t>(
                std::count_if(items.elements.begin(), items.elements.end(), matches));
            return name == "contains"
                ? RuntimeValue{occurrences != 0}
                : RuntimeValue{static_cast<double>(occurrences)};
        }
        if (name == "first" || name == "last") {
            if (items.elements.empty()) {
                throw std::out_of_range(name + " cannot be used with an empty array");
            }
            const RuntimeValue& result =
                name == "first" ? items.elements.front() : items.elements.back();
            if (!isNumber(result)) {
                throw std::invalid_argument(name + " requires number[]");
            }
            return result;
        }
        std::vector<double> numbers = numericItems();
        if (name == "sum" || name == "average") {
            if (name == "average" && numbers.empty()) {
                throw std::domain_error("average cannot be computed for an empty array");
            }
            const double total = std::accumulate(numbers.begin(), numbers.end(), 0.0);
            return name == "sum" ? total : total / static_cast<double>(numbers.size());
        }
        if (name == "min" || name == "max") {
            if (numbers.empty()) {
                throw std::domain_error(name + " cannot be computed for an empty array");
            }
            const auto selected = name == "min"
                ? std::min_element(numbers.begin(), numbers.end())
                : std::max_element(numbers.begin(), numbers.end());
            return *selected;
        }
        if (name == "reverse") {
            std::vector<RuntimeValue> reversed = items.elements;
            std::reverse(reversed.begin(), reversed.end());
            return managedHeap().makeArray(std::move(reversed));
        }
        if (name == "sort") {
            std::vector<RuntimeValue> sorted = items.elements;
            std::stable_sort(sorted.begin(), sorted.end(),
                [](const RuntimeValue& left, const RuntimeValue& right) {
                    return std::get<double>(left) < std::get<double>(right);
                });
            return managedHeap().makeArray(std::move(sorted));
        }
        if (name == "unique") {
            std::vector<RuntimeValue> unique;
            unique.reserve(items.elements.size());
            for (const RuntimeValue& item : items.elements) {
                if (std::none_of(unique.begin(), unique.end(),
                    [&item](const RuntimeValue& existing) {
                        return std::get<double>(existing) == std::get<double>(item);
                    })) {
                    unique.push_back(item);
                }
            }
            return managedHeap().makeArray(std::move(unique));
        }
    }
    if (operation.rfind("random_", 0) == 0) {
        const std::string name = operation.substr(7);
        if (name == "boolean") {
            if (count != 0) failArity();
            return zplus::stdlib::randomBoolean();
        }
        if (name == "seed") {
            if (count != 1) failArity();
            const std::int64_t seed = standardInteger(value(0), "random seed");
            if (seed < 0) throw std::invalid_argument("random seed must be non-negative");
            zplus::stdlib::randomSeed(static_cast<std::uint64_t>(seed));
            return false;
        }
        if (name == "integer" || name == "number") {
            if (count != 2) failArity();
            if (name == "integer") {
                return static_cast<double>(zplus::stdlib::randomInteger(
                    standardInteger(value(0), "random minimum"),
                    standardInteger(value(1), "random maximum")));
            }
            return zplus::stdlib::randomNumber(number(0), number(1));
        }
        if (count != 1) failArity();
        RuntimeArray& items = array(0);
        if (items.elements.empty()) {
            throw std::out_of_range(name + " cannot be used with an empty array");
        }
        if (name == "choice") {
            const auto index = static_cast<std::size_t>(
                zplus::stdlib::randomInteger(
                    0, static_cast<std::int64_t>(items.elements.size() - 1)));
            if (!isNumber(items.elements[index])) {
                throw std::invalid_argument("random.choice currently requires number[]");
            }
            return items.elements[index];
        }
        if (name == "shuffle") {
            for (std::size_t index = items.elements.size(); index > 1; --index) {
                const std::size_t selected = static_cast<std::size_t>(
                    zplus::stdlib::randomInteger(
                        0, static_cast<std::int64_t>(index - 1)));
                std::swap(items.elements[index - 1], items.elements[selected]);
            }
            return std::get<RuntimeArrayPtr>(value(0));
        }
    }
    if (operation.rfind("path_", 0) == 0) {
        const std::string name = operation.substr(5);
        if (name == "join") {
            if (count != 2) failArity();
            return managedHeap().makeString(zplus::stdlib::pathJoin(text(0), text(1)));
        }
        if (count != 1) failArity();
        const std::string result = zplus::stdlib::pathUnary(name, text(0));
        return name == "isAbsolute"
            ? RuntimeValue{result == "true"}
            : RuntimeValue{managedHeap().makeString(result)};
    }
    if (operation.rfind("file_", 0) == 0) {
        const std::string name = operation.substr(5);
        if (name == "readLines") {
            if (count != 1) failArity();
            const auto lines = zplus::stdlib::fileReadLines(text(0));
            std::vector<RuntimeValue> values;
            values.reserve(lines.size());
            ManagedHeap& heap = managedHeap();
            for (const auto& line : lines) {
                values.emplace_back(heap.makeString(
                    line, std::span<const RuntimeValue>(values)));
            }
            return heap.makeArray(std::move(values));
        }
        if (name == "writeLines") {
            if (count != 2) failArity();
            RuntimeArray& lines = array(1);
            std::vector<std::string> contents;
            contents.reserve(lines.elements.size());
            for (const RuntimeValue& line : lines.elements) {
                if (!isString(line)) {
                    throw std::invalid_argument("writeLines expects an array of strings");
                }
                contents.push_back(stringValue(line));
            }
            zplus::stdlib::fileWriteLines(text(0), contents);
            return false;
        }
        if (name == "write" || name == "append") {
            if (count != 2) failArity();
            zplus::stdlib::fileWrite(text(0), text(1), name == "append");
            return false;
        }
        if (name == "copyFile" || name == "moveFile") {
            if (count != 3) failArity();
            RuntimeValue overwrite = value(2);
            if (!std::holds_alternative<bool>(overwrite)) {
                throw std::invalid_argument(callee + " argument 3 must be a boolean");
            }
            return name == "copyFile"
                ? zplus::installer::copyFile(
                    text(0), text(1), std::get<bool>(overwrite))
                : zplus::installer::moveFile(
                    text(0), text(1), std::get<bool>(overwrite));
        }
        if (name == "removeEmptyDirectory") {
            if (count != 1) failArity();
            return zplus::installer::removeEmptyDirectory(text(0));
        }
        if (name == "scheduleDeleteOnReboot") {
            if (count != 1) failArity();
            return zplus::installer::scheduleFileRemovalOnReboot(text(0));
        }
        if (count != 1) failArity();
        if (name == "read") return managedHeap().makeString(zplus::stdlib::fileRead(text(0)));
        if (name == "exists") return zplus::stdlib::fileExists(text(0));
        if (name == "delete") return zplus::stdlib::fileDelete(text(0));
        if (name == "createDirectory") return zplus::stdlib::fileCreateDirectory(text(0));
        if (name == "isDirectory") return zplus::stdlib::fileIsDirectory(text(0));
        if (name == "size") return static_cast<double>(zplus::stdlib::fileSize(text(0)));
        if (name == "listDirectory") {
            const auto files = zplus::stdlib::fileListDirectory(text(0));
            std::vector<RuntimeValue> values;
            values.reserve(files.size());
            ManagedHeap& heap = managedHeap();
            for (const auto& entry : files) {
                values.emplace_back(heap.makeString(
                    entry, std::span<const RuntimeValue>(values)));
            }
            return heap.makeArray(std::move(values));
        }
    }
    if (operation == "crypto_sha256File") {
        if (count != 1) failArity();
        return managedHeap().makeString(zplus::installer::sha256File(text(0)));
    }
    if (operation == "process_run") {
        if (count != 4) failArity();
        RuntimeArray& arguments = array(1);
        std::vector<std::string> strings;
        strings.reserve(arguments.elements.size());
        for (const RuntimeValue& argument : arguments.elements) {
            if (!isString(argument)) {
                throw std::invalid_argument(callee + " arguments must contain only strings");
            }
            strings.push_back(stringValue(argument));
        }
        return managedHeap().makeString(zplus::installer::runProcess(
            text(0), strings, text(2),
            standardInteger(value(3), "process timeout")));
    }
    if (operation.rfind("system_", 0) == 0) {
        const std::string name = operation.substr(7);
        if (name == "platform" || name == "architecture" ||
            name == "osVersion" || name == "executablePath") {
            if (count != 0) failArity();
            if (name == "platform") {
                return managedHeap().makeString(zplus::installer::platformName());
            }
            if (name == "architecture") {
                return managedHeap().makeString(zplus::installer::architecture());
            }
            if (name == "osVersion") {
                return managedHeap().makeString(zplus::installer::operatingSystemVersion());
            }
            return managedHeap().makeString(zplus::installer::executablePath());
        }
        if (name == "availableSpace") {
            if (count != 1) failArity();
            return static_cast<double>(zplus::installer::availableSpace(text(0)));
        }
        if (name == "selectFolder") {
            if (count != 2) failArity();
            return managedHeap().makeString(
                zplus::installer::selectFolder(text(0), text(1)));
        }
        if (name == "messageBox") {
            if (count != 4) failArity();
            return static_cast<double>(zplus::installer::messageBox(
                text(0), text(1), text(2), text(3)));
        }
        if (name == "createShortcut") {
            if (count != 4) failArity();
            return zplus::installer::createShortcut(
                text(0), text(1), text(2), text(3));
        }
        if (name == "shortcutTarget" || name == "shortcutArguments") {
            if (count != 1) failArity();
            return managedHeap().makeString(name == "shortcutTarget"
                ? zplus::installer::shortcutTarget(text(0))
                : zplus::installer::shortcutArguments(text(0)));
        }
        if (name == "removeShortcutIfMatches") {
            if (count != 3) failArity();
            return zplus::installer::removeShortcutIfMatches(
                text(0), text(1), text(2));
        }
    }
    if (operation == "registry_read") {
        if (count != 2) failArity();
        return managedHeap().makeString(
            zplus::installer::registryRead(text(0), text(1)));
    }
    if (operation == "registry_valueKind") {
        if (count != 2) failArity();
        return managedHeap().makeString(
            zplus::installer::registryValueKind(text(0), text(1)));
    }
    if (operation == "registry_write") {
        if (count != 4) failArity();
        return zplus::installer::registryWrite(
            text(0), text(1), text(2), text(3));
    }
    if (operation == "registry_deleteValue") {
        if (count != 2) failArity();
        return zplus::installer::registryDeleteValue(text(0), text(1));
    }
    if (operation.rfind("env_", 0) == 0) {
        const std::string name = operation.substr(4);
        if (name == "homeDirectory" || name == "currentDirectory") {
            if (count != 0) failArity();
            const std::string result = name == "homeDirectory"
                ? zplus::stdlib::homeDirectory()
                : zplus::stdlib::currentDirectory();
            return managedHeap().makeString(result);
        }
        if (name == "set") {
            if (count != 2) failArity();
            zplus::stdlib::environmentSet(text(0), text(1));
            return false;
        }
        if (count != 1) failArity();
        if (name == "get") return managedHeap().makeString(zplus::stdlib::environmentGet(text(0)));
        if (name == "exists") return zplus::stdlib::environmentExists(text(0));
    }
    if (operation.rfind("time_", 0) == 0) {
        const std::string name = operation.substr(5);
        if (name == "now") {
            if (count != 0) failArity();
            return zplus::stdlib::timeNow();
        }
        if (name == "format" || name == "parse") {
            if (count != 2) failArity();
            if (name == "format") {
                return managedHeap().makeString(zplus::stdlib::timeFormat(number(0), text(1)));
            }
            return zplus::stdlib::timeParse(text(0), text(1));
        }
        if (name == "sleep") {
            if (count != 1) failArity();
            zplus::stdlib::timeSleep(standardInteger(value(0), "sleep duration"));
            return false;
        }
        if (count != 1) failArity();
        return zplus::stdlib::timePart(name, number(0));
    }
    if (operation.rfind("args_", 0) == 0) {
        const std::string name = operation.substr(5);
        const auto& arguments = zplus::stdlib::processArguments();
        if (name == "count") {
            if (count != 0) failArity();
            return static_cast<double>(arguments.size());
        }
        if (name == "get") {
            if (count != 1) failArity();
            const std::int64_t index = standardInteger(value(0), "argument index");
            if (index < 0 || static_cast<std::uint64_t>(index) >= arguments.size()) {
                throw std::out_of_range("command-line argument index is out of range");
            }
            return managedHeap().makeString(arguments[static_cast<std::size_t>(index)]);
        }
        if (name == "has") {
            if (count != 1) failArity();
            return std::find(arguments.begin(), arguments.end(), text(0)) != arguments.end();
        }
        if (name == "all") {
            if (count != 0) failArity();
            std::vector<RuntimeValue> values;
            values.reserve(arguments.size());
            ManagedHeap& heap = managedHeap();
            for (const std::string& argument : arguments) {
                values.emplace_back(heap.makeString(
                    argument, std::span<const RuntimeValue>(values)));
            }
            return heap.makeArray(std::move(values));
        }
    }
    if (operation.rfind("log_", 0) == 0) {
        const std::string name = operation.substr(4);
        if (name == "setLevel" || name == "setOutput") {
            if (count != 1) failArity();
            if (name == "setLevel") zplus::stdlib::logSetLevel(text(0));
            else zplus::stdlib::logSetOutput(text(0));
            return false;
        }
        if (count != 1) failArity();
        zplus::stdlib::logWrite(name, text(0));
        return false;
    }
    throw std::runtime_error("unsupported standard-library operation '" + operation + "'");
}

static std::vector<RuntimeValue> graphicsArguments(
    const zplus::ir::Instruction& instruction,
    const Environment& environment,
    std::size_t expected
) {
    if (instruction.operands.size() != expected + 1) {
        throw std::runtime_error(
            instruction.operands.empty() ? "graphics call has no callee" :
                valueToString(resolveValue(instruction.operands[0], environment)) +
                " expects " + std::to_string(expected) + " arguments"
        );
    }

    std::vector<RuntimeValue> arguments;
    arguments.reserve(expected);
    for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
        arguments.push_back(resolveValue(instruction.operands[i], environment));
    }
    return arguments;
}

static int graphicsInteger(const RuntimeValue& value, const std::string& argumentName) {
    const double number = toNumber(value);
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(argumentName + " must be a finite integer");
    }
    return static_cast<int>(number);
}

static RuntimeValue executeGraphicsBuiltin(
    const std::string& callee,
    const zplus::ir::Instruction& instruction,
    const Environment& environment
) {
    using zplus::graphics::Application;
    using zplus::graphics::Event;
    using zplus::graphics::parseColor;

    if (callee == "__zgraphics_numberText") {
        const auto args = graphicsArguments(instruction, environment, 1);
        return valueToString(args[0]);
    }

    Application& app = graphicsApplication();

    if (callee == "__zgraphics_createWindow") {
        const auto args = graphicsArguments(instruction, environment, 3);
        return static_cast<double>(app.createWindow(
            valueToString(args[0]), graphicsInteger(args[1], "width"),
            graphicsInteger(args[2], "height")));
    }

    if (callee == "__zgraphics_setPosition") {
        const auto args = graphicsArguments(instruction, environment, 3);
        app.setPosition(graphicsInteger(args[0], "window"), graphicsInteger(args[1], "x"),
                        graphicsInteger(args[2], "y"));
        return false;
    }

    if (callee == "__zgraphics_resize") {
        const auto args = graphicsArguments(instruction, environment, 3);
        app.resize(graphicsInteger(args[0], "window"), graphicsInteger(args[1], "width"),
                   graphicsInteger(args[2], "height"));
        return false;
    }

    if (callee == "__zgraphics_isOpen") {
        const auto args = graphicsArguments(instruction, environment, 1);
        return app.isOpen(graphicsInteger(args[0], "window"));
    }

    if (callee == "__zgraphics_update") {
        const auto args = graphicsArguments(instruction, environment, 1);
        app.update(graphicsInteger(args[0], "window"));
        return false;
    }

    if (callee == "__zgraphics_setTargetFPS") {
        const auto args = graphicsArguments(instruction, environment, 2);
        app.setTargetFPS(graphicsInteger(args[0], "window"),
                         graphicsInteger(args[1], "fps"));
        return false;
    }

    if (callee == "__zgraphics_deltaTimeMs" ||
        callee == "__zgraphics_framesPerSecond" ||
        callee == "__zgraphics_mouseX" || callee == "__zgraphics_mouseY") {
        const auto args = graphicsArguments(instruction, environment, 1);
        const int window = graphicsInteger(args[0], "window");
        if (callee == "__zgraphics_deltaTimeMs") return static_cast<double>(app.deltaTimeMs(window));
        if (callee == "__zgraphics_framesPerSecond") {
            return static_cast<double>(app.framesPerSecond(window));
        }
        if (callee == "__zgraphics_mouseX") return static_cast<double>(app.mouseX(window));
        return static_cast<double>(app.mouseY(window));
    }

    if (callee == "__zgraphics_keyDown" || callee == "__zgraphics_keyPressed" ||
        callee == "__zgraphics_keyReleased" ||
        callee == "__zgraphics_mouseButtonDown" ||
        callee == "__zgraphics_mouseButtonPressed" ||
        callee == "__zgraphics_mouseButtonReleased") {
        const auto args = graphicsArguments(instruction, environment, 2);
        const int window = graphicsInteger(args[0], "window");
        const int code = graphicsInteger(args[1],
            callee.rfind("__zgraphics_key", 0) == 0 ? "key" : "button");
        if (callee == "__zgraphics_keyDown") return app.keyDown(window, code);
        if (callee == "__zgraphics_keyPressed") return app.keyPressed(window, code);
        if (callee == "__zgraphics_keyReleased") return app.keyReleased(window, code);
        if (callee == "__zgraphics_mouseButtonDown") return app.mouseButtonDown(window, code);
        if (callee == "__zgraphics_mouseButtonPressed") return app.mouseButtonPressed(window, code);
        return app.mouseButtonReleased(window, code);
    }

    if (callee == "__zgraphics_setTitle") {
        const auto args = graphicsArguments(instruction, environment, 2);
        app.setTitle(graphicsInteger(args[0], "window"), valueToString(args[1]));
    } else if (callee == "__zgraphics_setBackground") {
        const auto args = graphicsArguments(instruction, environment, 2);
        app.setBackground(graphicsInteger(args[0], "window"), parseColor(valueToString(args[1])));
    } else if (callee == "__zgraphics_show" || callee == "__zgraphics_hide" ||
               callee == "__zgraphics_minimize" || callee == "__zgraphics_maximize" ||
               callee == "__zgraphics_restore" || callee == "__zgraphics_focus" ||
               callee == "__zgraphics_close" || callee == "__zgraphics_run" ||
               callee == "__zgraphics_clear" || callee == "__zgraphics_pollEvent" ||
               callee == "__zgraphics_waitEvent" ||
               callee == "__zgraphics_eventX" || callee == "__zgraphics_eventY" ||
               callee == "__zgraphics_eventKey" ||
               callee == "__zgraphics_eventText") {
        const auto args = graphicsArguments(instruction, environment, 1);
        const int window = graphicsInteger(args[0], "window");
        if (callee == "__zgraphics_show") app.show(window);
        else if (callee == "__zgraphics_hide") app.hide(window);
        else if (callee == "__zgraphics_minimize") app.minimize(window);
        else if (callee == "__zgraphics_maximize") app.maximize(window);
        else if (callee == "__zgraphics_restore") app.restore(window);
        else if (callee == "__zgraphics_focus") app.focus(window);
        else if (callee == "__zgraphics_close") app.close(window);
        else if (callee == "__zgraphics_run") return static_cast<double>(app.run(window));
        else if (callee == "__zgraphics_clear") app.clear(window);
        else if (callee == "__zgraphics_pollEvent") {
            Event event = app.pollEvent(window);
            graphicsEvents()[window] = event;
            return static_cast<double>(static_cast<int>(event.type));
        } else if (callee == "__zgraphics_waitEvent") {
            Event event = app.waitEvent(window);
            graphicsEvents()[window] = event;
            return static_cast<double>(static_cast<int>(event.type));
        } else {
            const auto found = graphicsEvents().find(window);
            if (found == graphicsEvents().end()) {
                throw std::runtime_error("pollEvent must be called before reading event details");
            }
            if (callee == "__zgraphics_eventX") return static_cast<double>(found->second.x);
            if (callee == "__zgraphics_eventY") return static_cast<double>(found->second.y);
            if (callee == "__zgraphics_eventKey") return static_cast<double>(found->second.key);
            return found->second.text;
        }
    } else if (callee == "__zgraphics_drawText") {
        const auto args = graphicsArguments(instruction, environment, 6);
        app.drawText(graphicsInteger(args[0], "window"), valueToString(args[1]),
                     graphicsInteger(args[2], "x"), graphicsInteger(args[3], "y"),
                     parseColor(valueToString(args[4])), graphicsInteger(args[5], "size"));
    } else if (callee == "__zgraphics_drawImage") {
        const auto args = graphicsArguments(instruction, environment, 6);
        app.drawImage(graphicsInteger(args[0], "window"), valueToString(args[1]),
                      graphicsInteger(args[2], "x"), graphicsInteger(args[3], "y"),
                      graphicsInteger(args[4], "width"), graphicsInteger(args[5], "height"));
    } else if (callee == "__zgraphics_drawLine") {
        const auto args = graphicsArguments(instruction, environment, 7);
        app.drawLine(graphicsInteger(args[0], "window"), graphicsInteger(args[1], "x1"),
                     graphicsInteger(args[2], "y1"), graphicsInteger(args[3], "x2"),
                     graphicsInteger(args[4], "y2"), parseColor(valueToString(args[5])),
                     graphicsInteger(args[6], "thickness"));
    } else if (callee == "__zgraphics_drawRectangle") {
        const auto args = graphicsArguments(instruction, environment, 8);
        app.drawRectangle(graphicsInteger(args[0], "window"), graphicsInteger(args[1], "x"),
                          graphicsInteger(args[2], "y"), graphicsInteger(args[3], "width"),
                          graphicsInteger(args[4], "height"), parseColor(valueToString(args[5])),
                          toBool(args[6]), graphicsInteger(args[7], "thickness"));
    } else if (callee == "__zgraphics_drawCircle") {
        const auto args = graphicsArguments(instruction, environment, 7);
        app.drawCircle(graphicsInteger(args[0], "window"), graphicsInteger(args[1], "x"),
                       graphicsInteger(args[2], "y"), graphicsInteger(args[3], "radius"),
                       parseColor(valueToString(args[4])), toBool(args[5]),
                       graphicsInteger(args[6], "thickness"));
    } else {
        throw std::runtime_error("unknown ZGraphics runtime function '" + callee + "'");
    }
    return false;
}


// ============================================================
// Execute call
// ============================================================

static RuntimeValue executeCall(
    const zplus::ir::Module& module,
    const zplus::ir::Instruction& instruction,
    Environment& environment
) {

    if (instruction.operands.empty()) {

        throw std::runtime_error(
            "call instruction has no callee"
        );
    }


    const zplus::ir::Function* function = nullptr;
    std::string dynamicCallee;
    const std::string* callee = nullptr;
    if (instruction.calleeFunctionIndex != zplus::ir::kInvalidFunctionIndex) {
        if (instruction.calleeFunctionIndex >= module.functions.size()) {
            throw std::runtime_error("call instruction has an invalid function index");
        }
        function = &module.functions[instruction.calleeFunctionIndex];
        callee = &function->name;
    } else {
        dynamicCallee = valueToString(
            resolveValue(instruction.operands[0], environment)
        );
        callee = &dynamicCallee;
    }

    if (!function) {
        if (const auto* builtin = module.builtinRegistry.find(*callee)) {
            if (!builtin->interpreterAvailable) {
                throw std::runtime_error(
                    "builtin '" + *callee + "' is unavailable in the interpreter");
            }
            const std::size_t argumentCount = instruction.operands.size() - 1;
            const std::size_t parameterCount =
                builtin->signature.parameters.size();
            if (argumentCount < builtin->minimumArgumentCount() ||
                (!builtin->variadic && argumentCount > parameterCount)) {
                throw std::runtime_error(
                    "invalid IR call to builtin '" + *callee + "'");
            }
        }
    }

    if (!function && callee->rfind("__zgraphics_", 0) == 0) {
        return executeGraphicsBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zmemory_", 0) == 0) {
        return executeMemoryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zstdlib_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zaudio_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zphysics_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zjson_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zhttp_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zweb_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }
    if (!function && callee->rfind("__zsqlite_", 0) == 0) {
        return executeStandardLibraryBuiltin(*callee, instruction, environment);
    }

    // --------------------------------------------------------
    // say(...)
    // --------------------------------------------------------

    if (!function && (
        *callee == "say" ||
        *callee == "println")
    ) {

        return builtinSay(
            instruction,
            environment
        );
    }


    // --------------------------------------------------------
    // printz(...) and the deprecated printm(...) alias
    // --------------------------------------------------------

    if (!function && (
        *callee == "printz" ||
        *callee == "printm" ||
        *callee == "print")
    ) {

        return builtinPrint(
            instruction,
            environment
        );
    }

    if (!function && *callee == "string") {
        if (instruction.operands.size() != 2) {
            throw std::runtime_error("string expects exactly one argument");
        }
        RuntimeValue value = resolveValue(instruction.operands[1], environment);
        if (!isString(value)) {
            throw std::invalid_argument("string expects a string");
        }
        return value;
    }

    if (!function && (
        *callee == "read" ||
        *callee == "read_number" ||
        *callee == "read_integer" ||
        *callee == "read_boolean")) {
        if (instruction.operands.size() > 2) {
            throw std::runtime_error(*callee + " expects at most one prompt argument");
        }
        zplus::runtime::input::ensureUtf8ConsoleInput();
        std::string_view prompt;
        RuntimeValue promptStorage;
        if (instruction.operands.size() == 2) {
            promptStorage = resolveValue(instruction.operands[1], environment);
            if (!isString(promptStorage)) {
                throw std::invalid_argument(*callee + " prompt must be a string");
            }
            prompt = stringValue(promptStorage);
        }
        zplus::runtime::input::writePrompt(std::cout, prompt);

        if (*callee == "read") {
            const std::optional<std::string> line =
                zplus::runtime::input::readLine(std::cin);
            return managedHeap().makeString(line ? *line : std::string{});
        }
        if (*callee == "read_number") {
            return zplus::runtime::input::parseNumber(
                zplus::runtime::input::readRequiredLine(std::cin, "read_number"));
        }
        if (*callee == "read_integer") {
            return zplus::runtime::input::parseInteger(
                zplus::runtime::input::readRequiredLine(std::cin, "read_integer"));
        }
        return zplus::runtime::input::parseBoolean(
            zplus::runtime::input::readRequiredLine(std::cin, "read_boolean"));
    }


    // --------------------------------------------------------
    // time()
    // --------------------------------------------------------

    if (!function && *callee == "time") {
        return builtinTime();
    }

    if (!function && *callee == "to_string") {
        if (instruction.operands.size() != 2) {
            throw std::runtime_error("to_string expects exactly one argument");
        }
        const RuntimeValue value = resolveValue(instruction.operands[1], environment);
        if (!isNumber(value)) {
            throw std::invalid_argument("to_string expects a number");
        }
        return managedHeap().makeString(
            zplus::runtime::formatNumber(std::get<double>(value)));
    }

    if (!function && *callee == "length") {
        if (instruction.operands.size() != 2) {
            throw std::runtime_error("length expects exactly one argument");
        }
        const RuntimeValue value = resolveValue(instruction.operands[1], environment);
        if (!isArray(value)) {
            throw std::runtime_error("length expects an array");
        }
        return static_cast<double>(
            std::get<RuntimeArrayPtr>(value)->elements.size()
        );
    }

    if (!function && (
        *callee == "push" || *callee == "pop" || *callee == "clear" ||
        *callee == "insert" || *callee == "remove" || *callee == "contains"
    )) {
        const std::size_t expected =
            *callee == "push" || *callee == "contains" ? 2 :
            *callee == "insert" ? 3 :
            *callee == "remove" ? 2 : 1;
        if (instruction.operands.size() != expected + 1) {
            throw std::runtime_error(
                *callee + " expects " + std::to_string(expected) +
                (expected == 1 ? " argument" : " arguments"));
        }

        const RuntimeValue arrayValue =
            resolveValue(instruction.operands[1], environment);
        if (!isArray(arrayValue)) {
            throw std::runtime_error(*callee + " expects an array as its first argument");
        }
        RuntimeArray& array = *std::get<RuntimeArrayPtr>(arrayValue);
        ManagedHeap& heap = managedHeap();

        if (*callee == "push") {
            RuntimeValue value = resolveValue(instruction.operands[2], environment);
            heap.push(array, std::move(value));
            return 0.0;
        }
        if (*callee == "pop") {
            return heap.pop(array);
        }
        if (*callee == "clear") {
            heap.clear(array);
            return 0.0;
        }

        const RuntimeValue indexValue =
            *callee == "insert" || *callee == "remove"
                ? resolveValue(instruction.operands[2], environment)
                : RuntimeValue{0.0};
        const RuntimeValue value =
            *callee == "insert"
                ? resolveValue(instruction.operands[3], environment)
                : *callee == "remove"
                    ? RuntimeValue{0.0}
                    : resolveValue(instruction.operands[2], environment);

        if (*callee == "contains") {
            const auto matches = [&value](const RuntimeValue& element) {
                if (isString(element) || isString(value)) {
                    return isString(element) && isString(value) &&
                        stringValue(element) == stringValue(value);
                }
                if (element.index() != value.index()) return false;
                if (const auto* number = std::get_if<double>(&element)) {
                    return *number == std::get<double>(value);
                }
                if (const auto* boolean = std::get_if<bool>(&element)) {
                    return *boolean == std::get<bool>(value);
                }
                if (const auto* nested = std::get_if<RuntimeArrayPtr>(&element)) {
                    return *nested == std::get<RuntimeArrayPtr>(value);
                }
                if (const auto* structure =
                        std::get_if<RuntimeStructPtr>(&element)) {
                    return *structure == std::get<RuntimeStructPtr>(value);
                }
                return false;
            };
            return std::any_of(
                array.elements.begin(), array.elements.end(), matches);
        }

        if (!isNumber(indexValue)) {
            throw std::runtime_error(*callee + " index must be a number");
        }
        const double rawIndex = std::get<double>(indexValue);
        if (!std::isfinite(rawIndex) || std::floor(rawIndex) != rawIndex ||
            rawIndex < 0.0 ||
            rawIndex >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            throw std::out_of_range(*callee + " index must be a non-negative integer");
        }
        const std::size_t index = static_cast<std::size_t>(rawIndex);
        if (*callee == "insert") {
            heap.insert(array, index, value);
            return 0.0;
        }
        return heap.remove(array, index);
    }


    // --------------------------------------------------------
    // User function
    // --------------------------------------------------------

    if (!function) {
        function = module.findFunction(*callee);
    }

    if (!function) {

        throw std::runtime_error(
            "unknown function '" +
            *callee +
            "'"
        );
    }

    const std::size_t argumentCount = instruction.operands.size() - 1;
    const auto invoke = [&](std::span<RuntimeValue> arguments) -> RuntimeValue {
        ExecutionResult result = executeFunction(
            module,
            *function,
            arguments,
            environment.parent
        );
        if (result.status != 0) {
            throw std::runtime_error(
                "function '" + *callee + "' failed"
            );
        }
        return std::move(result.value);
    };

    if (argumentCount == 0) {
        return invoke({});
    }

    const auto invokeInline = [&]<std::size_t Count>() -> RuntimeValue {
        std::array<RuntimeValue, Count> arguments{};
        for (std::size_t i = 0; i < Count; ++i) {
            arguments[i] = resolveValue(
                instruction.operands[i + 1],
                environment
            );
        }
        return invoke(std::span<RuntimeValue>(arguments));
    };

    switch (argumentCount) {
        case 1: return invokeInline.template operator()<1>();
        case 2: return invokeInline.template operator()<2>();
        case 3: return invokeInline.template operator()<3>();
        case 4: return invokeInline.template operator()<4>();
        case 5: return invokeInline.template operator()<5>();
        case 6: return invokeInline.template operator()<6>();
        case 7: return invokeInline.template operator()<7>();
        case 8: return invokeInline.template operator()<8>();
        default: {
            std::vector<RuntimeValue> arguments;
            arguments.reserve(argumentCount);
            for (std::size_t i = 0; i < argumentCount; ++i) {
                arguments.push_back(resolveValue(
                    instruction.operands[i + 1],
                    environment
                ));
            }
            return invoke(std::span<RuntimeValue>(arguments));
        }
    }
}


// ============================================================
// Binary numeric helper
// ============================================================

template <typename Operation>
static RuntimeValue numericBinary(
    const zplus::ir::Instruction& instruction,
    const Environment& environment,
    Operation operation
) {

    if (instruction.operands.size() < 2) {

        throw std::runtime_error(
            instruction.opcode +
            " requires two operands"
        );
    }


    const double left =
        toNumber(
            resolveValue(
                instruction.operands[0],
                environment
            )
        );

    const double right =
        toNumber(
            resolveValue(
                instruction.operands[1],
                environment
            )
        );


    return operation(
        left,
        right
    );
}


// ============================================================
// Execute function
// ============================================================

static ExecutionResult executeFunction(
    const zplus::ir::Module& module,
    const zplus::ir::Function& function,
    std::span<RuntimeValue> arguments,
    Environment* parentEnvironment
) {

    RuntimeFrameScope frame(function);
    ExecutionResult result;

    Environment environment;
    environment.parent = parentEnvironment;
    environment.globals = parentEnvironment
        ? (parentEnvironment->globals
               ? parentEnvironment->globals
               : parentEnvironment)
        : nullptr;
    environment.prepareSlots(function.runtimeSlotCount);
    EnvironmentRootScope environmentRoot(environment);


    // ========================================================
    // Bind parameters
    // ========================================================

    for (
        std::size_t i = 0;
        i < function.parameters.size();
        ++i
    ) {

        const auto& parameter =
            function.parameters[i];

        if (!parameter.valid()) {
            continue;
        }

        if (i < arguments.size()) {

            if (parameter.runtimeSlot != zplus::ir::kInvalidRuntimeSlot) {
                environment.slot(parameter.runtimeSlot) = std::move(arguments[i]);
            } else {
                environment[parameter.name] = std::move(arguments[i]);
            }

        } else {

            if (parameter.runtimeSlot != zplus::ir::kInvalidRuntimeSlot) {
                environment.slot(parameter.runtimeSlot) = 0.0;
            } else {
                environment[parameter.name] = 0.0;
            }
        }
    }


    // ========================================================
    // Entry block
    // ========================================================

    const zplus::ir::BasicBlock* block =
        function.entryBlock();

    if (!block) {
        return result;
    }


    std::size_t ip = 0;
    struct ExceptionHandler {
        std::string blockLabel;
        zplus::ir::Value errorBinding;
        std::size_t deferScopeDepth = 0;
        std::size_t deferredCallDepth = 0;
    };
    std::vector<ExceptionHandler> exceptionHandlers;
    struct DeferredCall {
        std::string callee;
        std::vector<RuntimeValue> arguments;
        std::size_t rootStart = 0;
    };
    std::vector<DeferredCall> deferredCalls;
    std::vector<std::size_t> deferScopeMarks;

    const auto runDeferredTo = [&](std::size_t targetDepth) {
        std::exception_ptr firstError;
        while (deferredCalls.size() > targetDepth) {
            DeferredCall deferred = std::move(deferredCalls.back());
            deferredCalls.pop_back();
            try {
                Environment deferredEnvironment;
                deferredEnvironment.parent = &environment;
                deferredEnvironment.globals = environment.globals;
                EnvironmentRootScope deferredRoot(deferredEnvironment);
                zplus::ir::Instruction deferredInstruction;
                deferredInstruction.opcode = "call";
                deferredInstruction.operands.emplace_back(
                    deferred.callee, zplus::ir::Value::Kind::Identifier);
                for (std::size_t i = 0; i < deferred.arguments.size(); ++i) {
                    const std::string captureName =
                        "__zplus_defer_arg_" + std::to_string(i);
                    deferredEnvironment[captureName] =
                        deferred.arguments[i];
                    deferredInstruction.operands.emplace_back(
                        captureName, zplus::ir::Value::Kind::Identifier);
                }
                static_cast<void>(executeCall(
                    module, deferredInstruction, deferredEnvironment));
            } catch (...) {
                if (!firstError) firstError = std::current_exception();
            }
            environment.deferredRoots.resize(deferred.rootStart);
        }
        if (firstError) std::rethrow_exception(firstError);
    };


    // ========================================================
    // CFG interpreter
    // ========================================================

    while (block) {

        if (
            ip >=
            block->instructions.size()
        ) {

            // A properly generated block should end with
            // br / condbr / ret.
            //
            // Falling off the end is treated as function end.

            return result;
        }


        const zplus::ir::Instruction&
            instruction =
                block->instructions[ip];
        activeRuntimeLocation = instruction.location;


        const std::string& op =
            instruction.opcode;

        try {

        // ====================================================
        // Empty instruction
        // ====================================================

        if (instruction.isEmpty()) {

            ++ip;
            continue;
        }

        if (op == "try_push") {
            if (instruction.operands.size() != 2) {
                throw std::runtime_error(
                    "try_push requires a handler label and error binding");
            }
            exceptionHandlers.push_back({
                instruction.operands[0].name,
                instruction.operands[1],
                deferScopeMarks.size(),
                deferredCalls.size()
            });
            ++ip;
            continue;
        }

        if (op == "try_pop") {
            if (exceptionHandlers.empty()) {
                throw std::runtime_error("try_pop has no active handler");
            }
            exceptionHandlers.pop_back();
            ++ip;
            continue;
        }

        if (op == "throw") {
            if (instruction.operands.size() != 1) {
                throw std::runtime_error("throw requires one error value");
            }
            const RuntimeValue value =
                resolveValue(instruction.operands[0], environment);
            std::string message = valueToString(value);
            if (isStruct(value)) {
                const RuntimeStructPtr structure = std::get<RuntimeStructPtr>(value);
                if (structure->typeName == "Error") {
                    const auto field = structure->fields.find("message");
                    if (field != structure->fields.end() && isString(field->second)) {
                        message = stringValue(field->second);
                    }
                }
            }
            throw std::runtime_error(std::move(message));
        }

        if (op == "scope_push") {
            deferScopeMarks.push_back(deferredCalls.size());
            ++ip;
            continue;
        }

        if (op == "scope_pop") {
            if (deferScopeMarks.empty()) {
                throw std::runtime_error("scope_pop has no active scope");
            }
            const std::size_t targetDepth = deferScopeMarks.back();
            deferScopeMarks.pop_back();
            runDeferredTo(targetDepth);
            ++ip;
            continue;
        }

        if (op == "defer_push") {
            if (instruction.operands.empty() ||
                instruction.operands.front().kind !=
                    zplus::ir::Value::Kind::Identifier) {
                throw std::runtime_error(
                    "defer requires a named function call");
            }
            DeferredCall deferred;
            deferred.callee = instruction.operands.front().name;
            deferred.rootStart = environment.deferredRoots.size();
            deferred.arguments.reserve(instruction.operands.size() - 1);
            for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                deferred.arguments.push_back(
                    resolveValue(instruction.operands[i], environment));
                environment.deferredRoots.push_back(deferred.arguments.back());
            }
            deferredCalls.push_back(std::move(deferred));
            ++ip;
            continue;
        }


        // ====================================================
        // ALLOC
        // ====================================================

        if (op == "alloc") {

            if (instruction.operands.empty()) {

                throw std::runtime_error(
                    "alloc requires a variable name"
                );
            }

            const std::string& name = instruction.operands[0].name;
            const auto& binding = instruction.operands[0];
            if (binding.runtimeSlot != zplus::ir::kInvalidRuntimeSlot) {
                Environment* storage = binding.globalSlot
                    ? environment.globals
                    : &environment;
                if (!storage) {
                    throw std::runtime_error("module global storage is unavailable");
                }
                storage->slot(binding.runtimeSlot) = 0.0;
                ++ip;
                continue;
            }
            const bool isModuleGlobal =
                environment.parent &&
                std::find(
                    module.globalVariables.begin(),
                    module.globalVariables.end(),
                    name
                ) != module.globalVariables.end();
            if (isModuleGlobal) {
                (*environment.parent)[name] = 0.0;
            } else {
                environment[name] = 0.0;
            }

            ++ip;
            continue;
        }


        // ====================================================
        // FREEZE
        // ====================================================

        if (op == "freeze") {

            // Const correctness is already checked by
            // semantic analysis.

            ++ip;
            continue;
        }


        // ====================================================
        // PARAM
        // ====================================================

        if (op == "param") {

            ++ip;
            continue;
        }


        // ====================================================
        // MOV
        // ====================================================

        if (op == "mov") {

            if (instruction.operands.empty()) {

                throw std::runtime_error(
                    "mov requires a source operand"
                );
            }

            storeResult(
                instruction,
                resolveValue(
                    instruction.operands[0],
                    environment
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // STORE
        // ====================================================

        if (op == "store") {

            if (instruction.operands.size() < 2) {

                throw std::runtime_error(
                    "store requires a target and value"
                );
            }

            storeVariable(
                environment,
                instruction.operands[0],
                resolveValue(instruction.operands[1], environment)
            );

            ++ip;
            continue;
        }

        if (op == "array") {
            std::vector<RuntimeValue> elements;
            elements.reserve(instruction.operands.size());
            for (const auto& operand : instruction.operands) {
                elements.push_back(resolveValue(operand, environment));
            }
            storeResult(
                instruction,
                managedHeap().makeArray(std::move(elements)),
                environment
            );
            ++ip;
            continue;
        }

        if (op == "struct_new") {
            if (instruction.operands.empty()) {
                throw std::runtime_error("struct_new requires a type name");
            }
            const auto* structureType =
                module.findStructType(instruction.operands[0].name);
            if (!structureType) {
                throw std::runtime_error(
                    "unknown struct type '" + instruction.operands[0].name + "'");
            }
            const std::size_t argumentCount = instruction.operands.size() - 1;
            if (argumentCount > structureType->fields.size()) {
                throw std::runtime_error(
                    "too many constructor arguments for '" + structureType->name + "'");
            }

            std::unordered_map<std::string, RuntimeValue> fields;
            std::vector<RuntimeValue> roots;
            roots.reserve(structureType->fields.size());
            for (std::size_t i = 0; i < structureType->fields.size(); ++i) {
                const auto& field = structureType->fields[i];
                RuntimeValue value;
                if (i < argumentCount) {
                    value = resolveValue(instruction.operands[i + 1], environment);
                } else if (field.hasDefault) {
                    value = resolveValue(field.defaultValue, environment);
                } else {
                    throw std::runtime_error(
                        "missing required constructor argument '" + field.name +
                        "' for '" + structureType->name + "'");
                }
                roots.push_back(value);
                fields.emplace(field.name, std::move(value));
            }
            storeResult(
                instruction,
                managedHeap().makeStruct(structureType->name, std::move(fields), roots),
                environment
            );
            ++ip;
            continue;
        }

        if (op == "index") {
            if (instruction.operands.size() != 2) {
                throw std::runtime_error("index requires an array and an index");
            }
            const RuntimeValue object = resolveValue(instruction.operands[0], environment);
            if (!isArray(object)) {
                throw std::runtime_error("index operation requires an array value");
            }
            const auto& array = *std::get<RuntimeArrayPtr>(object);
            const std::size_t index = checkedArrayIndex(
                resolveValue(instruction.operands[1], environment),
                array
            );
            storeResult(instruction, array.elements[index], environment);
            ++ip;
            continue;
        }

        if (op == "index_store") {
            if (instruction.operands.size() != 3) {
                throw std::runtime_error(
                    "index_store requires an array, an index, and a value"
                );
            }
            const RuntimeValue object = resolveValue(instruction.operands[0], environment);
            if (!isArray(object)) {
                throw std::runtime_error("indexed assignment requires an array value");
            }
            const auto array = std::get<RuntimeArrayPtr>(object);
            const std::size_t index = checkedArrayIndex(
                resolveValue(instruction.operands[1], environment),
                *array
            );
            array->elements[index] = resolveValue(instruction.operands[2], environment);
            ++ip;
            continue;
        }

        if (op == "member") {
            if (instruction.operands.size() != 2) {
                throw std::runtime_error("member requires an object and member name");
            }
            const RuntimeValue object =
                resolveValue(instruction.operands[0], environment);
            if (!isStruct(object)) {
                if (isString(object)) {
                    const std::string qualifiedName =
                        stringValue(object) + "." + instruction.operands[1].name;
                    const std::string variableName =
                        module.resolveVariable(qualifiedName);
                    if (variableName != qualifiedName) {
                        storeResult(
                            instruction,
                            resolveValue(
                                zplus::ir::Value{
                                    variableName,
                                    zplus::ir::Value::Kind::Identifier},
                                environment),
                            environment);
                        ++ip;
                        continue;
                    }
                }
                throw std::runtime_error("member access requires a struct value");
            }
            auto* structure = std::get<RuntimeStructPtr>(object);
            const auto field = structure->fields.find(instruction.operands[1].name);
            if (field == structure->fields.end()) {
                throw std::runtime_error(
                    "struct '" + structure->typeName + "' has no field '" +
                    instruction.operands[1].name + "'");
            }
            storeResult(instruction, field->second, environment);
            ++ip;
            continue;
        }

        if (op == "member_store") {
            if (instruction.operands.size() != 3) {
                throw std::runtime_error(
                    "member_store requires an object, member name, and value");
            }
            const RuntimeValue object =
                resolveValue(instruction.operands[0], environment);
            if (!isStruct(object)) {
                if (isString(object)) {
                    const std::string qualifiedName =
                        stringValue(object) + "." + instruction.operands[1].name;
                    const std::string variableName =
                        module.resolveVariable(qualifiedName);
                    if (variableName != qualifiedName) {
                        storeVariable(
                            environment,
                            zplus::ir::Value{
                                variableName,
                                zplus::ir::Value::Kind::Identifier},
                            resolveValue(instruction.operands[2], environment));
                        ++ip;
                        continue;
                    }
                }
                throw std::runtime_error(
                    "member assignment requires a struct value");
            }
            auto* structure = std::get<RuntimeStructPtr>(object);
            const auto field = structure->fields.find(instruction.operands[1].name);
            if (field == structure->fields.end()) {
                throw std::runtime_error(
                    "struct '" + structure->typeName + "' has no field '" +
                    instruction.operands[1].name + "'");
            }
            field->second = resolveValue(instruction.operands[2], environment);
            ++ip;
            continue;
        }

        // ====================================================
        // LOAD
        // ====================================================

        if (op == "load") {

            if (instruction.operands.empty()) {

                throw std::runtime_error(
                    "load requires an operand"
                );
            }

            storeResult(
                instruction,
                resolveValue(
                    instruction.operands[0],
                    environment
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // ADD
        // ====================================================

        if (op == "add") {

            if (instruction.operands.size() < 2) {

                throw std::runtime_error(
                    "add requires two operands"
                );
            }


            const RuntimeValue left =
                resolveValue(
                    instruction.operands[0],
                    environment
                );

            const RuntimeValue right =
                resolveValue(
                    instruction.operands[1],
                    environment
                );


            if (
                isString(left) ||
                isString(right)
            ) {

                storeResult(
                    instruction,
                    managedHeap().makeString(
                        valueToString(left) + valueToString(right)
                    ),
                    environment
                );

            } else {

                storeResult(
                    instruction,
                    toNumber(left) +
                    toNumber(right),
                    environment
                );
            }

            ++ip;
            continue;
        }


        // ====================================================
        // SUB
        // ====================================================

        if (op == "sub") {

            storeResult(
                instruction,
                numericBinary(
                    instruction,
                    environment,
                    [](
                        double left,
                        double right
                    ) {

                        return left - right;
                    }
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // MUL
        // ====================================================

        if (op == "mul") {

            storeResult(
                instruction,
                numericBinary(
                    instruction,
                    environment,
                    [](
                        double left,
                        double right
                    ) {

                        return left * right;
                    }
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // DIV
        // ====================================================

        if (op == "div") {

            const RuntimeValue value =
                numericBinary(
                    instruction,
                    environment,
                    [](
                        double left,
                        double right
                    ) {
                        return left / right;
                    }
                );

            storeResult(
                instruction,
                value,
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // MOD
        // ====================================================

        if (op == "mod") {

            const RuntimeValue value =
                numericBinary(
                    instruction,
                    environment,
                    [](
                        double left,
                        double right
                    ) {

                        if (right == 0.0) {

                            throw std::runtime_error(
                                "modulo by zero"
                            );
                        }

                        return std::fmod(
                            left,
                            right
                        );
                    }
                );

            storeResult(
                instruction,
                value,
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // CMP
        // ====================================================

        if (
            op == "cmp_eq" ||
            op == "cmp_ne" ||
            op == "cmp_lt" ||
            op == "cmp_le" ||
            op == "cmp_gt" ||
            op == "cmp_ge"
        ) {

            if (instruction.operands.size() < 2) {

                throw std::runtime_error(
                    op +
                    " requires two operands"
                );
            }


            const RuntimeValue left =
                resolveValue(
                    instruction.operands[0],
                    environment
                );

            const RuntimeValue right =
                resolveValue(
                    instruction.operands[1],
                    environment
                );


            bool comparison = false;


            if (
                isNumber(left) &&
                isNumber(right)
            ) {

                const double l =
                    toNumber(left);

                const double r =
                    toNumber(right);


                if (op == "cmp_eq")
                    comparison = l == r;

                else if (op == "cmp_ne")
                    comparison = l != r;

                else if (op == "cmp_lt")
                    comparison = l < r;

                else if (op == "cmp_le")
                    comparison = l <= r;

                else if (op == "cmp_gt")
                    comparison = l > r;

                else
                    comparison = l >= r;

            } else if (isStruct(left) || isStruct(right)) {
                if (op != "cmp_eq" && op != "cmp_ne") {
                    throw std::runtime_error(
                        "relational comparison is not supported for structs");
                }
                const bool sameObject = isStruct(left) && isStruct(right) &&
                    std::get<RuntimeStructPtr>(left) ==
                        std::get<RuntimeStructPtr>(right);
                comparison = op == "cmp_eq" ? sameObject : !sameObject;
            } else {
                const std::string l =
                    valueToString(left);

                const std::string r =
                    valueToString(right);


                if (op == "cmp_eq")
                    comparison = l == r;

                else if (op == "cmp_ne")
                    comparison = l != r;

                else if (op == "cmp_lt")
                    comparison = l < r;

                else if (op == "cmp_le")
                    comparison = l <= r;

                else if (op == "cmp_gt")
                    comparison = l > r;

                else
                    comparison = l >= r;
            }


            storeResult(
                instruction,
                comparison,
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // AND
        // ====================================================

        if (op == "and") {

            const RuntimeValue left =
                resolveValue(
                    instruction.operands.at(0),
                    environment
                );

            const RuntimeValue right =
                resolveValue(
                    instruction.operands.at(1),
                    environment
                );

            storeResult(
                instruction,
                toBool(left) &&
                toBool(right),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // OR
        // ====================================================

        if (op == "or") {

            const RuntimeValue left =
                resolveValue(
                    instruction.operands.at(0),
                    environment
                );

            const RuntimeValue right =
                resolveValue(
                    instruction.operands.at(1),
                    environment
                );

            storeResult(
                instruction,
                toBool(left) ||
                toBool(right),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // BITWISE
        // ====================================================

        if (
            op == "bitand" ||
            op == "bitor" ||
            op == "bitxor" ||
            op == "shl" ||
            op == "shr"
        ) {

            storeResult(
                instruction,
                static_cast<double>(applyBitwise(
                    op,
                    toBitwiseInteger(resolveValue(
                        instruction.operands.at(0), environment)),
                    toBitwiseInteger(resolveValue(
                        instruction.operands.at(1), environment)))),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // POS
        // ====================================================

        if (op == "pos") {

            storeResult(
                instruction,
                toNumber(
                    resolveValue(
                        instruction.operands.at(0),
                        environment
                    )
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // NEG
        // ====================================================

        if (op == "neg") {

            storeResult(
                instruction,
                -toNumber(
                    resolveValue(
                        instruction.operands.at(0),
                        environment
                    )
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // NOT
        // ====================================================

        if (op == "not") {

            storeResult(
                instruction,
                !toBool(
                    resolveValue(
                        instruction.operands.at(0),
                        environment
                    )
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // BIT NOT
        // ====================================================

        if (op == "bitnot") {

            const std::int64_t value = toBitwiseInteger(resolveValue(
                instruction.operands.at(0), environment));

            storeResult(
                instruction,
                static_cast<double>(~value),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // DEC
        // ====================================================

        if (op == "dec") {

            if (instruction.operands.empty()) {

                throw std::runtime_error(
                    "dec requires an operand"
                );
            }

            const RuntimeValue value =
                resolveValue(
                    instruction.operands[0],
                    environment
                );

            storeVariable(environment, instruction.operands[0], toNumber(value) - 1.0);

            ++ip;
            continue;
        }


        // ====================================================
        // PRE/POST INC
        // ====================================================

        if (
            op == "pre_inc" ||
            op == "post_inc"
        ) {

            const double oldValue =
                toNumber(
                    resolveValue(
                        instruction.operands.at(0),
                        environment
                    )
                );

            const double newValue =
                oldValue + 1.0;

            storeVariable(environment, instruction.operands.at(0), newValue);


            storeResult(
                instruction,
                op == "pre_inc"
                    ? RuntimeValue{newValue}
                    : RuntimeValue{oldValue},
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // PRE/POST DEC
        // ====================================================

        if (
            op == "pre_dec" ||
            op == "post_dec"
        ) {

            const double oldValue =
                toNumber(
                    resolveValue(
                        instruction.operands.at(0),
                        environment
                    )
                );

            const double newValue =
                oldValue - 1.0;

            storeVariable(environment, instruction.operands.at(0), newValue);


            storeResult(
                instruction,
                op == "pre_dec"
                    ? RuntimeValue{newValue}
                    : RuntimeValue{oldValue},
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // Compound assignments
        // ====================================================

        if (
            op == "add_assign" ||
            op == "sub_assign" ||
            op == "mul_assign" ||
            op == "div_assign" ||
            op == "mod_assign"
        ) {

            if (instruction.operands.size() < 2) {

                throw std::runtime_error(
                    op +
                    " requires target and value"
                );
            }


            const RuntimeValue left =
                resolveValue(
                    instruction.operands[0],
                    environment
                );

            const RuntimeValue right =
                resolveValue(
                    instruction.operands[1],
                    environment
                );


            RuntimeValue newValue;


            if (op == "add_assign") {

                if (
                    isString(left) ||
                    isString(right)
                ) {

                    newValue =
                        managedHeap().makeString(
                            valueToString(left) + valueToString(right)
                        );

                } else {

                    newValue =
                        toNumber(left) +
                        toNumber(right);
                }

            } else if (op == "sub_assign") {

                newValue =
                    toNumber(left) -
                    toNumber(right);

            } else if (op == "mul_assign") {

                newValue =
                    toNumber(left) *
                    toNumber(right);

            } else if (op == "div_assign") {

                const double divisor =
                    toNumber(right);

                newValue =
                    toNumber(left) /
                    divisor;

            } else {

                const double divisor =
                    toNumber(right);

                if (divisor == 0.0) {

                    throw std::runtime_error(
                        "modulo by zero"
                    );
                }

                newValue =
                    std::fmod(
                        toNumber(left),
                        divisor
                    );
            }


            storeVariable(environment, instruction.operands[0], newValue);

            ++ip;
            continue;
        }


        // ====================================================
        // Bitwise compound assignments
        // ====================================================

        if (
            op == "and_assign" ||
            op == "or_assign" ||
            op == "xor_assign" ||
            op == "shl_assign" ||
            op == "shr_assign"
        ) {

            storeVariable(
                environment,
                instruction.operands[0],
                static_cast<double>(applyBitwise(
                    op,
                    toBitwiseInteger(resolveValue(
                        instruction.operands.at(0), environment)),
                    toBitwiseInteger(resolveValue(
                        instruction.operands.at(1), environment))))
            );

            ++ip;
            continue;
        }


        // ====================================================
        // CALL
        // ====================================================

        if (op == "call") {

            RuntimeValue callResult =
                executeCall(
                    module,
                    instruction,
                    environment
                );

            storeResult(
                instruction,
                std::move(callResult),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // SELECT
        // ====================================================

        if (op == "select") {

            if (instruction.operands.size() < 3) {

                throw std::runtime_error(
                    "select requires condition, true value and false value"
                );
            }


            const bool condition =
                toBool(
                    resolveValue(
                        instruction.operands[0],
                        environment
                    )
                );


            storeResult(
                instruction,
                resolveValue(
                    condition
                        ? instruction.operands[1]
                        : instruction.operands[2],
                    environment
                ),
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // BR
        // ====================================================

        if (op == "br") {

            if (instruction.operands.empty()) {

                throw std::runtime_error(
                    "br requires a target block"
                );
            }


            const std::string& target =
                instruction.operands[0].name;


            const zplus::ir::BasicBlock*
                targetBlock =
                    findBlock(
                        function,
                        target
                    );


            if (!targetBlock) {

                throw std::runtime_error(
                    "unknown basic block '" +
                    target +
                    "'"
                );
            }


            block =
                targetBlock;

            ip = 0;

            continue;
        }


        // ====================================================
        // CONDBR
        // ====================================================

        if (op == "condbr") {

            if (instruction.operands.size() < 3) {

                throw std::runtime_error(
                    "condbr requires condition and two targets"
                );
            }


            const bool condition =
                toBool(
                    resolveValue(
                        instruction.operands[0],
                        environment
                    )
                );


            const std::string& target =
                condition
                    ? instruction.operands[1].name
                    : instruction.operands[2].name;


            const zplus::ir::BasicBlock*
                targetBlock =
                    findBlock(
                        function,
                        target
                    );


            if (!targetBlock) {

                throw std::runtime_error(
                    "unknown basic block '" +
                    target +
                    "'"
                );
            }


            block =
                targetBlock;

            ip = 0;

            continue;
        }


        // ====================================================
        // SWITCH
        // ====================================================

        if (op == "switch") {

            if (instruction.operands.size() < 2) {

                throw std::runtime_error(
                    "switch requires a value and default target"
                );
            }


            const RuntimeValue switchValue =
                resolveValue(
                    instruction.operands[0],
                    environment
                );


            std::string target =
                instruction.operands.back().name;


            for (
                std::size_t i = 1;
                i + 1 < instruction.operands.size() - 1;
                i += 2
            ) {

                const RuntimeValue caseValue =
                    resolveValue(
                        instruction.operands[i],
                        environment
                    );


                bool equal = false;

                if (isStruct(switchValue) || isStruct(caseValue)) {
                    equal = isStruct(switchValue) && isStruct(caseValue) &&
                        std::get<RuntimeStructPtr>(switchValue) ==
                            std::get<RuntimeStructPtr>(caseValue);
                } else if (
                    isNumber(switchValue) &&
                    isNumber(caseValue)
                ) {

                    equal =
                        toNumber(switchValue) ==
                        toNumber(caseValue);

                } else {

                    equal =
                        valueToString(switchValue) ==
                        valueToString(caseValue);
                }


                if (equal) {

                    target =
                        instruction.operands[
                            i + 1
                        ].name;

                    break;
                }
            }


            const zplus::ir::BasicBlock*
                targetBlock =
                    findBlock(
                        function,
                        target
                    );


            if (!targetBlock) {

                throw std::runtime_error(
                    "unknown switch block '" +
                    target +
                    "'"
                );
            }


            block =
                targetBlock;

            ip = 0;

            continue;
        }


        // ====================================================
        // RET
        // ====================================================

        if (op == "ret") {

            runDeferredTo(0);
            deferScopeMarks.clear();
            result.returned = true;


            if (
                !instruction.operands.empty()
            ) {

                result.value =
                    resolveValue(
                        instruction.operands[0],
                        environment
                    );
            }


            return result;
        }


        // ====================================================
        if (op == "member_ptr") {

            throw std::runtime_error(
                "runtime does not support IR opcode '" +
                op +
                "' yet"
            );
        }


        // ====================================================
        // IS
        // ====================================================

        if (op == "is") {
            if (instruction.operands.size() != 2) {
                throw std::runtime_error("'is' requires a value and a type name");
            }
            const RuntimeValue value =
                resolveValue(instruction.operands[0], environment);
            const RuntimeValue typeName =
                resolveValue(instruction.operands[1], environment);
            const bool matches =
                isStruct(value) && isString(typeName) &&
                std::get<RuntimeStructPtr>(value)->typeName ==
                    stringValue(typeName);

            storeResult(
                instruction,
                matches,
                environment
            );

            ++ip;
            continue;
        }


        // ====================================================
        // Unknown opcode
        // ====================================================

        throw std::runtime_error(
            "unknown IR opcode '" +
            op +
            "'"
        );
        } catch (const std::exception&) {
            std::exception_ptr pendingError = std::current_exception();
            bool handled = false;
            while (!exceptionHandlers.empty()) {
                const ExceptionHandler handler =
                    std::move(exceptionHandlers.back());
                exceptionHandlers.pop_back();
                try {
                    while (deferScopeMarks.size() > handler.deferScopeDepth) {
                        const std::size_t targetDepth = deferScopeMarks.back();
                        deferScopeMarks.pop_back();
                        runDeferredTo(targetDepth);
                    }
                    runDeferredTo(handler.deferredCallDepth);
                } catch (...) {
                    pendingError = std::current_exception();
                    continue;
                }

                std::string message;
                try {
                    std::rethrow_exception(pendingError);
                } catch (const std::exception& pendingException) {
                    message = pendingException.what();
                } catch (...) {
                    message = "unknown error";
                }
                RuntimeValue errorMessage = managedHeap().makeString(message);
                const std::array<RuntimeValue, 1> roots{errorMessage};
                auto* errorObject = managedHeap().makeStruct(
                    "Error",
                    {{"message", std::move(errorMessage)}},
                    roots
                );
                storeVariable(
                    environment,
                    handler.errorBinding,
                    RuntimeStructPtr{errorObject}
                );

                const auto target = std::find_if(
                    function.blocks.begin(),
                    function.blocks.end(),
                    [&handler](const zplus::ir::BasicBlock& candidate) {
                        return candidate.label == handler.blockLabel;
                    }
                );
                if (target == function.blocks.end()) {
                    throw std::runtime_error(
                        "exception handler block '" + handler.blockLabel +
                        "' does not exist");
                }
                block = &*target;
                ip = 0;
                handled = true;
                break;
            }

            if (!handled) {
                runDeferredTo(0);
                deferScopeMarks.clear();
                std::rethrow_exception(pendingError);
            }
        }
    }


    return result;
}


// ============================================================
// Execute module
// ============================================================

int Runtime::execute(
    const zplus::ir::Module& module,
    const std::vector<std::string>& arguments
) {
    const auto diagnostics = zplus::ir::verifyModule(module);
    if (!diagnostics.empty()) {
        const auto& diagnostic = diagnostics.front();
        throw std::runtime_error(
            "invalid IR (" + diagnostic.code + "): " + diagnostic.message);
    }

    RuntimeContextScope context;
    runtimeFrames.clear();
    activeRuntimeEnvironments.clear();
    activeRuntimeLocation = {};
    zplus::stdlib::setProcessArguments(arguments);
    initialize();

    ManagedHeap heap;
    ManagedHeapScope heapScope(heap);
    struct HttpServerShutdown {
        ~HttpServerShutdown() { zplus::http::shutdownServers(); }
    };
    [[maybe_unused]] const HttpServerShutdown httpServerShutdown;
    struct SQLiteShutdown {
        ~SQLiteShutdown() { zplus::sqlite::shutdownDatabases(); }
    };
    [[maybe_unused]] const SQLiteShutdown sqliteShutdown;

    try {
        Environment globals;
        globals.prepareSlots(module.runtimeGlobalSlotCount);
        globals.globals = &globals;
        EnvironmentRootScope globalsRoot(globals);
        if (const auto* initializer =
                module.findFunction(zplus::ir::kModuleInitializerName)) {
            const ExecutionResult initialization =
                executeFunction(module, *initializer, {}, &globals);
            if (initialization.status != 0) {
                return initialization.status;
            }
        }

        const zplus::ir::Function* entry =
            module.findFunction("main");

        if (!entry) {
            entry = module.findFunction(zplus::ir::kTopLevelEntryName);
        }

        if (!entry) {
            entry = module.findFunction("start");
        }

        if (!entry && !module.functions.empty()) {
            entry = &module.functions.front();
        }

        if (!entry) {
            return 0;
        }

        const ExecutionResult result =
            executeFunction(module, *entry, {}, &globals);
        return result.status;
    } catch (const std::exception& error) {
        throw std::runtime_error(formatRuntimeError(error));
    }
}


// ============================================================
// Runtime initialization
// ============================================================

void Runtime::initialize() {
    // No global runtime state currently requires initialization.
}


// ============================================================
// Module execution wrapper
// ============================================================

int execute(const zplus::ir::Module& module) {
    Runtime runtime;
    return runtime.execute(module);
}

// ============================================================
// Execute textual bytecode
// ============================================================
//
// This is intentionally simple.
//
// The primary runtime path is Module -> Function -> BasicBlock.
// This helper exists mainly for compatibility/testing.
//
// Format:
//
//     block entry
//     alloc x
//     ...
//     block loop
//     ...
//
// ============================================================

int executeBytecode(
    const std::string& bytecode
) {

    zplus::ir::Module module;

    module.name =
        "bytecode";


    zplus::ir::Function& function =
        module.createFunction("main");


    zplus::ir::BasicBlock* currentBlock =
        nullptr;


    std::istringstream input(
        bytecode
    );


    std::string line;


    while (
        std::getline(
            input,
            line
        )
    ) {

        if (line.empty()) {
            continue;
        }


        std::istringstream parser(
            line
        );


        std::string first;

        parser >> first;


        if (first.empty()) {
            continue;
        }


        // ----------------------------------------------------
        // Block declaration
        // ----------------------------------------------------

        if (first == "block") {

            std::string label;

            parser >> label;


            if (label.empty()) {

                throw std::runtime_error(
                    "bytecode block has no label"
                );
            }


            currentBlock =
                &function.createBlock(
                    label
                );


            continue;
        }


        // ----------------------------------------------------
        // Automatic entry block
        // ----------------------------------------------------

        if (!currentBlock) {

            currentBlock =
                &function.createBlock(
                    "entry"
                );
        }


        // ----------------------------------------------------
        // Instruction
        // ----------------------------------------------------

        zplus::ir::Instruction instruction;

        instruction.opcode =
            first;


        std::string token;


        while (parser >> token) {

            instruction.operands.emplace_back(
                token
            );
        }


        currentBlock->emit(
            std::move(instruction)
        );
    }


    return execute(module);
}

} // namespace zplus::runtime
