#include "ir/IRVerifier.hpp"
#include "semantic/Analyzer.hpp"

#include <iostream>

namespace {

bool validReturnIsAccepted() {
    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    function.returnType = zplus::semantic::Type::number();
    auto& entry = function.createBlock("entry");
    zplus::ir::Value value{"1", zplus::ir::Value::Kind::Number};
    value.type = zplus::semantic::Type::number();
    entry.emit(zplus::ir::Instruction{"ret", {}, {value}});
    return zplus::ir::verifyModule(module).empty();
}

bool invalidReturnIsRejected() {
    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    function.returnType = zplus::semantic::Type::number();
    auto& entry = function.createBlock("entry");
    zplus::ir::Value value{"wrong", zplus::ir::Value::Kind::String};
    value.type = zplus::semantic::Type::string();
    entry.emit(zplus::ir::Instruction{"ret", {}, {value}});
    const auto diagnostics = zplus::ir::verifyModule(module);
    return !diagnostics.empty() &&
        diagnostics.front().category == zplus::DiagnosticCategory::IR;
}

bool unreachableImplicitReturnIsAccepted() {
    zplus::ir::Module module;
    auto& function = module.createFunction("choose");
    function.returnType = zplus::semantic::Type::number();
    auto& entry = function.createBlock("entry");
    auto& thenBlock = function.createBlock("then");
    auto& elseBlock = function.createBlock("else");
    function.createBlock("unreachable-join");

    zplus::ir::Value condition{"true", zplus::ir::Value::Kind::Boolean};
    condition.type = zplus::semantic::Type::boolean();
    entry.emit(zplus::ir::Instruction{
        "condbr", {}, {condition, zplus::ir::Value{"then"},
                       zplus::ir::Value{"else"}}});
    zplus::ir::Value thenValue{"1", zplus::ir::Value::Kind::Number};
    thenValue.type = zplus::semantic::Type::number();
    thenBlock.emit(zplus::ir::Instruction{"ret", {}, {thenValue}});
    zplus::ir::Value elseValue{"2", zplus::ir::Value::Kind::Number};
    elseValue.type = zplus::semantic::Type::number();
    elseBlock.emit(zplus::ir::Instruction{"ret", {}, {elseValue}});
    function.blocks.back().emit(zplus::ir::Instruction{"ret"});

    return zplus::ir::verifyModule(module).empty();
}

bool invalidCallIsRejected() {
    zplus::ir::Module module;
    auto& callee = module.createFunction("callee");
    callee.parameters.emplace_back("value", zplus::semantic::Type::number());
    callee.requiredParameterCount = 1;
    auto& calleeEntry = callee.createBlock("entry");
    calleeEntry.emit(zplus::ir::Instruction{"ret"});

    auto& caller = module.createFunction("caller");
    auto& callerEntry = caller.createBlock("entry");
    zplus::ir::Instruction call{"call", "%result"};
    call.resultType = zplus::semantic::Type::number();
    call.operands.emplace_back("callee", zplus::ir::Value::Kind::Identifier);
    zplus::ir::Value argument{"wrong", zplus::ir::Value::Kind::String};
    argument.type = zplus::semantic::Type::string();
    call.operands.push_back(argument);
    callerEntry.emit(std::move(call));
    callerEntry.emit(zplus::ir::Instruction{"ret"});

    const auto diagnostics = zplus::ir::verifyModule(module);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("incompatible IR type") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool genericTypesUnifyAndSubstitute() {
    const zplus::semantic::Type parameter =
        zplus::semantic::Type::typeParameter("T");
    const zplus::semantic::Type pattern =
        zplus::semantic::Type::custom(
            "Box", {zplus::semantic::Type::array(parameter)});
    const zplus::semantic::Type actual =
        zplus::semantic::Type::custom(
            "Box", {zplus::semantic::Type::array(
                       zplus::semantic::Type::number())});
    zplus::semantic::TypeSubstitution substitutions;
    if (!zplus::semantic::unify(pattern, actual, substitutions)) return false;
    if (!zplus::semantic::typesEqual(
            zplus::semantic::substitute(pattern, substitutions), actual)) {
        return false;
    }

    zplus::semantic::TypeSubstitution chained{
        {"T", zplus::semantic::Type::typeParameter("U")},
        {"U", zplus::semantic::Type::number()}
    };
    if (!zplus::semantic::typesEqual(
            zplus::semantic::substitute(
                zplus::semantic::Type::array(parameter), chained),
            zplus::semantic::Type::array(zplus::semantic::Type::number()))) {
        return false;
    }

    zplus::semantic::TypeSubstitution recursive;
    if (zplus::semantic::unify(
            parameter,
            zplus::semantic::Type::array(parameter),
            recursive)) {
        return false;
    }

    const auto genericFunction = zplus::semantic::Type::function(
        {zplus::semantic::Type::array(parameter)}, parameter);
    const auto concreteFunction = zplus::semantic::Type::function(
        {zplus::semantic::Type::array(zplus::semantic::Type::number())},
        zplus::semantic::Type::number());
    zplus::semantic::TypeSubstitution functionSubstitution;
    return zplus::semantic::unify(
        genericFunction, concreteFunction, functionSubstitution) &&
        zplus::semantic::typesEqual(
            zplus::semantic::substitute(genericFunction, functionSubstitution),
            concreteFunction);
}

bool builtinMetadataCoversInterpreterAliasesAndNativeAvailability() {
    zplus::ast::Program program;
    zplus::semantic::Analyzer analyzer;
    if (!analyzer.analyze(program)) return false;
    const auto& registry = analyzer.builtinRegistry();
    const auto* print = registry.find("print");
    const auto* println = registry.find("println");
    const auto* time = registry.find("time");
    const auto* read = registry.find("read");
    const auto* audio = registry.find("__zaudio_play");
    const auto* arrays = registry.find("length");
    if (!print || !println || !time || !read || !audio || !arrays) {
        return false;
    }
    return print->variadic && println->variadic &&
        print->minimumArgumentCount() == 0 &&
        println->minimumArgumentCount() == 0 &&
        print->signature.returnType &&
        print->signature.returnType->kind == zplus::semantic::TypeKind::Void &&
        time->minimumArgumentCount() == 0 &&
        time->signature.returnType &&
        time->signature.returnType->kind == zplus::semantic::TypeKind::Number &&
        print->nativeAvailability ==
            zplus::semantic::NativeAvailability::InterpreterOnly &&
        read->nativeAvailability ==
            zplus::semantic::NativeAvailability::WindowsAndLinuxX64 &&
        audio->nativeAvailability ==
            zplus::semantic::NativeAvailability::WindowsAndLinuxX64 &&
        arrays->nativeAvailability ==
            zplus::semantic::NativeAvailability::WindowsX64;
}

bool invalidBranchTargetIsRejected() {
    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    auto& entry = function.createBlock("entry");
    entry.emit(zplus::ir::Instruction{
        "br", {}, {zplus::ir::Value{"missing"}}});
    const auto diagnostics = zplus::ir::verifyModule(module);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("invalid target label") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool unknownOpcodeIsRejected() {
    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    auto& entry = function.createBlock("entry");
    entry.emit(zplus::ir::Instruction{"not_an_opcode"});
    const auto diagnostics = zplus::ir::verifyModule(module);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("unknown IR opcode") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool switchTargetsAreValidated() {
    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    auto& entry = function.createBlock("entry");
    entry.emit(zplus::ir::Instruction{
        "switch", {},
        {zplus::ir::Value{"1"}, zplus::ir::Value{"1"},
         zplus::ir::Value{"missing-case"}, zplus::ir::Value{"missing-default"}}});
    const auto diagnostics = zplus::ir::verifyModule(module);
    bool foundCase = false;
    bool foundDefault = false;
    for (const auto& diagnostic : diagnostics) {
        foundCase |= diagnostic.message.find("invalid case target") != std::string::npos;
        foundDefault |= diagnostic.message.find("invalid default target") != std::string::npos;
    }
    return foundCase && foundDefault;
}

} // namespace

int main() {
    if (!validReturnIsAccepted()) {
        std::cerr << "valid typed return was rejected\n";
        return 1;
    }
    if (!invalidReturnIsRejected()) {
        std::cerr << "invalid return type was accepted\n";
        return 1;
    }
    if (!unreachableImplicitReturnIsAccepted()) {
        std::cerr << "unreachable implicit return was rejected\n";
        return 1;
    }
    if (!invalidCallIsRejected()) {
        std::cerr << "invalid call argument type was accepted\n";
        return 1;
    }
    if (!genericTypesUnifyAndSubstitute()) {
        std::cerr << "nested generic type substitution failed\n";
        return 1;
    }
    if (!builtinMetadataCoversInterpreterAliasesAndNativeAvailability()) {
        std::cerr << "builtin registry metadata is incomplete or inaccurate\n";
        return 1;
    }
    if (!invalidBranchTargetIsRejected()) {
        std::cerr << "invalid branch target was accepted\n";
        return 1;
    }
    if (!unknownOpcodeIsRejected()) {
        std::cerr << "unknown IR opcode was accepted\n";
        return 1;
    }
    if (!switchTargetsAreValidated()) {
        std::cerr << "invalid switch target was accepted\n";
        return 1;
    }
    return 0;
}
