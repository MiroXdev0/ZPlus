#include "ir/IRAnalysis.hpp"
#include "ir/IROptimizer.hpp"
#include "ir/IRVerifier.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {

using zplus::ir::Function;
using zplus::ir::Instruction;
using zplus::ir::Module;
using zplus::ir::Value;
using zplus::semantic::Type;

Value number(const std::string& value) {
    Value result{value, Value::Kind::Number};
    result.type = Type::number();
    return result;
}

Value variable(const std::string& name, const Type& type) {
    Value result{name, Value::Kind::Identifier};
    result.type = type;
    return result;
}

Instruction resultInstruction(
    std::string opcode,
    std::string result,
    std::vector<Value> operands,
    Type type
) {
    Instruction instruction{
        std::move(opcode), std::move(result), std::move(operands)};
    instruction.resultType = std::move(type);
    return instruction;
}

bool constantPropagationAndFolding() {
    Module module;
    Function& function = module.createFunction("main");
    function.returnType = Type::number();
    auto& entry = function.createBlock("entry");
    entry.emit(resultInstruction(
        "add", "%first", {number("2"), number("3")}, Type::number()));
    entry.emit(resultInstruction(
        "mul", "%answer", {Value{"%first"}, number("4")}, Type::number()));
    entry.emit(Instruction{"ret", {}, {Value{"%answer"}}});

    const auto statistics = zplus::ir::optimizeModule(module);
    const Instruction* result = module.functions.front().blocks.front()
                                    .lastInstruction();
    return statistics.foldedInstructions >= 2 &&
        result && result->opcode == "ret" &&
        result->operands.size() == 1 &&
        result->operands.front().kind == Value::Kind::Number &&
        result->operands.front().numericValue == 20.0 &&
        zplus::ir::verifyModule(module).empty();
}

bool cseUsesAreRewrittenLocally() {
    Module module;
    Function& function = module.createFunction("twice");
    function.parameters.emplace_back("value", Type::number());
    function.requiredParameterCount = 1;
    function.returnType = Type::number();
    auto& entry = function.createBlock("entry");
    const Value value = variable("value", Type::number());
    entry.emit(resultInstruction(
        "add", "%first", {value, number("2")}, Type::number()));
    entry.emit(resultInstruction(
        "add", "%second", {value, number("2")}, Type::number()));
    entry.emit(Instruction{"ret", {}, {Value{"%second"}}});

    if (zplus::ir::eliminateCommonSubexpressions(module) != 1) return false;
    const auto& instructions = module.functions.front().blocks.front().instructions;
    return instructions.size() == 2 &&
        instructions.back().operands.front().name == "%first" &&
        zplus::ir::verifyModule(module).empty();
}

bool throwingAndEffectfulInstructionsRemain() {
    Module module;
    Function& function = module.createFunction("main");
    auto& entry = function.createBlock("entry");
    entry.emit(resultInstruction(
        "div", "%possibly_trapping", {number("1"), number("0")},
        Type::number()));
    entry.emit(Instruction{"ret"});

    if (zplus::ir::eliminateDeadCode(module) != 0) return false;
    return entry.instructions.size() == 2 &&
        entry.instructions.front().opcode == "div" &&
        zplus::ir::verifyModule(module).empty();
}

bool unreachableBlocksAreRemovedAfterConstantBranches() {
    Module module;
    Function& function = module.createFunction("choose");
    function.returnType = Type::number();
    auto& entry = function.createBlock("entry");
    auto& selected = function.createBlock("selected");
    auto& rejected = function.createBlock("rejected");
    Value condition{"true", Value::Kind::Boolean};
    condition.type = Type::boolean();
    entry.emit(Instruction{
        "condbr", {},
        {condition, Value{"selected"}, Value{"rejected"}}});
    selected.emit(Instruction{"ret", {}, {number("7")}});
    rejected.emit(Instruction{"ret", {}, {number("9")}});

    if (zplus::ir::foldConstants(module) != 1) return false;
    if (zplus::ir::eliminateUnreachableBlocks(module) != 1) return false;
    return module.functions.front().blocks.size() == 2 &&
        module.functions.front().blocks.front().lastInstruction()->opcode == "br" &&
        zplus::ir::verifyModule(module).empty();
}

bool controlFlowSimplificationPreservesExplicitFallthroughEdges() {
    Module module;
    Function& function = module.createFunction("main");
    function.parameters.emplace_back("condition", Type::boolean());
    function.requiredParameterCount = 1;
    function.returnType = Type::voidType();
    auto& entry = function.createBlock("entry");
    auto& next = function.createBlock("next");
    entry.emit(Instruction{
        "condbr", {},
        {variable("condition", Type::boolean()),
         Value{"next"}, Value{"next"}}});
    next.emit(Instruction{"ret"});

    if (zplus::ir::simplifyControlFlow(module) != 1) return false;
    const Instruction* branch = entry.lastInstruction();
    if (!branch || branch->opcode != "br" ||
        branch->operands.size() != 1 ||
        branch->operands.front().name != "next") {
        return false;
    }

    Module explicitBranchModule;
    Function& explicitBranchFunction =
        explicitBranchModule.createFunction("main");
    explicitBranchFunction.returnType = Type::voidType();
    auto& explicitEntry = explicitBranchFunction.createBlock("entry");
    auto& explicitNext = explicitBranchFunction.createBlock("next");
    explicitEntry.emit(Instruction{"br", {}, {Value{"next"}}});
    explicitNext.emit(Instruction{"ret"});
    const auto explicitChanges =
        zplus::ir::simplifyControlFlow(explicitBranchModule);
    return explicitChanges == 0 &&
        explicitEntry.lastInstruction()->opcode == "br" &&
        zplus::ir::verifyModule(module).empty() &&
        zplus::ir::verifyModule(explicitBranchModule).empty();
}

bool loopAndExceptionLiveness() {
    Module module;
    Function& target = module.createFunction("may_throw");
    target.blocks.emplace_back("entry");
    target.blocks.front().emit(Instruction{"ret"});

    Function& function = module.createFunction("loop");
    function.returnType = Type::number();
    auto& entry = function.createBlock("entry");
    auto& condition = function.createBlock("condition");
    auto& body = function.createBlock("body");
    auto& exit = function.createBlock("exit");
    auto& handler = function.createBlock("handler");
    const Value counter = variable("counter", Type::number());
    entry.emit(Instruction{"alloc", {}, {counter}});
    entry.emit(Instruction{"store", {}, {counter, number("0")}});
    entry.emit(Instruction{
        "try_push", {}, {Value{"handler"}, Value{"error"}}});
    entry.emit(Instruction{"br", {}, {Value{"condition"}}});

    condition.emit(resultInstruction(
        "load", "%current", {counter}, Type::number()));
    Value conditionValue = variable("%continue", Type::boolean());
    condition.emit(resultInstruction(
        "cmp_lt", "%continue", {Value{"%current"}, number("4")},
        Type::boolean()));
    condition.emit(Instruction{
        "condbr", {},
        {conditionValue, Value{"body"}, Value{"exit"}}});

    body.emit(resultInstruction(
        "add", "%next", {Value{"%current"}, number("1")}, Type::number()));
    body.emit(Instruction{
        "store", {}, {counter, Value{"%next"}}});
    body.emit(Instruction{"call", {}, {Value{"may_throw"}}});
    body.emit(Instruction{"br", {}, {Value{"condition"}}});

    exit.emit(resultInstruction(
        "load", "%answer", {counter}, Type::number()));
    exit.emit(Instruction{"try_pop", {}, {Value{"handler"}}});
    exit.emit(Instruction{"ret", {}, {Value{"%answer"}}});

    handler.emit(resultInstruction(
        "load", "%caught_counter", {counter}, Type::number()));
    handler.emit(Instruction{"ret", {}, {Value{"%caught_counter"}}});

    if (!zplus::ir::verifyModule(module).empty()) return false;
    const auto analysis = zplus::ir::analyzeLiveness(function);
    if (analysis.blocks.size() != 5 ||
        !analysis.blocks[1].liveIn.contains("counter") ||
        !analysis.blocks[2].liveOut.contains("counter")) {
        return false;
    }
    const auto call = std::find_if(
        analysis.blocks[2].instructions.begin(),
        analysis.blocks[2].instructions.end(),
        [](const auto& instruction) { return instruction.mayCall; });
    return call != analysis.blocks[2].instructions.end() &&
        call->liveBefore.contains("counter") &&
        call->uses.contains("may_throw") == false;
}

bool smallFunctionInlining() {
    Module module;
    Function& callee = module.createFunction("addTwo");
    callee.parameters.emplace_back("value", Type::number());
    callee.requiredParameterCount = 1;
    callee.returnType = Type::number();
    auto& calleeEntry = callee.createBlock("entry");
    calleeEntry.emit(resultInstruction(
        "add", "answer", {variable("value", Type::number()), number("2")},
        Type::number()));
    calleeEntry.emit(Instruction{
        "ret", {}, {Value{"answer"}}});

    Function& caller = module.createFunction("main");
    caller.returnType = Type::number();
    auto& callerEntry = caller.createBlock("entry");
    Instruction call{"call", "answer", {Value{"addTwo"}, number("5")}};
    call.resultType = Type::number();
    callerEntry.emit(std::move(call));
    callerEntry.emit(Instruction{
        "ret", {}, {Value{"answer"}}});

    if (zplus::ir::inlineSmallFunctions(module) != 1) return false;
    for (const Instruction& instruction : callerEntry.instructions) {
        if (instruction.opcode == "call") return false;
    }
    return zplus::ir::verifyModule(module).empty();
}

} // namespace

int main() {
    if (!constantPropagationAndFolding()) {
        std::cerr << "constant propagation and folding failed\n";
        return 1;
    }
    if (!cseUsesAreRewrittenLocally()) {
        std::cerr << "local common-subexpression elimination failed\n";
        return 1;
    }
    if (!throwingAndEffectfulInstructionsRemain()) {
        std::cerr << "dead-code elimination removed a potentially throwing operation\n";
        return 1;
    }
    if (!unreachableBlocksAreRemovedAfterConstantBranches()) {
        std::cerr << "constant control-flow simplification failed\n";
        return 1;
    }
    if (!controlFlowSimplificationPreservesExplicitFallthroughEdges()) {
        std::cerr << "control-flow simplification removed an explicit edge\n";
        return 1;
    }
    if (!loopAndExceptionLiveness()) {
        std::cerr << "loop or exception liveness analysis failed\n";
        return 1;
    }
    if (!smallFunctionInlining()) {
        std::cerr << "small-function inlining failed\n";
        return 1;
    }
    return 0;
}
