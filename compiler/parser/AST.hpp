#pragma once

#include "../lexer/Token.hpp"
#include "../semantic/Type.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zplus::ast {

// ============================================================
// Source location
// ============================================================

struct Location {
    int line = 1;
    int column = 1;
    std::string sourceFile;

    Location() = default;

    Location(int lineNumber, int columnNumber, std::string file = {})
        : line(lineNumber),
          column(columnNumber),
          sourceFile(std::move(file)) {
    }
};


// ============================================================
// Forward declarations
// ============================================================

struct Node;

struct Program;

struct Type;
struct Parameter;

struct Statement;
struct Expression;

struct StartStatement;
struct BlockStatement;

struct ExpressionStatement;
struct VariableDeclaration;
struct AssignmentStatement;
struct ReturnStatement;
struct ThrowStatement;
struct TryStatement;
struct DeferStatement;

struct IfStatement;
struct WhileStatement;
struct ForStatement;
struct RepeatStatement;

struct BreakStatement;
struct ContinueStatement;

struct SwitchStatement;
struct CaseStatement;

struct FunctionDeclaration;

struct StructField;
struct StructDeclaration;

struct ClassField;
struct ClassDeclaration;

struct EnumValue;
struct EnumDeclaration;

struct NamespaceDeclaration;
struct ImportDeclaration;
struct UseDeclaration;
struct ExportDeclaration;

struct Literal;
struct Identifier;

struct UnaryExpression;
struct BinaryExpression;
struct AssignmentExpression;
struct ConditionalExpression;

struct CallExpression;
struct MemberExpression;
struct IndexExpression;
struct ArrayExpression;


// ============================================================
// Smart pointer aliases
// ============================================================

using NodePtr = std::unique_ptr<Node>;

using Expr = std::unique_ptr<Expression>;
using Stmt = std::unique_ptr<Statement>;

using TypePtr = std::unique_ptr<Type>;
using ParameterPtr = std::unique_ptr<Parameter>;


// ============================================================
// AST node kinds
// ============================================================

enum class NodeKind {

    Unknown,

    // Root / structural
    Program,
    Block,

    // Types
    Type,
    Parameter,

    // Statements
    StartStatement,
    ExpressionStatement,
    VariableDeclaration,
    AssignmentStatement,
    ReturnStatement,
    ThrowStatement,
    TryStatement,
    DeferStatement,

    IfStatement,
    WhileStatement,
    ForStatement,
    RepeatStatement,

    BreakStatement,
    ContinueStatement,

    SwitchStatement,
    CaseStatement,

    FunctionDeclaration,

    StructField,
    StructDeclaration,

    ClassField,
    ClassDeclaration,

    EnumValue,
    EnumDeclaration,

    NamespaceDeclaration,
    ImportDeclaration,
    UseDeclaration,
    ExportDeclaration,

    // Expressions
    Literal,
    Identifier,
    UnaryExpression,
    BinaryExpression,
    AssignmentExpression,
    ConditionalExpression,
    CallExpression,
    MemberExpression,
    IndexExpression,
    ArrayExpression
};


// ============================================================
// Base AST node
// ============================================================

struct Node {

    explicit Node(
        Location sourceLocation = {},
        NodeKind nodeKind = NodeKind::Unknown
    )
        : location(sourceLocation),
          kind(nodeKind) {
    }

    virtual ~Node() = default;

    Location location;

    NodeKind kind = NodeKind::Unknown;
    mutable std::optional<zplus::semantic::Type> resolvedType;

    // Optional ID for tooling, debugging, AST dumps, etc.
    std::uint64_t id = 0;
};


// ============================================================
// Type
// ============================================================

struct Type final : Node {

    enum class Kind {

        Unknown,

        Number,
        String,
        Character,
        Boolean,
        Void,

        Custom,

        Array,
        Pointer
    };

    explicit Type(
        Kind typeKind = Kind::Unknown,
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::Type
        ),
          kind(typeKind) {
    }

    explicit Type(
        std::string customName,
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::Type
        ),
          kind(Kind::Custom),
          name(std::move(customName)) {
    }

    Kind kind = Kind::Unknown;

    std::string name;

    TypePtr elementType;

    bool isArray() const {
        return kind == Kind::Array;
    }

    bool isPointer() const {
        return kind == Kind::Pointer;
    }

    bool isBuiltin() const {

        switch (kind) {

            case Kind::Number:
            case Kind::String:
            case Kind::Character:
            case Kind::Boolean:
            case Kind::Void:
                return true;

            default:
                return false;
        }
    }
};


// ============================================================
// Parameter
// ============================================================

struct Parameter final : Node {

    Parameter(
        std::string parameterName = {},
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::Parameter
        ),
          name(std::move(parameterName)) {
    }

    std::string name;

    mutable std::string bindingName;

    TypePtr type;

    Expr defaultValue;

    bool isConst = false;

    bool hasType() const {
        return type != nullptr;
    }

    bool hasDefaultValue() const {
        return defaultValue != nullptr;
    }
};


// ============================================================
// Expression base
// ============================================================

struct Expression : Node {

    explicit Expression(
        Location sourceLocation = {},
        NodeKind nodeKind = NodeKind::Unknown
    )
        : Node(
            sourceLocation,
            nodeKind
        ) {
    }

    ~Expression() override = default;
};


// ============================================================
// Statement base
// ============================================================

struct Statement : Node {

    explicit Statement(
        Location sourceLocation = {},
        NodeKind nodeKind = NodeKind::Unknown
    )
        : Node(
            sourceLocation,
            nodeKind
        ) {
    }

    ~Statement() override = default;
};


// ============================================================
// Program
// ============================================================

struct Program final : Node {

    explicit Program(
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::Program
        ) {
    }

    std::vector<Stmt> statements;

    bool empty() const {
        return statements.empty();
    }

    std::size_t size() const {
        return statements.size();
    }
};


// ============================================================
// Block
// ============================================================
//
// Generic block container. The parser currently uses the
// concrete statement vectors directly, but this node is useful
// for future compiler features and AST tooling.
// ============================================================

struct BlockStatement final : Statement {

    explicit BlockStatement(
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::Block
        ) {
    }

    std::vector<Stmt> statements;

    bool empty() const {
        return statements.empty();
    }
};


// ============================================================
// start: ... stop
// ============================================================

struct StartStatement final : Statement {

    explicit StartStatement(
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::StartStatement
        ) {
    }

    std::vector<Stmt> body;

    bool empty() const {
        return body.empty();
    }
};


// ============================================================
// Literal
// ============================================================

struct Literal final : Expression {

    enum class Kind {
        Number,
        String,
        Character,
        Boolean,
        Null
    };

    Literal(
        Kind literalKind = Kind::Number,
        std::string literalValue = {},
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::Literal
        ),
          kind(literalKind),
          value(std::move(literalValue)) {
    }

    Kind kind = Kind::Number;

    std::string value;

    bool isNumber = false;

    bool isNumberLiteral() const {
        return kind == Kind::Number;
    }

    bool isString() const {
        return kind == Kind::String;
    }

    bool isCharacter() const {
        return kind == Kind::Character;
    }

    bool isBoolean() const {
        return kind == Kind::Boolean;
    }

    bool isNull() const {
        return kind == Kind::Null;
    }
};


// ============================================================
// Identifier
// ============================================================

struct Identifier final : Expression {

    Identifier(
        std::string identifierName = {},
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::Identifier
        ),
          name(std::move(identifierName)) {
    }

    std::string name;

    mutable std::string bindingName;
};


// ============================================================
// Unary expression
// ============================================================
//
// Examples:
//   -x
//   +x
//   !x
//   ~x
//   &x
//   *x
//   ++x
//   x++
// ============================================================

struct UnaryExpression final : Expression {

    UnaryExpression(
        zplus::TokenType operatorToken =
            zplus::TokenType::Unknown,
        Expr expression = nullptr,
        bool prefixOperator = true,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::UnaryExpression
        ),
          operatorType(operatorToken),
          operand(std::move(expression)),
          prefix(prefixOperator) {
    }

    zplus::TokenType operatorType =
        zplus::TokenType::Unknown;

    Expr operand;

    bool prefix = true;
};


// ============================================================
// Binary expression
// ============================================================
//
// Examples:
//   a + b
//   a * b
//   a == b
//   a && b
//   a << b
// ============================================================

struct BinaryExpression final : Expression {

    BinaryExpression(
        Expr leftExpression = nullptr,
        zplus::TokenType operatorToken =
            zplus::TokenType::Unknown,
        Expr rightExpression = nullptr,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::BinaryExpression
        ),
          left(std::move(leftExpression)),
          operatorType(operatorToken),
          right(std::move(rightExpression)) {
    }

    Expr left;

    zplus::TokenType operatorType =
        zplus::TokenType::Unknown;

    Expr right;
};


// ============================================================
// Assignment expression
// ============================================================
//
// Examples:
//   x = 10
//   x += 1
//   x <<= 2
// ============================================================

struct AssignmentExpression final : Expression {

    AssignmentExpression(
        Expr targetExpression = nullptr,
        zplus::TokenType operatorToken =
            zplus::TokenType::Equal,
        Expr valueExpression = nullptr,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::AssignmentExpression
        ),
          target(std::move(targetExpression)),
          operatorType(operatorToken),
          value(std::move(valueExpression)) {
    }

    Expr target;

    zplus::TokenType operatorType =
        zplus::TokenType::Equal;

    Expr value;
};


// ============================================================
// Conditional expression
// ============================================================
//
// condition ? thenValue : elseValue
// ============================================================

struct ConditionalExpression final : Expression {

    Expr condition;

    Expr thenExpression;

    Expr elseExpression;
};


// ============================================================
// Function call
// ============================================================

struct CallExpression final : Expression {

    explicit CallExpression(
        Expr calleeExpression = nullptr,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::CallExpression
        ),
          callee(std::move(calleeExpression)) {
    }

    Expr callee;

    std::vector<Expr> arguments;

    std::size_t argumentCount() const {
        return arguments.size();
    }
};


// ============================================================
// Member expression
// ============================================================
//
// object.member
// object->member
// ============================================================

struct MemberExpression final : Expression {

    MemberExpression(
        Expr objectExpression = nullptr,
        std::string memberName = {},
        bool pointer = false,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::MemberExpression
        ),
          object(std::move(objectExpression)),
          member(std::move(memberName)),
          pointerAccess(pointer) {
    }

    Expr object;

    std::string member;

    bool pointerAccess = false;

    mutable std::string resolvedName;

    mutable bool requiresReceiver = false;
};


// ============================================================
// Index expression
// ============================================================
//
// array[index]
// ============================================================

struct IndexExpression final : Expression {

    IndexExpression(
        Expr objectExpression = nullptr,
        Expr indexExpression = nullptr,
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::IndexExpression
        ),
          object(std::move(objectExpression)),
          index(std::move(indexExpression)) {
    }

    Expr object;

    Expr index;
};


// ============================================================
// Array expression
// ============================================================
//
// [1, 2, 3]
// ============================================================

struct ArrayExpression final : Expression {

    explicit ArrayExpression(
        Location sourceLocation = {}
    )
        : Expression(
            sourceLocation,
            NodeKind::ArrayExpression
        ) {
    }

    std::vector<Expr> elements;

    std::size_t size() const {
        return elements.size();
    }

    bool empty() const {
        return elements.empty();
    }
};


// ============================================================
// Expression statement
// ============================================================

struct ExpressionStatement final : Statement {

    explicit ExpressionStatement(
        Expr expressionNode = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ExpressionStatement
        ),
          expression(std::move(expressionNode)) {
    }

    Expr expression;
};


// ============================================================
// Assignment statement
// ============================================================
//
// Kept as a separate statement node so semantic analysis can
// distinguish a standalone assignment from any other expression.
// ============================================================

struct AssignmentStatement final : Statement {

    AssignmentStatement(
        Expr targetExpression = nullptr,
        Expr valueExpression = nullptr,
        zplus::TokenType operatorToken =
            zplus::TokenType::Equal,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::AssignmentStatement
        ),
          target(std::move(targetExpression)),
          value(std::move(valueExpression)),
          operatorType(operatorToken) {
    }

    Expr target;

    Expr value;

    zplus::TokenType operatorType =
        zplus::TokenType::Equal;
};


// ============================================================
// Variable declaration
// ============================================================

struct VariableDeclaration final : Statement {

    explicit VariableDeclaration(
        std::string variableName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::VariableDeclaration
        ),
          name(std::move(variableName)) {
    }

    std::string name;

    TypePtr type;

    Expr initializer;

    bool isConst = false;

    bool hasExplicitType = false;

    bool isPublic = false;

    std::string moduleName;

    std::string internalName;

    mutable std::string bindingName;

    bool hasInitializer() const {
        return initializer != nullptr;
    }
};


// ============================================================
// Return
// ============================================================

struct ReturnStatement final : Statement {

    explicit ReturnStatement(
        Expr returnValue = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ReturnStatement
        ),
          value(std::move(returnValue)) {
    }

    Expr value;

    bool hasValue = false;
};

struct ThrowStatement final : Statement {
    explicit ThrowStatement(
        Expr thrownValue = nullptr,
        Location sourceLocation = {}
    )
        : Statement(sourceLocation, NodeKind::ThrowStatement),
          value(std::move(thrownValue)) {
    }

    Expr value;
};

struct TryStatement final : Statement {
    explicit TryStatement(Location sourceLocation = {})
        : Statement(sourceLocation, NodeKind::TryStatement) {
    }

    std::vector<Stmt> tryBody;
    std::vector<Stmt> catchBody;
    std::string catchName;
    mutable std::string catchBindingName;
};

struct DeferStatement final : Statement {
    explicit DeferStatement(Location sourceLocation = {})
        : Statement(sourceLocation, NodeKind::DeferStatement) {
    }

    std::vector<Stmt> body;
};


// ============================================================
// If
// ============================================================

struct IfStatement final : Statement {

    explicit IfStatement(
        Expr conditionExpression = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::IfStatement
        ),
          condition(std::move(conditionExpression)) {
    }

    Expr condition;

    std::vector<Stmt> thenBody;

    std::vector<Stmt> elseBody;

    bool hasElse = false;
};


// ============================================================
// While
// ============================================================

struct WhileStatement final : Statement {

    explicit WhileStatement(
        Expr conditionExpression = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::WhileStatement
        ),
          condition(std::move(conditionExpression)) {
    }

    Expr condition;

    std::vector<Stmt> body;
};


// ============================================================
// For
// ============================================================
//
// for (initializer; condition; increment):
//     ...
// end
// ============================================================

struct ForStatement final : Statement {

    explicit ForStatement(
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ForStatement
        ) {
    }

    Stmt initializer;

    Expr condition;

    Expr increment;

    std::vector<Stmt> body;
};


// ============================================================
// Repeat
// ============================================================
//
// repeat (count):
//     ...
// end
// ============================================================

struct RepeatStatement final : Statement {

    explicit RepeatStatement(
        Expr countExpression = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::RepeatStatement
        ),
          count(std::move(countExpression)) {
    }

    Expr count;

    std::vector<Stmt> body;
};


// ============================================================
// Break
// ============================================================

struct BreakStatement final : Statement {

    explicit BreakStatement(
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::BreakStatement
        ) {
    }
};


// ============================================================
// Continue
// ============================================================

struct ContinueStatement final : Statement {

    explicit ContinueStatement(
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ContinueStatement
        ) {
    }
};


// ============================================================
// Switch case
// ============================================================

struct CaseStatement final : Statement {

    explicit CaseStatement(
        Expr caseValue = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::CaseStatement
        ),
          value(std::move(caseValue)) {
    }

    Expr value;

    std::vector<Stmt> body;
};


// ============================================================
// Switch
// ============================================================

struct SwitchStatement final : Statement {

    explicit SwitchStatement(
        Expr switchExpression = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::SwitchStatement
        ),
          expression(std::move(switchExpression)) {
    }

    Expr expression;

    std::vector<
        std::unique_ptr<CaseStatement>
    > cases;

    std::vector<Stmt> defaultBody;

    bool hasDefault = false;
};


// ============================================================
// Function declaration
// ============================================================
//
// fn add(a: number, b: number) -> number:
//     return a + b
// end
// ============================================================

struct FunctionDeclaration final : Statement {

    explicit FunctionDeclaration(
        std::string functionName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::FunctionDeclaration
        ),
          name(std::move(functionName)) {
    }

    std::string name;

    std::vector<ParameterPtr> parameters;

    TypePtr returnType;

    std::vector<Stmt> body;

    bool isPublic = false;

    bool isPrivate = false;

    bool isExported = false;

    std::string moduleName;

    std::string internalName;

    std::string ownerClass;

    std::string namespaceName;

    bool hasReturnType() const {
        return returnType != nullptr;
    }

    std::size_t parameterCount() const {
        return parameters.size();
    }
};


// ============================================================
// Struct field
// ============================================================

struct StructField final : Node {

    explicit StructField(
        std::string fieldName = {},
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::StructField
        ),
          name(std::move(fieldName)) {
    }

    std::string name;

    TypePtr type;

    Expr defaultValue;
};


// ============================================================
// Struct
// ============================================================

struct StructDeclaration final : Statement {

    explicit StructDeclaration(
        std::string structName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::StructDeclaration
        ),
          name(std::move(structName)) {
    }

    std::string name;

    std::vector<
        std::unique_ptr<StructField>
    > fields;

    bool isPublic = false;

    std::string moduleName;
};


// ============================================================
// Class field
// ============================================================

struct ClassField final : Node {

    explicit ClassField(
        std::string fieldName = {},
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::ClassField
        ),
          name(std::move(fieldName)) {
    }

    std::string name;

    TypePtr type;

    Expr initializer;

    bool isPublic = true;

    bool isPrivate = false;

    bool isProtected = false;
};


// ============================================================
// Class
// ============================================================

struct ClassDeclaration final : Statement {

    explicit ClassDeclaration(
        std::string className = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ClassDeclaration
        ),
          name(std::move(className)) {
    }

    std::string name;

    std::string baseClass;

    std::vector<
        std::unique_ptr<ClassField>
    > fields;

    std::vector<
        std::unique_ptr<FunctionDeclaration>
    > methods;

    bool isPublic = false;

    std::string moduleName;
};


// ============================================================
// Enum value
// ============================================================

struct EnumValue final : Node {

    explicit EnumValue(
        std::string enumName = {},
        Location sourceLocation = {}
    )
        : Node(
            sourceLocation,
            NodeKind::EnumValue
        ),
          name(std::move(enumName)) {
    }

    std::string name;

    Expr value;
};


// ============================================================
// Enum
// ============================================================

struct EnumDeclaration final : Statement {

    explicit EnumDeclaration(
        std::string enumName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::EnumDeclaration
        ),
          name(std::move(enumName)) {
    }

    std::string name;

    std::vector<
        std::unique_ptr<EnumValue>
    > values;

    bool isPublic = false;

    std::string moduleName;
};


// ============================================================
// Namespace
// ============================================================

struct NamespaceDeclaration final : Statement {

    explicit NamespaceDeclaration(
        std::string namespaceName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::NamespaceDeclaration
        ),
          name(std::move(namespaceName)) {
    }

    std::string name;

    std::vector<Stmt> body;
};


// ============================================================
// Import
// ============================================================

struct ImportDeclaration final : Statement {

    explicit ImportDeclaration(
        std::string moduleName = {},
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ImportDeclaration
        ),
          module(std::move(moduleName)) {
    }

    std::string module;

    std::string alias;
};

struct UseDeclaration final : Statement {

    explicit UseDeclaration(
        std::string modulePath = {},
        Location sourceLocation = {}
    )
        : Statement(sourceLocation, NodeKind::UseDeclaration),
          path(std::move(modulePath)) {
    }

    std::string path;

    std::string alias;
};


// ============================================================
// Export
// ============================================================

struct ExportDeclaration final : Statement {

    explicit ExportDeclaration(
        Stmt exportedDeclaration = nullptr,
        Location sourceLocation = {}
    )
        : Statement(
            sourceLocation,
            NodeKind::ExportDeclaration
        ),
          declaration(std::move(exportedDeclaration)) {
    }

    Stmt declaration;
};


// ============================================================
// Utility names
// ============================================================

inline const char* nodeKindName(
    NodeKind kind
) {

    switch (kind) {

        case NodeKind::Unknown:
            return "Unknown";

        case NodeKind::Program:
            return "Program";

        case NodeKind::Block:
            return "Block";

        case NodeKind::Type:
            return "Type";

        case NodeKind::Parameter:
            return "Parameter";

        case NodeKind::StartStatement:
            return "StartStatement";

        case NodeKind::ExpressionStatement:
            return "ExpressionStatement";

        case NodeKind::VariableDeclaration:
            return "VariableDeclaration";

        case NodeKind::AssignmentStatement:
            return "AssignmentStatement";

        case NodeKind::ReturnStatement:
            return "ReturnStatement";

        case NodeKind::ThrowStatement:
            return "ThrowStatement";

        case NodeKind::TryStatement:
            return "TryStatement";

        case NodeKind::DeferStatement:
            return "DeferStatement";

        case NodeKind::IfStatement:
            return "IfStatement";

        case NodeKind::WhileStatement:
            return "WhileStatement";

        case NodeKind::ForStatement:
            return "ForStatement";

        case NodeKind::RepeatStatement:
            return "RepeatStatement";

        case NodeKind::BreakStatement:
            return "BreakStatement";

        case NodeKind::ContinueStatement:
            return "ContinueStatement";

        case NodeKind::SwitchStatement:
            return "SwitchStatement";

        case NodeKind::CaseStatement:
            return "CaseStatement";

        case NodeKind::FunctionDeclaration:
            return "FunctionDeclaration";

        case NodeKind::StructField:
            return "StructField";

        case NodeKind::StructDeclaration:
            return "StructDeclaration";

        case NodeKind::ClassField:
            return "ClassField";

        case NodeKind::ClassDeclaration:
            return "ClassDeclaration";

        case NodeKind::EnumValue:
            return "EnumValue";

        case NodeKind::EnumDeclaration:
            return "EnumDeclaration";

        case NodeKind::NamespaceDeclaration:
            return "NamespaceDeclaration";

        case NodeKind::ImportDeclaration:
            return "ImportDeclaration";
        case NodeKind::UseDeclaration:
            return "UseDeclaration";

        case NodeKind::ExportDeclaration:
            return "ExportDeclaration";

        case NodeKind::Literal:
            return "Literal";

        case NodeKind::Identifier:
            return "Identifier";

        case NodeKind::UnaryExpression:
            return "UnaryExpression";

        case NodeKind::BinaryExpression:
            return "BinaryExpression";

        case NodeKind::AssignmentExpression:
            return "AssignmentExpression";

        case NodeKind::ConditionalExpression:
            return "ConditionalExpression";

        case NodeKind::CallExpression:
            return "CallExpression";

        case NodeKind::MemberExpression:
            return "MemberExpression";

        case NodeKind::IndexExpression:
            return "IndexExpression";

        case NodeKind::ArrayExpression:
            return "ArrayExpression";
    }

    return "Unknown";
}


// ============================================================
// Type utility
// ============================================================

inline const char* typeKindName(
    Type::Kind kind
) {

    switch (kind) {

        case Type::Kind::Unknown:
            return "unknown";

        case Type::Kind::Number:
            return "number";

        case Type::Kind::String:
            return "string";

        case Type::Kind::Character:
            return "char";

        case Type::Kind::Boolean:
            return "boolean";

        case Type::Kind::Void:
            return "void";

        case Type::Kind::Custom:
            return "custom";

        case Type::Kind::Array:
            return "array";

        case Type::Kind::Pointer:
            return "pointer";
    }

    return "unknown";
}


// ============================================================
// Literal utility
// ============================================================

inline const char* literalKindName(
    Literal::Kind kind
) {

    switch (kind) {

        case Literal::Kind::Number:
            return "number";

        case Literal::Kind::String:
            return "string";

        case Literal::Kind::Character:
            return "character";

        case Literal::Kind::Boolean:
            return "boolean";

        case Literal::Kind::Null:
            return "null";
    }

    return "unknown";
}


// ============================================================
// Expression helpers
// ============================================================

inline bool isIdentifier(
    const Expression* expression
) {
    return dynamic_cast<
        const Identifier*
    >(expression) != nullptr;
}

inline bool isLiteral(
    const Expression* expression
) {
    return dynamic_cast<
        const Literal*
    >(expression) != nullptr;
}

inline bool isAssignment(
    const Expression* expression
) {
    return dynamic_cast<
        const AssignmentExpression*
    >(expression) != nullptr;
}

} // namespace zplus::ast