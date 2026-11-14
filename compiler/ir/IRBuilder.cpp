#include "IR.hpp"
#include "../NumericLiteral.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace zplus::ir {

namespace {

// ============================================================
// AST cast helper
// ============================================================

template <typename T, typename U>
const T* as(const U* node) {
    return dynamic_cast<const T*>(node);
}

// ============================================================
// Terminator detection
// ============================================================

bool isTerminator(const Instruction& instruction) {
    return
        instruction.opcode == "br" ||
        instruction.opcode == "condbr" ||
        instruction.opcode == "switch" ||
        instruction.opcode == "ret" ||
        instruction.opcode == "throw";
}


bool blockTerminated(const BasicBlock* block) {
    if (!block || block->instructions.empty()) {
        return false;
    }

    return isTerminator(block->instructions.back());
}

zplus::semantic::Type irType(const zplus::ast::Type* type) {
    using zplus::semantic::Type;
    if (!type) return Type::unknown();
    switch (type->kind) {
        case zplus::ast::Type::Kind::Number: return Type::number();
        case zplus::ast::Type::Kind::String: return Type::string();
        case zplus::ast::Type::Kind::Character: return Type::character();
        case zplus::ast::Type::Kind::Boolean: return Type::boolean();
        case zplus::ast::Type::Kind::Void: return Type::voidType();
        case zplus::ast::Type::Kind::Custom: return Type::custom(type->name);
        case zplus::ast::Type::Kind::Array:
            return Type::array(irType(type->elementType.get()));
        case zplus::ast::Type::Kind::Pointer:
            return Type::pointer(irType(type->elementType.get()));
        default: return Type::unknown();
    }
}

zplus::semantic::Type resolvedType(
    const zplus::ast::Node& node,
    const zplus::ast::Type* syntax = nullptr
) {
    return node.resolvedType.value_or(irType(syntax));
}

// ============================================================
// Binary operators
// ============================================================

std::string binaryOpcode(zplus::TokenType type) {

    switch (type) {

        case zplus::TokenType::Plus:
            return "add";

        case zplus::TokenType::Minus:
            return "sub";

        case zplus::TokenType::Star:
            return "mul";

        case zplus::TokenType::Slash:
            return "div";

        case zplus::TokenType::Percent:
            return "mod";

        case zplus::TokenType::EqualEqual:
            return "cmp_eq";

        case zplus::TokenType::NotEqual:
            return "cmp_ne";

        case zplus::TokenType::Less:
            return "cmp_lt";

        case zplus::TokenType::LessEqual:
            return "cmp_le";

        case zplus::TokenType::Greater:
            return "cmp_gt";

        case zplus::TokenType::GreaterEqual:
            return "cmp_ge";

        case zplus::TokenType::AndAnd:
        case zplus::TokenType::And:
            return "and";

        case zplus::TokenType::OrOr:
        case zplus::TokenType::Or:
            return "or";

        case zplus::TokenType::BitAnd:
            return "bitand";

        case zplus::TokenType::BitOr:
            return "bitor";

        case zplus::TokenType::BitXor:
            return "bitxor";

        case zplus::TokenType::LeftShift:
            return "shl";

        case zplus::TokenType::RightShift:
            return "shr";

        case zplus::TokenType::Is:
            return "is";

        default:
            throw std::runtime_error(
                "unsupported binary operator"
            );
    }
}


// ============================================================
// Assignment operators
// ============================================================

std::string assignmentOpcode(zplus::TokenType type) {

    switch (type) {

        case zplus::TokenType::Equal:
            return "store";

        case zplus::TokenType::PlusEqual:
            return "add_assign";

        case zplus::TokenType::MinusEqual:
            return "sub_assign";

        case zplus::TokenType::StarEqual:
            return "mul_assign";

        case zplus::TokenType::SlashEqual:
            return "div_assign";

        case zplus::TokenType::PercentEqual:
            return "mod_assign";

        case zplus::TokenType::AndEqual:
            return "and_assign";

        case zplus::TokenType::OrEqual:
            return "or_assign";

        case zplus::TokenType::XorEqual:
            return "xor_assign";

        case zplus::TokenType::LeftShiftEqual:
            return "shl_assign";

        case zplus::TokenType::RightShiftEqual:
            return "shr_assign";

        default:
            throw std::runtime_error(
                "unsupported assignment operator"
            );
    }
}

void assignRuntimeSlots(Module& module) {
    std::unordered_map<std::string, std::size_t> globalSlots;
    for (const auto& name : module.globalVariables) {
        globalSlots.try_emplace(name, globalSlots.size());
    }
    module.runtimeGlobalSlotCount = globalSlots.size();

    for (auto& currentFunction : module.functions) {
        std::unordered_map<std::string, std::size_t> localSlots;
        auto addLocal = [&localSlots](const std::string& name) {
            if (!name.empty()) {
                localSlots.try_emplace(name, localSlots.size());
            }
        };

        for (auto& parameter : currentFunction.parameters) {
            addLocal(parameter.name);
        }
        for (auto& block : currentFunction.blocks) {
            for (auto& instruction : block.instructions) {
                addLocal(instruction.result);
                if (instruction.opcode == "alloc" &&
                    !instruction.operands.empty() &&
                    !globalSlots.contains(instruction.operands[0].name)) {
                    addLocal(instruction.operands[0].name);
                }
            }
        }
        currentFunction.runtimeSlotCount = localSlots.size();

        for (auto& parameter : currentFunction.parameters) {
            const auto slot = localSlots.find(parameter.name);
            if (slot != localSlots.end()) {
                parameter.runtimeSlot = slot->second;
            }
        }
        for (auto& block : currentFunction.blocks) {
            for (auto& instruction : block.instructions) {
                if (const auto result = localSlots.find(instruction.result);
                    result != localSlots.end()) {
                    instruction.resultSlot = result->second;
                }

                for (auto& operand : instruction.operands) {
                    if (const auto local = localSlots.find(operand.name);
                        local != localSlots.end()) {
                        operand.runtimeSlot = local->second;
                    } else if (const auto global = globalSlots.find(operand.name);
                               global != globalSlots.end()) {
                        operand.runtimeSlot = global->second;
                        operand.globalSlot = true;
                    }
                }
            }
        }
    }

    std::unordered_map<const Function*, std::size_t> functionIndices;
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        functionIndices.emplace(&module.functions[i], i);
    }

    for (auto& callerFunction : module.functions) {
        for (auto& block : callerFunction.blocks) {
            for (auto& instruction : block.instructions) {
                if (instruction.opcode != "call" ||
                    instruction.operands.empty()) {
                    continue;
                }

                const Value& callee = instruction.operands.front();
                if (callee.kind != Value::Kind::Identifier ||
                    callee.runtimeSlot != kInvalidRuntimeSlot) {
                    continue;
                }

                const Function* target = module.findFunction(callee.name);
                if (target &&
                    target->name != "say" &&
                    target->name != "println" &&
                    target->name != "printz" &&
                    target->name != "printm" &&
                    target->name != "print" &&
                    target->name != "time" &&
                    target->name != "length" &&
                    target->name != "push" &&
                    target->name != "pop" &&
                    target->name != "clear" &&
                    target->name != "insert" &&
                    target->name != "remove" &&
                    target->name != "contains" &&
                    target->name.rfind("__zgraphics_", 0) != 0) {
                    instruction.calleeFunctionIndex = functionIndices.at(target);
                }
            }
        }
    }
}

} // namespace


// ============================================================
// Build module
// ============================================================

Module IRBuilder::build(
    const zplus::ast::Program& program
) const {

    temporaryCounter_ = 0;
    labelCounter_ = 0;
    defaultFunctionCounter_ = 0;

    breakTargets_.clear();
    continueTargets_.clear();
    breakScopeDepths_.clear();
    loopScopeDepths_.clear();
    deferScopeDepth_ = 0;
    deferScopeIds_.clear();

    Module module;
    module.name = "main";

    buildProgram(
        program,
        module
    );
    assignRuntimeSlots(module);

    return module;
}


// ============================================================
// Program
// ============================================================

void IRBuilder::buildProgram(
    const zplus::ast::Program& program,
    Module& module
) const {

    const auto isSourceBuiltin = [](const std::string& name) {
        return name == "length" || name == "push" || name == "pop" ||
            name == "clear" || name == "insert" || name == "remove" ||
            name == "contains" || name == "read" ||
            name == "read_number" || name == "read_integer" ||
            name == "read_boolean" || name == "say" ||
            name == "printz" || name == "printm";
    };
    std::vector<const zplus::ast::VariableDeclaration*> moduleVariables;
    std::unordered_map<std::string, std::size_t> functionNameCounts;
    std::unordered_map<std::string, std::size_t> variableNameCounts;
    std::unordered_map<std::string, std::size_t> structNameCounts;
    std::unordered_map<std::string, std::size_t> classNameCounts;
    for (const auto& statement : program.statements) {
        if (const auto* function = statement
                ? as<zplus::ast::FunctionDeclaration>(statement.get())
                : nullptr;
            function && !function->moduleName.empty()) {
            ++functionNameCounts[function->name];
            module.functionAliases[function->moduleName + "." + function->name] =
                function->internalName;
        }
        if (const auto* variable = statement
                ? as<zplus::ast::VariableDeclaration>(statement.get())
                : nullptr;
            variable && !variable->moduleName.empty()) {
            ++variableNameCounts[variable->name];
            module.variableAliases[variable->moduleName + "." + variable->name] =
                variable->bindingName.empty()
                    ? (variable->internalName.empty()
                           ? variable->name
                           : variable->internalName)
                    : variable->bindingName;
        }
        if (const auto* structure = statement
                ? as<zplus::ast::StructDeclaration>(statement.get())
                : nullptr;
            structure && !structure->moduleName.empty()) {
            ++structNameCounts[structure->name];
        }
        if (const auto* klass = statement
                ? as<zplus::ast::ClassDeclaration>(statement.get())
                : nullptr;
            klass && !klass->moduleName.empty()) {
            ++classNameCounts[klass->name];
        }
    }
    for (const auto& statement : program.statements) {
        if (const auto* function = statement
                ? as<zplus::ast::FunctionDeclaration>(statement.get())
                : nullptr;
            function && !function->moduleName.empty()) {
            if (functionNameCounts[function->name] == 1 &&
                !isSourceBuiltin(function->name)) {
                module.functionAliases[function->name] = function->internalName;
            }
        }
        if (const auto* variable = statement
                ? as<zplus::ast::VariableDeclaration>(statement.get())
                : nullptr;
            variable && !variable->moduleName.empty() &&
            variableNameCounts[variable->name] == 1) {
            module.variableAliases[variable->name] = variable->bindingName.empty()
                ? (variable->internalName.empty()
                       ? variable->name
                       : variable->internalName)
                : variable->bindingName;
        }
        if (const auto* structure = statement
                ? as<zplus::ast::StructDeclaration>(statement.get())
                : nullptr;
            structure && !structure->moduleName.empty()) {
            const std::string canonical =
                structure->moduleName + "::" + structure->name;
            module.structAliases[structure->moduleName + "." + structure->name] =
                canonical;
            if (structNameCounts[structure->name] == 1) {
                module.structAliases[structure->name] = canonical;
            }
        }
        if (const auto* klass = statement
                ? as<zplus::ast::ClassDeclaration>(statement.get())
                : nullptr;
            klass && !klass->moduleName.empty()) {
            const std::string canonical =
                klass->moduleName + "::" + klass->name;
            module.structAliases[klass->moduleName + "." + klass->name] =
                canonical;
            if (classNameCounts[klass->name] == 1) {
                module.structAliases[klass->name] = canonical;
            }
        }
    }

    std::function<void(const zplus::ast::Statement&)> prepareNamespaceFunctions;
    prepareNamespaceFunctions = [&](const zplus::ast::Statement& statement) {
        const zplus::ast::Statement* declaration = &statement;
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }
        if (const auto* function =
                declaration
                    ? as<zplus::ast::FunctionDeclaration>(declaration)
                    : nullptr) {
            prepareFunction(*function, module);
        } else if (const auto* namespaceNode =
                       declaration
                           ? as<zplus::ast::NamespaceDeclaration>(declaration)
                           : nullptr) {
            for (const auto& child : namespaceNode->body) {
                if (child) prepareNamespaceFunctions(*child);
            }
        }
    };

    for (const auto& statement : program.statements) {
        const zplus::ast::Statement* declaration =
            statement.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }
        if (const auto* function =
                declaration
                    ? as<zplus::ast::FunctionDeclaration>(declaration)
                    : nullptr) {
            prepareFunction(*function, module);
        }
        if (const auto* structure =
                declaration
                    ? as<zplus::ast::StructDeclaration>(declaration)
                    : nullptr) {
            buildStruct(*structure, module);
        }
        if (const auto* klass =
                declaration
                    ? as<zplus::ast::ClassDeclaration>(declaration)
                    : nullptr) {
            buildClass(*klass, module);
        }
        if (const auto* enumeration =
                declaration
                    ? as<zplus::ast::EnumDeclaration>(declaration)
                    : nullptr) {
            buildEnum(*enumeration, module);
        }

        if (const auto* start =
                declaration
                    ? as<zplus::ast::StartStatement>(declaration)
                    : nullptr) {
            for (const auto& child : start->body) {
                const zplus::ast::Statement* nested = child.get();
                while (const auto* exported =
                           nested
                               ? as<zplus::ast::ExportDeclaration>(nested)
                               : nullptr) {
                    nested = exported->declaration.get();
                }
                if (const auto* function =
                        nested
                            ? as<zplus::ast::FunctionDeclaration>(nested)
                            : nullptr) {
                    prepareFunction(*function, module);
                } else if (const auto* variable =
                               nested
                                   ? as<zplus::ast::VariableDeclaration>(nested)
                                   : nullptr) {
                    module.globalVariables.push_back(
                        variable->bindingName.empty()
                            ? (variable->internalName.empty()
                                   ? variable->name
                                   : variable->internalName)
                            : variable->bindingName
                    );
                } else if (const auto* nestedStruct =
                               nested
                                   ? as<zplus::ast::StructDeclaration>(nested)
                                   : nullptr) {
                    buildStruct(*nestedStruct, module);
                } else if (const auto* enumeration =
                               nested
                                   ? as<zplus::ast::EnumDeclaration>(nested)
                                   : nullptr) {
                    buildEnum(*enumeration, module);
                } else if (const auto* klass =
                               nested
                                   ? as<zplus::ast::ClassDeclaration>(nested)
                                   : nullptr) {
                    buildClass(*klass, module);
                } else if (nested &&
                           as<zplus::ast::NamespaceDeclaration>(nested)) {
                    prepareNamespaceFunctions(*nested);
                }
            }
        } else if (declaration &&
                   as<zplus::ast::NamespaceDeclaration>(declaration)) {
            prepareNamespaceFunctions(*declaration);
        }
    }

    Function& main =
        module.createFunction(kTopLevelEntryName);
    main.location = program.location;

    BasicBlock& entry =
        main.createBlock("entry");

    BasicBlock* currentBlock =
        &entry;
    const std::string topLevelScope = nextLabel("scope");
    Instruction topLevelScopePush{"scope_push"};
    topLevelScopePush.scopeId = topLevelScope;
    topLevelScopePush.scopeDepth = deferScopeIds_.size();
    currentBlock->emit(std::move(topLevelScopePush));
    deferScopeIds_.push_back(topLevelScope);
    ++deferScopeDepth_;

    for (const auto& statement :
         program.statements) {

        if (!statement) {
            continue;
        }

        const zplus::ast::Statement* declaration = statement.get();
        while (const auto* exported =
                   as<zplus::ast::ExportDeclaration>(declaration)) {
            declaration = exported->declaration.get();
            if (!declaration) break;
        }

        if (const auto* variable =
                as<zplus::ast::VariableDeclaration>(declaration)) {
            moduleVariables.push_back(variable);
            module.globalVariables.push_back(
                variable->bindingName.empty()
                    ? (variable->internalName.empty()
                           ? variable->name
                           : variable->internalName)
                    : variable->bindingName
            );
            continue;
        }

        // Functions live at module level.
        if (const auto* function =
                as<zplus::ast::FunctionDeclaration>(
                    statement.get())) {

            buildFunction(
                *function,
                module
            );

            continue;
        }

        buildStatement(
            *statement,
            module,
            main,
            currentBlock
        );

        if (!currentBlock) {
            break;
        }

        if (blockTerminated(currentBlock)) {
            break;
        }
    }

    if (
        currentBlock &&
        !blockTerminated(currentBlock)
    ) {

        Instruction topLevelScopePop{"scope_pop"};
        topLevelScopePop.scopeId = topLevelScope;
        currentBlock->emit(std::move(topLevelScopePop));
        currentBlock->emit(
            Instruction{"ret"}
        );
    }
    deferScopeIds_.pop_back();
    --deferScopeDepth_;

    if (!moduleVariables.empty()) {
        Function& initializer =
            module.createFunction(kModuleInitializerName);
        BasicBlock& initializerEntry =
            initializer.createBlock("entry");
        BasicBlock* initializerBlock = &initializerEntry;

        for (const auto* variable : moduleVariables) {
            buildVariable(
                *variable,
                module,
                initializer,
                initializerBlock
            );
        }

        if (initializerBlock && !blockTerminated(initializerBlock)) {
            initializerBlock->emit(Instruction{"ret"});
        }
    }
}


// ============================================================
// Statement dispatch
// ============================================================

void IRBuilder::buildStatement(
    const zplus::ast::Statement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        return;
    }

    if (blockTerminated(currentBlock)) {
        return;
    }

    if (const auto* node =
            as<zplus::ast::BlockStatement>(&statement)) {
        buildScopedStatements(node->statements, module, function, currentBlock);
        return;
    }

    if (const auto* node =
            as<zplus::ast::StartStatement>(
                &statement)) {

        buildStart(
            *node,
            module,
            function
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::VariableDeclaration>(
                &statement)) {

        buildVariable(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ExpressionStatement>(
                &statement)) {

        buildExpressionStatement(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ReturnStatement>(
                &statement)) {

        buildReturn(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }

    if (const auto* node = as<zplus::ast::ThrowStatement>(&statement)) {
        buildThrow(*node, module, function, currentBlock);
        return;
    }

    if (const auto* node = as<zplus::ast::TryStatement>(&statement)) {
        buildTry(*node, module, function, currentBlock);
        return;
    }

    if (const auto* node = as<zplus::ast::DeferStatement>(&statement)) {
        buildDefer(*node, module, function, currentBlock);
        return;
    }


    if (const auto* node =
            as<zplus::ast::IfStatement>(
                &statement)) {

        buildIf(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::WhileStatement>(
                &statement)) {

        buildWhile(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::RepeatStatement>(
                &statement)) {

        buildRepeat(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ForStatement>(
                &statement)) {

        buildFor(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::BreakStatement>(
                &statement)) {

        buildBreak(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ContinueStatement>(
                &statement)) {

        buildContinue(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::FunctionDeclaration>(
                &statement)) {

        buildFunction(
            *node,
            module
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::SwitchStatement>(
                &statement)) {

        buildSwitch(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::StructDeclaration>(
                &statement)) {

        buildStruct(
            *node,
            module
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ClassDeclaration>(
                &statement)) {

        buildClass(
            *node,
            module
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::EnumDeclaration>(
                &statement)) {

        buildEnum(
            *node,
            module
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::NamespaceDeclaration>(
                &statement)) {

        buildNamespace(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    if (const auto* node =
            as<zplus::ast::ImportDeclaration>(
                &statement)) {

        buildImport(
            *node,
            module
        );

        return;
    }

    if (as<zplus::ast::UseDeclaration>(&statement)) {
        throw std::runtime_error("'use' statement was not resolved before IR generation");
    }


    if (const auto* node =
            as<zplus::ast::ExportDeclaration>(
                &statement)) {

        buildExport(
            *node,
            module,
            function,
            currentBlock
        );

        return;
    }


    throw std::runtime_error(
        "unsupported Z+ AST statement"
    );
}


// ============================================================
// start
// ============================================================

void IRBuilder::buildStart(
    const zplus::ast::StartStatement& statement,
    Module& module,
    Function& function
) const {

    BasicBlock* block =
        function.entryBlock();

    if (!block) {
        return;
    }

    buildScopedStatements(statement.body, module, function, block);
}


// ============================================================
// Variable
// ============================================================

void IRBuilder::buildVariable(
    const zplus::ast::VariableDeclaration& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        return;
    }

    if (statement.name.empty()) {
        throw std::runtime_error(
            "variable declaration has an empty name"
        );
    }

    const std::string variableName = !statement.bindingName.empty()
        ? statement.bindingName
        : (statement.internalName.empty()
               ? statement.name
               : statement.internalName);
    Value target{variableName, Value::Kind::Identifier};
    target.type = resolvedType(statement, statement.type.get());
    currentBlock->emit(Instruction{"alloc", {}, {target}});

    if (statement.initializer) {

        Value value =
            buildExpression(
                *statement.initializer,
                module,
                function,
                currentBlock
            );

        if (!currentBlock) {
            throw std::runtime_error(
                "initializer lost its basic block"
            );
        }

        currentBlock->emit(
            Instruction{
                "store",
                {},
                {
                    target,
                    value
                }
            }
        );
    }

    if (statement.isConst) {

        currentBlock->emit(
            Instruction{
                "freeze",
                {},
                {
                    Value{statement.name}
                }
            }
        );
    }
}


// ============================================================
// Expression statement
// ============================================================

void IRBuilder::buildExpressionStatement(
    const zplus::ast::ExpressionStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !statement.expression ||
        !currentBlock
    ) {
        return;
    }

    buildExpression(
        *statement.expression,
        module,
        function,
        currentBlock
    );
}


// ============================================================
// Return
// ============================================================

void IRBuilder::buildReturn(
    const zplus::ast::ReturnStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        return;
    }

    Value value;
    if (
        statement.hasValue &&
        statement.value
    ) {

        value = buildExpression(
            *statement.value,
            module,
            function,
            currentBlock
        );

        if (!currentBlock) {
            throw std::runtime_error(
                "return expression lost its basic block"
            );
        }
    }

    for (auto scope = deferScopeIds_.rbegin();
         scope != deferScopeIds_.rend(); ++scope) {
        Instruction scopePop{"scope_pop"};
        scopePop.scopeId = *scope;
        currentBlock->emit(std::move(scopePop));
    }

    if (statement.hasValue && statement.value) {
        currentBlock->emit(
            Instruction{
                "ret",
                {},
                {value}
            }
        );

    } else {

        currentBlock->emit(
            Instruction{"ret"}
        );
    }
}

void IRBuilder::buildThrow(
    const zplus::ast::ThrowStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {
    if (!currentBlock || !statement.value) return;
    Value value = buildExpression(
        *statement.value, module, function, currentBlock);
    if (!currentBlock) {
        throw std::runtime_error("throw expression lost its basic block");
    }
    Instruction instruction{"throw", {}, {value}};
    instruction.location = statement.location;
    currentBlock->emit(std::move(instruction));
}

void IRBuilder::buildTry(
    const zplus::ast::TryStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {
    if (!currentBlock) return;

    BasicBlock& tryBlock = createBlock(function, "try_body");
    BasicBlock& catchBlock = createBlock(function, "catch");
    BasicBlock& mergeBlock = createBlock(function, "try_merge");
    const std::string catchName = statement.catchBindingName.empty()
        ? statement.catchName
        : statement.catchBindingName;
    const auto branchTo = [](BasicBlock& from, const std::string& label) {
        from.emit(Instruction{"br", {}, {Value{label, Value::Kind::Identifier}}});
    };

    branchTo(*currentBlock, tryBlock.label);
    currentBlock = &tryBlock;
    currentLocation_ = statement.location;
    currentBlock->emit(Instruction{
        "alloc", {}, {Value{catchName, Value::Kind::Identifier}}});
    Instruction begin{"try_push", {}, {
        Value{catchBlock.label, Value::Kind::Identifier},
        Value{catchName, Value::Kind::Identifier}
    }};
    begin.location = statement.location;
    begin.handlerScopeDepth = deferScopeIds_.size();
    currentBlock->emit(std::move(begin));
    buildScopedStatements(statement.tryBody, module, function, currentBlock);
    if (currentBlock && !blockTerminated(currentBlock)) {
        currentBlock->emit(Instruction{
            "try_pop", {}, {Value{catchBlock.label, Value::Kind::Identifier}}});
        branchTo(*currentBlock, mergeBlock.label);
    }

    currentBlock = &catchBlock;
    buildScopedStatements(statement.catchBody, module, function, currentBlock);
    if (currentBlock && !blockTerminated(currentBlock)) {
        branchTo(*currentBlock, mergeBlock.label);
    }
    currentBlock = &mergeBlock;
}

void IRBuilder::buildDefer(
    const zplus::ast::DeferStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {
    if (!currentBlock || statement.body.size() != 1 || !statement.body.front()) {
        throw std::runtime_error("defer requires one function call");
    }
    const auto* expressionStatement =
        as<zplus::ast::ExpressionStatement>(statement.body.front().get());
    const auto* call = expressionStatement && expressionStatement->expression
        ? as<zplus::ast::CallExpression>(expressionStatement->expression.get())
        : nullptr;
    if (!call || !call->callee) {
        throw std::runtime_error("defer requires one function call");
    }

    Value callee = buildExpression(*call->callee, module, function, currentBlock);
    if (!currentBlock) {
        throw std::runtime_error("deferred call lost its basic block");
    }
    std::vector<Value> operands;
    operands.reserve(call->arguments.size() + 1);
    operands.push_back(std::move(callee));
    for (const auto& argument : call->arguments) {
        if (!argument) {
            throw std::runtime_error("deferred call has a missing argument");
        }
        operands.push_back(
            buildExpression(*argument, module, function, currentBlock));
        if (!currentBlock) {
            throw std::runtime_error("deferred argument lost its basic block");
        }
    }
    Instruction instruction{"defer_push", {}, std::move(operands)};
    instruction.location = statement.location;
    if (deferScopeIds_.empty()) {
        throw std::runtime_error("defer has no enclosing scope");
    }
    instruction.scopeId = deferScopeIds_.back();
    instruction.siteId = nextLabel("defer");
    currentBlock->emit(std::move(instruction));
}

void IRBuilder::buildScopedStatements(
    const std::vector<zplus::ast::Stmt>& statements,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {
    if (!currentBlock) return;
    const std::string scopeId = nextLabel("scope");
    Instruction scopePush{"scope_push"};
    scopePush.scopeId = scopeId;
    scopePush.scopeDepth = deferScopeIds_.size();
    currentBlock->emit(std::move(scopePush));
    deferScopeIds_.push_back(scopeId);
    ++deferScopeDepth_;
    for (const auto& child : statements) {
        if (!child || !currentBlock) continue;
        buildStatement(*child, module, function, currentBlock);
        if (!currentBlock || blockTerminated(currentBlock)) break;
    }
    if (currentBlock && !blockTerminated(currentBlock)) {
        Instruction scopePop{"scope_pop"};
        scopePop.scopeId = scopeId;
        currentBlock->emit(std::move(scopePop));
    }
    deferScopeIds_.pop_back();
    --deferScopeDepth_;
}


// ============================================================
// If
// ============================================================

void IRBuilder::buildIf(
    const zplus::ast::IfStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        !statement.condition
    ) {
        return;
    }

    Value condition =
        buildExpression(
            *statement.condition,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "if condition lost its basic block"
        );
    }

    BasicBlock& thenBlock =
        createBlock(
            function,
            "if.then"
        );

    BasicBlock& endBlock =
        createBlock(
            function,
            "if.end"
        );

    BasicBlock* elseBlock = nullptr;

    if (statement.hasElse) {

        elseBlock =
            &createBlock(
                function,
                "if.else"
            );
    }

    currentBlock->emit(
        Instruction{
            "condbr",
            {},
            {
                condition,
                Value{thenBlock.label},
                Value{
                    statement.hasElse
                        ? elseBlock->label
                        : endBlock.label
                }
            }
        }
    );


    // --------------------------------------------------------
    // Then
    // --------------------------------------------------------

    BasicBlock* thenCurrent = &thenBlock;
    buildScopedStatements(
        statement.thenBody, module, function, thenCurrent);

    if (
        thenCurrent &&
        !blockTerminated(thenCurrent)
    ) {

        thenCurrent->emit(
            Instruction{
                "br",
                {},
                {
                    Value{endBlock.label}
                }
            }
        );
    }


    // --------------------------------------------------------
    // Else
    // --------------------------------------------------------

    if (elseBlock) {

        BasicBlock* elseCurrent = elseBlock;
        buildScopedStatements(
            statement.elseBody, module, function, elseCurrent);

        if (
            elseCurrent &&
            !blockTerminated(elseCurrent)
        ) {

            elseCurrent->emit(
                Instruction{
                    "br",
                    {},
                    {
                        Value{endBlock.label}
                    }
                }
            );
        }
    }

    currentBlock =
        &endBlock;
}


// ============================================================
// While
// ============================================================

void IRBuilder::buildWhile(
    const zplus::ast::WhileStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        !statement.condition
    ) {
        return;
    }

    BasicBlock& conditionBlock =
        createBlock(
            function,
            "while.cond"
        );

    BasicBlock& bodyBlock =
        createBlock(
            function,
            "while.body"
        );

    BasicBlock& endBlock =
        createBlock(
            function,
            "while.end"
        );

    currentBlock->emit(
        Instruction{
            "br",
            {},
            {
                Value{conditionBlock.label}
            }
        }
    );


    // --------------------------------------------------------
    // Condition
    // --------------------------------------------------------

    BasicBlock* conditionCurrent =
        &conditionBlock;

    Value condition =
        buildExpression(
            *statement.condition,
            module,
            function,
            conditionCurrent
        );

    if (!conditionCurrent) {
        throw std::runtime_error(
            "while condition lost its basic block"
        );
    }

    conditionCurrent->emit(
        Instruction{
            "condbr",
            {},
            {
                condition,
                Value{bodyBlock.label},
                Value{endBlock.label}
            }
        }
    );


    // --------------------------------------------------------
    // Loop context
    // --------------------------------------------------------

    breakTargets_.push_back(
        endBlock.label
    );
    breakScopeDepths_.push_back(deferScopeDepth_);
    loopScopeDepths_.push_back(deferScopeDepth_);

    continueTargets_.push_back(
        conditionBlock.label
    );


    // --------------------------------------------------------
    // Body
    // --------------------------------------------------------

    BasicBlock* bodyCurrent = &bodyBlock;
    buildScopedStatements(statement.body, module, function, bodyCurrent);

    if (
        bodyCurrent &&
        !blockTerminated(bodyCurrent)
    ) {

        bodyCurrent->emit(
            Instruction{
                "br",
                {},
                {
                    Value{conditionBlock.label}
                }
            }
        );
    }


    breakTargets_.pop_back();
    continueTargets_.pop_back();
    loopScopeDepths_.pop_back();
    breakScopeDepths_.pop_back();

    currentBlock =
        &endBlock;
}


// ============================================================
// Repeat
// ============================================================
//
// repeat (N):
//
//     body
//
// end
//
// becomes:
//
//     counter = N
//
// repeat.cond:
//     cmp_gt counter, 0
//     condbr cmp, repeat.body, repeat.end
//
// repeat.body:
//     body
//     dec counter
//     br repeat.cond
//
// repeat.end:
//
// IMPORTANT:
// N does NOT get expanded into N IR instructions.
// repeat (1000000) still creates only 3 blocks.
//

void IRBuilder::buildRepeat(
    const zplus::ast::RepeatStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        !statement.count
    ) {
        return;
    }

    Value count =
        buildExpression(
            *statement.count,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "repeat count lost its basic block"
        );
    }


    // --------------------------------------------------------
    // Counter
    // --------------------------------------------------------

    const std::string counter =
        nextTemporary();

    currentBlock->emit(
        Instruction{
            "mov",
            counter,
            {
                count
            }
        }
    );


    // --------------------------------------------------------
    // Blocks
    // --------------------------------------------------------

    BasicBlock& conditionBlock =
        createBlock(
            function,
            "repeat.cond"
        );

    BasicBlock& bodyBlock =
        createBlock(
            function,
            "repeat.body"
        );

    BasicBlock& endBlock =
        createBlock(
            function,
            "repeat.end"
        );


    currentBlock->emit(
        Instruction{
            "br",
            {},
            {
                Value{conditionBlock.label}
            }
        }
    );


    // --------------------------------------------------------
    // Condition
    // --------------------------------------------------------

    const std::string comparison =
        nextTemporary();

    conditionBlock.emit(
        Instruction{
            "cmp_gt",
            comparison,
            {
                Value{counter},
                Value{"0"}
            }
        }
    );

    conditionBlock.emit(
        Instruction{
            "condbr",
            {},
            {
                Value{comparison},
                Value{bodyBlock.label},
                Value{endBlock.label}
            }
        }
    );


    // --------------------------------------------------------
    // Loop context
    // --------------------------------------------------------

    breakTargets_.push_back(
        endBlock.label
    );
    breakScopeDepths_.push_back(deferScopeDepth_);
    loopScopeDepths_.push_back(deferScopeDepth_);

    continueTargets_.push_back(
        conditionBlock.label
    );


    // --------------------------------------------------------
    // Body
    // --------------------------------------------------------

    BasicBlock* bodyCurrent = &bodyBlock;
    buildScopedStatements(statement.body, module, function, bodyCurrent);


    // --------------------------------------------------------
    // Decrement
    // --------------------------------------------------------

    if (
        bodyCurrent &&
        !blockTerminated(bodyCurrent)
    ) {

        bodyCurrent->emit(
            Instruction{
                "dec",
                {},
                {
                    Value{counter}
                }
            }
        );

        bodyCurrent->emit(
            Instruction{
                "br",
                {},
                {
                    Value{conditionBlock.label}
                }
            }
        );
    }


    breakTargets_.pop_back();
    continueTargets_.pop_back();
    loopScopeDepths_.pop_back();
    breakScopeDepths_.pop_back();

    currentBlock =
        &endBlock;
}


// ============================================================
// For
// ============================================================

void IRBuilder::buildFor(
    const zplus::ast::ForStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        return;
    }


    // --------------------------------------------------------
    // Initializer
    // --------------------------------------------------------

    if (statement.initializer) {

        buildStatement(
            *statement.initializer,
            module,
            function,
            currentBlock
        );

        if (!currentBlock) {
            return;
        }
    }


    // --------------------------------------------------------
    // Blocks
    // --------------------------------------------------------

    BasicBlock& conditionBlock =
        createBlock(
            function,
            "for.cond"
        );

    BasicBlock& bodyBlock =
        createBlock(
            function,
            "for.body"
        );

    BasicBlock& incrementBlock =
        createBlock(
            function,
            "for.inc"
        );

    BasicBlock& endBlock =
        createBlock(
            function,
            "for.end"
        );


    currentBlock->emit(
        Instruction{
            "br",
            {},
            {
                Value{conditionBlock.label}
            }
        }
    );


    // --------------------------------------------------------
    // Condition
    // --------------------------------------------------------

    BasicBlock* conditionCurrent =
        &conditionBlock;

    if (statement.condition) {

        Value condition =
            buildExpression(
                *statement.condition,
                module,
                function,
                conditionCurrent
            );

        if (!conditionCurrent) {
            throw std::runtime_error(
                "for condition lost its basic block"
            );
        }

        conditionCurrent->emit(
            Instruction{
                "condbr",
                {},
                {
                    condition,
                    Value{bodyBlock.label},
                    Value{endBlock.label}
                }
            }
        );

    } else {

        conditionBlock.emit(
            Instruction{
                "br",
                {},
                {
                    Value{bodyBlock.label}
                }
            }
        );
    }


    // --------------------------------------------------------
    // Loop context
    // --------------------------------------------------------

    breakTargets_.push_back(
        endBlock.label
    );
    breakScopeDepths_.push_back(deferScopeDepth_);
    loopScopeDepths_.push_back(deferScopeDepth_);

    continueTargets_.push_back(
        incrementBlock.label
    );


    // --------------------------------------------------------
    // Body
    // --------------------------------------------------------

    BasicBlock* bodyCurrent = &bodyBlock;
    buildScopedStatements(statement.body, module, function, bodyCurrent);


    if (
        bodyCurrent &&
        !blockTerminated(bodyCurrent)
    ) {

        bodyCurrent->emit(
            Instruction{
                "br",
                {},
                {
                    Value{incrementBlock.label}
                }
            }
        );
    }


    // --------------------------------------------------------
    // Increment
    // --------------------------------------------------------

    BasicBlock* incrementCurrent =
        &incrementBlock;

    if (statement.increment) {

        buildExpression(
            *statement.increment,
            module,
            function,
            incrementCurrent
        );
    }

    if (
        incrementCurrent &&
        !blockTerminated(incrementCurrent)
    ) {

        incrementCurrent->emit(
            Instruction{
                "br",
                {},
                {
                    Value{conditionBlock.label}
                }
            }
        );
    }


    breakTargets_.pop_back();
    continueTargets_.pop_back();
    loopScopeDepths_.pop_back();
    breakScopeDepths_.pop_back();

    currentBlock =
        &endBlock;
}


// ============================================================
// Break
// ============================================================

void IRBuilder::buildBreak(
    const zplus::ast::BreakStatement&,
    Module&,
    Function&,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        breakTargets_.empty()
    ) {
        return;
    }

    if (blockTerminated(currentBlock)) {
        return;
    }

    const std::size_t scopeDepth = breakScopeDepths_.empty()
        ? 0
        : breakScopeDepths_.back();
    for (std::size_t i = deferScopeIds_.size(); i > scopeDepth; --i) {
        Instruction scopePop{"scope_pop"};
        scopePop.scopeId = deferScopeIds_[i - 1];
        currentBlock->emit(std::move(scopePop));
    }

    currentBlock->emit(
        Instruction{
            "br",
            {},
            {
                Value{
                    breakTargets_.back()
                }
            }
        }
    );
}


// ============================================================
// Continue
// ============================================================

void IRBuilder::buildContinue(
    const zplus::ast::ContinueStatement&,
    Module&,
    Function&,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        continueTargets_.empty()
    ) {
        return;
    }

    if (blockTerminated(currentBlock)) {
        return;
    }

    const std::size_t scopeDepth = loopScopeDepths_.empty()
        ? 0
        : loopScopeDepths_.back();
    for (std::size_t i = deferScopeIds_.size(); i > scopeDepth; --i) {
        Instruction scopePop{"scope_pop"};
        scopePop.scopeId = deferScopeIds_[i - 1];
        currentBlock->emit(std::move(scopePop));
    }

    currentBlock->emit(
        Instruction{
            "br",
            {},
            {
                Value{
                    continueTargets_.back()
                }
            }
        }
    );
}


// ============================================================
// Function
// ============================================================

void IRBuilder::buildFunction(
    const zplus::ast::FunctionDeclaration& statement,
    Module& module
) const {

    if (statement.name.empty()) {
        throw std::runtime_error(
            "function declaration has an empty name"
        );
    }

    const std::string functionName = statement.internalName.empty()
        ? statement.name
        : statement.internalName;
    Function* functionPointer = module.findFunction(functionName);
    if (!functionPointer) {
        prepareFunction(statement, module);
        functionPointer = module.findFunction(functionName);
    }
    Function& function = *functionPointer;
    if (!function.blocks.empty()) {
        return;
    }

    const std::size_t receiverOffset = statement.ownerClass.empty() ? 0 : 1;

    for (std::size_t i = 0; i < statement.parameters.size(); ++i) {
        const auto& parameter = statement.parameters[i];
        if (!parameter || !parameter->defaultValue) {
            continue;
        }

        Function* helperPointer =
            module.findFunction(
                function.parameters[i + receiverOffset].defaultFunction);
        if (!helperPointer || !helperPointer->blocks.empty()) {
            throw std::runtime_error(
                "default argument helper was not prepared correctly"
            );
        }
        Function& helper = *helperPointer;
        BasicBlock& helperEntry = helper.createBlock("entry");
        BasicBlock* helperBlock = &helperEntry;
        for (const auto& previous : helper.parameters) {
            helperBlock->emit(Instruction{"param", {}, {Value{previous.name}}});
        }

        const Value defaultValue = buildExpression(
            *parameter->defaultValue,
            module,
            helper,
            helperBlock
        );
        if (!helperBlock || blockTerminated(helperBlock)) {
            throw std::runtime_error(
                "default argument expression did not produce a return value"
            );
        }
        helperBlock->emit(Instruction{"ret", {}, {defaultValue}});
    }

    BasicBlock& entry = function.createBlock("entry");
    BasicBlock* currentBlock = &entry;

    if (receiverOffset != 0) {
        Value receiver{"this", Value::Kind::Identifier};
        receiver.type = zplus::semantic::Type::custom(statement.ownerClass);
        currentBlock->emit(
            Instruction{"param", {}, {receiver}}
        );
    }

    for (const auto& parameter : statement.parameters) {
        if (!parameter) {
            continue;
        }
        Value argument{
            parameter->bindingName.empty()
                ? parameter->name
                : parameter->bindingName,
            Value::Kind::Identifier
        };
        argument.type = resolvedType(*parameter, parameter->type.get());
        currentBlock->emit(
            Instruction{
                "param",
                {},
                {argument}
            }
        );
    }

    // --------------------------------------------------------
    // Body
    // --------------------------------------------------------

    const std::size_t savedDeferDepth = deferScopeDepth_;
    std::vector<std::string> savedDeferScopeIds =
        std::move(deferScopeIds_);
    deferScopeDepth_ = 0;
    deferScopeIds_.clear();
    buildScopedStatements(statement.body, module, function, currentBlock);
    deferScopeDepth_ = savedDeferDepth;
    deferScopeIds_ = std::move(savedDeferScopeIds);


    // --------------------------------------------------------
    // Implicit return
    // --------------------------------------------------------

    if (
        currentBlock &&
        !blockTerminated(currentBlock)
    ) {

        currentBlock->emit(
            Instruction{"ret"}
        );
    }
}

void IRBuilder::prepareFunction(
    const zplus::ast::FunctionDeclaration& statement,
    Module& module
) const {
    if (statement.name.empty()) {
        throw std::runtime_error("function declaration has an empty name");
    }

    const std::string functionName = statement.internalName.empty()
        ? statement.name
        : statement.internalName;
    if (module.findFunction(functionName)) {
        throw std::runtime_error(
            "duplicate function '" + functionName + "'"
        );
    }

    Function& function = module.createFunction(functionName);
    function.location = statement.location;
    const zplus::semantic::Type signature = resolvedType(statement);
    function.returnType = signature.kind == zplus::semantic::TypeKind::Function &&
            signature.returnType
        ? *signature.returnType
        : (statement.returnType
               ? irType(statement.returnType.get())
               : zplus::semantic::Type::voidType());
    if (!statement.ownerClass.empty()) {
        function.parameters.emplace_back(
            "this", zplus::semantic::Type::custom(statement.ownerClass));
    }
    function.requiredParameterCount =
        statement.ownerClass.empty() ? 0 : 1;
    for (const auto& parameter : statement.parameters) {
        if (!parameter) {
            continue;
        }
        function.parameters.emplace_back(
            parameter->bindingName.empty()
                ? parameter->name
                : parameter->bindingName,
            resolvedType(*parameter, parameter->type.get())
        );
        if (function.requiredParameterCount == function.parameters.size() - 1 &&
            !parameter->defaultValue) {
            ++function.requiredParameterCount;
        }
    }

    for (std::size_t i = 0; i < statement.parameters.size(); ++i) {
        const auto& parameter = statement.parameters[i];
        if (!parameter || !parameter->defaultValue) {
            continue;
        }

        const std::string helperName =
            "__zplus_default$" + std::to_string(defaultFunctionCounter_++);
        const std::size_t receiverOffset = statement.ownerClass.empty() ? 0 : 1;
        function.parameters[i + receiverOffset].defaultFunction = helperName;

        Function& helper = module.createFunction(helperName);
        helper.location = parameter->location;
        helper.returnType = resolvedType(*parameter, parameter->type.get());
        if (!statement.ownerClass.empty()) {
            helper.parameters.emplace_back(
                "this", zplus::semantic::Type::custom(statement.ownerClass));
        }
        for (std::size_t preceding = 0; preceding < i; ++preceding) {
            const auto& previous = statement.parameters[preceding];
            if (!previous) {
                continue;
            }
            helper.parameters.emplace_back(
                previous->bindingName.empty()
                    ? previous->name
                    : previous->bindingName,
                resolvedType(*previous, previous->type.get())
            );
        }
        helper.requiredParameterCount = helper.parameters.size();
    }
}


// ============================================================
// Switch
// ============================================================

void IRBuilder::buildSwitch(
    const zplus::ast::SwitchStatement& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !currentBlock ||
        !statement.expression
    ) {
        return;
    }

    Value value =
        buildExpression(
            *statement.expression,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "switch expression lost its basic block"
        );
    }


    BasicBlock& endBlock =
        createBlock(
            function,
            "switch.end"
        );


    std::vector<BasicBlock*> caseBlocks;

    for (std::size_t i = 0;
         i < statement.cases.size();
         ++i) {

        caseBlocks.push_back(
            &createBlock(
                function,
                "switch.case"
            )
        );
    }


    BasicBlock* defaultBlock =
        nullptr;

    if (statement.hasDefault) {

        defaultBlock =
            &createBlock(
                function,
                "switch.default"
            );
    }


    // --------------------------------------------------------
    // Switch instruction
    // --------------------------------------------------------

    Instruction instruction{
        "switch"
    };

    instruction.operands.push_back(
        value
    );


    for (std::size_t i = 0;
         i < statement.cases.size();
         ++i) {

        if (!statement.cases[i]) {
            continue;
        }

        if (!statement.cases[i]->value) {
            continue;
        }

        Value caseValue =
            buildExpression(
                *statement.cases[i]->value,
                module,
                function,
                currentBlock
            );

        if (!currentBlock) {
            throw std::runtime_error(
                "switch case expression lost its basic block"
            );
        }

        instruction.operands.push_back(
            caseValue
        );

        instruction.operands.push_back(
            Value{
                caseBlocks[i]->label
            }
        );
    }


    instruction.operands.push_back(
        Value{
            defaultBlock
                ? defaultBlock->label
                : endBlock.label
        }
    );


    currentBlock->emit(
        std::move(instruction)
    );


    // --------------------------------------------------------
    // Case context
    // --------------------------------------------------------

    breakTargets_.push_back(
        endBlock.label
    );
    breakScopeDepths_.push_back(deferScopeDepth_);


    // --------------------------------------------------------
    // Cases
    // --------------------------------------------------------

    for (std::size_t i = 0;
         i < statement.cases.size();
         ++i) {

        if (!statement.cases[i]) {
            continue;
        }

        BasicBlock* caseCurrent = caseBlocks[i];
        buildScopedStatements(
            statement.cases[i]->body, module, function, caseCurrent);

        if (
            caseCurrent &&
            !blockTerminated(caseCurrent)
        ) {

            caseCurrent->emit(
                Instruction{
                    "br",
                    {},
                    {
                        Value{endBlock.label}
                    }
                }
            );
        }
    }


    // --------------------------------------------------------
    // Default
    // --------------------------------------------------------

    if (defaultBlock) {

        BasicBlock* defaultCurrent = defaultBlock;
        buildScopedStatements(
            statement.defaultBody, module, function, defaultCurrent);

        if (
            defaultCurrent &&
            !blockTerminated(defaultCurrent)
        ) {

            defaultCurrent->emit(
                Instruction{
                    "br",
                    {},
                    {
                        Value{endBlock.label}
                    }
                }
            );
        }
    }


    breakTargets_.pop_back();
    breakScopeDepths_.pop_back();

    currentBlock =
        &endBlock;
}


// ============================================================
// Struct
// ============================================================

void IRBuilder::buildStruct(
    const zplus::ast::StructDeclaration& statement,
    Module& module
) const {

    if (statement.name.empty()) {
        throw std::runtime_error(
            "struct declaration has an empty name"
        );
    }

    const std::string canonicalName = statement.moduleName.empty()
        ? statement.name
        : statement.moduleName + "::" + statement.name;
    if (module.findStructType(canonicalName)) return;

    StructType type;
    type.name = canonicalName;
    for (const auto& field : statement.fields) {
        if (!field) continue;
        StructField irField;
        irField.name = field->name;
        irField.type = resolvedType(*field, field->type.get());
        if (field->defaultValue) {
            const auto* literal =
                as<zplus::ast::Literal>(field->defaultValue.get());
            if (!literal) {
                throw std::runtime_error(
                    "struct field defaults must be literals in the current IR"
                );
            }
            irField.defaultValue = buildLiteral(*literal);
            irField.hasDefault = true;
        }
        type.fields.push_back(std::move(irField));
    }
    module.structTypes.push_back(std::move(type));
    module.types.push_back("struct:" + statement.name);
}


// ============================================================
// Class
// ============================================================

void IRBuilder::buildClass(
    const zplus::ast::ClassDeclaration& statement,
    Module& module
) const {

    if (statement.name.empty()) {
        throw std::runtime_error(
            "class declaration has an empty name"
        );
    }

    const std::string canonicalName = statement.moduleName.empty()
        ? statement.name
        : statement.moduleName + "::" + statement.name;
    const std::string typeName = "class:" + canonicalName;
    if (std::find(module.types.begin(), module.types.end(), typeName) ==
        module.types.end()) {
        module.types.push_back(typeName);
    }

    if (!module.findStructType(canonicalName)) {
        StructType type;
        type.name = canonicalName;
        for (const auto& field : statement.fields) {
            if (!field) continue;
            StructField irField;
            irField.name = field->name;
            irField.type = resolvedType(*field, field->type.get());
            if (field->initializer) {
                const auto* literal =
                    as<zplus::ast::Literal>(field->initializer.get());
                if (!literal) {
                    throw std::runtime_error(
                        "class field initializers must be literals");
                }
                irField.defaultValue = buildLiteral(*literal);
                irField.hasDefault = true;
            }
            type.fields.push_back(std::move(irField));
        }
        module.structTypes.push_back(std::move(type));
    }

    if (!statement.moduleName.empty()) {
        module.structAliases[statement.moduleName + "." + statement.name] =
            canonicalName;
    }

    for (const auto& method :
         statement.methods) {

        if (!method) {
            continue;
        }

        buildFunction(
            *method,
            module
        );
    }
}


// ============================================================
// Enum
// ============================================================

void IRBuilder::buildEnum(
    const zplus::ast::EnumDeclaration& statement,
    Module& module
) const {

    if (statement.name.empty()) {
        throw std::runtime_error(
            "enum declaration has an empty name"
        );
    }

    const std::string canonicalName = statement.moduleName.empty()
        ? statement.name
        : statement.moduleName + "::" + statement.name;
    const std::string typeName = "enum:" + canonicalName;
    if (std::find(module.types.begin(), module.types.end(), typeName) !=
        module.types.end()) {
        return;
    }

    module.types.push_back(typeName);
    double nextValue = 0.0;
    for (const auto& member : statement.values) {
        if (!member) continue;

        double numericValue = nextValue;
        if (member->value) {
            const zplus::ast::Literal* literal =
                as<zplus::ast::Literal>(member->value.get());
            double sign = 1.0;
            if (const auto* unary =
                    as<zplus::ast::UnaryExpression>(member->value.get())) {
                if (unary->operatorType == zplus::TokenType::Minus) {
                    sign = -1.0;
                }
                literal = as<zplus::ast::Literal>(unary->operand.get());
            }
            if (!literal || literal->kind != zplus::ast::Literal::Kind::Number ||
                !zplus::numeric::parseLiteral(literal->value, numericValue)) {
                throw std::runtime_error(
                    "enum values must be numeric literals");
            }
            numericValue *= sign;
        }

        Value value{std::to_string(numericValue), Value::Kind::Number};
        value.numericValue = numericValue;
        value.hasNumericValue = true;
        const std::string memberName = statement.name + "." + member->name;
        module.enumConstants[memberName] = value;
        if (!statement.moduleName.empty()) {
            module.enumConstants[statement.moduleName + "." + memberName] = value;
            module.enumConstants[canonicalName + "." + member->name] = value;
        }
        nextValue = numericValue + 1.0;
    }
}


// ============================================================
// Namespace
// ============================================================

void IRBuilder::buildNamespace(
    const zplus::ast::NamespaceDeclaration& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (statement.name.empty()) {
        throw std::runtime_error(
            "namespace declaration has an empty name"
        );
    }

    module.types.push_back(
        "namespace:" +
        statement.name
    );

    for (const auto& child :
         statement.body) {

        if (!child || !currentBlock) {
            continue;
        }

        const zplus::ast::Statement* declaration = child.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }
        if (const auto* functionDeclaration =
                declaration
                    ? as<zplus::ast::FunctionDeclaration>(declaration)
                    : nullptr;
            functionDeclaration &&
            !functionDeclaration->namespaceName.empty()) {
            module.functionAliases[
                functionDeclaration->namespaceName + "." +
                functionDeclaration->name
            ] = functionDeclaration->internalName;
        }

        buildStatement(
            *child,
            module,
            function,
            currentBlock
        );

        if (
            !currentBlock ||
            blockTerminated(currentBlock)
        ) {
            break;
        }
    }
}


// ============================================================
// Import
// ============================================================

void IRBuilder::buildImport(
    const zplus::ast::ImportDeclaration& statement,
    Module& module
) const {

    if (statement.module.empty()) {
        throw std::runtime_error(
            "import has an empty module name"
        );
    }

    Instruction instruction{
        "import"
    };

    instruction.operands.push_back(
        Value{
            statement.module
        }
    );

    if (!statement.alias.empty()) {

        instruction.operands.push_back(
            Value{
                statement.alias
            }
        );
    }

    module.globals.push_back(
        std::move(instruction)
    );
}


// ============================================================
// Export
// ============================================================

void IRBuilder::buildExport(
    const zplus::ast::ExportDeclaration& statement,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!statement.declaration) {
        return;
    }

    buildStatement(
        *statement.declaration,
        module,
        function,
        currentBlock
    );
}


// ============================================================
// Expression dispatch
// ============================================================

Value IRBuilder::buildExpression(
    const zplus::ast::Expression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        throw std::runtime_error(
            "cannot emit expression without a basic block"
        );
    }

    std::unordered_map<const BasicBlock*, std::size_t> instructionCounts;
    for (const auto& block : function.blocks) {
        instructionCounts.emplace(&block, block.instructions.size());
    }

    Value result;
    if (const auto* literal = as<zplus::ast::Literal>(&expression)) {
        result = buildLiteral(*literal);
    } else if (const auto* identifier = as<zplus::ast::Identifier>(&expression)) {
        result = buildIdentifier(*identifier);
        result.name = module.resolveVariable(
            module.resolveFunction(result.name)
        );
    } else if (const auto* unary = as<zplus::ast::UnaryExpression>(&expression)) {
        result = buildUnary(*unary, module, function, currentBlock);
    } else if (const auto* binary = as<zplus::ast::BinaryExpression>(&expression)) {
        result = buildBinary(*binary, module, function, currentBlock);
    } else if (const auto* assignment = as<zplus::ast::AssignmentExpression>(&expression)) {
        result = buildAssignment(*assignment, module, function, currentBlock);
    } else if (const auto* conditional = as<zplus::ast::ConditionalExpression>(&expression)) {
        result = buildConditional(*conditional, module, function, currentBlock);
    } else if (const auto* call = as<zplus::ast::CallExpression>(&expression)) {
        result = buildCall(*call, module, function, currentBlock);
    } else if (const auto* index = as<zplus::ast::IndexExpression>(&expression)) {
        result = buildIndex(*index, module, function, currentBlock);
    } else if (const auto* member = as<zplus::ast::MemberExpression>(&expression)) {
        result = buildMember(*member, module, function, currentBlock);
    } else if (const auto* array = as<zplus::ast::ArrayExpression>(&expression)) {
        result = buildArray(*array, module, function, currentBlock);
    } else {
        throw std::runtime_error("unsupported Z+ AST expression");
    }

    if (!expression.location.sourceFile.empty()) {
        for (auto& block : function.blocks) {
            const auto prior = instructionCounts.find(&block);
            const std::size_t begin = prior == instructionCounts.end()
                ? 0
                : prior->second;
            for (std::size_t i = begin; i < block.instructions.size(); ++i) {
                if (block.instructions[i].location.sourceFile.empty()) {
                    block.instructions[i].location = expression.location;
                }
            }
        }
    }
    if (expression.resolvedType) {
        result.type = *expression.resolvedType;
        for (auto& block : function.blocks) {
            const auto prior = instructionCounts.find(&block);
            const std::size_t begin = prior == instructionCounts.end()
                ? 0
                : prior->second;
            for (std::size_t i = begin; i < block.instructions.size(); ++i) {
                Instruction& instruction = block.instructions[i];
                if (!result.name.empty() && instruction.result == result.name) {
                    instruction.resultType = result.type;
                }
                if (instruction.opcode == "alloc" &&
                    !instruction.operands.empty() &&
                    instruction.operands[0].name == result.name) {
                    instruction.operands[0].type = result.type;
                }
            }
        }
    }
    return result;
}


// ============================================================
// Literal
// ============================================================

Value IRBuilder::buildLiteral(
    const zplus::ast::Literal& expression
) const {

    Value::Kind kind = Value::Kind::Unknown;
    switch (expression.kind) {
        case zplus::ast::Literal::Kind::Number:
            kind = Value::Kind::Number;
            break;
        case zplus::ast::Literal::Kind::String:
            kind = Value::Kind::String;
            break;
        case zplus::ast::Literal::Kind::Character:
            kind = Value::Kind::Character;
            break;
        case zplus::ast::Literal::Kind::Boolean:
            kind = Value::Kind::Boolean;
            break;
        case zplus::ast::Literal::Kind::Null:
            kind = Value::Kind::Null;
            break;
    }

    Value value{expression.value, kind};
    if (kind == Value::Kind::Number) {
        double parsed = 0.0;
        if (zplus::numeric::parseLiteral(expression.value, parsed)) {
            value.numericValue = parsed;
            value.hasNumericValue = true;
        }
    }
    return value;
}


// ============================================================
// Identifier
// ============================================================

Value IRBuilder::buildIdentifier(
    const zplus::ast::Identifier& expression
) const {

    if (expression.name.empty()) {
        throw std::runtime_error(
            "identifier has an empty name"
        );
    }

    return Value{
        expression.bindingName.empty()
            ? expression.name
            : expression.bindingName,
        Value::Kind::Identifier
    };
}


// ============================================================
// Unary
// ============================================================

Value IRBuilder::buildUnary(
    const zplus::ast::UnaryExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!expression.operand) {
        throw std::runtime_error(
            "unary expression has no operand"
        );
    }

    Value operand =
        buildExpression(
            *expression.operand,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "unary expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    std::string opcode;

    switch (expression.operatorType) {

        case zplus::TokenType::Plus:
            opcode = "pos";
            break;

        case zplus::TokenType::Minus:
            opcode = "neg";
            break;

        case zplus::TokenType::Exclamation:
        case zplus::TokenType::Not:
            opcode = "not";
            break;

        case zplus::TokenType::BitNot:
            opcode = "bitnot";
            break;

        case zplus::TokenType::Star:
            opcode = "load";
            break;

        case zplus::TokenType::PlusPlus:
            opcode =
                expression.prefix
                    ? "pre_inc"
                    : "post_inc";
            break;

        case zplus::TokenType::MinusMinus:
            opcode =
                expression.prefix
                    ? "pre_dec"
                    : "post_dec";
            break;

        default:
            throw std::runtime_error(
                "unsupported unary operator"
            );
    }

    currentBlock->emit(
        Instruction{
            opcode,
            result,
            {
                operand
            }
        }
    );

    return Value{
        result
    };
}


// ============================================================
// Binary
// ============================================================

Value IRBuilder::buildBinary(
    const zplus::ast::BinaryExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !expression.left ||
        !expression.right
    ) {
        throw std::runtime_error(
            "binary expression has a missing operand"
        );
    }

    Value left =
        buildExpression(
            *expression.left,
            module,
            function,
            currentBlock
        );

    const bool isAnd =
        expression.operatorType == zplus::TokenType::And ||
        expression.operatorType == zplus::TokenType::AndAnd;
    const bool isOr =
        expression.operatorType == zplus::TokenType::Or ||
        expression.operatorType == zplus::TokenType::OrOr;

    if (isAnd || isOr) {
        if (!currentBlock) {
            throw std::runtime_error(
                "logical expression lost its left operand block"
            );
        }

        const std::string result = nextTemporary();
        currentBlock->emit(Instruction{"alloc", {}, {Value{result}}});

        BasicBlock& rightBlock = createBlock(function, "logic.right");
        BasicBlock& shortBlock = createBlock(function, "logic.short");
        BasicBlock& endBlock = createBlock(function, "logic.end");

        currentBlock->emit(Instruction{
            "condbr",
            {},
            {
                left,
                Value{isAnd ? rightBlock.label : shortBlock.label},
                Value{isAnd ? shortBlock.label : rightBlock.label}
            }
        });

        shortBlock.emit(Instruction{
            "store",
            {},
            {
                Value{result, Value::Kind::Identifier},
                Value{isOr ? "true" : "false", Value::Kind::Boolean}
            }
        });
        shortBlock.emit(Instruction{"br", {}, {Value{endBlock.label}}});

        BasicBlock* rightCurrent = &rightBlock;
        const Value right = buildExpression(
            *expression.right,
            module,
            function,
            rightCurrent
        );
        if (!rightCurrent || blockTerminated(rightCurrent)) {
            throw std::runtime_error(
                "logical expression right operand did not complete"
            );
        }
        rightCurrent->emit(Instruction{
            "store",
            {},
            {
                Value{result, Value::Kind::Identifier},
                right
            }
        });
        rightCurrent->emit(Instruction{"br", {}, {Value{endBlock.label}}});

        currentBlock = &endBlock;
        return Value{result};
    }

    Value right;
    if (expression.operatorType == zplus::TokenType::Is) {
        const auto* typeName =
            as<zplus::ast::Identifier>(expression.right.get());
        if (!typeName) {
            throw std::runtime_error(
                "the right operand of 'is' must be a type identifier");
        }
        right = Value{typeName->name, Value::Kind::String};
    } else {
        right = buildExpression(
            *expression.right,
            module,
            function,
            currentBlock
        );
    }

    if (!currentBlock) {
        throw std::runtime_error(
            "binary expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    currentBlock->emit(
        Instruction{
            binaryOpcode(
                expression.operatorType
            ),
            result,
            {
                left,
                right
            }
        }
    );

    return Value{
        result
    };
}


// ============================================================
// Assignment
// ============================================================

Value IRBuilder::buildAssignment(
    const zplus::ast::AssignmentExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !expression.target ||
        !expression.value
    ) {
        throw std::runtime_error(
            "assignment has a missing target or value"
        );
    }

    if (const auto* indexTarget =
            as<zplus::ast::IndexExpression>(expression.target.get())) {
        if (!indexTarget->object || !indexTarget->index) {
            throw std::runtime_error("indexed assignment target is incomplete");
        }

        const Value object = buildExpression(
            *indexTarget->object,
            module,
            function,
            currentBlock
        );
        const Value index = buildExpression(
            *indexTarget->index,
            module,
            function,
            currentBlock
        );
        if (!currentBlock) {
            throw std::runtime_error("indexed assignment target lost its block");
        }

        Value assignedValue;
        if (expression.operatorType == zplus::TokenType::Equal) {
            assignedValue = buildExpression(
                *expression.value,
                module,
                function,
                currentBlock
            );
        } else {
            const std::string currentValueName = nextTemporary();
            currentBlock->emit(Instruction{
                "index",
                currentValueName,
                {object, index}
            });

            const Value right = buildExpression(
                *expression.value,
                module,
                function,
                currentBlock
            );
            std::string opcode;
            switch (expression.operatorType) {
                case zplus::TokenType::PlusEqual: opcode = "add"; break;
                case zplus::TokenType::MinusEqual: opcode = "sub"; break;
                case zplus::TokenType::StarEqual: opcode = "mul"; break;
                case zplus::TokenType::SlashEqual: opcode = "div"; break;
                case zplus::TokenType::PercentEqual: opcode = "mod"; break;
                case zplus::TokenType::AndEqual: opcode = "bitand"; break;
                case zplus::TokenType::OrEqual: opcode = "bitor"; break;
                case zplus::TokenType::XorEqual: opcode = "bitxor"; break;
                case zplus::TokenType::LeftShiftEqual: opcode = "shl"; break;
                case zplus::TokenType::RightShiftEqual: opcode = "shr"; break;
                default:
                    throw std::runtime_error("unsupported indexed assignment operator");
            }
            const std::string resultName = nextTemporary();
            currentBlock->emit(Instruction{
                opcode,
                resultName,
                {Value{currentValueName}, right}
            });
            assignedValue = Value{resultName};
        }
        if (!currentBlock) {
            throw std::runtime_error("indexed assignment value lost its block");
        }
        currentBlock->emit(Instruction{
            "index_store",
            {},
            {object, index, assignedValue}
        });
        return assignedValue;
    }

    if (const auto* memberTarget =
            as<zplus::ast::MemberExpression>(expression.target.get());
        memberTarget && memberTarget->object) {
        if (const auto* qualifier =
                as<zplus::ast::Identifier>(memberTarget->object.get())) {
            const std::string qualifiedName =
                qualifier->name + "." + memberTarget->member;
            const std::string variableName =
                module.resolveVariable(qualifiedName);
            if (variableName != qualifiedName) {
                const Value value = buildExpression(
                    *expression.value, module, function, currentBlock);
                if (!currentBlock) {
                    throw std::runtime_error(
                        "module variable assignment lost its block");
                }
                currentBlock->emit(Instruction{
                    assignmentOpcode(expression.operatorType),
                    {},
                    {Value{variableName, Value::Kind::Identifier}, value}
                });
                return value;
            }
        }
    }

    if (const auto* memberTarget =
            as<zplus::ast::MemberExpression>(expression.target.get())) {
        if (!memberTarget->object) {
            throw std::runtime_error("member assignment has no object");
        }
        const Value object = buildExpression(
            *memberTarget->object, module, function, currentBlock);
        Value assignedValue = buildExpression(
            *expression.value, module, function, currentBlock);
        if (!currentBlock) {
            throw std::runtime_error("member assignment lost its block");
        }
        if (expression.operatorType != zplus::TokenType::Equal) {
            const std::string currentValue = nextTemporary();
            currentBlock->emit(Instruction{
                "member", currentValue, {object, Value{memberTarget->member}}
            });
            std::string opcode;
            switch (expression.operatorType) {
                case zplus::TokenType::PlusEqual: opcode = "add"; break;
                case zplus::TokenType::MinusEqual: opcode = "sub"; break;
                case zplus::TokenType::StarEqual: opcode = "mul"; break;
                case zplus::TokenType::SlashEqual: opcode = "div"; break;
                case zplus::TokenType::PercentEqual: opcode = "mod"; break;
                default:
                    throw std::runtime_error(
                        "unsupported compound member assignment operator");
            }
            const std::string result = nextTemporary();
            currentBlock->emit(Instruction{
                opcode, result, {Value{currentValue}, assignedValue}
            });
            assignedValue = Value{result};
        }
        currentBlock->emit(Instruction{
            "member_store", {}, {object, Value{memberTarget->member}, assignedValue}
        });
        return assignedValue;
    }
    Value value =
        buildExpression(
            *expression.value,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "assignment lost its basic block"
        );
    }


    if (const auto* identifier =
            as<zplus::ast::Identifier>(
                expression.target.get()
            )) {

        if (identifier->name.empty()) {
            throw std::runtime_error(
                "assignment target has an empty name"
            );
        }

        const std::string targetName = identifier->bindingName.empty()
            ? module.resolveVariable(
                  module.resolveFunction(identifier->name)
              )
            : identifier->bindingName;
        currentBlock->emit(
            Instruction{
                assignmentOpcode(
                    expression.operatorType
                ),
                {},
                {
                    Value{targetName, Value::Kind::Identifier},
                    value
                }
            }
        );

        return value;
    }


    if (
        as<zplus::ast::MemberExpression>(
            expression.target.get()
        ) ||
        as<zplus::ast::IndexExpression>(
            expression.target.get()
        )
    ) {

        Value target =
            buildExpression(
                *expression.target,
                module,
                function,
                currentBlock
            );

        if (!currentBlock) {
            throw std::runtime_error(
                "assignment target lost its basic block"
            );
        }

        currentBlock->emit(
            Instruction{
                assignmentOpcode(
                    expression.operatorType
                ),
                {},
                {
                    target,
                    value
                }
            }
        );

        return value;
    }


    throw std::runtime_error(
        "invalid assignment target"
    );
}


// ============================================================
// Conditional
// ============================================================

Value IRBuilder::buildConditional(
    const zplus::ast::ConditionalExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !expression.condition ||
        !expression.thenExpression ||
        !expression.elseExpression
    ) {
        throw std::runtime_error(
            "conditional expression is incomplete"
        );
    }

    Value condition =
        buildExpression(
            *expression.condition,
            module,
            function,
            currentBlock
        );

    Value thenValue =
        buildExpression(
            *expression.thenExpression,
            module,
            function,
            currentBlock
        );

    Value elseValue =
        buildExpression(
            *expression.elseExpression,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "conditional expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    currentBlock->emit(
        Instruction{
            "select",
            result,
            {
                condition,
                thenValue,
                elseValue
            }
        }
    );

    return Value{
        result
    };
}


// ============================================================
// Call
// ============================================================

Value IRBuilder::buildCall(
    const zplus::ast::CallExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!expression.callee) {
        throw std::runtime_error(
            "call expression has no callee"
        );
    }

    const auto* memberCallee =
        as<zplus::ast::MemberExpression>(expression.callee.get());
    const bool instanceMethod =
        memberCallee && memberCallee->requiresReceiver;
    Value callee = memberCallee && !memberCallee->resolvedName.empty()
        ? Value{memberCallee->resolvedName, Value::Kind::Identifier}
        : buildExpression(*expression.callee, module, function, currentBlock);

    if (!currentBlock) {
        throw std::runtime_error(
            "call expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    if (callee.kind == Value::Kind::Identifier) {
        const StructType* structure = module.findStructType(callee.name);
        if (structure) {
            Instruction instruction{"struct_new", result};
            instruction.operands.push_back(Value{structure->name});
            std::size_t argumentIndex = 0;
            for (const auto& field : structure->fields) {
                if (argumentIndex < expression.arguments.size() &&
                    expression.arguments[argumentIndex]) {
                    instruction.operands.push_back(buildExpression(
                        *expression.arguments[argumentIndex],
                        module, function, currentBlock));
                    ++argumentIndex;
                } else if (field.hasDefault) {
                    instruction.operands.push_back(field.defaultValue);
                } else {
                    throw std::runtime_error(
                        "missing required constructor argument for struct '" +
                        structure->name + "'");
                }
            }
            currentBlock->emit(std::move(instruction));
            return Value{result};
        }
    }

    Instruction instruction{
        "call",
        result
    };
    instruction.location = expression.location;

    instruction.operands.push_back(
        callee
    );

    std::vector<Value> arguments;
    if (instanceMethod) {
        arguments.push_back(buildExpression(
            *memberCallee->object, module, function, currentBlock));
        if (!currentBlock) {
            throw std::runtime_error(
                "method receiver lost its basic block");
        }
    }
    for (const auto& argument :
         expression.arguments) {

        if (!argument) {
            continue;
        }

        arguments.push_back(
            buildExpression(
                *argument,
                module,
                function,
                currentBlock
            )
        );

        if (!currentBlock) {
            throw std::runtime_error(
                "call argument lost its basic block"
            );
        }
    }

    if (const Function* target = module.findFunction(callee.name)) {
        for (std::size_t i = arguments.size(); i < target->parameters.size(); ++i) {
            const std::string& helperName =
                target->parameters[i].defaultFunction;
            if (helperName.empty()) {
                throw std::runtime_error(
                    "missing required argument while building call to '" +
                    callee.name + "'"
                );
            }

            const std::string defaultResult = nextTemporary();
            Instruction defaultCall{"call", defaultResult};
            defaultCall.operands.push_back(
                Value{helperName, Value::Kind::Identifier}
            );
            for (std::size_t preceding = 0; preceding < i; ++preceding) {
                defaultCall.operands.push_back(arguments[preceding]);
            }
            currentBlock->emit(std::move(defaultCall));
            arguments.emplace_back(defaultResult);
        }
    }

    instruction.operands.insert(
        instruction.operands.end(),
        arguments.begin(),
        arguments.end()
    );

    currentBlock->emit(
        std::move(instruction)
    );

    return Value{
        result
    };
}


// ============================================================
// Index
// ============================================================

Value IRBuilder::buildIndex(
    const zplus::ast::IndexExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (
        !expression.object ||
        !expression.index
    ) {
        throw std::runtime_error(
            "index expression is incomplete"
        );
    }

    Value object =
        buildExpression(
            *expression.object,
            module,
            function,
            currentBlock
        );

    Value index =
        buildExpression(
            *expression.index,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "index expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    currentBlock->emit(
        Instruction{
            "index",
            result,
            {
                object,
                index
            }
        }
    );

    return Value{
        result
    };
}


// ============================================================
// Member
// ============================================================

Value IRBuilder::buildMember(
    const zplus::ast::MemberExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!expression.object) {
        throw std::runtime_error(
            "member expression has no object"
        );
    }

    if (expression.member.empty()) {
        throw std::runtime_error(
            "member expression has an empty member name"
        );
    }

    if (!expression.resolvedName.empty()) {
        return Value{expression.resolvedName, Value::Kind::Identifier};
    }

    if (const auto* enumType =
            as<zplus::ast::Identifier>(expression.object.get())) {
        const std::string qualifiedName =
            enumType->name + "." + expression.member;
        if (const Value* constant = module.findEnumConstant(qualifiedName)) {
            return *constant;
        }
    } else if (const auto* qualifiedEnum =
                   as<zplus::ast::MemberExpression>(expression.object.get())) {
        const auto* moduleName =
            as<zplus::ast::Identifier>(qualifiedEnum->object.get());
        if (moduleName) {
            const std::string qualifiedName =
                moduleName->name + "." + qualifiedEnum->member + "." +
                expression.member;
            if (const Value* constant = module.findEnumConstant(qualifiedName)) {
                return *constant;
            }
        }
    }

    if (const auto* qualifier =
            as<zplus::ast::Identifier>(expression.object.get())) {
        const std::string qualifiedName =
            qualifier->name + "." + expression.member;
        const std::string functionName = module.resolveFunction(qualifiedName);
        if (functionName != qualifiedName) {
            return Value{functionName, Value::Kind::Identifier};
        }
        const std::string variableName = module.resolveVariable(qualifiedName);
        if (variableName != qualifiedName) {
            return Value{variableName, Value::Kind::Identifier};
        }
    }

    Value object =
        buildExpression(
            *expression.object,
            module,
            function,
            currentBlock
        );

    if (!currentBlock) {
        throw std::runtime_error(
            "member expression lost its basic block"
        );
    }

    const std::string result =
        nextTemporary();

    currentBlock->emit(
        Instruction{
            expression.pointerAccess
                ? "member_ptr"
                : "member",
            result,
            {
                object,
                Value{expression.member}
            }
        }
    );

    return Value{
        result
    };
}


// ============================================================
// Array
// ============================================================

Value IRBuilder::buildArray(
    const zplus::ast::ArrayExpression& expression,
    Module& module,
    Function& function,
    BasicBlock*& currentBlock
) const {

    if (!currentBlock) {
        throw std::runtime_error(
            "array expression has no basic block"
        );
    }

    const std::string result =
        nextTemporary();

    Instruction instruction{
        "array",
        result
    };

    for (const auto& element :
         expression.elements) {

        if (!element) {
            continue;
        }

        instruction.operands.push_back(
            buildExpression(
                *element,
                module,
                function,
                currentBlock
            )
        );

        if (!currentBlock) {
            throw std::runtime_error(
                "array element lost its basic block"
            );
        }
    }

    currentBlock->emit(
        std::move(instruction)
    );

    return Value{
        result
    };
}

} // namespace zplus::ir