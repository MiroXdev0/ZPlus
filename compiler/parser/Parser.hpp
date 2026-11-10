#pragma once

#include "AST.hpp"
#include "../lexer/Token.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace zplus::parser {

// ============================================================
// Parse error
// ============================================================

struct ParseError {

    std::string message;

    int line = 1;

    int column = 1;

    std::string sourceFile;
};


// ============================================================
// Parser
// ============================================================

class Parser {

public:

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------

    explicit Parser(
        std::vector<zplus::Token> tokens
    );

    // --------------------------------------------------------
    // Parse complete source
    // --------------------------------------------------------

    std::unique_ptr<zplus::ast::Program>
    parse();

    // --------------------------------------------------------
    // Errors
    // --------------------------------------------------------

    const std::vector<ParseError>&
    errors() const {
        return errors_;
    }

private:

    // ========================================================
    // Token navigation
    // ========================================================

    bool isAtEnd() const;

    const zplus::Token&
    current() const;

    const zplus::Token&
    previous() const;

    const zplus::Token&
    peek(
        std::size_t offset = 1
    ) const;

    bool check(
        zplus::TokenType type
    ) const;

    bool checkNext(
        zplus::TokenType type
    ) const;

    bool match(
        zplus::TokenType type
    );

    bool matchAny(
        std::initializer_list<
            zplus::TokenType
        > types
    );

    const zplus::Token&
    advance();

    // ========================================================
    // Token consumption
    // ========================================================

    const zplus::Token&
    consume(
        zplus::TokenType type,
        const std::string& message
    );

    // ========================================================
    // Errors
    // ========================================================

    void error(
        const std::string& message
    );

    void errorAt(
        const zplus::Token& token,
        const std::string& message
    );

    // ========================================================
    // Newlines
    // ========================================================

    void skipNewlines();

    void consumeNewline();

    bool matchNewline();

    void consumeOptionalSemicolon();

    // ========================================================
    // Program
    // ========================================================

    std::unique_ptr<zplus::ast::Program>
    parseProgram();

    // ========================================================
    // Statements
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseDeclaration();

    std::unique_ptr<zplus::ast::Statement>
    parseVariable(
        bool constant
    );

    std::unique_ptr<zplus::ast::Statement>
    parseExpressionStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseReturnStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseThrowStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseTryStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseDeferStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseBreakStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseContinueStatement();

    // ========================================================
    // Program / blocks
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseStartBlock();

    std::vector<
        std::unique_ptr<zplus::ast::Statement>
    >
    parseBlock();

    std::vector<
        std::unique_ptr<zplus::ast::Statement>
    >
    parseUntil(
        zplus::TokenType endToken
    );

    // ========================================================
    // Control flow
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseIfStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseWhileStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseForStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseRepeatStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseSwitchStatement();

    std::unique_ptr<zplus::ast::Statement>
    parseCase();

    // ========================================================
    // Functions
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseFunction();

    std::vector<
        std::unique_ptr<zplus::ast::Parameter>
    >
    parseParameters();

    // ========================================================
    // Structures
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseStruct();

    std::unique_ptr<zplus::ast::Statement>
    parseClass();

    std::unique_ptr<zplus::ast::Statement>
    parseEnum();

    // ========================================================
    // Modules / namespaces
    // ========================================================

    std::unique_ptr<zplus::ast::Statement>
    parseNamespace();

    std::unique_ptr<zplus::ast::Statement>
    parseImport();

    std::unique_ptr<zplus::ast::Statement>
    parseUse();

    std::unique_ptr<zplus::ast::Statement>
    parsePublicDeclaration();

    std::unique_ptr<zplus::ast::Statement>
    parseExport();

    // ========================================================
    // Conditions
    // ========================================================

    std::unique_ptr<zplus::ast::Expression>
    parseCondition();

    // ========================================================
    // Expressions
    // ========================================================

    std::unique_ptr<zplus::ast::Expression>
    parseExpression();

    std::unique_ptr<zplus::ast::Expression>
    parseAssignment();

    std::unique_ptr<zplus::ast::Expression>
    parseConditional();

    std::unique_ptr<zplus::ast::Expression>
    parseLogicalOr();

    std::unique_ptr<zplus::ast::Expression>
    parseLogicalAnd();

    std::unique_ptr<zplus::ast::Expression>
    parseBitwiseOr();

    std::unique_ptr<zplus::ast::Expression>
    parseBitwiseXor();

    std::unique_ptr<zplus::ast::Expression>
    parseBitwiseAnd();

    std::unique_ptr<zplus::ast::Expression>
    parseEquality();

    std::unique_ptr<zplus::ast::Expression>
    parseComparison();

    std::unique_ptr<zplus::ast::Expression>
    parseShift();

    std::unique_ptr<zplus::ast::Expression>
    parseTerm();

    std::unique_ptr<zplus::ast::Expression>
    parseFactor();

    std::unique_ptr<zplus::ast::Expression>
    parseUnary();

    std::unique_ptr<zplus::ast::Expression>
    parsePostfix();

    std::unique_ptr<zplus::ast::Expression>
    parsePrimary();

    // ========================================================
    // Postfix expressions
    // ========================================================

    std::unique_ptr<zplus::ast::Expression>
    parseCall(
        std::unique_ptr<zplus::ast::Expression> callee
    );

    std::unique_ptr<zplus::ast::Expression>
    parseIndex(
        std::unique_ptr<zplus::ast::Expression> object
    );

    std::unique_ptr<zplus::ast::Expression>
    parseMember(
        std::unique_ptr<zplus::ast::Expression> object
    );

    // ========================================================
    // Function arguments
    // ========================================================

    std::vector<
        std::unique_ptr<zplus::ast::Expression>
    >
    parseArguments();

    // ========================================================
    // Types
    // ========================================================

    std::unique_ptr<zplus::ast::Type>
    parseType();

    bool isTypeStart() const;

    bool isTypeToken(
        zplus::TokenType type
    ) const;

    // ========================================================
    // Parser helpers
    // ========================================================

    bool isStatementStart() const;

    bool isBinaryOperator(
        zplus::TokenType type
    ) const;

    bool isUnaryOperator(
        zplus::TokenType type
    ) const;

    bool isAssignmentOperator(
        zplus::TokenType type
    ) const;

    void synchronize();

private:

    // ========================================================
    // Token stream
    // ========================================================

    std::vector<zplus::Token> tokens_;

    std::size_t current_ = 0;

    // ========================================================
    // Parser errors
    // ========================================================

    std::vector<ParseError> errors_;
};

} // namespace zplus::parser