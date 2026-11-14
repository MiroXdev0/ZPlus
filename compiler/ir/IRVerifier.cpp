#include "IRVerifier.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace zplus::ir {
namespace {

using zplus::semantic::Type;

void report(
    std::vector<zplus::Diagnostic>& diagnostics,
    const zplus::ast::Location& location,
    std::string message
) {
    diagnostics.emplace_back(
        std::move(message),
        location.sourceFile,
        static_cast<std::size_t>(location.line),
        static_cast<std::size_t>(location.column),
        "E4001",
        zplus::DiagnosticCategory::IR
    );
}

bool isTerminator(const Instruction& instruction) {
    return instruction.opcode == "br" ||
        instruction.opcode == "condbr" ||
        instruction.opcode == "switch" ||
        instruction.opcode == "ret" ||
        instruction.opcode == "throw";
}

bool isKnownOpcode(const std::string& opcode) {
    static const std::unordered_set<std::string> opcodes{
        "add", "add_assign", "alloc", "and", "and_assign", "array",
        "bitand", "bitnot", "bitor", "bitxor", "br", "call", "cmp_eq",
        "cmp_ge", "cmp_gt", "cmp_le", "cmp_lt", "cmp_ne", "condbr",
        "dec", "defer_push", "div", "div_assign", "freeze", "index",
        "index_store", "is", "load", "member", "member_ptr", "member_store",
        "mod", "mod_assign", "mov", "mul", "mul_assign", "neg", "not",
        "or", "or_assign", "param", "pos", "post_dec", "post_inc",
        "pre_dec", "pre_inc", "ret", "scope_pop", "scope_push", "select",
        "shl", "shl_assign", "shr", "shr_assign", "store", "struct_new",
        "sub", "sub_assign", "switch", "throw", "try_pop", "try_push",
        "xor_assign"
    };
    return opcodes.contains(opcode);
}

const Type& operandType(const Value& value) {
    return value.type;
}

bool hasKnownType(const Type& type) {
    return type.kind != zplus::semantic::TypeKind::Unknown;
}

void requireType(
    const Value& value,
    zplus::semantic::TypeKind expected,
    const Instruction& instruction,
    std::vector<zplus::Diagnostic>& diagnostics,
    const std::string& role
) {
    if (hasKnownType(value.type) && value.type.kind != expected) {
        report(diagnostics, instruction.location,
               role + " in '" + instruction.opcode + "' has type '" +
                   zplus::semantic::typeName(value.type) + "'");
    }
}

void verifyShape(
    const Function& function,
    const Instruction& instruction,
    std::vector<zplus::Diagnostic>& diagnostics
) {
    if (!isKnownOpcode(instruction.opcode)) {
        report(diagnostics, instruction.location,
               "unknown IR opcode '" + instruction.opcode + "'");
        return;
    }

    const std::size_t count = instruction.operands.size();
    const auto exact = [&](std::size_t expected) {
        if (count != expected) {
            report(diagnostics, instruction.location,
                   "IR opcode '" + instruction.opcode + "' expects " +
                       std::to_string(expected) + " operands");
        }
    };
    const auto atLeast = [&](std::size_t minimum) {
        if (count < minimum) {
            report(diagnostics, instruction.location,
                   "IR opcode '" + instruction.opcode + "' expects at least " +
                       std::to_string(minimum) + " operands");
        }
    };

    if (instruction.opcode == "alloc" || instruction.opcode == "freeze" ||
        instruction.opcode == "param" || instruction.opcode == "mov" ||
        instruction.opcode == "load" || instruction.opcode == "index" ||
        instruction.opcode == "member" || instruction.opcode == "member_ptr" ||
        instruction.opcode == "is" || instruction.opcode == "br" ||
        instruction.opcode == "try_pop") {
        exact(instruction.opcode == "index" || instruction.opcode == "member" ||
                      instruction.opcode == "member_ptr" || instruction.opcode == "is"
                  ? 2
                  : 1);
    } else if (instruction.opcode == "store" ||
               instruction.opcode == "index_store" ||
               instruction.opcode == "member_store" ||
               instruction.opcode == "add" ||
               instruction.opcode == "sub" ||
               instruction.opcode == "mul" ||
               instruction.opcode == "div" ||
               instruction.opcode == "mod" ||
               instruction.opcode == "cmp_eq" ||
               instruction.opcode == "cmp_ne" ||
               instruction.opcode == "cmp_lt" ||
               instruction.opcode == "cmp_le" ||
               instruction.opcode == "cmp_gt" ||
               instruction.opcode == "cmp_ge" ||
               instruction.opcode == "and" ||
               instruction.opcode == "or" ||
               instruction.opcode == "bitand" ||
               instruction.opcode == "bitor" ||
               instruction.opcode == "bitxor" ||
               instruction.opcode == "shl" ||
               instruction.opcode == "shr" ||
               instruction.opcode == "add_assign" ||
               instruction.opcode == "sub_assign" ||
               instruction.opcode == "mul_assign" ||
               instruction.opcode == "div_assign" ||
               instruction.opcode == "mod_assign" ||
               instruction.opcode == "and_assign" ||
               instruction.opcode == "or_assign" ||
               instruction.opcode == "xor_assign" ||
               instruction.opcode == "shl_assign" ||
               instruction.opcode == "shr_assign") {
        exact(instruction.opcode == "store" ? 2 :
              instruction.opcode == "index_store" ||
                      instruction.opcode == "member_store" ? 3 : 2);
    } else if (instruction.opcode == "neg" ||
               instruction.opcode == "pos" ||
               instruction.opcode == "not" ||
               instruction.opcode == "bitnot" ||
               instruction.opcode == "dec" ||
               instruction.opcode == "pre_inc" ||
               instruction.opcode == "post_inc" ||
               instruction.opcode == "pre_dec" ||
               instruction.opcode == "post_dec") {
        exact(1);
    } else if (instruction.opcode == "condbr" ||
               instruction.opcode == "select") {
        exact(3);
    } else if (instruction.opcode == "try_push") {
        exact(2);
    } else if (instruction.opcode == "ret") {
        if (count > 1) exact(1);
    } else if (instruction.opcode == "throw") {
        exact(1);
    } else if (instruction.opcode == "call" ||
               instruction.opcode == "defer_push") {
        atLeast(1);
    } else if (instruction.opcode == "struct_new") {
        atLeast(1);
    } else if (instruction.opcode == "switch") {
        if (count < 2 || count % 2 != 0) {
            report(diagnostics, instruction.location,
                   "switch requires a value, case/target pairs, and a default target");
        }
    } else if (instruction.opcode == "scope_push" ||
               instruction.opcode == "scope_pop") {
        exact(0);
    }

    const bool mustHaveResult =
        instruction.opcode == "mov" || instruction.opcode == "array" ||
        instruction.opcode == "struct_new" || instruction.opcode == "index" ||
        instruction.opcode == "member" || instruction.opcode == "member_ptr" ||
        instruction.opcode == "load" || instruction.opcode == "add" ||
        instruction.opcode == "sub" || instruction.opcode == "mul" ||
        instruction.opcode == "div" || instruction.opcode == "mod" ||
        instruction.opcode.rfind("cmp_", 0) == 0 ||
        instruction.opcode == "and" || instruction.opcode == "or" ||
        instruction.opcode == "bitand" || instruction.opcode == "bitor" ||
        instruction.opcode == "bitxor" || instruction.opcode == "shl" ||
        instruction.opcode == "shr" || instruction.opcode == "neg" ||
        instruction.opcode == "pos" || instruction.opcode == "not" ||
        instruction.opcode == "bitnot" || instruction.opcode == "pre_inc" ||
        instruction.opcode == "post_inc" || instruction.opcode == "pre_dec" ||
        instruction.opcode == "post_dec" || instruction.opcode == "select" ||
        instruction.opcode == "is";
    const bool cannotHaveResult =
        instruction.opcode == "alloc" || instruction.opcode == "store" ||
        instruction.opcode == "index_store" ||
        instruction.opcode == "member_store" || instruction.opcode == "freeze" ||
        instruction.opcode == "param" || instruction.opcode == "br" ||
        instruction.opcode == "condbr" || instruction.opcode == "switch" ||
        instruction.opcode == "ret" || instruction.opcode == "throw" ||
        instruction.opcode == "try_push" || instruction.opcode == "try_pop" ||
        instruction.opcode == "scope_push" || instruction.opcode == "scope_pop" ||
        instruction.opcode == "defer_push" || instruction.opcode == "dec" ||
        instruction.opcode.ends_with("_assign");
    if (mustHaveResult && !instruction.hasResult()) {
        report(diagnostics, instruction.location,
               "IR opcode '" + instruction.opcode + "' requires a result");
    }
    if (cannotHaveResult && instruction.hasResult()) {
        report(diagnostics, instruction.location,
               "IR opcode '" + instruction.opcode + "' cannot have a result");
    }

    if (instruction.opcode == "import") {
        report(diagnostics, instruction.location,
               "module-only IR opcode 'import' appears in function '" +
                   function.name + "'");
    }
}

void verifyInstructionTypes(
    const Module& module,
    const Instruction& instruction,
    std::vector<zplus::Diagnostic>& diagnostics
) {
    const auto requireResult = [&](zplus::semantic::TypeKind expected) {
        if (hasKnownType(instruction.resultType) &&
            instruction.resultType.kind != expected) {
            report(diagnostics, instruction.location,
                   "result of IR opcode '" + instruction.opcode +
                       "' has incompatible type '" +
                       zplus::semantic::typeName(instruction.resultType) + "'");
        }
    };
    const auto requireNumberOperands = [&](std::size_t count) {
        for (std::size_t index = 0;
             index < count && index < instruction.operands.size();
             ++index) {
            requireType(instruction.operands[index],
                        zplus::semantic::TypeKind::Number,
                        instruction, diagnostics, "operand");
        }
    };

    if (instruction.opcode == "add") {
        const bool strings =
            instruction.operands.size() >= 2 &&
            instruction.operands[0].type.kind == zplus::semantic::TypeKind::String &&
            instruction.operands[1].type.kind == zplus::semantic::TypeKind::String;
        requireResult(strings ? zplus::semantic::TypeKind::String
                              : zplus::semantic::TypeKind::Number);
        if (!strings) requireNumberOperands(2);
    } else if (instruction.opcode == "sub" ||
               instruction.opcode == "mul" ||
               instruction.opcode == "div" ||
               instruction.opcode == "mod" ||
               instruction.opcode == "bitand" ||
               instruction.opcode == "bitor" ||
               instruction.opcode == "bitxor" ||
               instruction.opcode == "shl" ||
               instruction.opcode == "shr") {
        requireResult(zplus::semantic::TypeKind::Number);
        requireNumberOperands(2);
    } else if (instruction.opcode.rfind("cmp_", 0) == 0 ||
               instruction.opcode == "is") {
        requireResult(zplus::semantic::TypeKind::Boolean);
    } else if (instruction.opcode == "and" || instruction.opcode == "or") {
        requireResult(zplus::semantic::TypeKind::Boolean);
        for (std::size_t index = 0;
             index < 2 && index < instruction.operands.size();
             ++index) {
            requireType(instruction.operands[index],
                        zplus::semantic::TypeKind::Boolean,
                        instruction, diagnostics, "logical operand");
        }
    } else if (instruction.opcode == "not") {
        requireResult(zplus::semantic::TypeKind::Boolean);
        if (!instruction.operands.empty()) {
            requireType(instruction.operands.front(),
                        zplus::semantic::TypeKind::Boolean,
                        instruction, diagnostics, "operand");
        }
    } else if (instruction.opcode == "neg" || instruction.opcode == "pos" ||
               instruction.opcode == "bitnot" || instruction.opcode == "dec" ||
               instruction.opcode == "pre_inc" ||
               instruction.opcode == "post_inc" ||
               instruction.opcode == "pre_dec" ||
               instruction.opcode == "post_dec") {
        if (instruction.opcode != "dec") {
            requireResult(zplus::semantic::TypeKind::Number);
        }
        requireNumberOperands(1);
    } else if (instruction.opcode == "mov" || instruction.opcode == "load") {
        if (!instruction.operands.empty() &&
            hasKnownType(instruction.resultType) &&
            hasKnownType(instruction.operands[0].type) &&
            !zplus::semantic::isAssignable(
                instruction.resultType, instruction.operands[0].type)) {
            report(diagnostics, instruction.location,
                   "result type does not match the source of IR opcode '" +
                       instruction.opcode + "'");
        }
    } else if (instruction.opcode == "store" &&
               instruction.operands.size() == 2) {
        if (hasKnownType(instruction.operands[0].type) &&
            hasKnownType(instruction.operands[1].type) &&
            !zplus::semantic::isAssignable(
                instruction.operands[0].type, instruction.operands[1].type)) {
            report(diagnostics, instruction.location,
                   "stored value has an incompatible IR type");
        }
    } else if (instruction.opcode == "array") {
        if (instruction.resultType.kind != zplus::semantic::TypeKind::Unknown &&
            instruction.resultType.kind != zplus::semantic::TypeKind::Array) {
            report(diagnostics, instruction.location,
                   "array instruction result is not an array type");
        } else if (instruction.resultType.elementType) {
            for (const Value& operand : instruction.operands) {
                if (hasKnownType(operand.type) &&
                    !zplus::semantic::isAssignable(
                        *instruction.resultType.elementType, operand.type)) {
                    report(diagnostics, instruction.location,
                           "array element operand has an incompatible IR type");
                }
            }
        }
    } else if (instruction.opcode == "index") {
        if (instruction.operands.size() == 2) {
            requireType(instruction.operands[1],
                        zplus::semantic::TypeKind::Number,
                        instruction, diagnostics, "array index");
            const Type& arrayType = instruction.operands[0].type;
            if (hasKnownType(arrayType) &&
                arrayType.kind != zplus::semantic::TypeKind::Array) {
                report(diagnostics, instruction.location,
                       "index operand is not an array type");
            } else if (arrayType.kind == zplus::semantic::TypeKind::Array &&
                arrayType.elementType && hasKnownType(instruction.resultType) &&
                !zplus::semantic::isAssignable(
                    *arrayType.elementType, instruction.resultType)) {
                report(diagnostics, instruction.location,
                       "index result type does not match the array element type");
            }
        }
    } else if (instruction.opcode == "index_store" &&
               instruction.operands.size() == 3) {
        requireType(instruction.operands[1],
                    zplus::semantic::TypeKind::Number,
                    instruction, diagnostics, "array index");
        const Type& arrayType = instruction.operands[0].type;
        if (hasKnownType(arrayType) &&
            arrayType.kind != zplus::semantic::TypeKind::Array) {
            report(diagnostics, instruction.location,
                   "index_store operand is not an array type");
        } else if (arrayType.kind == zplus::semantic::TypeKind::Array &&
            arrayType.elementType && hasKnownType(instruction.operands[2].type) &&
            !zplus::semantic::isAssignable(
                *arrayType.elementType, instruction.operands[2].type)) {
            report(diagnostics, instruction.location,
                   "indexed value has an incompatible IR type");
        }
    } else if (instruction.opcode == "struct_new" &&
               !instruction.operands.empty()) {
        const StructType* structure =
            module.findStructType(instruction.operands.front().name);
        if (!structure) {
            report(diagnostics, instruction.location,
                   "struct_new references an unknown IR type '" +
                       instruction.operands.front().name + "'");
        } else {
            if (instruction.operands.size() != structure->fields.size() + 1) {
                report(diagnostics, instruction.location,
                       "struct_new operand count does not match its field count");
            }
            const std::size_t count = std::min(
                instruction.operands.size() - 1, structure->fields.size());
            for (std::size_t index = 0; index < count; ++index) {
                const Type& expected = structure->fields[index].type;
                const Type& actual = instruction.operands[index + 1].type;
                if (hasKnownType(expected) && hasKnownType(actual) &&
                    !zplus::semantic::isAssignable(expected, actual)) {
                    report(diagnostics, instruction.location,
                           "struct_new field operand has an incompatible IR type");
                }
            }
        }
    } else if (instruction.opcode == "select" &&
               instruction.operands.size() == 3) {
        requireType(instruction.operands[0],
                    zplus::semantic::TypeKind::Boolean,
                    instruction, diagnostics, "select condition");
        for (std::size_t index = 1; index < 3; ++index) {
            if (hasKnownType(instruction.resultType) &&
                hasKnownType(instruction.operands[index].type) &&
                !zplus::semantic::isAssignable(
                    instruction.resultType, instruction.operands[index].type)) {
                report(diagnostics, instruction.location,
                       "select operand type does not match its result type");
            }
        }
    }
}

void verifyCall(
    const Module& module,
    const Instruction& instruction,
    std::vector<zplus::Diagnostic>& diagnostics
) {
    if (instruction.operands.empty()) {
        report(diagnostics, instruction.location, "call instruction has no callee");
        return;
    }

    const Value& callee = instruction.operands.front();
    const Function* target = nullptr;
    if (instruction.calleeFunctionIndex != kInvalidFunctionIndex) {
        if (instruction.calleeFunctionIndex >= module.functions.size()) {
            report(diagnostics, instruction.location,
                   "call instruction references an invalid function index");
            return;
        }
        target = &module.functions[instruction.calleeFunctionIndex];
        if (!callee.name.empty() && target->name != callee.name) {
            report(diagnostics, instruction.location,
                   "call function index does not match its callee");
        }
    } else {
        target = module.findFunction(callee.name);
    }

    const std::size_t argumentCount = instruction.operands.size() - 1;
    if (target) {
        if (target->requiredParameterCount > target->parameters.size()) {
            report(diagnostics, target->location,
                   "function '" + target->name +
                       "' has an invalid required-parameter count");
        }
        if (argumentCount < target->requiredParameterCount ||
            argumentCount > target->parameters.size()) {
            report(diagnostics, instruction.location,
                   "call to '" + target->name +
                       "' does not match its parameter count");
        }
        const std::size_t count =
            argumentCount < target->parameters.size()
                ? argumentCount
                : target->parameters.size();
        for (std::size_t index = 0; index < count; ++index) {
            const Type& expected = target->parameters[index].type;
            const Type& actual = operandType(instruction.operands[index + 1]);
            if (expected.kind != zplus::semantic::TypeKind::Unknown &&
                actual.kind != zplus::semantic::TypeKind::Unknown &&
                !zplus::semantic::isAssignable(expected, actual)) {
                report(diagnostics, instruction.location,
                       "argument " + std::to_string(index + 1) +
                           " to '" + target->name + "' has an incompatible IR type");
            }
        }
        if (instruction.hasResult() &&
            instruction.resultType.kind != zplus::semantic::TypeKind::Unknown &&
            target->returnType.kind != zplus::semantic::TypeKind::Unknown &&
            !zplus::semantic::isAssignable(
                target->returnType, instruction.resultType)) {
            report(diagnostics, instruction.location,
                   "call result type does not match function '" + target->name + "'");
        }
        return;
    }

    const auto* builtin = module.builtinRegistry.find(callee.name);
    if (!builtin) {
        if (callee.runtimeSlot == kInvalidRuntimeSlot) {
            report(diagnostics, instruction.location,
                   "call refers to unknown function or builtin '" + callee.name + "'");
        }
        return;
    }
    const std::size_t parameterCount = builtin->signature.parameters.size();
    if (argumentCount < builtin->minimumArgumentCount() ||
        (!builtin->variadic && argumentCount > parameterCount)) {
        report(diagnostics, instruction.location,
               "call to builtin '" + builtin->name +
                   "' does not match its parameter count");
        return;
    }

    zplus::semantic::TypeSubstitution substitutions;
    for (std::size_t index = 0; index < argumentCount && parameterCount > 0; ++index) {
        const Type& expected =
            builtin->signature.parameters[index < parameterCount ? index : parameterCount - 1];
        const Type& actual = operandType(instruction.operands[index + 1]);
        if (actual.kind != zplus::semantic::TypeKind::Unknown &&
            !zplus::semantic::unify(expected, actual, substitutions)) {
            report(diagnostics, instruction.location,
                   "argument " + std::to_string(index + 1) +
                       " to builtin '" + builtin->name +
                       "' has an incompatible IR type");
        }
    }
    if (instruction.hasResult() && builtin->signature.returnType &&
        instruction.resultType.kind != zplus::semantic::TypeKind::Unknown) {
        const Type expected =
            zplus::semantic::substitute(*builtin->signature.returnType, substitutions);
        if (expected.kind != zplus::semantic::TypeKind::TypeParameter &&
            expected.kind != zplus::semantic::TypeKind::Unknown &&
            !zplus::semantic::isAssignable(expected, instruction.resultType)) {
            report(diagnostics, instruction.location,
                   "builtin call result has an incompatible IR type");
        }
    }
}

} // namespace

std::vector<zplus::Diagnostic> verifyModule(const Module& module) {
    std::vector<zplus::Diagnostic> diagnostics;
    std::unordered_set<std::string> functionNames;

    for (const Instruction& instruction : module.globals) {
        if (instruction.opcode != "import") {
            report(diagnostics, instruction.location,
                   "unsupported module-level IR opcode '" +
                       instruction.opcode + "'");
        } else if (instruction.hasResult() ||
                   instruction.operands.empty() ||
                   instruction.operands.size() > 2) {
            report(diagnostics, instruction.location,
                   "module import has invalid operands or a result");
        }
    }

    for (const Function& function : module.functions) {
        if (function.name.empty() || !functionNames.insert(function.name).second) {
            report(diagnostics, function.location,
                   "IR contains an empty or duplicate function name");
        }
        if (function.requiredParameterCount > function.parameters.size()) {
            report(diagnostics, function.location,
                   "function '" + function.name +
                       "' has more required parameters than parameters");
        }
        if (!function.external && function.blocks.empty()) {
            report(diagnostics, function.location,
                   "function '" + function.name + "' has no basic blocks");
        }

        std::unordered_set<std::string> labels;
        for (const BasicBlock& block : function.blocks) {
            if (block.label.empty() || !labels.insert(block.label).second) {
                report(diagnostics, function.location,
                       "function '" + function.name +
                           "' contains an empty or duplicate basic-block label");
            }
        }

        std::unordered_map<std::string, std::size_t> blockIndices;
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            blockIndices.emplace(function.blocks[index].label, index);
        }
        std::unordered_set<std::string> reachableBlocks;
        if (!function.blocks.empty()) {
            std::vector<std::size_t> worklist{0};
            while (!worklist.empty()) {
                const std::size_t blockIndex = worklist.back();
                worklist.pop_back();
                const BasicBlock& block = function.blocks[blockIndex];
                if (!reachableBlocks.insert(block.label).second) continue;
                const Instruction* last = block.lastInstruction();
                auto addTarget = [&](const Value& target) {
                    if (const auto found = blockIndices.find(target.name);
                        found != blockIndices.end()) {
                        worklist.push_back(found->second);
                    }
                };
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
                } else if ((!last || !isTerminator(*last)) &&
                           blockIndex + 1 < function.blocks.size()) {
                    worklist.push_back(blockIndex + 1);
                }
                for (const Instruction& instruction : block.instructions) {
                    if (instruction.opcode == "try_push" &&
                        !instruction.operands.empty()) {
                        addTarget(instruction.operands.front());
                    }
                }
            }
        }

        for (std::size_t blockIndex = 0;
             blockIndex < function.blocks.size();
             ++blockIndex) {
            const BasicBlock& block = function.blocks[blockIndex];
            bool terminated = false;
            for (std::size_t index = 0; index < block.instructions.size(); ++index) {
                const Instruction& instruction = block.instructions[index];
                if (instruction.opcode.empty()) {
                    report(diagnostics, instruction.location,
                           "IR instruction has no opcode");
                }
                if (terminated) {
                    report(diagnostics, instruction.location,
                           "instruction appears after a basic-block terminator");
                    break;
                }
                verifyShape(function, instruction, diagnostics);
                verifyInstructionTypes(module, instruction, diagnostics);
                if (instruction.hasResult() &&
                    instruction.resultType.kind == zplus::semantic::TypeKind::Unknown) {
                    report(diagnostics, instruction.location,
                           "result '" + instruction.result +
                               "' of instruction '" + instruction.opcode +
                               "' in function '" + function.name +
                               "' has no explicit IR type");
                }
                if (!instruction.hasResult() &&
                    instruction.resultType.kind != zplus::semantic::TypeKind::Unknown) {
                    report(diagnostics, instruction.location,
                           "instruction without a result has a result type");
                }

                if (instruction.opcode == "call") {
                    verifyCall(module, instruction, diagnostics);
                } else if (instruction.opcode == "defer_push") {
                    Instruction deferredCall{"call"};
                    deferredCall.operands = instruction.operands;
                    deferredCall.location = instruction.location;
                    verifyCall(module, deferredCall, diagnostics);
                } else if (instruction.opcode == "ret") {
                    if (instruction.operands.size() > 1) {
                        report(diagnostics, instruction.location,
                               "return instruction has more than one value");
                    } else if (instruction.operands.empty()) {
                        if (reachableBlocks.contains(block.label) &&
                            function.returnType.kind !=
                                zplus::semantic::TypeKind::Void &&
                            function.returnType.kind !=
                                zplus::semantic::TypeKind::Unknown) {
                            report(diagnostics, instruction.location,
                                   "function '" + function.name +
                                       "' returns without a value");
                        }
                    } else if (
                        function.returnType.kind != zplus::semantic::TypeKind::Unknown &&
                        operandType(instruction.operands.front()).kind !=
                            zplus::semantic::TypeKind::Unknown &&
                        !zplus::semantic::isAssignable(
                            function.returnType, operandType(instruction.operands.front()))) {
                        report(diagnostics, instruction.location,
                               "return value type does not match function return type");
                    }
                } else if (instruction.opcode == "br") {
                    if (instruction.operands.size() != 1 ||
                        !labels.contains(instruction.operands.front().name)) {
                        report(diagnostics, instruction.location,
                               "branch has an invalid target label");
                    }
                } else if (instruction.opcode == "condbr") {
                    if (instruction.operands.size() != 3 ||
                        !labels.contains(instruction.operands[1].name) ||
                        !labels.contains(instruction.operands[2].name)) {
                        report(diagnostics, instruction.location,
                               "conditional branch has invalid operands or target labels");
                    } else if (
                        instruction.operands[0].type.kind !=
                            zplus::semantic::TypeKind::Unknown &&
                        instruction.operands[0].type.kind !=
                            zplus::semantic::TypeKind::Boolean) {
                        report(diagnostics, instruction.location,
                               "conditional branch condition is not boolean");
                    }
                } else if (instruction.opcode == "switch" &&
                           instruction.operands.size() >= 2 &&
                           instruction.operands.size() % 2 == 0) {
                    for (std::size_t targetIndex = 2;
                         targetIndex < instruction.operands.size();
                         targetIndex += 2) {
                        if (!labels.contains(
                                instruction.operands[targetIndex].name)) {
                            report(diagnostics, instruction.location,
                                   "switch has an invalid case target label");
                        }
                        const Type& selector = instruction.operands.front().type;
                        const Type& caseType =
                            instruction.operands[targetIndex - 1].type;
                        if (hasKnownType(selector) && hasKnownType(caseType) &&
                            !zplus::semantic::isAssignable(selector, caseType)) {
                            report(diagnostics, instruction.location,
                                   "switch case value has an incompatible IR type");
                        }
                    }
                    if (!labels.contains(instruction.operands.back().name)) {
                        report(diagnostics, instruction.location,
                               "switch has an invalid default target label");
                    }
                } else if ((instruction.opcode == "try_push" ||
                            instruction.opcode == "try_pop") &&
                           !instruction.operands.empty() &&
                           !labels.contains(instruction.operands.front().name)) {
                    report(diagnostics, instruction.location,
                           "exception handler has an invalid target label");
                }

                terminated = isTerminator(instruction);
            }
            if (reachableBlocks.contains(block.label) &&
                blockIndex + 1 == function.blocks.size() && !terminated &&
                function.returnType.kind !=
                    zplus::semantic::TypeKind::Unknown &&
                function.returnType.kind !=
                    zplus::semantic::TypeKind::Void) {
                report(diagnostics, function.location,
                       "function '" + function.name +
                           "' can reach its end without returning a value");
            }
        }
    }
    return diagnostics;
}

} // namespace zplus::ir
