#include "IROptimizer.hpp"

#include "IRAnalysis.hpp"
#include "IRVerifier.hpp"
#include "../NumericLiteral.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace zplus::ir {
namespace {

using zplus::semantic::Type;
using zplus::semantic::TypeKind;

void verifyOrThrow(const Module& module) {
    const auto diagnostics = verifyModule(module);
    if (diagnostics.empty()) return;
    const auto& diagnostic = diagnostics.front();
    throw std::runtime_error(
        "IR optimization rejected invalid IR (" + diagnostic.code + "): " +
        diagnostic.message);
}

bool isReference(const Value& value) {
    return value.kind == Value::Kind::Identifier ||
        (value.kind == Value::Kind::Unknown &&
         !value.name.empty() && value.name.front() == '%');
}

bool isUseOperand(const Instruction& instruction, std::size_t index) {
    const std::string& opcode = instruction.opcode;
    if (opcode == "store") return index == 1;
    if (opcode.ends_with("_assign")) return index < 2;
    if (opcode == "pre_inc" || opcode == "post_inc" ||
        opcode == "pre_dec" || opcode == "post_dec" ||
        opcode == "dec") {
        return index == 0;
    }
    if (opcode == "alloc" || opcode == "freeze" ||
        opcode == "param" || opcode == "scope_push" ||
        opcode == "scope_pop" || opcode == "try_push" ||
        opcode == "try_pop" || opcode == "br") {
        return false;
    }
    if (opcode == "condbr") return index == 0;
    if (opcode == "switch") {
        return index == 0 ||
            (index > 0 && index + 1 < instruction.operands.size() &&
             index % 2 == 1);
    }
    if (opcode == "call" || opcode == "defer_push") return index > 0;
    if (opcode == "struct_new") return index > 0;
    if (opcode == "member" || opcode == "member_ptr" || opcode == "is") {
        return index == 0;
    }
    if (opcode == "member_store") return index == 0 || index == 2;
    return true;
}

bool isNumber(const Value& value, double& number) {
    if (value.kind != Value::Kind::Number) return false;
    if (value.hasNumericValue) {
        number = value.numericValue;
        return true;
    }
    return zplus::numeric::parseLiteral(value.name, number);
}

bool isBoolean(const Value& value, bool& boolean) {
    if (value.kind != Value::Kind::Boolean) return false;
    if (value.name == "true" || value.name == "True") {
        boolean = true;
        return true;
    }
    if (value.name == "false" || value.name == "False") {
        boolean = false;
        return true;
    }
    return false;
}

Value makeNumber(double number) {
    char buffer[128];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), number,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{}) {
        throw std::runtime_error("could not format a folded numeric literal");
    }
    Value value{
        std::string(buffer, converted.ptr),
        Value::Kind::Number
    };
    value.type = Type::number();
    value.numericValue = number;
    value.hasNumericValue = true;
    return value;
}

Value makeBoolean(bool boolean) {
    Value value{boolean ? "true" : "false", Value::Kind::Boolean};
    value.type = Type::boolean();
    return value;
}

using TypeFacts = std::unordered_map<std::string, TypeKind>;

TypeFacts collectTypeFacts(const Function& function) {
    TypeFacts types;
    for (const Parameter& parameter : function.parameters) {
        if (parameter.type.kind != TypeKind::Unknown) {
            types.insert_or_assign(parameter.name, parameter.type.kind);
        }
    }
    for (const BasicBlock& block : function.blocks) {
        for (const Instruction& instruction : block.instructions) {
            if (instruction.opcode == "alloc" &&
                !instruction.operands.empty() &&
                instruction.operands.front().type.kind != TypeKind::Unknown) {
                types.insert_or_assign(
                    instruction.operands.front().name,
                    instruction.operands.front().type.kind);
            }
            if (instruction.hasResult() &&
                instruction.resultType.kind != TypeKind::Unknown) {
                types.insert_or_assign(
                    instruction.result, instruction.resultType.kind);
            }
        }
    }
    return types;
}

TypeKind valueType(const Value& value, const TypeFacts& types) {
    if (value.type.kind != TypeKind::Unknown) return value.type.kind;
    switch (value.kind) {
        case Value::Kind::Number: return TypeKind::Number;
        case Value::Kind::String: return TypeKind::String;
        case Value::Kind::Character: return TypeKind::Character;
        case Value::Kind::Boolean: return TypeKind::Boolean;
        case Value::Kind::Null: return TypeKind::Null;
        case Value::Kind::Identifier:
        case Value::Kind::Unknown:
            if (const auto found = types.find(value.name); found != types.end()) {
                return found->second;
            }
            return TypeKind::Unknown;
    }
    return TypeKind::Unknown;
}

bool operandsAreNumbers(
    const Instruction& instruction,
    const TypeFacts& types
) {
    return instruction.operands.size() >= 2 &&
        valueType(instruction.operands[0], types) == TypeKind::Number &&
        valueType(instruction.operands[1], types) == TypeKind::Number;
}

void rewriteUses(
    Instruction& instruction,
    const InstructionLiveness& liveness,
    const std::unordered_map<std::string, Value>& replacements,
    std::size_t& changed
) {
    for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
        Value& operand = instruction.operands[index];
        if (!isUseOperand(instruction, index) ||
            !liveness.uses.contains(operand.name)) {
            continue;
        }
        const auto replacement = replacements.find(operand.name);
        if (replacement == replacements.end()) continue;
        Value value = replacement->second;
        if (value.type.kind == TypeKind::Unknown) {
            value.type = operand.type;
        }
        operand = std::move(value);
        ++changed;
    }
}

bool replaceWithMove(Instruction& instruction, Value value) {
    if (!instruction.hasResult()) return false;
    instruction.opcode = "mov";
    instruction.operands.clear();
    instruction.operands.push_back(std::move(value));
    instruction.comment.clear();
    return true;
}

bool foldInstruction(
    Instruction& instruction,
    const TypeFacts& types
) {
    if (instruction.opcode == "select" &&
        instruction.operands.size() == 3) {
        bool condition = false;
        if (isBoolean(instruction.operands[0], condition)) {
            return replaceWithMove(
                instruction,
                instruction.operands[condition ? 1 : 2]);
        }
    }

    if (instruction.opcode == "not" &&
        instruction.operands.size() == 1) {
        bool operand = false;
        if (isBoolean(instruction.operands.front(), operand)) {
            return replaceWithMove(instruction, makeBoolean(!operand));
        }
    }

    if ((instruction.opcode == "neg" ||
         instruction.opcode == "pos") &&
        instruction.operands.size() == 1 &&
        instruction.resultType.kind == TypeKind::Number) {
        double operand = 0.0;
        if (isNumber(instruction.operands.front(), operand) &&
            std::isfinite(operand)) {
            const double result =
                instruction.opcode == "neg" ? -operand : operand;
            if (std::isfinite(result)) {
                return replaceWithMove(instruction, makeNumber(result));
            }
        }
    }

    if (instruction.opcode == "and" || instruction.opcode == "or") {
        if (instruction.operands.size() != 2 ||
            instruction.resultType.kind != TypeKind::Boolean) {
            return false;
        }
        bool left = false;
        bool right = false;
        if (isBoolean(instruction.operands[0], left) &&
            isBoolean(instruction.operands[1], right)) {
            return replaceWithMove(
                instruction,
                makeBoolean(instruction.opcode == "and"
                    ? left && right
                    : left || right));
        }
    }

    if (instruction.operands.size() != 2) return false;
    const std::string& opcode = instruction.opcode;
    if (opcode == "add" || opcode == "sub" ||
        opcode == "mul" || opcode == "div") {
        if (instruction.resultType.kind != TypeKind::Number ||
            !operandsAreNumbers(instruction, types)) {
            return false;
        }
        double left = 0.0;
        double right = 0.0;
        if (!isNumber(instruction.operands[0], left) ||
            !isNumber(instruction.operands[1], right) ||
            !std::isfinite(left) || !std::isfinite(right) ||
            (opcode == "div" && right == 0.0)) {
            return false;
        }
        double result = 0.0;
        if (opcode == "add") result = left + right;
        else if (opcode == "sub") result = left - right;
        else if (opcode == "mul") result = left * right;
        else result = left / right;
        if (!std::isfinite(result)) return false;
        return replaceWithMove(instruction, makeNumber(result));
    }

    if (opcode.starts_with("cmp_") &&
        instruction.resultType.kind == TypeKind::Boolean &&
        operandsAreNumbers(instruction, types)) {
        double left = 0.0;
        double right = 0.0;
        if (!isNumber(instruction.operands[0], left) ||
            !isNumber(instruction.operands[1], right) ||
            !std::isfinite(left) || !std::isfinite(right)) {
            return false;
        }
        bool result = false;
        if (opcode == "cmp_eq") result = left == right;
        else if (opcode == "cmp_ne") result = left != right;
        else if (opcode == "cmp_lt") result = left < right;
        else if (opcode == "cmp_le") result = left <= right;
        else if (opcode == "cmp_gt") result = left > right;
        else if (opcode == "cmp_ge") result = left >= right;
        else return false;
        return replaceWithMove(instruction, makeBoolean(result));
    }

    return false;
}

bool isPureNumericExpression(
    const Instruction& instruction,
    const TypeFacts& types
) {
    return instruction.hasResult() &&
        instruction.resultType.kind == TypeKind::Number &&
        (instruction.opcode == "add" ||
         instruction.opcode == "sub" ||
         instruction.opcode == "mul") &&
        operandsAreNumbers(instruction, types);
}

bool isSafeToDiscard(
    const Instruction& instruction,
    const TypeFacts& types
) {
    if (!instruction.hasResult()) return false;
    const std::string& opcode = instruction.opcode;
    if (opcode == "mov" || opcode == "pos" || opcode == "neg") {
        return instruction.resultType.kind != TypeKind::Unknown;
    }
    if ((opcode == "add" || opcode == "sub" || opcode == "mul") &&
        isPureNumericExpression(instruction, types)) {
        return true;
    }
    if ((opcode == "and" || opcode == "or") &&
        instruction.resultType.kind == TypeKind::Boolean &&
        instruction.operands.size() == 2 &&
        valueType(instruction.operands[0], types) == TypeKind::Boolean &&
        valueType(instruction.operands[1], types) == TypeKind::Boolean) {
        return true;
    }
    if (opcode == "not" && instruction.resultType.kind == TypeKind::Boolean &&
        instruction.operands.size() == 1 &&
        valueType(instruction.operands.front(), types) == TypeKind::Boolean) {
        return true;
    }
    if (opcode.starts_with("cmp_") &&
        instruction.resultType.kind == TypeKind::Boolean &&
        operandsAreNumbers(instruction, types)) {
        return true;
    }
    return false;
}

bool hasOnlyLocalUses(
    const FunctionLiveness& analysis,
    const std::string& name,
    std::size_t blockIndex
) {
    for (std::size_t index = 0; index < analysis.blocks.size(); ++index) {
        if (index != blockIndex && analysis.blocks[index].liveIn.contains(name)) {
            return false;
        }
        if (index != blockIndex && analysis.blocks[index].liveOut.contains(name)) {
            return false;
        }
        for (const InstructionLiveness& instruction :
             analysis.blocks[index].instructions) {
            if (index != blockIndex && instruction.uses.contains(name)) {
                return false;
            }
        }
    }
    return !analysis.blocks[blockIndex].liveOut.contains(name);
}

std::size_t eliminateUnreachableBlocksUnchecked(Module& module) {
    std::size_t removed = 0;
    for (Function& function : module.functions) {
        if (function.blocks.empty()) continue;
        std::unordered_map<std::string, std::size_t> blockIndices;
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            blockIndices.emplace(function.blocks[index].label, index);
        }
        std::unordered_set<std::size_t> reachable;
        std::vector<std::size_t> worklist{0};
        const auto addTarget = [&](const Value& target) {
            if (const auto found = blockIndices.find(target.name);
                found != blockIndices.end()) {
                worklist.push_back(found->second);
            }
        };
        while (!worklist.empty()) {
            const std::size_t index = worklist.back();
            worklist.pop_back();
            if (!reachable.insert(index).second) continue;
            const BasicBlock& block = function.blocks[index];
            const Instruction* last = block.lastInstruction();
            if (last && last->opcode == "br" &&
                last->operands.size() == 1) {
                addTarget(last->operands.front());
            } else if (last && last->opcode == "condbr" &&
                       last->operands.size() == 3) {
                addTarget(last->operands[1]);
                addTarget(last->operands[2]);
            } else if (last && last->opcode == "switch" &&
                       last->operands.size() >= 2 &&
                       last->operands.size() % 2 == 0) {
                for (std::size_t targetIndex = 2;
                     targetIndex < last->operands.size();
                     targetIndex += 2) {
                    addTarget(last->operands[targetIndex]);
                }
                addTarget(last->operands.back());
            } else if ((!last || (last->opcode != "ret" &&
                                  last->opcode != "throw")) &&
                       index + 1 < function.blocks.size()) {
                worklist.push_back(index + 1);
            }
            for (const Instruction& instruction : block.instructions) {
                if (instruction.opcode == "try_push" &&
                    !instruction.operands.empty()) {
                    addTarget(instruction.operands.front());
                }
            }
        }

        for (std::size_t index = function.blocks.size(); index-- > 0;) {
            if (reachable.contains(index)) continue;
            function.blocks.erase(
                function.blocks.begin() + static_cast<std::ptrdiff_t>(index));
            ++removed;
        }
    }
    return removed;
}

template<typename Pass>
std::size_t runVerifiedPass(Module& module, Pass&& pass) {
    verifyOrThrow(module);
    const std::size_t changes = pass();
    if (changes != 0) verifyOrThrow(module);
    return changes;
}

} // namespace

std::size_t eliminateUnreachableBlocks(Module& module) {
    return runVerifiedPass(module, [&] {
        return eliminateUnreachableBlocksUnchecked(module);
    });
}

static std::size_t propagateConstantsUnchecked(Module& module) {
    std::size_t replacements = 0;
    for (Function& function : module.functions) {
        const FunctionLiveness analysis = analyzeLiveness(function);
        for (std::size_t blockIndex = 0;
             blockIndex < function.blocks.size();
             ++blockIndex) {
            BasicBlock& block = function.blocks[blockIndex];
            std::unordered_map<std::string, Value> constants;
            for (std::size_t instructionIndex = 0;
                 instructionIndex < block.instructions.size();
                 ++instructionIndex) {
                Instruction& instruction =
                    block.instructions[instructionIndex];
                rewriteUses(
                    instruction,
                    analysis.blocks[blockIndex].instructions[instructionIndex],
                    constants,
                    replacements);
                if (instruction.hasResult()) {
                    constants.erase(instruction.result);
                }
                if (instruction.opcode == "mov" &&
                    instruction.operands.size() == 1 &&
                    instruction.hasResult() &&
                    instruction.resultType.kind != TypeKind::Unknown) {
                    const Value& value = instruction.operands.front();
                    if (value.kind == Value::Kind::Number ||
                        value.kind == Value::Kind::Boolean ||
                        value.kind == Value::Kind::Null) {
                        Value constant = value;
                        if (constant.type.kind == TypeKind::Unknown) {
                            constant.type = instruction.resultType;
                        }
                        constants.insert_or_assign(
                            instruction.result, std::move(constant));
                    }
                }
            }
        }
    }
    return replacements;
}

std::size_t propagateConstants(Module& module) {
    return runVerifiedPass(module, [&] {
        return propagateConstantsUnchecked(module);
    });
}

static std::size_t foldConstantsUnchecked(Module& module) {
    std::size_t folded = 0;
    for (Function& function : module.functions) {
        const TypeFacts types = collectTypeFacts(function);
        for (BasicBlock& block : function.blocks) {
            for (Instruction& instruction : block.instructions) {
                if (foldInstruction(instruction, types)) ++folded;
                if (instruction.opcode == "condbr" &&
                    instruction.operands.size() == 3) {
                    bool condition = false;
                    if (isBoolean(instruction.operands[0], condition)) {
                        Value target =
                            instruction.operands[condition ? 1 : 2];
                        instruction.opcode = "br";
                        instruction.operands.assign(1, std::move(target));
                        ++folded;
                    }
                }
            }
        }
    }
    return folded;
}

std::size_t foldConstants(Module& module) {
    return runVerifiedPass(module, [&] {
        return foldConstantsUnchecked(module);
    });
}

static std::size_t simplifyAlgebraicUnchecked(Module& module) {
    std::size_t simplified = 0;
    for (Function& function : module.functions) {
        const TypeFacts types = collectTypeFacts(function);
        for (BasicBlock& block : function.blocks) {
            for (Instruction& instruction : block.instructions) {
                if (instruction.opcode == "pos" &&
                    instruction.operands.size() == 1 &&
                    instruction.resultType.kind == TypeKind::Number &&
                    valueType(instruction.operands.front(), types) ==
                        TypeKind::Number) {
                    replaceWithMove(instruction, instruction.operands.front());
                    ++simplified;
                }
            }
        }
    }
    return simplified;
}

std::size_t simplifyAlgebraic(Module& module) {
    return runVerifiedPass(module, [&] {
        return simplifyAlgebraicUnchecked(module);
    });
}

static std::size_t eliminateCommonSubexpressionsUnchecked(Module& module) {
    std::size_t eliminated = 0;
    for (Function& function : module.functions) {
        const FunctionLiveness analysis = analyzeLiveness(function);
        const TypeFacts types = collectTypeFacts(function);
        for (std::size_t blockIndex = 0;
             blockIndex < function.blocks.size();
             ++blockIndex) {
            BasicBlock& block = function.blocks[blockIndex];
            std::unordered_map<std::string, std::string> expressions;
            std::unordered_map<std::string, Value> aliases;
            for (std::size_t instructionIndex = 0;
                 instructionIndex < block.instructions.size();
                 ++instructionIndex) {
                Instruction& instruction =
                    block.instructions[instructionIndex];
                std::size_t ignored = 0;
                rewriteUses(
                    instruction,
                    analysis.blocks[blockIndex].instructions[instructionIndex],
                    aliases,
                    ignored);
                if (instruction.opcode == "call" ||
                    instruction.opcode == "store" ||
                    instruction.opcode.ends_with("_assign") ||
                    instruction.opcode == "scope_pop" ||
                    instruction.opcode == "throw" ||
                    instruction.opcode == "index_store" ||
                    instruction.opcode == "member_store") {
                    expressions.clear();
                }
                if (analysis.blocks[blockIndex]
                        .instructions[instructionIndex].mayCall) {
                    expressions.clear();
                }
                if (!isPureNumericExpression(instruction, types) ||
                    !hasOnlyLocalUses(
                        analysis, instruction.result, blockIndex)) {
                    continue;
                }
                const auto pureOperand = [&](const Value& operand) {
                    return valueType(operand, types) == TypeKind::Number &&
                        (operand.kind == Value::Kind::Number ||
                         isReference(operand));
                };
                if (!pureOperand(instruction.operands[0]) ||
                    !pureOperand(instruction.operands[1])) {
                    continue;
                }
                const std::string key =
                    instruction.opcode + "\x1f" +
                    std::to_string(static_cast<int>(
                        instruction.operands[0].kind)) + "\x1f" +
                    instruction.operands[0].name + "\x1f" +
                    std::to_string(static_cast<int>(
                        instruction.operands[1].kind)) + "\x1f" +
                    instruction.operands[1].name;
                if (const auto found = expressions.find(key);
                    found != expressions.end()) {
                    Value alias{found->second, Value::Kind::Unknown};
                    alias.type = instruction.resultType;
                    aliases.insert_or_assign(
                        instruction.result, std::move(alias));
                    instruction.opcode.clear();
                    ++eliminated;
                } else {
                    expressions.emplace(key, instruction.result);
                }
            }
            block.instructions.erase(
                std::remove_if(
                    block.instructions.begin(),
                    block.instructions.end(),
                    [](const Instruction& instruction) {
                        return instruction.opcode.empty();
                    }),
                block.instructions.end());
        }
    }
    return eliminated;
}

std::size_t eliminateCommonSubexpressions(Module& module) {
    return runVerifiedPass(module, [&] {
        return eliminateCommonSubexpressionsUnchecked(module);
    });
}

static std::size_t eliminateDeadCodeUnchecked(Module& module) {
    std::size_t removed = 0;
    for (Function& function : module.functions) {
        const FunctionLiveness analysis = analyzeLiveness(function);
        const TypeFacts types = collectTypeFacts(function);
        std::unordered_map<std::string, std::size_t> uses;
        for (const BasicBlockLiveness& block : analysis.blocks) {
            for (const InstructionLiveness& instruction : block.instructions) {
                for (const std::string& name : instruction.uses) {
                    ++uses[name];
                }
            }
        }
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            BasicBlock& block = function.blocks[index];
            for (const Instruction& instruction : block.instructions) {
                if (instruction.hasResult() &&
                    isSafeToDiscard(instruction, types) &&
                    uses[instruction.result] == 0) {
                    ++removed;
                }
            }
            block.instructions.erase(
                std::remove_if(
                    block.instructions.begin(),
                    block.instructions.end(),
                    [&](const Instruction& instruction) {
                        return instruction.hasResult() &&
                            isSafeToDiscard(instruction, types) &&
                            uses[instruction.result] == 0;
                    }),
                block.instructions.end());
        }
    }
    return removed;
}

std::size_t eliminateDeadCode(Module& module) {
    return runVerifiedPass(module, [&] {
        return eliminateDeadCodeUnchecked(module);
    });
}

static std::size_t simplifyControlFlowUnchecked(Module& module) {
    std::size_t simplified = 0;
    for (Function& function : module.functions) {
        for (BasicBlock& block : function.blocks) {
            Instruction* last = block.lastInstruction();
            if (last && last->opcode == "condbr" &&
                last->operands.size() == 3 &&
                last->operands[1].name == last->operands[2].name) {
                Value target = last->operands[1];
                last->opcode = "br";
                last->operands.assign(1, std::move(target));
                ++simplified;
            }
        }
    }
    return simplified;
}

std::size_t simplifyControlFlow(Module& module) {
    return runVerifiedPass(module, [&] {
        return simplifyControlFlowUnchecked(module);
    });
}

static bool inlineCandidate(const Function& function) {
    if (function.external || function.blocks.size() != 1 ||
        function.parameters.size() > 4 ||
        function.blocks.front().instructions.size() > 16) {
        return false;
    }
    const Instruction* last =
        function.blocks.front().lastInstruction();
    if (!last || last->opcode != "ret") return false;
    for (const Instruction& instruction : function.blocks.front().instructions) {
        if (instruction.hasResult() &&
            instruction.result.starts_with('%')) {
            return false;
        }
        const std::string& opcode = instruction.opcode;
        if (opcode == "call" || opcode == "defer_push" ||
            opcode == "throw" || opcode == "try_push" ||
            opcode == "try_pop" || opcode == "alloc" ||
            opcode == "store" || opcode == "scope_push" ||
            opcode == "scope_pop" || opcode == "br" ||
            opcode == "condbr" || opcode == "switch" ||
            opcode == "array" || opcode == "index" ||
            opcode == "index_store" || opcode == "member" ||
            opcode == "member_store" || opcode == "struct_new" ||
            opcode == "is") {
            if (&instruction != last) return false;
        }
    }
    return true;
}

static std::size_t inlineSmallFunctionsUnchecked(Module& module) {
    std::size_t inlined = 0;
    for (Function& caller : module.functions) {
        for (BasicBlock& block : caller.blocks) {
            std::vector<Instruction> replacement;
            replacement.reserve(block.instructions.size());
            bool changedBlock = false;
            for (const Instruction& instruction : block.instructions) {
                if (instruction.opcode != "call" ||
                    instruction.operands.empty() ||
                    instruction.operands[0].name == caller.name) {
                    replacement.push_back(instruction);
                    continue;
                }
                const Function* callee =
                    module.findFunction(instruction.operands[0].name);
                if (!callee || !inlineCandidate(*callee) ||
                    callee->parameters.size() + 1 !=
                        instruction.operands.size()) {
                    replacement.push_back(instruction);
                    continue;
                }

                std::unordered_map<std::string, Value> substitutions;
                for (std::size_t index = 0;
                     index < callee->parameters.size();
                     ++index) {
                    substitutions.insert_or_assign(
                        callee->parameters[index].name,
                        instruction.operands[index + 1]);
                }
                for (const Instruction& source :
                     callee->blocks.front().instructions) {
                    if (source.opcode == "ret") {
                        if (!instruction.result.empty() &&
                            !source.operands.empty()) {
                            Value returned = source.operands.front();
                            const auto substitution =
                                substitutions.find(returned.name);
                            if (substitution != substitutions.end()) {
                                returned = substitution->second;
                            }
                            Instruction mov(
                                "mov",
                                instruction.result,
                                {std::move(returned)});
                            mov.resultType = instruction.resultType;
                            replacement.push_back(std::move(mov));
                        }
                        continue;
                    }
                    Instruction cloned = source;
                    for (Value& operand : cloned.operands) {
                        const auto substitution =
                            substitutions.find(operand.name);
                        if (substitution != substitutions.end()) {
                            operand = substitution->second;
                        }
                    }
                    replacement.push_back(std::move(cloned));
                }
                ++inlined;
                changedBlock = true;
            }
            if (changedBlock) block.instructions = std::move(replacement);
        }
    }
    return inlined;
}

std::size_t inlineSmallFunctions(Module& module) {
    return runVerifiedPass(module, [&] {
        return inlineSmallFunctionsUnchecked(module);
    });
}

OptimizationStatistics optimizeModule(Module& module) {
    verifyOrThrow(module);
    OptimizationStatistics statistics;
    const auto runPass = [&module](auto&& pass) {
        const std::size_t changes = pass();
        if (changes != 0) verifyOrThrow(module);
        return changes;
    };
    statistics.removedBlocks += runPass([&] {
        return eliminateUnreachableBlocksUnchecked(module);
    });
    statistics.propagatedConstants += runPass([&] {
        return propagateConstantsUnchecked(module);
    });
    statistics.foldedInstructions += runPass([&] {
        return foldConstantsUnchecked(module);
    });
    statistics.propagatedConstants += runPass([&] {
        return propagateConstantsUnchecked(module);
    });
    statistics.foldedInstructions += runPass([&] {
        return foldConstantsUnchecked(module);
    });
    statistics.propagatedConstants += runPass([&] {
        return propagateConstantsUnchecked(module);
    });
    statistics.simplifiedInstructions += runPass([&] {
        return simplifyAlgebraicUnchecked(module);
    });
    statistics.propagatedConstants += runPass([&] {
        return propagateConstantsUnchecked(module);
    });
    statistics.simplifiedInstructions += runPass([&] {
        return inlineSmallFunctionsUnchecked(module);
    });
    statistics.commonSubexpressions += runPass([&] {
        return eliminateCommonSubexpressionsUnchecked(module);
    });
    statistics.removedInstructions += runPass([&] {
        return eliminateDeadCodeUnchecked(module);
    });
    statistics.simplifiedInstructions += runPass([&] {
        return simplifyControlFlowUnchecked(module);
    });
    statistics.removedBlocks += runPass([&] {
        return eliminateUnreachableBlocksUnchecked(module);
    });
    statistics.removedInstructions += runPass([&] {
        return eliminateDeadCodeUnchecked(module);
    });
    verifyOrThrow(module);
    return statistics;
}

} // namespace zplus::ir
