#pragma once

#include "../parser/AST.hpp"
#include "../semantic/BuiltinRegistry.hpp"
#include "../semantic/Type.hpp"

#include <cstddef>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zplus::ir {

inline constexpr char kTopLevelEntryName[] = "__zplus_top_level_entry";
inline constexpr char kModuleInitializerName[] = "__zplus_module_init";
inline constexpr std::size_t kInvalidRuntimeSlot =
    std::numeric_limits<std::size_t>::max();
inline constexpr std::size_t kInvalidFunctionIndex =
    std::numeric_limits<std::size_t>::max();

// ============================================================
// IR Value
// ============================================================

struct Value {

    enum class Kind {
        Unknown,
        Identifier,
        Number,
        String,
        Character,
        Boolean,
        Null
    };

    std::string name;
    Kind kind = Kind::Unknown;
    std::size_t runtimeSlot = kInvalidRuntimeSlot;
    bool globalSlot = false;
    double numericValue = 0.0;
    bool hasNumericValue = false;
    zplus::semantic::Type type;

    Value() = default;

    explicit Value(std::string value, Kind valueKind = Kind::Unknown)
        : name(std::move(value)),
          kind(valueKind) {
    }

    bool empty() const noexcept {
        return name.empty();
    }
};


// ============================================================
// IR Instruction
// ============================================================

struct Instruction {

    std::string opcode;

    std::string result;
    zplus::semantic::Type resultType;

    std::vector<Value> operands;

    std::string comment;

    zplus::ast::Location location;
    std::size_t resultSlot = kInvalidRuntimeSlot;
    std::size_t calleeFunctionIndex = kInvalidFunctionIndex;
    std::vector<zplus::semantic::Type> genericTypeArguments;
    std::string scopeId;
    std::string siteId;
    std::size_t scopeDepth = 0;
    std::size_t handlerScopeDepth = 0;

    Instruction() = default;

    explicit Instruction(
        std::string op
    )
        : opcode(std::move(op)) {
    }

    Instruction(
        std::string op,
        std::string resultName,
        std::vector<Value> args = {}
    )
        : opcode(std::move(op)),
          result(std::move(resultName)),
          operands(std::move(args)) {
    }

    bool isEmpty() const noexcept {
        return opcode.empty();
    }

    bool hasResult() const noexcept {
        return !result.empty();
    }

    bool hasOperands() const noexcept {
        return !operands.empty();
    }
};


// ============================================================
// Basic Block
// ============================================================

struct BasicBlock {

    std::string label;

    std::vector<Instruction> instructions;

    BasicBlock() = default;

    explicit BasicBlock(
        std::string name
    )
        : label(std::move(name)) {
    }

    void emit(
        Instruction instruction
    ) {
        instructions.push_back(
            std::move(instruction)
        );
    }

    bool empty() const noexcept {
        return instructions.empty();
    }

    Instruction* lastInstruction() noexcept {

        if (instructions.empty()) {
            return nullptr;
        }

        return &instructions.back();
    }

    const Instruction* lastInstruction() const noexcept {

        if (instructions.empty()) {
            return nullptr;
        }

        return &instructions.back();
    }
};


// ============================================================
// Function Parameter
// ============================================================

struct Parameter {

    std::string name;

    zplus::semantic::Type type;

    std::string defaultFunction;
    std::size_t runtimeSlot = kInvalidRuntimeSlot;

    Parameter() = default;

    Parameter(
        std::string n,
        zplus::semantic::Type t = {}
    )
        : name(std::move(n)),
          type(std::move(t)) {
    }

    bool valid() const noexcept {
        return !name.empty();
    }
};


// ============================================================
// IR Function
// ============================================================
//
// IMPORTANT:
//
// blocks uses std::deque instead of std::vector.
//
// IRBuilder.cpp stores BasicBlock* and BasicBlock& values while
// creating additional blocks. std::vector could invalidate those
// references when it reallocates.
//
// std::deque keeps references/pointers to existing elements stable
// when new elements are appended.
//

struct Function {

    std::string name;

    zplus::ast::Location location;

    std::vector<Parameter> parameters;
    std::vector<zplus::semantic::GenericTypeParameter> genericParameters;

    zplus::semantic::Type returnType = zplus::semantic::Type::voidType();

    std::deque<BasicBlock> blocks;
    std::size_t runtimeSlotCount = 0;
    std::size_t requiredParameterCount = 0;

    bool external = false;

    bool isBuiltin = false;

    zplus::semantic::Type signature() const {
        std::vector<zplus::semantic::Type> parameterTypes;
        parameterTypes.reserve(parameters.size());
        for (const Parameter& parameter : parameters) {
            parameterTypes.push_back(parameter.type);
        }
        zplus::semantic::Type result =
            zplus::semantic::Type::function(
                parameterTypes, returnType, genericParameters);
        result.requiredParameterCount = requiredParameterCount;
        return result;
    }


    // --------------------------------------------------------
    // Entry block
    // --------------------------------------------------------

    BasicBlock* entryBlock() noexcept {

        if (blocks.empty()) {
            return nullptr;
        }

        return &blocks.front();
    }

    const BasicBlock* entryBlock() const noexcept {

        if (blocks.empty()) {
            return nullptr;
        }

        return &blocks.front();
    }


    // --------------------------------------------------------
    // Create block
    // --------------------------------------------------------

    BasicBlock& createBlock(
        std::string label
    ) {

        blocks.emplace_back(
            std::move(label)
        );

        return blocks.back();
    }
};

struct StructField {
    std::string name;
    zplus::semantic::Type type;
    Value defaultValue;
    bool hasDefault = false;
};

struct StructType {
    std::string name;
    std::vector<zplus::semantic::GenericTypeParameter> genericParameters;
    std::vector<StructField> fields;
};


// ============================================================
// IR Module
// ============================================================
//
// functions also uses std::deque.
//
// IRBuilder.cpp keeps references to Function objects while it can
// create additional functions. std::deque prevents those existing
// Function references from being invalidated by insertion.
//

struct Module {

    std::string name;
    zplus::semantic::BuiltinRegistry builtinRegistry;

    std::vector<Instruction> globals;

    std::vector<std::string> globalVariables;
    std::size_t runtimeGlobalSlotCount = 0;

    std::deque<Function> functions;

    std::unordered_map<std::string, std::string> functionAliases;

    std::unordered_map<std::string, std::string> variableAliases;
    std::unordered_map<std::string, std::string> structAliases;

    std::vector<std::string> strings;

    std::vector<std::string> types;
    std::vector<StructType> structTypes;
    std::unordered_map<std::string, Value> enumConstants;

    const StructType* findStructType(const std::string& typeName) const noexcept {
        const auto alias = structAliases.find(typeName);
        const std::string& resolved =
            alias == structAliases.end() ? typeName : alias->second;
        for (const auto& type : structTypes) {
            if (type.name == resolved) return &type;
        }
        return nullptr;
    }

    std::size_t structTypeId(const std::string& typeName) const noexcept {
        const auto alias = structAliases.find(typeName);
        const std::string& resolved =
            alias == structAliases.end() ? typeName : alias->second;
        for (std::size_t i = 0; i < structTypes.size(); ++i) {
            if (structTypes[i].name == resolved) return i + 1;
        }
        return 0;
    }

    const StructType* findStructTypeById(std::size_t id) const noexcept {
        return id != 0 && id <= structTypes.size()
            ? &structTypes[id - 1]
            : nullptr;
    }

    const Value* findEnumConstant(const std::string& constantName) const noexcept {
        const auto found = enumConstants.find(constantName);
        return found == enumConstants.end() ? nullptr : &found->second;
    }


    // --------------------------------------------------------
    // Find function
    // --------------------------------------------------------

    Function* findFunction(
        const std::string& functionName
    ) noexcept {

        for (auto& function : functions) {

            if (function.name == functionName) {
                return &function;
            }
        }

        const auto alias = functionAliases.find(functionName);
        if (alias != functionAliases.end()) {
            for (auto& function : functions) {
                if (function.name == alias->second) return &function;
            }
        }

        return nullptr;
    }

    const Function* findFunction(
        const std::string& functionName
    ) const noexcept {

        for (const auto& function : functions) {

            if (function.name == functionName) {
                return &function;
            }
        }

        const auto alias = functionAliases.find(functionName);
        if (alias != functionAliases.end()) {
            for (const auto& function : functions) {
                if (function.name == alias->second) return &function;
            }
        }

        return nullptr;
    }


    // --------------------------------------------------------
    // Create function
    // --------------------------------------------------------

    Function& createFunction(
        std::string functionName
    ) {

        functions.emplace_back();

        functions.back().name =
            std::move(functionName);

        return functions.back();
    }


    // --------------------------------------------------------
    // Strings
    // --------------------------------------------------------

    void addString(
        std::string value
    ) {

        strings.push_back(
            std::move(value)
        );
    }


    // --------------------------------------------------------
    // Type
    // --------------------------------------------------------

    void addType(
        std::string type
    ) {

        types.push_back(
            std::move(type)
        );
    }

    std::string resolveVariable(const std::string& valueName) const {
        const auto alias = variableAliases.find(valueName);
        return alias == variableAliases.end() ? valueName : alias->second;
    }

    std::string resolveFunction(const std::string& functionName) const {
        const auto alias = functionAliases.find(functionName);
        return alias == functionAliases.end() ? functionName : alias->second;
    }
};


// ============================================================
// IR Builder
// ============================================================

class IRBuilder {

public:

    Module build(
        const zplus::ast::Program& program
    ) const;

    Module build(
        const zplus::ast::Program& program,
        const zplus::semantic::BuiltinRegistry& builtinRegistry
    ) const;


private:

    // ========================================================
    // Program
    // ========================================================

    void buildProgram(
        const zplus::ast::Program& program,
        Module& module
    ) const;


    // ========================================================
    // Statements
    // ========================================================

    void buildStatement(
        const zplus::ast::Statement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildStart(
        const zplus::ast::StartStatement& statement,
        Module& module,
        Function& function
    ) const;

    void buildVariable(
        const zplus::ast::VariableDeclaration& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildExpressionStatement(
        const zplus::ast::ExpressionStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildReturn(
        const zplus::ast::ReturnStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildThrow(
        const zplus::ast::ThrowStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildTry(
        const zplus::ast::TryStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildDefer(
        const zplus::ast::DeferStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildScopedStatements(
        const std::vector<zplus::ast::Stmt>& statements,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildIf(
        const zplus::ast::IfStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildWhile(
        const zplus::ast::WhileStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildRepeat(
        const zplus::ast::RepeatStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildFor(
        const zplus::ast::ForStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildBreak(
        const zplus::ast::BreakStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildContinue(
        const zplus::ast::ContinueStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildFunction(
        const zplus::ast::FunctionDeclaration& statement,
        Module& module
    ) const;

    void prepareFunction(
        const zplus::ast::FunctionDeclaration& statement,
        Module& module
    ) const;

    void buildSwitch(
        const zplus::ast::SwitchStatement& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildStruct(
        const zplus::ast::StructDeclaration& statement,
        Module& module
    ) const;

    void buildClass(
        const zplus::ast::ClassDeclaration& statement,
        Module& module
    ) const;

    void buildEnum(
        const zplus::ast::EnumDeclaration& statement,
        Module& module
    ) const;

    void buildNamespace(
        const zplus::ast::NamespaceDeclaration& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    void buildImport(
        const zplus::ast::ImportDeclaration& statement,
        Module& module
    ) const;

    void buildExport(
        const zplus::ast::ExportDeclaration& statement,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;


    // ========================================================
    // Expressions
    // ========================================================

    Value buildExpression(
        const zplus::ast::Expression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildLiteral(
        const zplus::ast::Literal& expression
    ) const;

    Value buildIdentifier(
        const zplus::ast::Identifier& expression
    ) const;

    Value buildUnary(
        const zplus::ast::UnaryExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildBinary(
        const zplus::ast::BinaryExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildAssignment(
        const zplus::ast::AssignmentExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildConditional(
        const zplus::ast::ConditionalExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildCall(
        const zplus::ast::CallExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildIndex(
        const zplus::ast::IndexExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildMember(
        const zplus::ast::MemberExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;

    Value buildArray(
        const zplus::ast::ArrayExpression& expression,
        Module& module,
        Function& function,
        BasicBlock*& currentBlock
    ) const;


    // ========================================================
    // Helpers
    // ========================================================

    BasicBlock& createBlock(
        Function& function,
        const std::string& prefix
    ) const;

    std::string nextTemporary() const;

    std::string nextLabel(
        const std::string& prefix
    ) const;


private:

    // ========================================================
    // Builder state
    // ========================================================

    mutable std::size_t temporaryCounter_ = 0;

    mutable std::size_t labelCounter_ = 0;

    mutable std::size_t defaultFunctionCounter_ = 0;

    mutable std::vector<std::string> breakTargets_;

    mutable std::vector<std::size_t> breakScopeDepths_;

    mutable std::vector<std::string> continueTargets_;

    mutable std::vector<std::size_t> loopScopeDepths_;

    mutable std::size_t deferScopeDepth_ = 0;
    mutable std::vector<std::string> deferScopeIds_;

    mutable zplus::ast::Location currentLocation_;
};

} // namespace zplus::ir