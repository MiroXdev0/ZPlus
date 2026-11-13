#pragma once

#include "../parser/AST.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zplus::semantic {

// ============================================================
// Type system
// ============================================================

enum class TypeKind {

    Unknown,

    Number,
    String,
    Character,
    Boolean,
    Void,
    Null,

    Array,
    Pointer,

    Function,

    Custom
};


// ============================================================
// Type
// ============================================================

struct Type {

    TypeKind kind = TypeKind::Unknown;

    // Used by Custom types.
    std::string name;

    // Used by Array and Pointer.
    std::shared_ptr<Type> elementType;

    // Used by Function.
    std::vector<Type> parameters;
    std::size_t requiredParameterCount = 0;
    std::shared_ptr<Type> returnType;


    // --------------------------------------------------------
    // Constructors
    // --------------------------------------------------------

    Type() = default;

    explicit Type(TypeKind kind)
        : kind(kind) {
    }


    // --------------------------------------------------------
    // Primitive factories
    // --------------------------------------------------------

    static Type unknown() {
        return Type(TypeKind::Unknown);
    }

    static Type number() {
        return Type(TypeKind::Number);
    }

    static Type string() {
        return Type(TypeKind::String);
    }

    static Type character() {
        return Type(TypeKind::Character);
    }

    static Type boolean() {
        return Type(TypeKind::Boolean);
    }

    static Type voidType() {
        return Type(TypeKind::Void);
    }

    static Type null() {
        return Type(TypeKind::Null);
    }


    // --------------------------------------------------------
    // Composite factories
    // --------------------------------------------------------

    static Type array(const Type& element) {

        Type type(TypeKind::Array);

        type.elementType =
            std::make_shared<Type>(element);

        return type;
    }

    static Type array(Type&& element) {

        Type type(TypeKind::Array);

        type.elementType =
            std::make_shared<Type>(
                std::move(element)
            );

        return type;
    }

    static Type pointer(const Type& element) {

        Type type(TypeKind::Pointer);

        type.elementType =
            std::make_shared<Type>(element);

        return type;
    }

    static Type pointer(Type&& element) {

        Type type(TypeKind::Pointer);

        type.elementType =
            std::make_shared<Type>(
                std::move(element)
            );

        return type;
    }

    static Type function(
        std::vector<Type> parameters,
        const Type& returnType
    ) {

        Type type(TypeKind::Function);

        type.parameters =
            std::move(parameters);
        type.requiredParameterCount =
            type.parameters.size();

        type.returnType =
            std::make_shared<Type>(
                returnType
            );

        return type;
    }

    static Type function(
        std::vector<Type> parameters,
        Type&& returnType
    ) {

        Type type(TypeKind::Function);

        type.parameters =
            std::move(parameters);
        type.requiredParameterCount =
            type.parameters.size();

        type.returnType =
            std::make_shared<Type>(
                std::move(returnType)
            );

        return type;
    }

    static Type custom(
        const std::string& name
    ) {

        Type type(TypeKind::Custom);

        type.name = name;

        return type;
    }
};


// ============================================================
// Symbol
// ============================================================

struct Symbol {

    std::string name;

    Type type;

    bool isConst = false;

    bool isFunction = false;

    bool initialized = false;

    bool isBuiltin = false;

    bool isPublic = false;

    std::string originalName;
    std::string moduleName;
    std::string sourceFile;
    std::string bindingName;
};


// ============================================================
// Semantic error
// ============================================================

struct SemanticError {

    std::string message;
    std::string sourceFile;
    std::size_t line = 0;
    std::size_t column = 0;

    SemanticError(
        const std::string& message,
        const std::string& sourceFile,
        std::size_t line,
        std::size_t column
    )
        : message(message),
          sourceFile(sourceFile),
          line(line),
          column(column) {
    }
};

// ============================================================
// Analyzer
// ============================================================

class Analyzer {

public:

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------

    Analyzer();


    // --------------------------------------------------------
    // Main analysis
    // --------------------------------------------------------

    bool analyze(
        const zplus::ast::Program& program
    );


    // --------------------------------------------------------
    // Error access
    // --------------------------------------------------------

    const std::vector<SemanticError>& errors() const {
        return errors_;
    }

    const std::vector<SemanticError>& warnings() const {
        return warnings_;
    }

    bool hasErrors() const {
        return !errors_.empty();
    }


private:

    // ========================================================
    // Scope management
    // ========================================================

    void enterScope();

    void exitScope();

    Symbol* findSymbol(
        const std::string& name
    );

    const Symbol* findSymbol(
        const std::string& name
    ) const;

    const Symbol* findModuleSymbol(
        const std::string& module,
        const std::string& name
    ) const;

    std::string moduleNameForFile(const std::string& sourceFile) const;

    std::string createBindingName();

    bool declareSymbol(
        const Symbol& symbol
    );


    // ========================================================
    // Errors
    // ========================================================

    void error(
        const zplus::ast::Node& node,
        const std::string& message
    );

    void error(
        const zplus::ast::Location& location,
        const std::string& message
    );

    void warning(
        const zplus::ast::Node& node,
        const std::string& message
    );


    // ========================================================
    // Type helpers
    // ========================================================

    bool isUnknown(
        const Type& type
    ) const;

    bool isNumeric(
        const Type& type
    ) const;

    bool isBoolean(
        const Type& type
    ) const;

    std::string typeName(
        const Type& type
    ) const;

    bool typesEqual(
        const Type& left,
        const Type& right
    ) const;

    bool isAssignable(
        const Type& target,
        const Type& value
    ) const;


    // ========================================================
    // AST type resolution
    // ========================================================

    Type resolveType(
        const zplus::ast::Type& type
    );


    // ========================================================
    // Program analysis
    // ========================================================

    void predeclareTypes(
        const std::vector<zplus::ast::Stmt>& statements
    );

    void predeclareFunctions(
        const std::vector<zplus::ast::Stmt>& statements
    );


    // ========================================================
    // Statement analysis
    // ========================================================

    void analyzeStatement(
        const zplus::ast::Statement& statement
    );

    void analyzeStart(
        const zplus::ast::StartStatement& statement
    );

    void analyzeBlock(
        const std::vector<zplus::ast::Stmt>& statements
    );

    void analyzeVariable(
        const zplus::ast::VariableDeclaration& statement
    );

    void analyzeAssignmentStatement(
        const zplus::ast::AssignmentStatement& statement
    );

    void analyzeExpressionStatement(
        const zplus::ast::ExpressionStatement& statement
    );

    void analyzeReturn(
        const zplus::ast::ReturnStatement& statement
    );

    void analyzeThrow(
        const zplus::ast::ThrowStatement& statement
    );

    void analyzeTry(
        const zplus::ast::TryStatement& statement
    );

    void analyzeDefer(
        const zplus::ast::DeferStatement& statement
    );

    void analyzeIf(
        const zplus::ast::IfStatement& statement
    );

    void analyzeWhile(
        const zplus::ast::WhileStatement& statement
    );

    void analyzeFor(
        const zplus::ast::ForStatement& statement
    );

    void analyzeRepeat(
        const zplus::ast::RepeatStatement& statement
    );

    void analyzeBreak(
        const zplus::ast::BreakStatement& statement
    );

    void analyzeContinue(
        const zplus::ast::ContinueStatement& statement
    );

    void analyzeFunction(
        const zplus::ast::FunctionDeclaration& statement
    );

    void analyzeSwitch(
        const zplus::ast::SwitchStatement& statement
    );

    void analyzeStruct(
        const zplus::ast::StructDeclaration& statement
    );

    void analyzeClass(
        const zplus::ast::ClassDeclaration& statement
    );

    void analyzeEnum(
        const zplus::ast::EnumDeclaration& statement
    );

    void analyzeNamespace(
        const zplus::ast::NamespaceDeclaration& statement
    );

    void analyzeImport(
        const zplus::ast::ImportDeclaration& statement
    );

    void analyzeUse(
        const zplus::ast::UseDeclaration& statement
    );

    void analyzeExport(
        const zplus::ast::ExportDeclaration& statement
    );


    // ========================================================
    // Expression analysis
    // ========================================================

    Type expressionType(
        const zplus::ast::Expression& expression
    );

    Type typeOfLiteral(
        const zplus::ast::Literal& expression
    );

    Type typeOfIdentifier(
        const zplus::ast::Identifier& expression
    );

    Type typeOfUnary(
        const zplus::ast::UnaryExpression& expression
    );

    Type typeOfBinary(
        const zplus::ast::BinaryExpression& expression
    );

    Type typeOfAssignment(
        const zplus::ast::AssignmentExpression& expression
    );

    Type typeOfConditional(
        const zplus::ast::ConditionalExpression& expression
    );

    Type typeOfCall(
        const zplus::ast::CallExpression& expression
    );

    Type typeOfMember(
        const zplus::ast::MemberExpression& expression
    );

    Type typeOfIndex(
        const zplus::ast::IndexExpression& expression
    );

    Type typeOfArray(
        const zplus::ast::ArrayExpression& expression
    );


    // ========================================================
    // Assignment helpers
    // ========================================================

    bool isValidAssignmentTarget(
        const zplus::ast::Expression& target
    ) const;

    Type targetType(
        const zplus::ast::Expression& target
    );


    // ========================================================
    // Control-flow helpers
    // ========================================================

    bool insideLoop() const {
        return loopDepth_ > 0;
    }

    bool insideFunction() const {
        return functionDepth_ > 0;
    }


private:

    // ========================================================
    // Symbol scopes
    // ========================================================

    using Scope =
        std::unordered_map<
            std::string,
            Symbol
        >;

    std::vector<Scope> scopes_;


    // ========================================================
    // User-defined types
    // ========================================================

    std::unordered_map<
        std::string,
        Type
    > userTypes_;

    std::unordered_map<
        std::string,
        const zplus::ast::StructDeclaration*
    > structDeclarations_;
    std::unordered_map<std::string, const zplus::ast::ClassDeclaration*>
        classDeclarations_;
    std::unordered_map<std::string, const zplus::ast::EnumDeclaration*>
        enumDeclarations_;

    std::unordered_map<std::string, std::string> moduleNamesByFile_;

    std::unordered_map<std::string, std::string> privateTypeOwners_;
    std::unordered_set<std::string> ambiguousPublicTypeNames_;


    // ========================================================
    // Semantic errors
    // ========================================================

    std::vector<SemanticError> errors_;
    std::vector<SemanticError> warnings_;


    // ========================================================
    // Control-flow state
    // ========================================================

    std::size_t loopDepth_ = 0;

    std::size_t functionDepth_ = 0;


    // ========================================================
    // Function state
    // ========================================================

    Type currentReturnType_ =
        Type::voidType();

    std::string currentFunctionName_;

    std::size_t nextBindingId_ = 0;

    std::string currentModuleName_;
};

} // namespace zplus::semantic