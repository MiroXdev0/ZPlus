#include "X64CodeGenerator.hpp"

#include "MachineCode.hpp"
#include "../../NumericLiteral.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zplus::backend::native {
namespace {

using zplus::ir::BasicBlock;
using zplus::ir::Function;
using zplus::ir::Instruction;
using zplus::ir::Module;
using zplus::ir::Parameter;
using zplus::ir::Value;
using mpp::native::MachineCode;

enum Reg : std::uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11
};

struct BranchFixup {
    std::size_t offset;
    std::string target;
};

struct CallFixup {
    std::size_t offset;
    std::string target;
};

std::size_t alignTo(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

class CodeGenerator {
public:
    CodeGenerator(const Module& module, bool windowsAbi)
        : module_(module), windowsAbi_(windowsAbi) {}

    NativeImage generate() {
        if (module_.functions.empty()) {
            throw std::runtime_error("native code generation requires an IR function");
        }
        if (!module_.globals.empty()) {
            throw std::runtime_error("native backend does not support module-level global instructions");
        }
        for (const auto& name : module_.globalVariables) {
            if (name.empty() || globalOffsets_.contains(name)) {
                throw std::runtime_error("native backend: invalid or duplicate module variable '" +
                                         name + "'");
            }
            globalOffsets_.emplace(name, data_.size());
            data_.emit64(0);
        }

        identifyManagedMemoryUse();

        for (const auto& function : module_.functions) {
            if (function.external) {
                throw std::runtime_error("native backend does not support external function '" +
                                         function.name + "'");
            }
            generateFunction(function);
        }

        generateEntryStub();
        resolveFunctionCalls();

        image_.entryOffset = entryOffset_;
        image_.writableData = !globalOffsets_.empty();
        image_.dataFixups = std::move(dataFixups_);
        image_.importFixups = std::move(importFixups_);
        image_.data = std::move(data_.bytes());
        image_.code = std::move(code_.bytes());
        return std::move(image_);
    }

private:
    void emit8(std::uint8_t value) { code_.emit8(value); }
    void emit32(std::int32_t value) { code_.emitS32(value); }
    void emit64(std::uint64_t value) { code_.emit64(value); }

    void rex(bool wide, std::uint8_t reg, std::uint8_t rm, bool force = false) {
        std::uint8_t prefix = 0x40;
        if (wide) prefix |= 0x08;
        if (reg >= 8) prefix |= 0x04;
        if (rm >= 8) prefix |= 0x01;
        if (prefix != 0x40 || force) emit8(prefix);
    }

    void modrm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
        emit8(static_cast<std::uint8_t>(
            (mod << 6) | ((reg & 7) << 3) | (rm & 7)));
    }

    void movImm(std::uint8_t reg, std::uint64_t value) {
        rex(true, 0, reg);
        emit8(static_cast<std::uint8_t>(0xB8 + (reg & 7)));
        emit64(value);
    }

    void movReg(std::uint8_t destination, std::uint8_t source) {
        rex(true, source, destination);
        emit8(0x89);
        modrm(3, source, destination);
    }

    void movLoad(std::uint8_t destination, std::int32_t displacement) {
        rex(true, destination, RBP);
        emit8(0x8B);
        modrm(2, destination, RBP);
        emit32(displacement);
    }

    void movStore(std::int32_t displacement, std::uint8_t source) {
        rex(true, source, RBP);
        emit8(0x89);
        modrm(2, source, RBP);
        emit32(displacement);
    }

    void xmmLoad(std::uint8_t destination, std::int32_t displacement) {
        emit8(0xF2);
        rex(false, destination, RBP);
        emit8(0x0F); emit8(0x10);
        modrm(2, destination, RBP);
        emit32(displacement);
    }

    void xmmStore(std::int32_t displacement, std::uint8_t source) {
        emit8(0xF2);
        rex(false, source, RBP);
        emit8(0x0F); emit8(0x11);
        modrm(2, source, RBP);
        emit32(displacement);
    }

    void xmmMoveFromGpr(std::uint8_t destination, std::uint8_t source) {
        emit8(0x66);
        rex(true, destination, source);
        emit8(0x0F); emit8(0x6E);
        modrm(3, destination, source);
    }

    void gprMoveFromXmm(std::uint8_t destination, std::uint8_t source) {
        emit8(0x66);
        rex(true, source, destination);
        emit8(0x0F); emit8(0x7E);
        modrm(3, source, destination);
    }

    void xmmBinary(std::uint8_t opcode, std::uint8_t destination,
                   std::uint8_t source) {
        emit8(0xF2);
        rex(false, destination, source);
        emit8(0x0F); emit8(opcode);
        modrm(3, destination, source);
    }

    void xmmCompare(std::uint8_t left, std::uint8_t right) {
        emit8(0x66);
        rex(false, left, right);
        emit8(0x0F); emit8(0x2E);
        modrm(3, left, right);
    }

    void convertXmmToSignedInteger(std::uint8_t destination, std::uint8_t source) {
        emit8(0xF2);
        rex(true, destination, source);
        emit8(0x0F); emit8(0x2C);
        modrm(3, destination, source);
    }

    void convertSignedIntegerToXmm(std::uint8_t destination, std::uint8_t source) {
        emit8(0xF2);
        rex(true, destination, source);
        emit8(0x0F); emit8(0x2A);
        modrm(3, destination, source);
    }

    void movStoreZero(std::int32_t displacement) {
        emit8(0x48);
        emit8(0xC7);
        modrm(2, 0, RBP);
        emit32(displacement);
        emit32(0);
    }

    void leaRbp(std::uint8_t destination, std::int32_t displacement) {
        rex(true, destination, RBP);
        emit8(0x8D);
        modrm(2, destination, RBP);
        emit32(displacement);
    }

    void leaRsp(std::uint8_t destination, std::int32_t displacement) {
        rex(true, destination, RSP);
        emit8(0x8D);
        modrm(2, destination, RSP);
        emit8(0x24);
        emit32(displacement);
    }

    void leaRip(std::uint8_t destination, std::size_t targetDataOffset) {
        rex(true, destination, 5);
        emit8(0x8D);
        modrm(0, destination, 5);
        const std::size_t offset = code_.size();
        emit32(0);
        dataFixups_.push_back({offset, targetDataOffset});
    }

    void loadGlobal(const std::string& name, std::uint8_t destination) {
        const auto found = globalOffsets_.find(name);
        if (found == globalOffsets_.end()) {
            throw std::runtime_error("native backend: undefined module variable '" + name + "'");
        }
        rex(true, destination, 5);
        emit8(0x8B);
        modrm(0, destination, 5);
        const std::size_t offset = code_.size();
        emit32(0);
        dataFixups_.push_back({offset, found->second});
    }

    void storeGlobal(const std::string& name, std::uint8_t source) {
        const auto found = globalOffsets_.find(name);
        if (found == globalOffsets_.end()) {
            throw std::runtime_error("native backend: undefined module variable '" + name + "'");
        }
        rex(true, source, 5);
        emit8(0x89);
        modrm(0, source, 5);
        const std::size_t offset = code_.size();
        emit32(0);
        dataFixups_.push_back({offset, found->second});
    }

    void loadVariable(const std::string& name, std::uint8_t destination) {
        if (globalOffsets_.contains(name)) loadGlobal(name, destination);
        else movLoad(destination, slot(name));
    }

    void storeVariable(const std::string& name, std::uint8_t source) {
        if (globalOffsets_.contains(name)) storeGlobal(name, source);
        else movStore(slot(name), source);
    }

    void loadXmmValue(const Value& value, std::uint8_t destination) {
        if (globalOffsets_.contains(value.name)) {
            loadGlobal(value.name, RAX);
            xmmMoveFromGpr(destination, RAX);
            return;
        }
        if (const auto found = slots_.find(value.name); found != slots_.end()) {
            xmmLoad(destination, found->second);
            return;
        }
        if (value.kind != Value::Kind::Number) {
            throw std::runtime_error(
                "native backend: expected a numeric value, got '" + value.name + "'");
        }
        double number = 0.0;
        if (!zplus::numeric::parseLiteral(value.name, number)) {
            throw std::runtime_error(
                "native backend: invalid numeric literal '" + value.name + "'");
        }
        movImm(RAX, std::bit_cast<std::uint64_t>(number));
        xmmMoveFromGpr(destination, RAX);
    }

    void loadXmmVariable(const std::string& name, std::uint8_t destination) {
        if (globalOffsets_.contains(name)) {
            loadGlobal(name, RAX);
            xmmMoveFromGpr(destination, RAX);
        } else {
            xmmLoad(destination, slot(name));
        }
    }

    void storeXmmResult(const Instruction& instruction, std::uint8_t source) {
        if (!instruction.result.empty()) {
            xmmStore(slot(instruction.result), source);
        }
    }

    void storeXmmVariable(const std::string& name, std::uint8_t source) {
        if (globalOffsets_.contains(name)) {
            gprMoveFromXmm(RAX, source);
            storeGlobal(name, RAX);
        } else {
            xmmStore(slot(name), source);
        }
    }

    bool isNumeric(const Value& value) const {
        return inferredType(value) == "number";
    }

    bool isNumericName(const std::string& value) const {
        const auto local = valueTypes_.find(value);
        if (local != valueTypes_.end()) return local->second == "number";
        const auto global = globalTypes_.find(value);
        return global != globalTypes_.end() && global->second == "number";
    }

    void binary(std::uint8_t opcode, std::uint8_t destination, std::uint8_t source) {
        rex(true, source, destination);
        emit8(opcode);
        modrm(3, source, destination);
    }

    void imul(std::uint8_t destination, std::uint8_t source) {
        rex(true, destination, source);
        emit8(0x0F);
        emit8(0xAF);
        modrm(3, destination, source);
    }

    void cmp(std::uint8_t left, std::uint8_t right) {
        rex(true, right, left);
        emit8(0x39);
        modrm(3, right, left);
    }

    void testRax() {
        emit8(0x48);
        emit8(0x85);
        emit8(0xC0);
    }

    void setCondition(std::uint8_t condition) {
        movImm(RAX, 0);
        emit8(0x0F);
        emit8(condition);
        emit8(0xC0);
    }

    void setConditionTo(std::uint8_t condition, std::uint8_t destination) {
        rex(false, 0, destination, destination >= 4);
        emit8(0x0F);
        emit8(condition);
        modrm(3, 0, destination);
        rex(true, destination, destination, destination >= 4);
        emit8(0x0F);
        emit8(0xB6);
        modrm(3, destination, destination);
    }

    std::size_t emitJump(std::uint8_t condition, const std::string& target) {
        if (condition == 0xFF) {
            emit8(0xE9);
        } else {
            emit8(0x0F);
            emit8(condition);
        }
        const std::size_t offset = code_.size();
        emit32(0);
        branchFixups_.push_back({offset, target});
        return offset;
    }

    void emitCall(const std::string& target) {
        emit8(0xE8);
        const std::size_t offset = code_.size();
        emit32(0);
        callFixups_.push_back({offset, target});
    }

    void emitImportCall(
        const std::string& name,
        const std::string& library = "KERNEL32.dll"
    ) {
        emit8(0xFF);
        emit8(0x15);
        const std::size_t offset = code_.size();
        emit32(0);
        importFixups_.push_back({offset, name, library});
    }

    static bool parseInteger(const std::string& text, std::int64_t& value) {
        if (text.empty()) return false;

        int base = 10;
        const char* begin = text.data();
        const char* end = begin + text.size();
        bool negative = false;
        if (*begin == '-') {
            negative = true;
            ++begin;
        } else if (*begin == '+') {
            ++begin;
        }
        if (begin == end) return false;
        if (end - begin > 2 && begin[0] == '0' &&
            (begin[1] == 'x' || begin[1] == 'X' ||
             begin[1] == 'b' || begin[1] == 'B')) {
            base = (begin[1] == 'b' || begin[1] == 'B') ? 2 : 16;
            begin += 2;
        }

        std::uint64_t unsignedValue = 0;
        const auto result = std::from_chars(begin, end, unsignedValue, base);
        if (result.ec != std::errc{} || result.ptr != end) {
            double floatingValue = 0.0;
            const auto floatingResult = std::from_chars(
                text.data(),
                text.data() + text.size(),
                floatingValue,
                std::chars_format::general
            );
            constexpr double int64UpperBound = 9223372036854775808.0;
            constexpr double int64LowerBound = -9223372036854775808.0;
            if (floatingResult.ec != std::errc{} ||
                floatingResult.ptr != text.data() + text.size() ||
                !std::isfinite(floatingValue) ||
                std::trunc(floatingValue) != floatingValue ||
                floatingValue < int64LowerBound ||
                floatingValue >= int64UpperBound) {
                return false;
            }
            value = static_cast<std::int64_t>(floatingValue);
            return true;
        }
        if (negative) {
            if (unsignedValue > (std::uint64_t{1} << 63)) return false;
            value = unsignedValue == (std::uint64_t{1} << 63)
                ? std::numeric_limits<std::int64_t>::min()
                : -static_cast<std::int64_t>(unsignedValue);
        } else {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            value = static_cast<std::int64_t>(unsignedValue);
        }
        return true;
    }

    std::size_t addString(const std::string& value) {
        const auto found = stringOffsets_.find(value);
        if (found != stringOffsets_.end()) return found->second;
        while (data_.size() % alignof(std::uint64_t) != 0) {
            data_.emit8(0);
        }
        const std::size_t offset = data_.size();
        data_.emitBytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
        data_.emit8(0);
        stringOffsets_.emplace(value, offset);
        return offset;
    }

    std::int32_t slot(const std::string& name) const {
        const auto found = slots_.find(name);
        if (found == slots_.end()) {
            throw std::runtime_error("native backend: undefined IR value '" + name + "'");
        }
        return found->second;
    }

    void loadValue(const Value& value, std::uint8_t destination) {
        if (globalOffsets_.contains(value.name)) {
            loadGlobal(value.name, destination);
            return;
        }
        const auto found = slots_.find(value.name);
        if (found != slots_.end()) {
            movLoad(destination, found->second);
            return;
        }
        if (value.kind == Value::Kind::String || value.kind == Value::Kind::Character) {
            if (value.name.find('\0') != std::string::npos) {
                throw std::runtime_error("native backend does not support embedded NUL in strings");
            }
            leaRip(destination, addString(value.name));
            return;
        }
        if (value.kind == Value::Kind::Null) {
            movImm(destination, 0);
            return;
        }
        if (value.name == "true" || value.name == "True") {
            movImm(destination, 1);
            return;
        }
        if (value.name == "false" || value.name == "False" ||
            value.name == "null" || value.name == "Null") {
            movImm(destination, 0);
            return;
        }
        if (value.kind == Value::Kind::Number) {
            double number = 0.0;
            if (!zplus::numeric::parseLiteral(value.name, number)) {
                throw std::runtime_error(
                    "native backend: invalid numeric literal '" + value.name + "'");
            }
            movImm(destination, std::bit_cast<std::uint64_t>(number));
            return;
        }
        std::int64_t integer = 0;
        if (parseInteger(value.name, integer)) {
            movImm(destination, static_cast<std::uint64_t>(integer));
            return;
        }
        if (value.kind == Value::Kind::Boolean) {
            movImm(destination, value.name == "true" || value.name == "True" ? 1 : 0);
            return;
        }
        if (value.kind == Value::Kind::Number) {
            throw std::runtime_error("native backend: unsupported integer literal '" + value.name + "'");
        }
        if (value.kind == Value::Kind::Identifier) {
            throw std::runtime_error("native backend: undefined IR value '" + value.name + "'");
        }
        throw std::runtime_error("native backend: untyped IR value '" + value.name + "'");
    }

    bool isStringValue(const Value& value) const {
        const std::string type = inferredType(value);
        return type == "string" || type == "char" || type == "character";
    }

    std::pair<const zplus::ir::StructType*, std::size_t> structField(
        const Value& object,
        const Value& fieldName
    ) const {
        const auto* structure =
            module_.findStructType(inferredType(object));
        if (!structure) {
            throw std::runtime_error(
                "native backend: member access requires a known struct type");
        }
        const auto field = std::find_if(
            structure->fields.begin(), structure->fields.end(),
            [&fieldName](const auto& candidate) {
                return candidate.name == fieldName.name;
            });
        if (field == structure->fields.end()) {
            throw std::runtime_error(
                "native backend: struct '" + structure->name +
                "' has no field '" + fieldName.name + "'");
        }
        return {structure, static_cast<std::size_t>(
                               std::distance(structure->fields.begin(), field))};
    }

    std::string inferredType(const Value& value) const {
        if (value.kind == Value::Kind::String ||
            value.kind == Value::Kind::Character) {
            return "string";
        }
        if (value.kind == Value::Kind::Number) return "number";
        if (value.kind == Value::Kind::Boolean) return "bool";
        if (value.kind == Value::Kind::Null) return "null";
        if (const auto found = valueTypes_.find(value.name);
            found != valueTypes_.end()) {
            return found->second;
        }
        if (const auto found = globalTypes_.find(value.name);
            found != globalTypes_.end()) {
            return found->second;
        }
        return {};
    }

    std::string standardLibraryReturnType(const std::string& callee) const {
        const std::string prefix = "__zstdlib_";
        if (callee.rfind(prefix, 0) != 0) return {};
        const std::string operation = callee.substr(prefix.size());

        if (operation.rfind("math_", 0) == 0) return "number";
        if (operation.rfind("string_", 0) == 0) {
            const std::string name = operation.substr(7);
            if (name == "contains" || name == "startsWith" || name == "endsWith") {
                return "bool";
            }
            if (name == "length") return "number";
            if (name == "split") return "string[]";
            return "string";
        }
        if (operation.rfind("collections_", 0) == 0) {
            const std::string name = operation.substr(12);
            if (name == "reverse" || name == "sort" || name == "unique") {
                return "number[]";
            }
            if (name == "contains" || name == "isEmpty") return "bool";
            return "number";
        }
        if (operation.rfind("random_", 0) == 0) {
            const std::string name = operation.substr(7);
            if (name == "boolean") return "bool";
            if (name == "shuffle") return "number[]";
            if (name == "seed") return "void";
            return "number";
        }
        if (operation.rfind("path_", 0) == 0) {
            return operation == "path_isAbsolute" ? "bool" : "string";
        }
        if (operation.rfind("file_", 0) == 0) {
            const std::string name = operation.substr(5);
            if (name == "read") return "string";
            if (name == "listDirectory") return "string[]";
            if (name == "size") return "number";
            if (name == "write" || name == "append") return "void";
            return "bool";
        }
        if (operation.rfind("crypto_", 0) == 0 ||
            operation == "process_run" ||
            operation == "registry_read" ||
            operation == "registry_valueKind" ||
            operation == "system_platform" ||
            operation == "system_architecture" ||
            operation == "system_osVersion" ||
            operation == "system_executablePath" ||
            operation == "system_selectFolder" ||
            operation == "system_shortcutTarget" ||
            operation == "system_shortcutArguments") {
            return "string";
        }
        if (operation == "system_availableSpace" ||
            operation == "system_messageBox") return "number";
        if (operation.rfind("system_", 0) == 0 ||
            operation.rfind("registry_", 0) == 0) return "bool";
        if (operation.rfind("env_", 0) == 0) {
            const std::string name = operation.substr(4);
            if (name == "get" || name == "homeDirectory" ||
                name == "currentDirectory") return "string";
            if (name == "set") return "void";
            return "bool";
        }
        if (operation.rfind("time_", 0) == 0) {
            if (operation == "time_format") return "string";
            if (operation == "time_sleep") return "void";
            return "number";
        }
        if (operation.rfind("args_", 0) == 0) {
            if (operation == "args_get") return "string";
            if (operation == "args_all") return "string[]";
            if (operation == "args_has") return "bool";
            return "number";
        }
        if (operation.rfind("log_", 0) == 0) return "void";
        return {};
    }

    static std::string arrayElementType(const std::string& type) {
        if (type.size() > 2 && type.ends_with("[]")) {
            return type.substr(0, type.size() - 2);
        }
        return {};
    }

    bool setInferredType(const std::string& name, const std::string& type) {
        if (name.empty() || type.empty()) return false;
        auto& current = valueTypes_[name];
        if (current == type) return false;
        if (!current.empty() && current != "unknown") return false;
        current = type;
        return true;
    }

    void storeResult(const Instruction& instruction, std::uint8_t source = RAX) {
        if (!instruction.result.empty()) {
            movStore(slot(instruction.result), source);
        }
    }

    void storeNumericCallResult(const Instruction& instruction) {
        if (!instruction.result.empty()) {
            gprMoveFromXmm(RAX, 0);
            storeResult(instruction);
        }
    }

    void requireOperands(const Instruction& instruction, std::size_t count) const {
        if (instruction.operands.size() < count) {
            throw std::runtime_error("native backend: IR opcode '" + instruction.opcode +
                                     "' has too few operands");
        }
    }

    std::vector<std::uint8_t> argumentRegisters() const {
        return windowsAbi_
            ? std::vector<std::uint8_t>{RCX, RDX, R8, R9}
            : std::vector<std::uint8_t>{RDI, RSI, RDX, RCX, R8, R9};
    }

    bool emitWriteCall(
        const Instruction& instruction,
        const std::string& functionName
    ) {
        requireOperands(instruction, 2);
        const std::string& callee = instruction.operands[0].name;
        if (callee != "say" && callee != "printz" && callee != "printm") {
            return false;
        }
        bool hasNumericArgument = false;
        for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
            if (isNumeric(instruction.operands[index])) hasNumericArgument = true;
        }
        if (hasNumericArgument && !windowsAbi_) {
            throw std::runtime_error(
                "native numeric output currently requires the Windows x64 runtime");
        }
        if (hasNumericArgument) {
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
        }
        for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
            const Value& value = instruction.operands[index];
            const std::uint8_t argument = windowsAbi_ ? RCX : RDI;
            if (isNumeric(value)) {
                loadValue(value, RCX);
                emitImportCall("zpg_numberText", "zgraphics_runtime.dll");
                movReg(RCX, RAX);
                emitImportCall("zpg_stringData", "zgraphics_runtime.dll");
                movReg(argument, RAX);
                usesManagedMemory_ = true;
                usesZGraphicsRuntime_ = true;
            } else if (isStringValue(value)) {
                loadValue(value, argument);
                if (usesManagedMemory_) {
                    emitImportCall("zpg_stringData", "zgraphics_runtime.dll");
                    movReg(argument, RAX);
                }
            } else {
                throw std::runtime_error(
                    "native backend supports string and numeric arguments only for '" + callee +
                    "'; received '" + value.name + "' with type '" +
                    inferredType(value) + "' in function '" + functionName + "'");
            }
            const bool appendNewline =
                callee == "say" && index + 1 == instruction.operands.size();
            movImm(windowsAbi_ ? RDX : RSI, appendNewline ? 1 : 0);
            emitCall("__zp_write");
        }
        if (!instruction.result.empty()) {
            movImm(RAX, 0);
            storeResult(instruction);
        }
        return true;
    }

    void emitCallInstruction(
        const Instruction& instruction,
        const std::string& functionName
    ) {
        requireOperands(instruction, 1);
        const std::string& callee = instruction.operands[0].name;
        if (callee == "say" || callee == "printz" || callee == "printm") {
            emitWriteCall(instruction, functionName);
            return;
        }
        const bool graphicsBuiltin = callee.rfind("__zgraphics_", 0) == 0;
        const bool audioBuiltin = callee.rfind("__zaudio_", 0) == 0;
        const bool physicsBuiltin = callee.rfind("__zphysics_", 0) == 0;
        const bool stringBuiltin = callee == "__zplus_stringConcat";
        static const std::unordered_map<std::string, std::string> arraySymbols{
            {"push", "zpg_arrayPush"},
            {"pop", "zpg_arrayPop"},
            {"clear", "zpg_arrayClear"},
            {"insert", "zpg_arrayInsert"},
            {"remove", "zpg_arrayRemove"},
            {"contains", "zpg_arrayContains"},
        };
        if (const auto arrayBuiltin = arraySymbols.find(callee);
            arrayBuiltin != arraySymbols.end() && !module_.findFunction(callee)) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: dynamic array operations require the Windows x64 managed runtime");
            }
            const std::size_t expected =
                callee == "push" || callee == "contains" || callee == "remove" ? 2 :
                callee == "insert" ? 3 : 1;
            if (instruction.operands.size() != expected + 1) {
                throw std::runtime_error(
                    "native backend: " + callee + " expects " +
                    std::to_string(expected) + " argument(s)");
            }
            if (callee == "insert") {
                loadValue(instruction.operands[1], RCX);
                loadValue(instruction.operands[2], RDX);
                loadValue(instruction.operands[3], R8);
            } else if (callee == "remove") {
                loadValue(instruction.operands[1], RCX);
                loadValue(instruction.operands[2], RDX);
            } else {
                emitNativeCallArguments(instruction);
            }
            emitImportCall(arrayBuiltin->second, "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            storeResult(instruction);
            return;
        }
        if (callee.rfind("__zmemory_", 0) == 0) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native managed memory is currently supported only by the Windows x64 runtime"
                );
            }
            static const std::unordered_map<std::string, std::string> symbols{
                {"__zmemory_heapUsed", "zpg_memory_heapUsed"},
                {"__zmemory_heapObjects", "zpg_memory_heapObjects"},
                {"__zmemory_collect", "zpg_memory_collect"},
                {"__zmemory_collectionCount", "zpg_memory_collectionCount"},
                {"__zmemory_setThreshold", "zpg_memory_setThreshold"},
            };
            const auto symbol = symbols.find(callee);
            if (symbol == symbols.end()) {
                throw std::runtime_error(
                    "native backend: unsupported managed-memory API '" + callee + "'");
            }
            emitNativeCallArguments(instruction);
            emitImportCall(symbol->second, "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            storeResult(instruction);
            return;
        }
        if (audioBuiltin) {
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpaudio_" + callee.substr(std::string("__zaudio_").size()),
                windowsAbi_ ? "zaudio_runtime.dll" : "libzaudio_runtime.so");
            storeResult(instruction);
            return;
        }
        if (physicsBuiltin) {
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpphysics_" + callee.substr(std::string("__zphysics_").size()),
                windowsAbi_ ? "zphysics_runtime.dll" : "libzphysics_runtime.so");
            storeResult(instruction);
            return;
        }
        if (callee.rfind("__zstdlib_", 0) == 0) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native standard-library runtime is currently supported only on Windows x64"
                );
            }
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpg_stdlib_" + callee.substr(std::string("__zstdlib_").size()),
                "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (callee.rfind("__zjson_", 0) == 0) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native JSON standard-library runtime is currently supported only on Windows x64"
                );
            }
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpg_json_" + callee.substr(std::string("__zjson_").size()),
                "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (callee.rfind("__zhttp_", 0) == 0) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native HTTP server runtime is currently supported only on Windows x64"
                );
            }
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpg_http_" + callee.substr(std::string("__zhttp_").size()),
                "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (callee.rfind("__zsqlite_", 0) == 0) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native SQLite runtime is currently supported only on Windows x64"
                );
            }
            emitNativeCallArguments(instruction);
            emitImportCall(
                "zpg_sqlite_" + callee.substr(std::string("__zsqlite_").size()),
                "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (graphicsBuiltin || stringBuiltin) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: ZGraphics and native string concatenation are supported "
                    "only by the Windows x64 runtime"
                );
            }
            const std::string symbol = graphicsBuiltin
                ? "zpg_" + callee.substr(std::string("__zgraphics_").size())
                : "zpg_concat";
            emitNativeCallArguments(instruction);
            emitImportCall(symbol, "zgraphics_runtime.dll");
            usesZGraphicsRuntime_ = true;
            if (stringBuiltin || callee == "__zgraphics_numberText" ||
                callee == "__zgraphics_eventText") {
                usesManagedMemory_ = true;
            }
            storeResult(instruction);
            return;
        }
        if (callee == "length" && !module_.findFunction(callee)) {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: array length requires the Windows x64 managed runtime");
            }
            if (instruction.operands.size() != 2) {
                throw std::runtime_error(
                    "native backend: length expects exactly one array argument");
            }
            loadValue(instruction.operands[1], RCX);
            emitImportCall("zpg_arrayLength", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            storeResult(instruction);
            return;
        }
        const Function* target = module_.findFunction(callee);
        if (!target) {
            throw std::runtime_error("native backend: unsupported external call '" + callee + "'");
        }
        emitNativeFunctionArguments(instruction, *target);
        emitCall(target->name);
        if (target->returnType == "number") storeNumericCallResult(instruction);
        else storeResult(instruction);
    }

    void emitNativeFunctionArguments(
        const Instruction& instruction,
        const Function& target
    ) {
        const std::size_t count = instruction.operands.size() - 1;
        if (count != target.parameters.size()) {
            throw std::runtime_error("native backend: argument count mismatch for '" +
                                     target.name + "'");
        }
        if (windowsAbi_) {
            if (count > 32) {
                throw std::runtime_error("native backend: calls may have at most 32 arguments");
            }
            for (std::size_t i = 0; i < count; ++i) {
                const Value& value = instruction.operands[i + 1];
                if (target.parameters[i].type == "number" && i < 4) {
                    loadXmmValue(value, static_cast<std::uint8_t>(i));
                } else {
                    loadValue(value, RAX);
                    if (i < 4) {
                        movReg(argumentRegisters()[i], RAX);
                    } else {
                        movStoreRspDisp(
                            32 + static_cast<std::int32_t>(
                                (i - 4) * sizeof(std::uint64_t)),
                            RAX
                        );
                    }
                }
            }
            return;
        }

        std::size_t integerIndex = 0;
        std::size_t floatIndex = 0;
        const auto integerRegisters = argumentRegisters();
        for (std::size_t i = 0; i < count; ++i) {
            const Value& value = instruction.operands[i + 1];
            if (target.parameters[i].type == "number") {
                if (floatIndex >= 8) {
                    throw std::runtime_error(
                        "native backend: more than eight floating-point parameters are unsupported");
                }
                loadXmmValue(value, static_cast<std::uint8_t>(floatIndex++));
            } else {
                if (integerIndex >= integerRegisters.size()) {
                    throw std::runtime_error(
                        "native backend: more than six integer/pointer parameters are unsupported");
                }
                loadValue(value, integerRegisters[integerIndex++]);
            }
        }
    }

    void emitFloatingComparison(
        const Instruction& instruction,
        const std::string& opcode
    ) {
        loadXmmValue(instruction.operands[0], 0);
        loadXmmValue(instruction.operands[1], 1);
        xmmCompare(0, 1);
        movImm(RAX, 0);
        movImm(R10, 0);
        if (opcode == "cmp_eq") {
            setConditionTo(0x94, RAX);
            setConditionTo(0x9B, R10);
            binary(0x21, RAX, R10);
        } else if (opcode == "cmp_ne") {
            setConditionTo(0x95, RAX);
            setConditionTo(0x9A, R10);
            binary(0x09, RAX, R10);
        } else if (opcode == "cmp_lt") {
            setConditionTo(0x92, RAX);
            setConditionTo(0x9B, R10);
            binary(0x21, RAX, R10);
        } else if (opcode == "cmp_le") {
            setConditionTo(0x96, RAX);
            setConditionTo(0x9B, R10);
            binary(0x21, RAX, R10);
        } else if (opcode == "cmp_gt") {
            setConditionTo(0x97, RAX);
        } else if (opcode == "cmp_ge") {
            setConditionTo(0x93, RAX);
        } else {
            throw std::runtime_error(
                "native backend: unsupported floating comparison '" + opcode + "'");
        }
        storeResult(instruction);
    }

    void emitNativeCallArguments(const Instruction& instruction) {
        const std::size_t count = instruction.operands.size() - 1;
        const auto registers = argumentRegisters();
        if (count > 32 || (!windowsAbi_ && count > registers.size())) {
            const std::string& callee = instruction.operands[0].name;
            throw std::runtime_error("native backend: too many arguments for native call '" +
                                     callee + "'");
        }
        if (windowsAbi_) {
            for (std::size_t i = registers.size(); i < count; ++i) {
                loadValue(instruction.operands[i + 1], RAX);
                movStoreRspDisp(32 + static_cast<std::int32_t>(
                    (i - registers.size()) * sizeof(std::uint64_t)), RAX);
            }
        }

        for (std::size_t i = 0; i < std::min(count, registers.size()); ++i) {
            loadValue(instruction.operands[i + 1], registers[i]);
        }
    }

    void movStoreRspDisp(std::int32_t displacement, std::uint8_t source) {
        rex(true, source, RSP);
        emit8(0x89);
        modrm(2, source, RSP);
        emit8(0x24);
        emit32(displacement);
    }

    void movLoadRspDisp(std::uint8_t destination, std::int32_t displacement) {
        rex(true, destination, RSP);
        emit8(0x8B);
        modrm(2, destination, RSP);
        emit8(0x24);
        emit32(displacement);
    }

    std::uint8_t comparisonCondition(const std::string& opcode) const {
        if (opcode == "cmp_eq") return 0x94;
        if (opcode == "cmp_ne") return 0x95;
        if (opcode == "cmp_lt") return 0x9C;
        if (opcode == "cmp_le") return 0x9E;
        if (opcode == "cmp_gt") return 0x9F;
        if (opcode == "cmp_ge") return 0x9D;
        throw std::runtime_error("native backend: unsupported comparison opcode '" + opcode + "'");
    }

    void emitInstruction(const Instruction& instruction, const std::string& functionName) {
        const std::string& op = instruction.opcode;
        if (op == "scope_push" || op == "scope_pop") {
            return;
        }
        if (op == "try_push" || op == "try_pop" || op == "throw" ||
            op == "defer_push") {
            std::string message =
                "native backend: exception handling and deferred cleanup are "
                "not supported";
            if (!instruction.location.sourceFile.empty()) {
                message += " at " + instruction.location.sourceFile;
                if (instruction.location.line > 0) {
                    message += ":" + std::to_string(instruction.location.line);
                    if (instruction.location.column > 0) {
                        message += ":" + std::to_string(instruction.location.column);
                    }
                }
            }
            message += " in function '" + functionName + "'";
            throw std::runtime_error(std::move(message));
        }
        if (op == "struct_new") {
            requireOperands(instruction, 1);
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed structs require the Windows x64 runtime");
            }
            const std::size_t typeId =
                module_.structTypeId(instruction.operands[0].name);
            if (typeId == 0) {
                throw std::runtime_error(
                    "native backend: unknown struct type '" +
                    instruction.operands[0].name + "'");
            }
            const std::size_t count = instruction.operands.size() - 1;
            for (std::size_t i = 0; i < count; ++i) {
                loadValue(instruction.operands[i + 1], RAX);
                movStoreRspDisp(
                    32 + static_cast<std::int32_t>(i * sizeof(std::uint64_t)), RAX);
            }
            movImm(RCX, typeId);
            leaRsp(RDX, 32);
            movImm(R8, count);
            emitImportCall("zpg_structCreate", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (op == "member") {
            requireOperands(instruction, 2);
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed struct members require the Windows x64 runtime");
            }
            const auto [structure, fieldIndex] =
                structField(instruction.operands[0], instruction.operands[1]);
            (void)structure;
            loadValue(instruction.operands[0], RCX);
            movImm(RDX, fieldIndex);
            emitImportCall("zpg_structGet", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (op == "member_store") {
            requireOperands(instruction, 3);
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed struct members require the Windows x64 runtime");
            }
            const auto [structure, fieldIndex] =
                structField(instruction.operands[0], instruction.operands[1]);
            (void)structure;
            loadValue(instruction.operands[0], RCX);
            movImm(RDX, fieldIndex);
            loadValue(instruction.operands[2], R8);
            emitImportCall("zpg_structSet", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            return;
        }
        if (op == "is") {
            requireOperands(instruction, 2);
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: runtime struct type checks require the Windows x64 runtime");
            }
            const std::size_t typeId =
                module_.structTypeId(instruction.operands[1].name);
            if (typeId == 0) {
                throw std::runtime_error(
                    "native backend: unknown struct type '" +
                    instruction.operands[1].name + "'");
            }
            loadValue(instruction.operands[0], RCX);
            movImm(RDX, typeId);
            emitImportCall("zpg_structIsType", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            usesZGraphicsRuntime_ = true;
            storeResult(instruction);
            return;
        }
        if (op.empty() || op == "freeze" || op == "param") return;
        if (op == "alloc") {
            requireOperands(instruction, 1);
            return;
        }
        if (op == "mov" || op == "load") {
            requireOperands(instruction, 1);
            if (isNumeric(instruction.operands[0]) ||
                (!instruction.result.empty() &&
                 isNumericName(instruction.result))) {
                loadXmmValue(instruction.operands[0], 0);
                storeXmmResult(instruction, 0);
                return;
            }
            loadValue(instruction.operands[0], RAX);
            storeResult(instruction);
            return;
        }
        if (op == "store") {
            requireOperands(instruction, 2);
            if (isNumericName(instruction.operands[0].name) ||
                isNumeric(instruction.operands[1])) {
                loadXmmValue(instruction.operands[1], 0);
                storeXmmVariable(instruction.operands[0].name, 0);
                return;
            }
            loadValue(instruction.operands[1], RAX);
            storeVariable(instruction.operands[0].name, RAX);
            const std::string& source = instruction.operands[1].name;
            const auto useCount = temporaryUseCounts_.find(source);
            if (usesManagedMemory_ &&
                source.starts_with('%') &&
                stringSlots_.contains(source) &&
                useCount != temporaryUseCounts_.end() &&
                useCount->second == 1) {
                movStoreZero(slot(source));
            }
            return;
        }
        if (op == "add" || op == "sub" || op == "mul" || op == "div" || op == "mod" ||
            op == "bitand" || op == "bitor" || op == "bitxor" ||
            op == "shl" || op == "shr" || op == "and" || op == "or" ||
            op.starts_with("cmp_")) {
            requireOperands(instruction, 2);
            if (op == "add" &&
                (isStringValue(instruction.operands[0]) || isStringValue(instruction.operands[1]))) {
                if (!windowsAbi_ || !isStringValue(instruction.operands[0]) ||
                    !isStringValue(instruction.operands[1])) {
                    throw std::runtime_error(
                        "native backend: string concatenation requires two strings and the "
                        "Windows x64 runtime"
                    );
                }
                loadValue(instruction.operands[0], RCX);
                loadValue(instruction.operands[1], RDX);
                emitImportCall("zpg_concat", "zgraphics_runtime.dll");
                usesZGraphicsRuntime_ = true;
                storeResult(instruction);
                return;
            }
            if (op.starts_with("cmp_") &&
                (isStringValue(instruction.operands[0]) || isStringValue(instruction.operands[1]))) {
                if (!windowsAbi_ ||
                    !isStringValue(instruction.operands[0]) ||
                    !isStringValue(instruction.operands[1])) {
                    throw std::runtime_error(
                        "native string comparison requires two strings and the Windows x64 runtime");
                }
                loadValue(instruction.operands[0], RCX);
                loadValue(instruction.operands[1], RDX);
                emitImportCall("zpg_compareStrings", "zgraphics_runtime.dll");
                movImm(R10, 0);
                cmp(RAX, R10);
                setCondition(comparisonCondition(op));
                usesManagedMemory_ = true;
                usesZGraphicsRuntime_ = true;
                storeResult(instruction);
                return;
            }
            if (op.starts_with("cmp_")) {
                const std::string leftType = inferredType(instruction.operands[0]);
                const std::string rightType = inferredType(instruction.operands[1]);
                if (!arrayElementType(leftType).empty() ||
                    !arrayElementType(rightType).empty()) {
                    throw std::runtime_error(
                        "native backend does not support array comparison");
                }
                if (leftType == "number" && rightType == "number") {
                    emitFloatingComparison(instruction, op);
                    return;
                }
            }
            if ((op == "bitand" || op == "bitor" || op == "bitxor" ||
                 op == "shl" || op == "shr") &&
                isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1])) {
                loadXmmValue(instruction.operands[0], 0);
                loadXmmValue(instruction.operands[1], 1);
                convertXmmToSignedInteger(RAX, 0);
                convertXmmToSignedInteger(R10, 1);
                if (op == "bitand") binary(0x21, RAX, R10);
                else if (op == "bitor") binary(0x09, RAX, R10);
                else if (op == "bitxor") binary(0x31, RAX, R10);
                else {
                    movReg(RCX, R10);
                    rex(true, op == "shl" ? 4 : 7, RAX);
                    emit8(0xD3);
                    modrm(3, op == "shl" ? 4 : 7, RAX);
                }
                convertSignedIntegerToXmm(0, RAX);
                storeXmmResult(instruction, 0);
                return;
            }
            if (isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1]) &&
                (op == "add" || op == "sub" || op == "mul" || op == "div")) {
                loadXmmValue(instruction.operands[0], 0);
                loadXmmValue(instruction.operands[1], 1);
                const std::uint8_t opcode =
                    op == "add" ? 0x58 : op == "sub" ? 0x5C :
                    op == "mul" ? 0x59 : 0x5E;
                xmmBinary(opcode, 0, 1);
                storeXmmResult(instruction, 0);
                return;
            }
            if (op == "mod" && isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1])) {
                if (!windowsAbi_) {
                    throw std::runtime_error(
                        "native floating-point remainder currently requires the Windows x64 runtime");
                }
                loadValue(instruction.operands[0], RCX);
                loadValue(instruction.operands[1], RDX);
                emitImportCall("zpg_numberRemainder", "zgraphics_runtime.dll");
                usesZGraphicsRuntime_ = true;
                storeResult(instruction);
                return;
            }
            loadValue(instruction.operands[0], RAX);
            loadValue(instruction.operands[1], R10);
            if (op == "add") binary(0x01, RAX, R10);
            else if (op == "sub") binary(0x29, RAX, R10);
            else if (op == "mul") imul(RAX, R10);
            else if (op == "div" || op == "mod") {
                emit8(0x48); emit8(0x99);
                rex(true, 7, R10); emit8(0xF7); modrm(3, 7, R10);
                if (op == "mod") movReg(RAX, RDX);
            } else if (op == "bitand" || op == "and") binary(0x21, RAX, R10);
            else if (op == "bitor" || op == "or") binary(0x09, RAX, R10);
            else if (op == "bitxor") binary(0x31, RAX, R10);
            else if (op == "shl" || op == "shr") {
                movReg(RCX, R10);
                rex(true, op == "shl" ? 4 : 7, RAX);
                emit8(0xD3);
                modrm(3, op == "shl" ? 4 : 7, RAX);
            } else {
                cmp(RAX, R10);
                setCondition(comparisonCondition(op));
            }
            storeResult(instruction);
            return;
        }
        if (op == "pos" || op == "neg" || op == "not" || op == "bitnot") {
            requireOperands(instruction, 1);
            if ((op == "pos" || op == "neg") &&
                isNumeric(instruction.operands[0])) {
                loadXmmValue(instruction.operands[0], 0);
                if (op == "neg") {
                    gprMoveFromXmm(RAX, 0);
                    movImm(R10, 0x8000000000000000ULL);
                    binary(0x31, RAX, R10);
                    xmmMoveFromGpr(0, RAX);
                }
                storeXmmResult(instruction, 0);
                return;
            }
            if (op == "bitnot" && isNumeric(instruction.operands[0])) {
                loadXmmValue(instruction.operands[0], 0);
                convertXmmToSignedInteger(RAX, 0);
                emit8(0x48); emit8(0xF7); emit8(0xD0);
                convertSignedIntegerToXmm(0, RAX);
                storeXmmResult(instruction, 0);
                return;
            }
            loadValue(instruction.operands[0], RAX);
            if (op == "neg") {
                emit8(0x48); emit8(0xF7); emit8(0xD8);
            } else if (op == "not") {
                emit8(0x48); emit8(0x83); emit8(0xF0); emit8(1);
            } else if (op == "bitnot") {
                emit8(0x48); emit8(0xF7); emit8(0xD0);
            }
            storeResult(instruction);
            return;
        }
        if (op == "pre_inc" || op == "post_inc" || op == "pre_dec" || op == "post_dec" || op == "dec") {
            requireOperands(instruction, 1);
            if (isNumeric(instruction.operands[0])) {
                const bool post = op == "post_inc" || op == "post_dec";
                const bool decrement = op == "pre_dec" || op == "post_dec" || op == "dec";
                loadXmmVariable(instruction.operands[0].name, 0);
                if (post) storeXmmResult(instruction, 0);
                movImm(RAX, std::bit_cast<std::uint64_t>(1.0));
                xmmMoveFromGpr(1, RAX);
                xmmBinary(decrement ? 0x5C : 0x58, 0, 1);
                storeXmmVariable(instruction.operands[0].name, 0);
                if (!post && op != "dec") storeXmmResult(instruction, 0);
                return;
            }
            loadVariable(instruction.operands[0].name, RAX);
            if (op == "post_inc" || op == "post_dec") movReg(R10, RAX);
            emit8(0x48); emit8(0xFF);
            modrm(3, (op == "pre_dec" || op == "post_dec" || op == "dec") ? 1 : 0, RAX);
            storeVariable(instruction.operands[0].name, RAX);
            if (op == "pre_inc" || op == "pre_dec" || op == "post_inc" || op == "post_dec") {
                storeResult(instruction, (op == "post_inc" || op == "post_dec") ? R10 : RAX);
            }
            return;
        }
        if (op.ends_with("_assign")) {
            requireOperands(instruction, 2);
            if (op == "add_assign" &&
                (isStringValue(instruction.operands[0]) ||
                 isStringValue(instruction.operands[1]))) {
                if (!windowsAbi_ || !isStringValue(instruction.operands[0]) ||
                    !isStringValue(instruction.operands[1])) {
                    throw std::runtime_error(
                        "native backend: string concatenation assignment requires two "
                        "strings and the Windows x64 runtime");
                }
                loadVariable(instruction.operands[0].name, RCX);
                loadValue(instruction.operands[1], RDX);
                emitImportCall("zpg_concat", "zgraphics_runtime.dll");
                usesManagedMemory_ = true;
                storeVariable(instruction.operands[0].name, RAX);
                return;
            }
            if ((op == "add_assign" || op == "sub_assign" ||
                 op == "mul_assign" || op == "div_assign") &&
                isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1])) {
                loadXmmVariable(instruction.operands[0].name, 0);
                loadXmmValue(instruction.operands[1], 1);
                const std::uint8_t opcode =
                    op == "add_assign" ? 0x58 : op == "sub_assign" ? 0x5C :
                    op == "mul_assign" ? 0x59 : 0x5E;
                xmmBinary(opcode, 0, 1);
                storeXmmVariable(instruction.operands[0].name, 0);
                return;
            }
            if (op == "mod_assign" &&
                isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1])) {
                if (!windowsAbi_) {
                    throw std::runtime_error(
                        "native floating-point remainder currently requires the Windows x64 runtime");
                }
                loadXmmVariable(instruction.operands[0].name, 0);
                gprMoveFromXmm(RCX, 0);
                loadValue(instruction.operands[1], RDX);
                emitImportCall("zpg_numberRemainder", "zgraphics_runtime.dll");
                storeVariable(instruction.operands[0].name, RAX);
                usesZGraphicsRuntime_ = true;
                return;
            }
            if ((op == "and_assign" || op == "or_assign" ||
                 op == "xor_assign" || op == "shl_assign" ||
                 op == "shr_assign") &&
                isNumeric(instruction.operands[0]) &&
                isNumeric(instruction.operands[1])) {
                loadXmmVariable(instruction.operands[0].name, 0);
                loadXmmValue(instruction.operands[1], 1);
                convertXmmToSignedInteger(RAX, 0);
                convertXmmToSignedInteger(R10, 1);
                if (op == "and_assign") binary(0x21, RAX, R10);
                else if (op == "or_assign") binary(0x09, RAX, R10);
                else if (op == "xor_assign") binary(0x31, RAX, R10);
                else {
                    movReg(RCX, R10);
                    rex(true, op == "shl_assign" ? 4 : 7, RAX);
                    emit8(0xD3);
                    modrm(3, op == "shl_assign" ? 4 : 7, RAX);
                }
                convertSignedIntegerToXmm(0, RAX);
                storeXmmVariable(instruction.operands[0].name, 0);
                return;
            }
            loadVariable(instruction.operands[0].name, RAX);
            loadValue(instruction.operands[1], R10);
            if (op == "add_assign") binary(0x01, RAX, R10);
            else if (op == "sub_assign") binary(0x29, RAX, R10);
            else if (op == "mul_assign") imul(RAX, R10);
            else if (op == "div_assign" || op == "mod_assign") {
                emit8(0x48); emit8(0x99);
                rex(true, 7, R10); emit8(0xF7); modrm(3, 7, R10);
                if (op == "mod_assign") movReg(RAX, RDX);
            } else if (op == "and_assign") binary(0x21, RAX, R10);
            else if (op == "or_assign") binary(0x09, RAX, R10);
            else if (op == "xor_assign") binary(0x31, RAX, R10);
            else if (op == "shl_assign" || op == "shr_assign") {
                movReg(RCX, R10);
                rex(true, op == "shl_assign" ? 4 : 7, RAX);
                emit8(0xD3);
                modrm(3, op == "shl_assign" ? 4 : 7, RAX);
            } else {
                throw std::runtime_error("native backend: unsupported IR opcode '" + op + "'");
            }
            storeVariable(instruction.operands[0].name, RAX);
            return;
        }
        if (op == "call") {
            emitCallInstruction(instruction, functionName);
            return;
        }
        if (op == "array") {
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed arrays require the Windows x64 runtime");
            }
            if (instruction.operands.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::int32_t>::max() - 32) /
                    static_cast<std::int32_t>(sizeof(std::uint64_t)))) {
                throw std::runtime_error("native backend: array literal is too large");
            }
            for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                loadValue(instruction.operands[i], RAX);
                movStoreRspDisp(
                    32 + static_cast<std::int32_t>(i * sizeof(std::uint64_t)), RAX);
            }
            leaRsp(RCX, 32);
            movImm(RDX, instruction.operands.size());
            emitImportCall("zpg_arrayCreate", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            storeResult(instruction);
            return;
        }
        if (op == "index") {
            requireOperands(instruction, 2);
            if (instruction.operands.size() != 2) {
                throw std::runtime_error("native backend: malformed array index IR");
            }
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed arrays require the Windows x64 runtime");
            }
            loadValue(instruction.operands[0], RCX);
            loadValue(instruction.operands[1], RDX);
            emitImportCall("zpg_arrayGet", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            storeResult(instruction);
            return;
        }
        if (op == "index_store") {
            requireOperands(instruction, 3);
            if (instruction.operands.size() != 3) {
                throw std::runtime_error("native backend: malformed indexed assignment IR");
            }
            if (!windowsAbi_) {
                throw std::runtime_error(
                    "native backend: managed arrays require the Windows x64 runtime");
            }
            loadValue(instruction.operands[0], RCX);
            loadValue(instruction.operands[1], RDX);
            loadValue(instruction.operands[2], R8);
            emitImportCall("zpg_arraySet", "zgraphics_runtime.dll");
            usesManagedMemory_ = true;
            return;
        }
        if (op == "select") {
            requireOperands(instruction, 3);
            const std::string falseLabel = "__select_false_" + std::to_string(uniqueLabel_++);
            const std::string endLabel = "__select_end_" + std::to_string(uniqueLabel_++);
            loadValue(instruction.operands[0], RAX);
            testRax();
            emitJump(0x84, falseLabel);
            loadValue(instruction.operands[1], RAX);
            emitJump(0xFF, endLabel);
            localLabels_[falseLabel] = code_.size();
            loadValue(instruction.operands[2], RAX);
            localLabels_[endLabel] = code_.size();
            storeResult(instruction);
            return;
        }
        if (op == "br") {
            requireOperands(instruction, 1);
            emitJump(0xFF, blockLabel(functionName, instruction.operands[0].name));
            return;
        }
        if (op == "condbr") {
            requireOperands(instruction, 3);
            loadValue(instruction.operands[0], RAX);
            testRax();
            emitJump(0x85, blockLabel(functionName, instruction.operands[1].name));
            emitJump(0xFF, blockLabel(functionName, instruction.operands[2].name));
            return;
        }
        if (op == "switch") {
            requireOperands(instruction, 2);
            if (instruction.operands.size() % 2 != 0) {
                throw std::runtime_error("native backend: malformed switch IR");
            }
            for (std::size_t i = 1; i + 1 < instruction.operands.size() - 1; i += 2) {
                loadValue(instruction.operands[0], RAX);
                loadValue(instruction.operands[i], R10);
                cmp(RAX, R10);
                emitJump(0x84, blockLabel(functionName, instruction.operands[i + 1].name));
            }
            emitJump(0xFF, blockLabel(functionName, instruction.operands.back().name));
            return;
        }
        if (op == "ret") {
            const Function* currentFunction = module_.findFunction(functionName);
            const bool numericReturn =
                currentFunction && currentFunction->returnType == "number";
            if (instruction.operands.empty()) {
                if (numericReturn) {
                    movImm(RAX, 0);
                    xmmMoveFromGpr(0, RAX);
                } else {
                    movImm(RAX, 0);
                }
            } else if (numericReturn) {
                loadXmmValue(instruction.operands[0], 0);
            } else {
                loadValue(instruction.operands[0], RAX);
            }
            if (usesManagedMemory_) {
                if (numericReturn) gprMoveFromXmm(RAX, 0);
                movStoreRspDisp(32, RAX);
                emitImportCall("zpg_popRoots", "zgraphics_runtime.dll");
                movLoadRspDisp(RAX, 32);
                if (numericReturn) xmmMoveFromGpr(0, RAX);
            }
            emit8(0xC9);
            emit8(0xC3);
            return;
        }
        throw std::runtime_error("native backend: unsupported IR opcode '" + op +
                                 "' in function '" + functionName + "'");
    }

    std::string blockLabel(const std::string& function, const std::string& block) const {
        return function + "::" + block;
    }

    void prepareFunction(const Function& function) {
        slots_.clear();
        stringSlots_.clear();
        valueTypes_.clear();
        temporaryUseCounts_.clear();
        localLabels_.clear();
        branchFixups_.clear();
        stackSize_ = 0;
        std::size_t outgoingStackSize = 48;
        for (const auto& parameter : function.parameters) {
            allocateSlot(parameter.name);
            if (!parameter.type.empty()) {
                valueTypes_[parameter.name] = parameter.type;
            }
            if (parameter.type == "string" || parameter.type == "char" ||
                parameter.type == "character") {
                stringSlots_.insert(parameter.name);
            }
        }
        for (const auto& [name, type] : globalTypes_) {
            valueTypes_.try_emplace(name, type);
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    if (!operand.name.empty() && operand.name.front() == '%') {
                        ++temporaryUseCounts_[operand.name];
                    }
                }
                if (instruction.opcode == "alloc" && !instruction.operands.empty()) {
                    if (!globalOffsets_.contains(instruction.operands[0].name)) {
                        allocateSlot(instruction.operands[0].name);
                    }
                }
                if (!instruction.result.empty()) allocateSlot(instruction.result);
                if (windowsAbi_ && instruction.opcode == "struct_new") {
                    if (instruction.operands.size() > 1) {
                        const std::size_t fieldCount =
                            instruction.operands.size() - 1;
                        if (fieldCount >
                            (static_cast<std::size_t>(
                                 std::numeric_limits<std::int32_t>::max()) - 32) /
                                sizeof(std::uint64_t)) {
                            throw std::runtime_error(
                                "native backend: struct has too many fields");
                        }
                        outgoingStackSize = std::max(
                            outgoingStackSize,
                            32 + fieldCount * sizeof(std::uint64_t));
                    }
                }
                if (windowsAbi_ && instruction.opcode == "call" &&
                    instruction.operands.size() > 5) {
                    const std::size_t argumentCount = instruction.operands.size() - 1;
                    if (argumentCount > 32) {
                        throw std::runtime_error("native backend: calls may have at most 32 arguments");
                    }
                    outgoingStackSize = std::max(
                        outgoingStackSize,
                        32 + (argumentCount - 4) * sizeof(std::uint64_t));
                }
                if (windowsAbi_ && instruction.opcode == "array") {
                    if (instruction.operands.size() >
                        (static_cast<std::size_t>(
                             std::numeric_limits<std::int32_t>::max()) - 32) /
                            sizeof(std::uint64_t)) {
                        throw std::runtime_error(
                            "native backend: array literal is too large");
                    }
                    outgoingStackSize = std::max(
                        outgoingStackSize,
                        32 + instruction.operands.size() * sizeof(std::uint64_t));
                }
            }
        }
        std::size_t instructionCount = 0;
        for (const auto& block : function.blocks) {
            instructionCount += block.instructions.size();
        }
        std::unordered_set<std::string> indexedValues;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.opcode == "index" && !instruction.result.empty()) {
                    indexedValues.insert(instruction.result);
                }
            }
        }
        for (std::size_t pass = 0; pass <= instructionCount; ++pass) {
            bool inferred = false;
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    const bool numericBinary =
                        instruction.opcode == "add" ||
                        instruction.opcode == "sub" ||
                        instruction.opcode == "mul" ||
                        instruction.opcode == "div" ||
                        instruction.opcode == "mod" ||
                        instruction.opcode.starts_with("cmp_");
                    if (numericBinary && instruction.operands.size() == 2) {
                        for (std::size_t index = 0; index < 2; ++index) {
                            const Value& candidate = instruction.operands[index];
                            const Value& other = instruction.operands[1 - index];
                            if (indexedValues.contains(candidate.name) &&
                                inferredType(candidate).empty() && isNumeric(other)) {
                                inferred |= setInferredType(candidate.name, "number");
                            }
                        }
                    }
                    if (instruction.opcode == "array" && !instruction.result.empty() &&
                        !instruction.operands.empty()) {
                        std::string elementType;
                        bool compatible = true;
                        for (const auto& operand : instruction.operands) {
                            const std::string type = inferredType(operand);
                            if (type.empty()) {
                                compatible = false;
                                break;
                            }
                            if (elementType.empty()) elementType = type;
                            else if (elementType != type) {
                                compatible = false;
                                break;
                            }
                        }
                        if (compatible && !elementType.empty()) {
                            inferred |= setInferredType(
                                instruction.result, elementType + "[]");
                        }
                    } else if (!instruction.result.empty() &&
                               (instruction.opcode == "add" ||
                                instruction.opcode == "sub" ||
                                instruction.opcode == "mul" ||
                                instruction.opcode == "div" ||
                                instruction.opcode == "mod" ||
                                instruction.opcode == "bitand" ||
                                instruction.opcode == "bitor" ||
                                instruction.opcode == "bitxor" ||
                                instruction.opcode == "shl" ||
                                instruction.opcode == "shr") &&
                               instruction.operands.size() == 2 &&
                               !isStringValue(instruction.operands[0]) &&
                               !isStringValue(instruction.operands[1])) {
                        inferred |= setInferredType(instruction.result, "number");
                    } else if (!instruction.result.empty() &&
                               (instruction.opcode == "neg" ||
                                instruction.opcode == "pos" ||
                                instruction.opcode == "bitnot" ||
                                instruction.opcode == "pre_inc" ||
                                instruction.opcode == "post_inc" ||
                                instruction.opcode == "pre_dec" ||
                                instruction.opcode == "post_dec" ||
                                instruction.opcode == "dec" ||
                                instruction.opcode.ends_with("_assign")) &&
                               !instruction.operands.empty() &&
                               (instruction.opcode.ends_with("_assign")
                                    ? isNumeric(instruction.operands[0])
                                    : isNumeric(instruction.operands[0]))) {
                        inferred |= setInferredType(instruction.result, "number");
                    } else if (!instruction.result.empty() &&
                               instruction.opcode.starts_with("cmp_")) {
                        inferred |= setInferredType(instruction.result, "bool");
                    } else if (instruction.opcode == "index" &&
                               instruction.operands.size() == 2) {
                        inferred |= setInferredType(
                            instruction.result,
                            arrayElementType(inferredType(instruction.operands[0])));
                    } else if ((instruction.opcode == "mov" ||
                                instruction.opcode == "load") &&
                               !instruction.result.empty() &&
                               !instruction.operands.empty()) {
                        inferred |= setInferredType(
                            instruction.result, inferredType(instruction.operands[0]));
                    } else if (instruction.opcode == "add" &&
                               !instruction.result.empty() &&
                               instruction.operands.size() == 2 &&
                               (isStringValue(instruction.operands[0]) ||
                                isStringValue(instruction.operands[1]))) {
                        inferred |= setInferredType(instruction.result, "string");
                    } else if (instruction.opcode == "select" &&
                               instruction.operands.size() == 3) {
                        const std::string trueType = inferredType(instruction.operands[1]);
                        if (trueType == inferredType(instruction.operands[2])) {
                            inferred |= setInferredType(instruction.result, trueType);
                        }
                    } else if (instruction.opcode == "struct_new" &&
                               !instruction.operands.empty()) {
                        inferred |= setInferredType(
                            instruction.result, instruction.operands[0].name);
                    } else if (instruction.opcode == "member" &&
                               instruction.operands.size() == 2) {
                        const zplus::ir::StructType* structure =
                            module_.findStructType(inferredType(instruction.operands[0]));
                        if (structure) {
                            const auto field = std::find_if(
                                structure->fields.begin(), structure->fields.end(),
                                [&instruction](const auto& candidate) {
                                    return candidate.name == instruction.operands[1].name;
                                });
                            if (field != structure->fields.end()) {
                                inferred |= setInferredType(
                                    instruction.result, field->type);
                            }
                        }
                    } else if (instruction.opcode == "store" &&
                               instruction.operands.size() == 2) {
                        const std::string& target = instruction.operands[0].name;
                        const std::string type = inferredType(instruction.operands[1]);
                        if (globalOffsets_.contains(target)) {
                            auto& current = globalTypes_[target];
                            if (!type.empty() && current != type) {
                                current = type;
                                inferred = true;
                            }
                        } else {
                            inferred |= setInferredType(target, type);
                        }
                    } else if (instruction.opcode == "call" &&
                               !instruction.result.empty() &&
                               !instruction.operands.empty()) {
                        if (const Function* target =
                                module_.findFunction(instruction.operands[0].name)) {
                            inferred |= setInferredType(
                                instruction.result, target->returnType);
                        } else if (instruction.operands[0].name == "length") {
                            inferred |= setInferredType(instruction.result, "number");
                        } else if (const std::string type =
                                       standardLibraryReturnType(
                                           instruction.operands[0].name);
                                   !type.empty()) {
                            inferred |= setInferredType(instruction.result, type);
                        }
                    }
                }
            }
            if (!inferred) break;
        }
        for (const auto& [name, type] : valueTypes_) {
            if (type == "string" || type == "char" || type == "character") {
                stringSlots_.insert(name);
            }
        }
        for (const auto& [name, type] : globalTypes_) {
            if (type == "string" || type == "char" || type == "character") {
                stringSlots_.insert(name);
            }
        }
        const std::size_t frameSize = static_cast<std::size_t>(stackSize_) +
            (windowsAbi_ ? outgoingStackSize : 0);
        if (frameSize > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 15)) {
            throw std::runtime_error("native backend: function stack frame is too large");
        }
        stackSize_ = static_cast<std::int32_t>(alignTo(frameSize, 16));
    }

    void identifyManagedMemoryUse() {
        for (std::size_t pass = 0; pass <= module_.functions.size(); ++pass) {
            const auto previousGlobalTypes = globalTypes_;
            for (const auto& function : module_.functions) {
                prepareFunction(function);
            }
            if (previousGlobalTypes == globalTypes_) break;
        }
        for (const auto& function : module_.functions) {
            prepareFunction(function);
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.opcode == "array" ||
                        instruction.opcode == "index" ||
                        instruction.opcode == "index_store") {
                        usesManagedMemory_ = true;
                    }
                    if (instruction.opcode == "call" &&
                        !instruction.operands.empty()) {
                        const std::string& callee = instruction.operands[0].name;
                        if ((callee == "say" || callee == "printz" || callee == "printm") &&
                            std::any_of(
                                instruction.operands.begin() + 1,
                                instruction.operands.end(),
                                [this](const Value& value) { return isNumeric(value); })) {
                            usesManagedMemory_ = true;
                            usesZGraphicsRuntime_ = true;
                        }
                        if (callee.rfind("__zmemory_", 0) == 0 ||
                            callee.rfind("__zstdlib_", 0) == 0 ||
                            callee.rfind("__zjson_", 0) == 0 ||
                            callee.rfind("__zhttp_", 0) == 0 ||
                            callee.rfind("__zsqlite_", 0) == 0 ||
                            callee == "__zplus_stringConcat" ||
                            callee == "push" || callee == "pop" ||
                            callee == "clear" || callee == "insert" ||
                            callee == "remove" || callee == "contains" ||
                            (callee == "length" && !module_.findFunction(callee)) ||
                            callee == "__zgraphics_numberText" ||
                            callee == "__zgraphics_eventText") {
                            usesManagedMemory_ = true;
                        }
                    }
                    if (instruction.opcode.starts_with("cmp_") &&
                        instruction.operands.size() >= 2 &&
                        (isStringValue(instruction.operands[0]) ||
                         isStringValue(instruction.operands[1]))) {
                        usesManagedMemory_ = true;
                    }
                    if (instruction.opcode == "add" &&
                        instruction.operands.size() >= 2 &&
                        (isStringValue(instruction.operands[0]) ||
                         isStringValue(instruction.operands[1]))) {
                        usesManagedMemory_ = true;
                    }
                }
            }
        }
    }

    void emitManagedFrameEnter() {
        const std::size_t slotBytes = slots_.size() * sizeof(std::uint64_t);
        if (slotBytes > static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("native backend: managed root frame is too large");
        }
        leaRbp(RCX, -static_cast<std::int32_t>(slotBytes));
        movImm(RDX, slots_.size());
        emitImportCall("zpg_pushRoots", "zgraphics_runtime.dll");
    }

    void emitManagedFrameExit() {
        emitImportCall("zpg_popRoots", "zgraphics_runtime.dll");
    }

    void allocateSlot(const std::string& name) {
        if (name.empty() || slots_.contains(name)) return;
        stackSize_ += 8;
        slots_.emplace(name, -stackSize_);
    }

    void generateFunction(const Function& function) {
        if (function.name.empty() || function.blocks.empty()) {
            throw std::runtime_error("native backend: function has no name or entry block");
        }
        prepareFunction(function);
        functionOffsets_[function.name] = code_.size();
        emit8(0x55);
        emit8(0x48); emit8(0x89); emit8(0xE5);
        if (stackSize_ > 0) {
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(stackSize_);
        }
        if (usesManagedMemory_) {
            for (const auto& [_, displacement] : slots_) {
                movStoreZero(displacement);
            }
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const Parameter& parameter = function.parameters[i];
            if (windowsAbi_ && i < 4) {
                if (parameter.type == "number") {
                    xmmStore(slot(parameter.name), static_cast<std::uint8_t>(i));
                } else {
                    movStore(slot(parameter.name), argumentRegisters()[i]);
                }
            } else if (windowsAbi_) {
                const std::size_t displacement = 48 +
                    (i - 4) * sizeof(std::uint64_t);
                if (displacement > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
                    throw std::runtime_error("native backend: function parameter area is too large");
                }
                movLoad(RAX, static_cast<std::int32_t>(displacement));
                movStore(slot(function.parameters[i].name), RAX);
            } else if (parameter.type == "number") {
                std::size_t floatIndex = 0;
                for (std::size_t previous = 0; previous < i; ++previous) {
                    if (function.parameters[previous].type == "number") ++floatIndex;
                }
                if (floatIndex >= 8) {
                    throw std::runtime_error(
                        "native backend: more than eight floating-point parameters are unsupported");
                }
                xmmStore(slot(parameter.name), static_cast<std::uint8_t>(floatIndex));
            } else {
                std::size_t integerIndex = 0;
                for (std::size_t previous = 0; previous < i; ++previous) {
                    if (function.parameters[previous].type != "number") ++integerIndex;
                }
                if (integerIndex >= argumentRegisters().size()) {
                    throw std::runtime_error(
                        "native backend: more than six integer/pointer parameters are unsupported");
                }
                movStore(slot(parameter.name), argumentRegisters()[integerIndex]);
            }
        }
        if (usesManagedMemory_) {
            emitManagedFrameEnter();
        }

        for (const auto& block : function.blocks) {
            localLabels_[blockLabel(function.name, block.label)] = code_.size();
            for (const auto& instruction : block.instructions) {
                emitInstruction(instruction, function.name);
            }
            const Instruction* last = block.lastInstruction();
            if (!last || (last->opcode != "br" && last->opcode != "condbr" &&
                          last->opcode != "switch" && last->opcode != "ret")) {
                if (usesManagedMemory_) {
                    emitManagedFrameExit();
                }
                if (function.returnType == "number") {
                    movImm(RAX, 0);
                    xmmMoveFromGpr(0, RAX);
                } else {
                    movImm(RAX, 0);
                }
                emit8(0xC9);
                emit8(0xC3);
            }
        }
        for (const auto& fixup : branchFixups_) {
            const auto found = localLabels_.find(fixup.target);
            if (found == localLabels_.end()) {
                throw std::runtime_error("native backend: unknown basic block '" + fixup.target + "'");
            }
            patchRelative(fixup.offset, found->second);
        }
        branchFixups_.clear();
        localLabels_.clear();
    }

    void generateEntryStub() {
        const Function* entry = module_.findFunction("main");
        if (!entry) entry = module_.findFunction(zplus::ir::kTopLevelEntryName);
        if (!entry) entry = module_.findFunction("start");
        if (!entry) entry = &module_.functions.front();
        if (!entry->parameters.empty()) {
            throw std::runtime_error("native entry function '" + entry->name +
                                     "' must not require parameters");
        }
        if (entry->returnType != "void" && entry->returnType != "number" &&
            entry->returnType != "boolean") {
            throw std::runtime_error("native entry function must return number, boolean, or void");
        }

        entryOffset_ = code_.size();
        if (windowsAbi_) {
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(40);
            if (usesManagedMemory_ && !globalOffsets_.empty()) {
                leaRip(RCX, 0);
                movImm(RDX, globalOffsets_.size());
                emitImportCall("zpg_pushRoots", "zgraphics_runtime.dll");
            }
            if (module_.findFunction(zplus::ir::kModuleInitializerName)) {
                emitCall(zplus::ir::kModuleInitializerName);
            }
            emitCall(entry->name);
            if (entry->returnType == "number") {
                convertXmmToSignedInteger(RAX, 0);
            }
            if (usesZGraphicsRuntime_) {
                movStoreRspDisp(32, RAX);
                emitImportCall("zpg_shutdown", "zgraphics_runtime.dll");
                movLoadRspDisp(RCX, 32);
                movReg(RAX, RCX);
            }
            if (usesManagedMemory_) {
                movStoreRspDisp(32, RAX);
                if (!globalOffsets_.empty()) {
                    emitImportCall("zpg_popRoots", "zgraphics_runtime.dll");
                }
                emitImportCall("zpg_memory_shutdown", "zgraphics_runtime.dll");
                movLoadRspDisp(RCX, 32);
                movReg(RAX, RCX);
            } else {
                movReg(RCX, RAX);
            }
            emitImportCall("ExitProcess");
            emit8(0x0F); emit8(0x0B);
            functionOffsets_["__zp_write"] = code_.size();
            emitWriteHelperWindows();
        } else {
            if (module_.findFunction(zplus::ir::kModuleInitializerName)) {
                emitCall(zplus::ir::kModuleInitializerName);
            }
            emitCall(entry->name);
            movReg(RDI, RAX);
            movImm(RAX, 60);
            emit8(0x0F); emit8(0x05);
            emit8(0x0F); emit8(0x0B);
            functionOffsets_["__zp_write"] = code_.size();
            emitWriteHelperLinux();
        }
    }

    void emitWriteHelperLinux() {
        const std::string loop = "__zp_write_loop";
        const std::string end = "__zp_write_end";
        const std::string finish = "__zp_write_finish";
        emit8(0x55);
        emit8(0x48); emit8(0x89); emit8(0xE5);
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(16);
        movStore(-8, RDI);
        movStore(-16, RSI);
        movReg(RSI, RDI);
        localLabels_[loop] = code_.size();
        movImm(RAX, 0);
        emit8(0x80);
        emit8(0x3C);
        emit8(0x07);
        emit8(0x00);
        emitJump(0x84, end);
        emit8(0x48); emit8(0xFF); emit8(0xC0);
        emitJump(0xFF, loop);
        localLabels_[end] = code_.size();
        movReg(RDX, RAX);
        movLoad(RSI, -8);
        movImm(RAX, 1);
        movImm(RDI, 1);
        emit8(0x0F); emit8(0x05);
        movLoad(RAX, -16);
        emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0);
        emitJump(0x84, finish);
        leaRip(RSI, addString("\n"));
        movImm(RDX, 1);
        movImm(RAX, 1);
        movImm(RDI, 1);
        emit8(0x0F); emit8(0x05);
        localLabels_[finish] = code_.size();
        emit8(0xC9);
        emit8(0xC3);
        resolveBranchesAtEnd();
    }

    void emitWriteHelperWindows() {
        const std::string loop = "__zp_write_win_loop";
        const std::string textDone = "__zp_write_win_text_done";
        const std::string skipNewline = "__zp_write_win_skip_newline";
        emit8(0x55);
        emit8(0x48); emit8(0x89); emit8(0xE5);
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(64);
        movStore(-8, RCX);
        movStore(-16, RDX);
        movImm(RAX, 0);
        movReg(R10, RCX);
        localLabels_[loop] = code_.size();
        emit8(0x41); emit8(0x80); emit8(0x3C); emit8(0x02); emit8(0x00);
        emitJump(0x84, textDone);
        emit8(0x48); emit8(0xFF); emit8(0xC0);
        emitJump(0xFF, loop);
        localLabels_[textDone] = code_.size();
        movStore(-24, RAX);
        emit8(0xB9); emit32(static_cast<std::int32_t>(0xFFFFFFF5U));
        emitImportCall("GetStdHandle");
        movReg(RCX, RAX);
        movLoad(RDX, -8);
        movLoad(R8, -24);
        leaRbp(R9, -48);
        movStoreRsp32Zero();
        emitImportCall("WriteFile");
        movLoad(RAX, -16);
        testRax();
        emitJump(0x84, skipNewline);
        emit8(0xB9); emit32(static_cast<std::int32_t>(0xFFFFFFF5U));
        emitImportCall("GetStdHandle");
        movReg(RCX, RAX);
        leaRip(RDX, addString("\n"));
        movImm(R8, 1);
        leaRbp(R9, -48);
        movStoreRsp32Zero();
        emitImportCall("WriteFile");
        localLabels_[skipNewline] = code_.size();
        emit8(0xC9);
        emit8(0xC3);
        resolveBranchesAtEnd();
    }

    void movStoreRsp32Zero() {
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
    }

    void resolveBranchesAtEnd() {
        for (const auto& fixup : branchFixups_) {
            const auto found = localLabels_.find(fixup.target);
            if (found == localLabels_.end()) {
                throw std::runtime_error("native backend: unresolved generated label '" + fixup.target + "'");
            }
            patchRelative(fixup.offset, found->second);
        }
        branchFixups_.clear();
        localLabels_.clear();
    }

    void patchRelative(std::size_t displacementOffset, std::size_t targetOffset) {
        const auto delta = static_cast<std::int64_t>(targetOffset) -
                           static_cast<std::int64_t>(displacementOffset + 4);
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("native backend: relative branch out of range");
        }
        code_.patch32(displacementOffset, static_cast<std::int32_t>(delta));
    }

    void resolveFunctionCalls() {
        for (const auto& fixup : callFixups_) {
            const auto found = functionOffsets_.find(fixup.target);
            if (found == functionOffsets_.end()) {
                throw std::runtime_error("native backend: unresolved function '" + fixup.target + "'");
            }
            patchRelative(fixup.offset, found->second);
        }
        callFixups_.clear();
    }

    const Module& module_;
    bool windowsAbi_;
    MachineCode code_;
    MachineCode data_;
    NativeImage image_;
    std::unordered_map<std::string, std::int32_t> slots_;
    std::unordered_set<std::string> stringSlots_;
    std::unordered_map<std::string, std::string> valueTypes_;
    std::unordered_map<std::string, std::string> globalTypes_;
    std::unordered_map<std::string, std::size_t> temporaryUseCounts_;
    std::unordered_map<std::string, std::size_t> localLabels_;
    std::unordered_map<std::string, std::size_t> functionOffsets_;
    std::unordered_map<std::string, std::size_t> stringOffsets_;
    std::unordered_map<std::string, std::size_t> globalOffsets_;
    std::vector<BranchFixup> branchFixups_;
    std::vector<CallFixup> callFixups_;
    std::vector<NativeImage::RelativeFixup> dataFixups_;
    std::vector<NativeImage::ImportFixup> importFixups_;
    std::int32_t stackSize_ = 0;
    std::size_t entryOffset_ = 0;
    std::size_t uniqueLabel_ = 0;
    bool usesZGraphicsRuntime_ = false;
    bool usesManagedMemory_ = false;
};

} // namespace

NativeImage generateX64(const zplus::ir::Module& module, bool windowsAbi) {
    return CodeGenerator(module, windowsAbi).generate();
}

} // namespace zplus::backend::native
