#include "Parser.hpp"

#include <initializer_list>
#include <utility>

namespace zplus::parser {

using zplus::Token;
using zplus::TokenType;

namespace {

// ============================================================
// Helpers
// ============================================================

template <typename T>
void setLocation(
    T& node,
    const Token& token
) {
    node.location =
        zplus::ast::Location(
            token.line,
            token.column,
            token.sourceFile
        );
}

bool isStatementBoundary(TokenType type) {

    return
        type == TokenType::Newline ||
        type == TokenType::Semicolon ||
        type == TokenType::EndOfFile ||
        type == TokenType::End ||
        type == TokenType::Else ||
        type == TokenType::Catch ||
        type == TokenType::Stop;
}

bool isBlockTerminator(TokenType type) {

    return
        type == TokenType::End ||
        type == TokenType::Else ||
        type == TokenType::Stop ||
        type == TokenType::EndOfFile;
}

std::string describeToken(const Token& token) {
    if (token.type == TokenType::EndOfFile) {
        return "end of file";
    }
    if (token.type == TokenType::Newline) {
        return "end of line";
    }
    if (!token.value.empty()) {
        return "'" + token.value + "' (" + zplus::tokenTypeName(token.type) + ")";
    }
    return zplus::tokenTypeName(token.type);
}

} // namespace

// ============================================================
// Constructor
// ============================================================

Parser::Parser(
    std::vector<Token> tokens
)
    : tokens_(std::move(tokens)) {

    if (tokens_.empty()) {
        tokens_.push_back(
            Token{
                TokenType::EndOfFile,
                "",
                1,
                1
            }
        );
    }
}

// ============================================================
// Token navigation
// ============================================================

bool Parser::isAtEnd() const {
    return current().type == TokenType::EndOfFile;
}

const Token& Parser::current() const {

    if (current_ >= tokens_.size()) {
        return tokens_.back();
    }

    return tokens_[current_];
}

const Token& Parser::previous() const {

    if (current_ == 0) {
        return tokens_.front();
    }

    return tokens_[current_ - 1];
}

const Token& Parser::peek(
    std::size_t offset
) const {

    const std::size_t index =
        current_ + offset;

    if (index >= tokens_.size()) {
        return tokens_.back();
    }

    return tokens_[index];
}

bool Parser::check(
    TokenType type
) const {
    return current().type == type;
}

bool Parser::checkNext(
    TokenType type
) const {
    return peek(1).type == type;
}

bool Parser::match(
    TokenType type
) {

    if (!check(type)) {
        return false;
    }

    advance();
    return true;
}

bool Parser::matchAny(
    std::initializer_list<TokenType> types
) {

    for (TokenType type : types) {

        if (check(type)) {
            advance();
            return true;
        }
    }

    return false;
}

const Token& Parser::advance() {

    if (!isAtEnd()) {
        ++current_;
    }

    return previous();
}

// ============================================================
// Errors
// ============================================================

void Parser::error(
    const std::string& message
) {

    errorAt(
        current(),
        message
    );
}

void Parser::errorAt(
    const Token& token,
    const std::string& message
) {

    if (!errors_.empty()) {
        const ParseError& previousError = errors_.back();
        if (previousError.line == token.line &&
            previousError.column == token.column &&
            previousError.sourceFile == token.sourceFile) {
            return;
        }
    }

    errors_.push_back({
        message,
        token.line,
        token.column,
        token.sourceFile
    });
}

const Token& Parser::consume(
    TokenType type,
    const std::string& message
) {

    if (check(type)) {
        return advance();
    }

    error(message + "; found " + describeToken(current()));
    return current();
}

// ============================================================
// Newlines
// ============================================================

void Parser::skipNewlines() {

    while (
        check(TokenType::Newline) ||
        check(TokenType::Semicolon)
    ) {
        advance();
    }
}

void Parser::consumeNewline() {

    if (check(TokenType::Newline)) {
        advance();
    }
}

bool Parser::matchNewline() {
    return match(TokenType::Newline);
}

void Parser::consumeOptionalSemicolon() {
    match(TokenType::Semicolon);
}

// ============================================================
// Main parse
// ============================================================

std::unique_ptr<zplus::ast::Program>
Parser::parse() {
    return parseProgram();
}

// ============================================================
// Program
// ============================================================

std::unique_ptr<zplus::ast::Program>
Parser::parseProgram() {

    auto program =
        std::make_unique<zplus::ast::Program>();

    if (!tokens_.empty()) {
        program->location =
            zplus::ast::Location(
                tokens_.front().line,
                tokens_.front().column,
                tokens_.front().sourceFile
            );
    }

    skipNewlines();

    while (!isAtEnd()) {

        auto statement =
            parseStatement();

        if (statement) {

            program->statements.push_back(
                std::move(statement)
            );

        } else {

            synchronize();
        }

        skipNewlines();
    }

    return program;
}

// ============================================================
// Statements
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseStatement() {

    skipNewlines();

    if (isAtEnd()) {
        return nullptr;
    }

    switch (current().type) {

        case TokenType::Start:
            return parseStartBlock();

        case TokenType::Let:
            return parseVariable(false);

        case TokenType::Const:
            return parseVariable(true);

        case TokenType::If:
            return parseIfStatement();

        case TokenType::While:
            return parseWhileStatement();

        case TokenType::For:
            return parseForStatement();

        case TokenType::Repeat:
            return parseRepeatStatement();

        case TokenType::Fn:
            return parseFunction();

        case TokenType::Try:
            return parseTryStatement();

        case TokenType::Defer:
            return parseDeferStatement();

        case TokenType::Throw:
            return parseThrowStatement();

        case TokenType::Return:
            return parseReturnStatement();

        case TokenType::Break:
            return parseBreakStatement();

        case TokenType::Continue:
            return parseContinueStatement();

        case TokenType::Switch:
            return parseSwitchStatement();

        case TokenType::Struct:
            return parseStruct();

        case TokenType::Class:
            return parseClass();

        case TokenType::Enum:
            return parseEnum();

        case TokenType::Namespace:
            return parseNamespace();

        case TokenType::Import:
            return parseImport();

        case TokenType::Use:
            return parseUse();

        case TokenType::Pub:
            return parsePublicDeclaration();

        case TokenType::Export:
            return parseExport();

        case TokenType::End:
            error("unexpected 'end'");
            advance();
            return nullptr;

        case TokenType::Else:
            error("unexpected 'else'");
            advance();
            return nullptr;

        case TokenType::Stop:
            error("unexpected 'stop'");
            advance();
            return nullptr;

        default:
            return parseExpressionStatement();
    }
}

// ============================================================
// Declarations
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseDeclaration() {

    return parseVariable(
        check(TokenType::Const)
    );
}

// ============================================================
// Variable declaration
// ============================================================
//
// let x
// let x = 10
// let x: number
// let x: number = 10
//
// const x = 10
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseVariable(
    bool constant
) {

    const Token token =
        advance();

    auto declaration =
        std::make_unique<
            zplus::ast::VariableDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    declaration->isConst =
        constant;

    if (!check(TokenType::Identifier)) {

        error(
            "expected variable name after declaration"
        );

        return nullptr;
    }

    declaration->name =
        current().value;

    advance();

    // --------------------------------------------------------
    // Optional type
    // --------------------------------------------------------

    if (match(TokenType::Colon)) {

        declaration->type =
            parseType();

        declaration->hasExplicitType =
            declaration->type != nullptr;
    }

    // --------------------------------------------------------
    // Optional initializer
    // --------------------------------------------------------

    if (match(TokenType::Equal)) {

        declaration->initializer =
            parseExpression();

        if (!declaration->initializer) {
            return nullptr;
        }

    } else if (constant) {

        error(
            "constant declaration requires an initializer"
        );

        return nullptr;
    }

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return declaration;
}

// ============================================================
// Start
// ============================================================
//
// start:
//     ...
// stop
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseStartBlock() {

    const Token token =
        consume(
            TokenType::Start,
            "expected 'start'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::StartStatement
        >();

    setLocation(
        *statement,
        token
    );

    consume(
        TokenType::Colon,
        "expected ':' after 'start'"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::Stop)
    ) {

        if (check(TokenType::End)) {

            error(
                "unexpected 'end'; use 'stop' to close 'start:'"
            );

            advance();
            skipNewlines();
            continue;
        }

        auto child =
            parseStatement();

        if (child) {

            statement->body.push_back(
                std::move(child)
            );

        } else {

            synchronize();
        }

        skipNewlines();
    }

    consume(
        TokenType::Stop,
        "expected 'stop' to close 'start:'"
    );

    return statement;
}

// ============================================================
// If
// ============================================================
//
// if x > 10:
//     ...
// end
//
// if (x > 10):
//     ...
// else:
//     ...
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseIfStatement() {

    const Token token =
        consume(
            TokenType::If,
            "expected 'if'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::IfStatement
        >();

    setLocation(
        *statement,
        token
    );

    statement->condition =
        parseCondition();

    if (!statement->condition) {
        return nullptr;
    }

    consume(
        TokenType::Colon,
        "expected ':' after if condition"
    );

    skipNewlines();

    statement->thenBody =
        parseUntil(
            TokenType::Else
        );

    if (match(TokenType::Else)) {

        statement->hasElse = true;

        consume(
            TokenType::Colon,
            "expected ':' after 'else'"
        );

        skipNewlines();

        statement->elseBody =
            parseUntil(
                TokenType::End
            );
    }

    consume(
        TokenType::End,
        "expected 'end' after if"
    );

    return statement;
}

// ============================================================
// While
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseWhileStatement() {

    const Token token =
        consume(
            TokenType::While,
            "expected 'while'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::WhileStatement
        >();

    setLocation(
        *statement,
        token
    );

    statement->condition =
        parseCondition();

    if (!statement->condition) {
        return nullptr;
    }

    consume(
        TokenType::Colon,
        "expected ':' after while condition"
    );

    skipNewlines();

    statement->body =
        parseUntil(
            TokenType::End
        );

    consume(
        TokenType::End,
        "expected 'end' after while"
    );

    return statement;
}

// ============================================================
// Repeat
// ============================================================
//
// repeat (10):
//     ...
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseRepeatStatement() {

    const Token token =
        consume(
            TokenType::Repeat,
            "expected 'repeat'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::RepeatStatement
        >();

    setLocation(
        *statement,
        token
    );

    consume(
        TokenType::LeftParen,
        "expected '(' after 'repeat'"
    );

    statement->count =
        parseExpression();

    if (!statement->count) {
        return nullptr;
    }

    consume(
        TokenType::RightParen,
        "expected ')' after repeat count"
    );

    consume(
        TokenType::Colon,
        "expected ':' after repeat count"
    );

    skipNewlines();

    statement->body =
        parseUntil(
            TokenType::End
        );

    consume(
        TokenType::End,
        "expected 'end' after repeat"
    );

    return statement;
}

// ============================================================
// While / For condition helper
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseCondition() {

    if (match(TokenType::LeftParen)) {

        auto condition =
            parseExpression();

        consume(
            TokenType::RightParen,
            "expected ')' after condition"
        );

        return condition;
    }

    return parseExpression();
}

// ============================================================
// For
// ============================================================
//
// for (let i = 0; i < 10; i++):
//     ...
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseForStatement() {

    const Token token =
        consume(
            TokenType::For,
            "expected 'for'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::ForStatement
        >();

    setLocation(
        *statement,
        token
    );

    consume(
        TokenType::LeftParen,
        "expected '(' after 'for'"
    );

    // --------------------------------------------------------
    // Initializer
    // --------------------------------------------------------

    if (!check(TokenType::Semicolon)) {

        if (
            check(TokenType::Let) ||
            check(TokenType::Const)
        ) {

            const Token declarationToken =
                advance();

            auto declaration =
                std::make_unique<
                    zplus::ast::VariableDeclaration
                >();

            setLocation(
                *declaration,
                declarationToken
            );

            declaration->isConst =
                declarationToken.type ==
                TokenType::Const;

            if (!check(TokenType::Identifier)) {

                error(
                    "expected variable name in for initializer"
                );

                return nullptr;
            }

            declaration->name =
                current().value;

            advance();

            if (match(TokenType::Colon)) {

                declaration->type =
                    parseType();

                declaration->hasExplicitType =
                    declaration->type != nullptr;
            }

            if (match(TokenType::Equal)) {

                declaration->initializer =
                    parseExpression();

                if (!declaration->initializer) {
                    return nullptr;
                }

            } else if (declaration->isConst) {

                error(
                    "constant declaration requires an initializer"
                );

                return nullptr;
            }

            statement->initializer =
                std::move(declaration);

        } else {

            auto initializerExpression =
                parseExpression();

            if (!initializerExpression) {
                return nullptr;
            }

            auto initializer =
                std::make_unique<
                    zplus::ast::ExpressionStatement
                >();

            initializer->expression =
                std::move(initializerExpression);

            statement->initializer =
                std::move(initializer);
        }
    }

    consume(
        TokenType::Semicolon,
        "expected ';' after for initializer"
    );

    // --------------------------------------------------------
    // Condition
    // --------------------------------------------------------

    if (!check(TokenType::Semicolon)) {

        statement->condition =
            parseExpression();

        if (!statement->condition) {
            return nullptr;
        }
    }

    consume(
        TokenType::Semicolon,
        "expected ';' after for condition"
    );

    // --------------------------------------------------------
    // Increment
    // --------------------------------------------------------

    if (!check(TokenType::RightParen)) {

        statement->increment =
            parseExpression();

        if (!statement->increment) {
            return nullptr;
        }
    }

    consume(
        TokenType::RightParen,
        "expected ')' after for expression"
    );

    consume(
        TokenType::Colon,
        "expected ':' after for expression"
    );

    skipNewlines();

    statement->body =
        parseUntil(
            TokenType::End
        );

    consume(
        TokenType::End,
        "expected 'end' after for"
    );

    return statement;
}

// ============================================================
// Function
// ============================================================
//
// fn add(a: number, b: number) -> number:
//     return a + b
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseFunction() {

    const Token token =
        consume(
            TokenType::Fn,
            "expected 'fn'"
        );

    auto function =
        std::make_unique<
            zplus::ast::FunctionDeclaration
        >();

    setLocation(
        *function,
        token
    );

    if (!check(TokenType::Identifier) && !check(TokenType::Repeat) &&
        !check(TokenType::Stop) &&
        !check(TokenType::NumberType) && !check(TokenType::BooleanType)) {

        error(
            "expected function name after 'fn'"
        );

        return nullptr;
    }

    function->name =
        current().value;

    advance();

    function->parameters =
        parseParameters();

    // --------------------------------------------------------
    // Return type
    //
    // -> number
    // --------------------------------------------------------

    if (match(TokenType::Arrow)) {

        function->returnType =
            parseType();

        if (!function->returnType) {
            return nullptr;
        }
    }

    consume(
        TokenType::Colon,
        "expected ':' after function declaration"
    );

    skipNewlines();

    function->body =
        parseUntil(
            TokenType::End
        );

    consume(
        TokenType::End,
        "expected 'end' after function"
    );

    return function;
}

// ============================================================
// Parameters
// ============================================================

std::vector<
    std::unique_ptr<zplus::ast::Parameter>
>
Parser::parseParameters() {

    std::vector<
        std::unique_ptr<zplus::ast::Parameter>
    > parameters;

    consume(
        TokenType::LeftParen,
        "expected '(' after function name"
    );

    if (check(TokenType::RightParen)) {

        advance();
        return parameters;
    }

    while (!isAtEnd()) {

        if (!check(TokenType::Identifier)) {

            error(
                "expected parameter name"
            );

            return parameters;
        }

        const Token parameterToken =
            current();

        auto parameter =
            std::make_unique<
                zplus::ast::Parameter
            >();

        setLocation(
            *parameter,
            parameterToken
        );

        parameter->name =
            parameterToken.value;

        advance();

        // Optional parameter type

        if (match(TokenType::Colon)) {

            parameter->type =
                parseType();

            if (!parameter->type) {
                return parameters;
            }
        }

        // Optional default value

        if (match(TokenType::Equal)) {

            parameter->defaultValue =
                parseExpression();

            if (!parameter->defaultValue) {
                return parameters;
            }
        }

        parameters.push_back(
            std::move(parameter)
        );

        if (!match(TokenType::Comma)) {
            break;
        }

        if (check(TokenType::RightParen)) {

            error(
                "expected parameter after ','"
            );

            break;
        }
    }

    consume(
        TokenType::RightParen,
        "expected ')' after parameters"
    );

    return parameters;
}

// ============================================================
// Return
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseReturnStatement() {

    const Token token =
        consume(
            TokenType::Return,
            "expected 'return'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::ReturnStatement
        >();

    setLocation(
        *statement,
        token
    );

    if (
        !isStatementBoundary(
            current().type
        )
    ) {

        statement->value =
            parseExpression();

        if (!statement->value) {
            return nullptr;
        }

        statement->hasValue = true;
    }

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return statement;
}

std::unique_ptr<zplus::ast::Statement>
Parser::parseThrowStatement() {
    const Token token = advance();
    auto statement = std::make_unique<zplus::ast::ThrowStatement>();
    setLocation(*statement, token);
    if (isStatementBoundary(current().type)) {
        error("expected an expression after 'throw'");
        return nullptr;
    }
    statement->value = parseExpression();
    if (!statement->value) {
        return nullptr;
    }
    consumeOptionalSemicolon();
    if (check(TokenType::Newline)) advance();
    return statement;
}

std::unique_ptr<zplus::ast::Statement>
Parser::parseTryStatement() {
    const Token token = advance();
    auto statement = std::make_unique<zplus::ast::TryStatement>();
    setLocation(*statement, token);
    consume(TokenType::Colon, "expected ':' after 'try'");
    skipNewlines();
    statement->tryBody = parseUntil(TokenType::Catch);
    if (!match(TokenType::Catch)) {
        error("expected 'catch' after 'try' block");
        return nullptr;
    }
    consume(TokenType::LeftParen, "expected '(' after 'catch'");
    if (!check(TokenType::Identifier)) {
        error("expected error variable name in 'catch'");
        return nullptr;
    }
    statement->catchName = advance().value;
    consume(TokenType::RightParen, "expected ')' after catch variable");
    consume(TokenType::Colon, "expected ':' after catch clause");
    skipNewlines();
    statement->catchBody = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after 'catch' block");
    return statement;
}

std::unique_ptr<zplus::ast::Statement>
Parser::parseDeferStatement() {
    const Token token = advance();
    auto statement = std::make_unique<zplus::ast::DeferStatement>();
    setLocation(*statement, token);
    consume(TokenType::Colon, "expected ':' after 'defer'");
    skipNewlines();
    statement->body = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after 'defer' block");
    return statement;
}

// ============================================================
// Break
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseBreakStatement() {

    const Token token =
        consume(
            TokenType::Break,
            "expected 'break'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::BreakStatement
        >();

    setLocation(
        *statement,
        token
    );

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return statement;
}

// ============================================================
// Continue
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseContinueStatement() {

    const Token token =
        consume(
            TokenType::Continue,
            "expected 'continue'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::ContinueStatement
        >();

    setLocation(
        *statement,
        token
    );

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return statement;
}

// ============================================================
// Expression statement
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseExpressionStatement() {

    const Token token =
        current();

    auto expression =
        parseExpression();

    if (!expression) {
        return nullptr;
    }

    auto statement =
        std::make_unique<
            zplus::ast::ExpressionStatement
        >();

    setLocation(
        *statement,
        token
    );

    statement->expression =
        std::move(expression);

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return statement;
}

// ============================================================
// Blocks
// ============================================================

std::vector<
    std::unique_ptr<zplus::ast::Statement>
>
Parser::parseBlock() {

    return parseUntil(
        TokenType::End
    );
}

std::vector<
    std::unique_ptr<zplus::ast::Statement>
>
Parser::parseUntil(
    TokenType endToken
) {

    std::vector<
        std::unique_ptr<zplus::ast::Statement>
    > statements;

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(endToken)
    ) {

        if (endToken == TokenType::Else && check(TokenType::End)) {
            break;
        }
        if (endToken == TokenType::Catch && check(TokenType::End)) {
            break;
        }

        if (
            endToken != TokenType::Else &&
            check(TokenType::Else)
        ) {
            break;
        }

        if (
            endToken != TokenType::Stop &&
            check(TokenType::Stop)
        ) {

            error(
                "unexpected 'stop'"
            );

            break;
        }

        auto statement =
            parseStatement();

        if (statement) {

            statements.push_back(
                std::move(statement)
            );

        } else {

            synchronize();
        }

        skipNewlines();
    }

    return statements;
}

// ============================================================
// Expressions
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseExpression() {

    return parseAssignment();
}

// ============================================================
// Assignment
// ============================================================

bool Parser::isAssignmentOperator(
    TokenType type
) const {

    switch (type) {

        case TokenType::Equal:
        case TokenType::PlusEqual:
        case TokenType::MinusEqual:
        case TokenType::StarEqual:
        case TokenType::SlashEqual:
        case TokenType::PercentEqual:
        case TokenType::AndEqual:
        case TokenType::OrEqual:
        case TokenType::XorEqual:
        case TokenType::LeftShiftEqual:
        case TokenType::RightShiftEqual:
            return true;

        default:
            return false;
    }
}

std::unique_ptr<zplus::ast::Expression>
Parser::parseAssignment() {

    auto left =
        parseConditional();

    if (
        !left ||
        !isAssignmentOperator(
            current().type
        )
    ) {
        return left;
    }

    const Token operatorToken =
        advance();

    auto right =
        parseAssignment();

    if (!right) {

        errorAt(
            operatorToken,
            "expected expression after assignment operator"
        );

        return nullptr;
    }

    // Only identifiers, members and indexes are assignable.
    if (
        !zplus::ast::isIdentifier(
            left.get()
        ) &&
        dynamic_cast<
            zplus::ast::MemberExpression*
        >(left.get()) == nullptr &&
        dynamic_cast<
            zplus::ast::IndexExpression*
        >(left.get()) == nullptr
    ) {

        errorAt(
            operatorToken,
            "invalid assignment target"
        );

        return nullptr;
    }

    auto assignment =
        std::make_unique<
            zplus::ast::AssignmentExpression
        >();

    setLocation(
        *assignment,
        operatorToken
    );

    assignment->target =
        std::move(left);

    assignment->operatorType =
        operatorToken.type;

    assignment->value =
        std::move(right);

    return assignment;
}

// ============================================================
// Conditional
// ============================================================
//
// condition ? value1 : value2
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseConditional() {

    auto condition =
        parseLogicalOr();

    if (!condition) {
        return nullptr;
    }

    if (!match(TokenType::Question)) {
        return condition;
    }

    const Token questionToken =
        previous();

    auto thenExpression =
        parseExpression();

    if (!thenExpression) {
        return nullptr;
    }

    consume(
        TokenType::Colon,
        "expected ':' in conditional expression"
    );

    auto elseExpression =
        parseConditional();

    if (!elseExpression) {
        return nullptr;
    }

    auto expression =
        std::make_unique<
            zplus::ast::ConditionalExpression
        >();

    setLocation(
        *expression,
        questionToken
    );

    expression->condition =
        std::move(condition);

    expression->thenExpression =
        std::move(thenExpression);

    expression->elseExpression =
        std::move(elseExpression);

    return expression;
}

// ============================================================
// Logical OR
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseLogicalOr() {

    auto expression =
        parseLogicalAnd();

    while (
        check(TokenType::OrOr) ||
        check(TokenType::Or)
    ) {

        const Token token =
            advance();

        auto right =
            parseLogicalAnd();

        if (!right) {
            errorAt(
                token,
                "expected expression after logical OR"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Logical AND
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseLogicalAnd() {

    auto expression =
        parseBitwiseOr();

    while (
        check(TokenType::AndAnd) ||
        check(TokenType::And)
    ) {

        const Token token =
            advance();

        auto right =
            parseBitwiseOr();

        if (!right) {
            errorAt(
                token,
                "expected expression after logical AND"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Bitwise OR
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseBitwiseOr() {

    auto expression =
        parseBitwiseXor();

    while (check(TokenType::BitOr)) {

        const Token token =
            advance();

        auto right =
            parseBitwiseXor();

        if (!right) {
            errorAt(
                token,
                "expected expression after '|'" 
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Bitwise XOR
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseBitwiseXor() {

    auto expression =
        parseBitwiseAnd();

    while (check(TokenType::BitXor)) {

        const Token token =
            advance();

        auto right =
            parseBitwiseAnd();

        if (!right) {
            errorAt(
                token,
                "expected expression after '^'"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Bitwise AND
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseBitwiseAnd() {

    auto expression =
        parseEquality();

    while (check(TokenType::BitAnd)) {

        const Token token =
            advance();

        auto right =
            parseEquality();

        if (!right) {
            errorAt(
                token,
                "expected expression after '&'"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Equality
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseEquality() {

    auto expression =
        parseComparison();

    while (
        check(TokenType::EqualEqual) ||
        check(TokenType::NotEqual)
    ) {

        const Token token =
            advance();

        auto right =
            parseComparison();

        if (!right) {
            errorAt(
                token,
                "expected expression after equality operator"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Comparison
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseComparison() {

    auto expression =
        parseShift();

    while (
        check(TokenType::Less) ||
        check(TokenType::LessEqual) ||
        check(TokenType::Greater) ||
        check(TokenType::GreaterEqual) ||
        check(TokenType::Is)
    ) {

        const Token token =
            advance();

        auto right =
            parseShift();

        if (!right) {
            errorAt(
                token,
                "expected expression after comparison operator"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Shift
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseShift() {

    auto expression =
        parseTerm();

    while (
        check(TokenType::LeftShift) ||
        check(TokenType::RightShift)
    ) {

        const Token token =
            advance();

        auto right =
            parseTerm();

        if (!right) {
            errorAt(
                token,
                "expected expression after shift operator"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Addition / subtraction
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseTerm() {

    auto expression =
        parseFactor();

    while (
        check(TokenType::Plus) ||
        check(TokenType::Minus)
    ) {

        const Token token =
            advance();

        auto right =
            parseFactor();

        if (!right) {
            errorAt(
                token,
                "expected expression after operator"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Multiplication / division
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseFactor() {

    auto expression =
        parseUnary();

    while (
        check(TokenType::Star) ||
        check(TokenType::Slash) ||
        check(TokenType::Percent)
    ) {

        const Token token =
            advance();

        auto right =
            parseUnary();

        if (!right) {
            errorAt(
                token,
                "expected expression after operator"
            );
            return nullptr;
        }

        auto binary =
            std::make_unique<
                zplus::ast::BinaryExpression
            >();

        setLocation(
            *binary,
            token
        );

        binary->left =
            std::move(expression);

        binary->operatorType =
            token.type;

        binary->right =
            std::move(right);

        expression =
            std::move(binary);
    }

    return expression;
}

// ============================================================
// Unary
// ============================================================

bool Parser::isUnaryOperator(
    TokenType type
) const {

    switch (type) {

        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Exclamation:
        case TokenType::Not:
        case TokenType::BitNot:
        case TokenType::BitAnd:
        case TokenType::Star:
        case TokenType::PlusPlus:
        case TokenType::MinusMinus:
            return true;

        default:
            return false;
    }
}

std::unique_ptr<zplus::ast::Expression>
Parser::parseUnary() {

    if (!isUnaryOperator(current().type)) {
        return parsePostfix();
    }

    const Token token =
        advance();

    auto expression =
        std::make_unique<
            zplus::ast::UnaryExpression
        >();

    setLocation(
        *expression,
        token
    );

    expression->operatorType =
        token.type;

    expression->prefix = true;

    expression->operand =
        parseUnary();

    if (!expression->operand) {
        errorAt(
            token,
            "expected expression after unary operator"
        );

        return nullptr;
    }

    return expression;
}

// ============================================================
// Postfix
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parsePostfix() {

    auto expression =
        parsePrimary();

    if (!expression) {
        return nullptr;
    }

    while (!isAtEnd()) {

        // ----------------------------------------------------
        // Call
        // ----------------------------------------------------

        if (check(TokenType::LeftParen)) {

            expression =
                parseCall(
                    std::move(expression)
                );

            continue;
        }

        // ----------------------------------------------------
        // Index
        // ----------------------------------------------------

        if (check(TokenType::LeftBracket)) {

            expression =
                parseIndex(
                    std::move(expression)
                );

            continue;
        }

        // ----------------------------------------------------
        // Member
        // ----------------------------------------------------

        if (check(TokenType::Dot)) {

            expression =
                parseMember(
                    std::move(expression)
                );

            continue;
        }

        // ----------------------------------------------------
        // Pointer member
        // ----------------------------------------------------

        if (check(TokenType::Arrow)) {

            const Token arrow =
                advance();

            if (!check(TokenType::Identifier) && !check(TokenType::Repeat) &&
                !check(TokenType::NumberType) && !check(TokenType::BooleanType)) {

                errorAt(
                    arrow,
                    "expected member name after '->'"
                );

                return nullptr;
            }

            auto member =
                std::make_unique<
                    zplus::ast::MemberExpression
                >();

            setLocation(
                *member,
                arrow
            );

            member->object =
                std::move(expression);

            member->member =
                current().value;

            member->pointerAccess =
                true;

            advance();

            expression =
                std::move(member);

            continue;
        }

        // ----------------------------------------------------
        // Postfix ++ / --
        // ----------------------------------------------------

        if (
            check(TokenType::PlusPlus) ||
            check(TokenType::MinusMinus)
        ) {

            const Token token =
                advance();

            auto unary =
                std::make_unique<
                    zplus::ast::UnaryExpression
                >();

            setLocation(
                *unary,
                token
            );

            unary->operatorType =
                token.type;

            unary->operand =
                std::move(expression);

            unary->prefix =
                false;

            expression =
                std::move(unary);

            continue;
        }

        break;
    }

    return expression;
}

// ============================================================
// Function call
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseCall(
    std::unique_ptr<zplus::ast::Expression> callee
) {

    const Token token =
        consume(
            TokenType::LeftParen,
            "expected '('"
        );

    auto expression =
        std::make_unique<
            zplus::ast::CallExpression
        >();

    setLocation(
        *expression,
        token
    );

    expression->callee =
        std::move(callee);

    expression->arguments =
        parseArguments();

    return expression;
}

// ============================================================
// Arguments
// ============================================================

std::vector<
    std::unique_ptr<zplus::ast::Expression>
>
Parser::parseArguments() {

    std::vector<
        std::unique_ptr<zplus::ast::Expression>
    > arguments;

    if (check(TokenType::RightParen)) {

        advance();
        return arguments;
    }

    while (!isAtEnd()) {

        auto argument =
            parseExpression();

        if (!argument) {
            return arguments;
        }

        arguments.push_back(
            std::move(argument)
        );

        if (!match(TokenType::Comma)) {
            break;
        }

        if (check(TokenType::RightParen)) {

            error(
                "expected expression after ','"
            );

            break;
        }
    }

    consume(
        TokenType::RightParen,
        "expected ')' after arguments"
    );

    return arguments;
}

// ============================================================
// Index
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseIndex(
    std::unique_ptr<zplus::ast::Expression> object
) {

    const Token token =
        consume(
            TokenType::LeftBracket,
            "expected '['"
        );

    auto expression =
        std::make_unique<
            zplus::ast::IndexExpression
        >();

    setLocation(
        *expression,
        token
    );

    expression->object =
        std::move(object);

    expression->index =
        parseExpression();

    if (!expression->index) {
        return nullptr;
    }

    consume(
        TokenType::RightBracket,
        "expected ']' after index"
    );

    return expression;
}

// ============================================================
// Member
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parseMember(
    std::unique_ptr<zplus::ast::Expression> object
) {

    const Token token =
        consume(
            TokenType::Dot,
            "expected '.'"
        );

    auto expression =
        std::make_unique<
            zplus::ast::MemberExpression
        >();

    setLocation(
        *expression,
        token
    );

    expression->object =
        std::move(object);

    if (!check(TokenType::Identifier) && !check(TokenType::Repeat) &&
        !check(TokenType::Stop) && !check(TokenType::NumberType) &&
        !check(TokenType::BooleanType)) {

        error(
            "expected member name after '.'"
        );

        return nullptr;
    }

    expression->member =
        current().value;

    advance();

    return expression;
}

// ============================================================
// Primary
// ============================================================

std::unique_ptr<zplus::ast::Expression>
Parser::parsePrimary() {

    const Token token =
        current();

    // --------------------------------------------------------
    // Number
    // --------------------------------------------------------

    if (match(TokenType::Number)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::Number;

        literal->value =
            token.value;

        literal->isNumber =
            true;

        return literal;
    }

    // --------------------------------------------------------
    // String
    // --------------------------------------------------------

    if (match(TokenType::String)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::String;

        literal->value =
            token.value;

        return literal;
    }

    // --------------------------------------------------------
    // Character
    // --------------------------------------------------------

    if (match(TokenType::Character)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::Character;

        literal->value =
            token.value;

        return literal;
    }

    // --------------------------------------------------------
    // true
    // --------------------------------------------------------

    if (match(TokenType::True)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::Boolean;

        literal->value =
            "true";

        return literal;
    }

    // --------------------------------------------------------
    // false
    // --------------------------------------------------------

    if (match(TokenType::False)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::Boolean;

        literal->value =
            "false";

        return literal;
    }

    // --------------------------------------------------------
    // null
    // --------------------------------------------------------

    if (match(TokenType::Null)) {

        auto literal =
            std::make_unique<
                zplus::ast::Literal
            >();

        setLocation(
            *literal,
            token
        );

        literal->kind =
            zplus::ast::Literal::Kind::Null;

        literal->value =
            "null";

        return literal;
    }

    // --------------------------------------------------------
    // Identifier
    // --------------------------------------------------------

    if (match(TokenType::Identifier)) {

        auto identifier =
            std::make_unique<
                zplus::ast::Identifier
            >();

        setLocation(
            *identifier,
            token
        );

        identifier->name =
            token.value;

        return identifier;
    }

    // --------------------------------------------------------
    // Built-in names
    // --------------------------------------------------------

    if (
        match(TokenType::Say) ||
        match(TokenType::Printz)
    ) {

        auto identifier =
            std::make_unique<
                zplus::ast::Identifier
            >();

        setLocation(
            *identifier,
            token
        );

        identifier->name =
            token.value;

        return identifier;
    }

    // --------------------------------------------------------
    // Parentheses
    // --------------------------------------------------------

    if (match(TokenType::LeftParen)) {

        auto expression =
            parseExpression();

        consume(
            TokenType::RightParen,
            "expected ')' after expression"
        );

        return expression;
    }

    // --------------------------------------------------------
    // Array
    // --------------------------------------------------------

    if (match(TokenType::LeftBracket)) {

        auto array =
            std::make_unique<
                zplus::ast::ArrayExpression
            >();

        setLocation(
            *array,
            token
        );

        if (check(TokenType::RightBracket)) {

            advance();
            return array;
        }

        while (!isAtEnd()) {

            auto element =
                parseExpression();

            if (!element) {
                return nullptr;
            }

            array->elements.push_back(
                std::move(element)
            );

            if (!match(TokenType::Comma)) {
                break;
            }

            if (check(TokenType::RightBracket)) {
                break;
            }
        }

        consume(
            TokenType::RightBracket,
            "expected ']' after array"
        );

        return array;
    }

    error(
        "expected expression; found " + describeToken(token)
    );

    return nullptr;
}

// ============================================================
// Types
// ============================================================

bool Parser::isTypeStart() const {

    return isTypeToken(
        current().type
    );
}

bool Parser::isTypeToken(
    TokenType type
) const {

    switch (type) {

        case TokenType::NumberType:
        case TokenType::StringType:
        case TokenType::CharacterType:
        case TokenType::BooleanType:
        case TokenType::VoidType:
        case TokenType::Identifier:
            return true;

        default:
            return false;
    }
}

std::unique_ptr<zplus::ast::Type>
Parser::parseType() {

    const Token token =
        current();

    std::unique_ptr<zplus::ast::Type> type;

    switch (token.type) {

        case TokenType::NumberType:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    zplus::ast::Type::Kind::Number,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            type->name =
                "number";

            advance();
            break;

        case TokenType::StringType:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    zplus::ast::Type::Kind::String,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            type->name =
                "string";

            advance();
            break;

        case TokenType::CharacterType:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    zplus::ast::Type::Kind::Character,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            type->name =
                "char";

            advance();
            break;

        case TokenType::BooleanType:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    zplus::ast::Type::Kind::Boolean,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            type->name =
                "boolean";

            advance();
            break;

        case TokenType::VoidType:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    zplus::ast::Type::Kind::Void,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            type->name =
                "void";

            advance();
            break;

        case TokenType::Identifier:

            type =
                std::make_unique<
                    zplus::ast::Type
                >(
                    token.value,
                    zplus::ast::Location{
                        token.line,
                        token.column
                    }
                );

            advance();
            break;

        default:

            error(
                "expected type"
            );

            return nullptr;
    }

    type->location.sourceFile = token.sourceFile;

    // --------------------------------------------------------
    // Array type
    //
    // number[]
    // string[]
    // --------------------------------------------------------

    while (
        check(TokenType::LeftBracket) &&
        peek(1).type == TokenType::RightBracket
    ) {
        advance();
        consume(
            TokenType::RightBracket,
            "expected ']' in array type"
        );

        auto arrayType =
            std::make_unique<
                zplus::ast::Type
            >(
                zplus::ast::Type::Kind::Array,
                type->location
            );

        arrayType->elementType =
            std::move(type);

        type = std::move(arrayType);
    }

    // --------------------------------------------------------
    // Pointer type
    //
    // number*
    // Player*
    // --------------------------------------------------------

    if (match(TokenType::Star)) {

        auto pointerType =
            std::make_unique<
                zplus::ast::Type
            >(
                zplus::ast::Type::Kind::Pointer,
                type->location
            );

        pointerType->elementType =
            std::move(type);

        return pointerType;
    }

    return type;
}

// ============================================================
// Switch
// ============================================================
//
// switch value:
//     case 1:
//         ...
//     case 2:
//         ...
//     default:
//         ...
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseSwitchStatement() {

    const Token token =
        consume(
            TokenType::Switch,
            "expected 'switch'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::SwitchStatement
        >();

    setLocation(
        *statement,
        token
    );

    if (match(TokenType::LeftParen)) {

        statement->expression =
            parseExpression();

        consume(
            TokenType::RightParen,
            "expected ')' after switch expression"
        );

    } else {

        statement->expression =
            parseExpression();
    }

    if (!statement->expression) {
        return nullptr;
    }

    consume(
        TokenType::Colon,
        "expected ':' after switch expression"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::End)
    ) {

        if (check(TokenType::Case)) {

            auto caseNode =
                parseCase();

            if (caseNode) {

                auto* raw =
                    dynamic_cast<
                        zplus::ast::CaseStatement*
                    >(
                        caseNode.release()
                    );

                if (raw) {
                    statement->cases.emplace_back(
                        raw
                    );
                }
            }

        } else if (match(TokenType::Default)) {

            statement->hasDefault = true;

            consume(
                TokenType::Colon,
                "expected ':' after 'default'"
            );

            skipNewlines();

            statement->defaultBody =
                parseUntil(
                    TokenType::End
                );

        } else {

            error(
                "expected 'case', 'default', or 'end' in switch"
            );

            synchronize();
        }

        skipNewlines();
    }

    consume(
        TokenType::End,
        "expected 'end' after switch"
    );

    return statement;
}

// ============================================================
// Case
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseCase() {

    const Token token =
        consume(
            TokenType::Case,
            "expected 'case'"
        );

    auto statement =
        std::make_unique<
            zplus::ast::CaseStatement
        >();

    setLocation(
        *statement,
        token
    );

    statement->value =
        parseExpression();

    if (!statement->value) {
        return nullptr;
    }

    consume(
        TokenType::Colon,
        "expected ':' after case value"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::Case) &&
        !check(TokenType::Default) &&
        !check(TokenType::End)
    ) {

        auto child =
            parseStatement();

        if (child) {

            statement->body.push_back(
                std::move(child)
            );

        } else {

            synchronize();
        }

        skipNewlines();
    }

    return statement;
}

// ============================================================
// Struct
// ============================================================
//
// struct Player:
//     name: string
//     health: number
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseStruct() {

    const Token token =
        consume(
            TokenType::Struct,
            "expected 'struct'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::StructDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    if (!check(TokenType::Identifier)) {

        error(
            "expected struct name"
        );

        return nullptr;
    }

    declaration->name =
        current().value;

    advance();

    consume(
        TokenType::Colon,
        "expected ':' after struct name"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::End)
    ) {

        if (!check(TokenType::Identifier)) {

            error(
                "expected struct field name"
            );

            synchronize();
            continue;
        }

        const Token fieldToken =
            current();

        auto field =
            std::make_unique<
                zplus::ast::StructField
            >();

        setLocation(
            *field,
            fieldToken
        );

        field->name =
            current().value;

        advance();

        if (match(TokenType::Colon)) {

            field->type =
                parseType();

            if (!field->type) {
                synchronize();
                continue;
            }
        }

        if (match(TokenType::Equal)) {

            field->defaultValue =
                parseExpression();
        }

        declaration->fields.push_back(
            std::move(field)
        );

        consumeOptionalSemicolon();

        if (check(TokenType::Newline)) {
            advance();
        }

        skipNewlines();
    }

    consume(
        TokenType::End,
        "expected 'end' after struct"
    );

    return declaration;
}

// ============================================================
// Class
// ============================================================
//
// class Player:
//     health: number
//
//     fn heal(amount: number):
//         ...
//     end
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseClass() {

    const Token token =
        consume(
            TokenType::Class,
            "expected 'class'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::ClassDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    if (!check(TokenType::Identifier)) {

        error(
            "expected class name"
        );

        return nullptr;
    }

    declaration->name =
        current().value;

    advance();

    consume(
        TokenType::Colon,
        "expected ':' after class name"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::End)
    ) {

        // ----------------------------------------------------
        // Method
        // ----------------------------------------------------

        if (check(TokenType::Fn)) {

            auto method =
                parseFunction();

            if (method) {

                auto* function =
                    dynamic_cast<
                        zplus::ast::FunctionDeclaration*
                    >(
                        method.release()
                    );

                if (function) {

                    declaration->methods.emplace_back(
                        function
                    );
                }
            }

            skipNewlines();
            continue;
        }

        // ----------------------------------------------------
        // Field
        // ----------------------------------------------------

        if (check(TokenType::Identifier)) {

            const Token fieldToken =
                current();

            auto field =
                std::make_unique<
                    zplus::ast::ClassField
                >();

            setLocation(
                *field,
                fieldToken
            );

            field->name =
                current().value;

            advance();

            if (match(TokenType::Colon)) {

                field->type =
                    parseType();

                if (!field->type) {
                    synchronize();
                    continue;
                }
            }

            if (match(TokenType::Equal)) {

                field->initializer =
                    parseExpression();
            }

            declaration->fields.push_back(
                std::move(field)
            );

            consumeOptionalSemicolon();

            if (check(TokenType::Newline)) {
                advance();
            }

            skipNewlines();
            continue;
        }

        error(
            "expected class field or method"
        );

        synchronize();
        skipNewlines();
    }

    consume(
        TokenType::End,
        "expected 'end' after class"
    );

    return declaration;
}

// ============================================================
// Enum
// ============================================================
//
// enum Color:
//     Red
//     Green
//     Blue = 10
// end
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseEnum() {

    const Token token =
        consume(
            TokenType::Enum,
            "expected 'enum'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::EnumDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    if (!check(TokenType::Identifier)) {

        error(
            "expected enum name"
        );

        return nullptr;
    }

    declaration->name =
        current().value;

    advance();

    consume(
        TokenType::Colon,
        "expected ':' after enum name"
    );

    skipNewlines();

    while (
        !isAtEnd() &&
        !check(TokenType::End)
    ) {

        if (!check(TokenType::Identifier)) {

            error(
                "expected enum member"
            );

            synchronize();
            continue;
        }

        const Token memberToken =
            current();

        auto member =
            std::make_unique<
                zplus::ast::EnumValue
            >();

        setLocation(
            *member,
            memberToken
        );

        member->name =
            current().value;

        advance();

        if (match(TokenType::Equal)) {

            member->value =
                parseExpression();

            if (!member->value) {
                return nullptr;
            }
        }

        declaration->values.push_back(
            std::move(member)
        );

        if (match(TokenType::Comma)) {
            skipNewlines();
            continue;
        }

        if (check(TokenType::Semicolon)) {
            advance();
        }

        if (check(TokenType::Newline)) {
            advance();
        }

        skipNewlines();
    }

    consume(
        TokenType::End,
        "expected 'end' after enum"
    );

    return declaration;
}

// ============================================================
// Namespace
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseNamespace() {

    const Token token =
        consume(
            TokenType::Namespace,
            "expected 'namespace'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::NamespaceDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    if (!check(TokenType::Identifier)) {

        error(
            "expected namespace name"
        );

        return nullptr;
    }

    declaration->name =
        current().value;

    advance();

    consume(
        TokenType::Colon,
        "expected ':' after namespace name"
    );

    skipNewlines();

    declaration->body =
        parseUntil(
            TokenType::End
        );

    consume(
        TokenType::End,
        "expected 'end' after namespace"
    );

    return declaration;
}

// ============================================================
// Import
// ============================================================
//
// import math
// import "math"
// import math as m
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseImport() {

    const Token token =
        consume(
            TokenType::Import,
            "expected 'import'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::ImportDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    if (
        check(TokenType::String) ||
        check(TokenType::Identifier)
    ) {

        declaration->module =
            current().value;

        advance();

    } else {

        error(
            "expected module name after 'import'"
        );

        return nullptr;
    }

    if (match(TokenType::As)) {

        if (!check(TokenType::Identifier)) {

            error(
                "expected alias after 'as'"
            );

            return nullptr;
        }

        declaration->alias =
            current().value;

        advance();
    }

    consumeOptionalSemicolon();

    if (check(TokenType::Newline)) {
        advance();
    }

    return declaration;
}

// ============================================================
// Use module
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseUse() {
    const Token token =
        consume(TokenType::Use, "expected 'use'");

    auto declaration =
        std::make_unique<zplus::ast::UseDeclaration>();
    setLocation(*declaration, token);

    if (match(TokenType::String)) {
        declaration->path = previous().value;
    } else {
        while (check(TokenType::Identifier) ||
               check(TokenType::Number) ||
               check(TokenType::Dot) ||
               check(TokenType::Slash) ||
               check(TokenType::Backslash) ||
               check(TokenType::Minus)) {
            declaration->path += current().value;
            advance();
        }
    }

    if (declaration->path.empty()) {
        error("expected a module path after 'use'");
        return nullptr;
    }

    if (match(TokenType::As)) {
        if (!check(TokenType::Identifier)) {
            error("expected module alias after 'as'");
            return nullptr;
        }
        declaration->alias = current().value;
        advance();
    }

    consumeOptionalSemicolon();
    return declaration;
}

std::unique_ptr<zplus::ast::Statement>
Parser::parsePublicDeclaration() {
    const Token token = consume(TokenType::Pub, "expected 'pub'");
    auto declaration = parseStatement();
    if (!declaration) return nullptr;

    if (auto* function =
            dynamic_cast<zplus::ast::FunctionDeclaration*>(declaration.get())) {
        function->isPublic = true;
    } else if (auto* variable =
                   dynamic_cast<zplus::ast::VariableDeclaration*>(declaration.get())) {
        variable->isPublic = true;
    } else if (auto* structure =
                   dynamic_cast<zplus::ast::StructDeclaration*>(declaration.get())) {
        structure->isPublic = true;
    } else if (auto* klass =
                   dynamic_cast<zplus::ast::ClassDeclaration*>(declaration.get())) {
        klass->isPublic = true;
    } else if (auto* enumeration =
                   dynamic_cast<zplus::ast::EnumDeclaration*>(declaration.get())) {
        enumeration->isPublic = true;
    } else {
        errorAt(token, "'pub' must precede a declaration");
        return nullptr;
    }
    return declaration;
}

// ============================================================
// Export
// ============================================================

std::unique_ptr<zplus::ast::Statement>
Parser::parseExport() {

    const Token token =
        consume(
            TokenType::Export,
            "expected 'export'"
        );

    auto declaration =
        std::make_unique<
            zplus::ast::ExportDeclaration
        >();

    setLocation(
        *declaration,
        token
    );

    declaration->declaration =
        parseStatement();

    if (!declaration->declaration) {
        return nullptr;
    }

    return declaration;
}

// ============================================================
// Helpers
// ============================================================

bool Parser::isStatementStart() const {

    switch (current().type) {

        case TokenType::Start:

        case TokenType::Let:
        case TokenType::Const:

        case TokenType::If:
        case TokenType::While:
        case TokenType::For:
        case TokenType::Repeat:

        case TokenType::Fn:

        case TokenType::Return:
        case TokenType::Try:
        case TokenType::Throw:
        case TokenType::Defer:
        case TokenType::Break:
        case TokenType::Continue:

        case TokenType::Switch:

        case TokenType::Struct:
        case TokenType::Class:
        case TokenType::Enum:

        case TokenType::Namespace:
        case TokenType::Import:
        case TokenType::Use:

        case TokenType::Pub:

        case TokenType::Export:

        case TokenType::Identifier:
        case TokenType::Number:
        case TokenType::String:
        case TokenType::Character:

        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:

        case TokenType::LeftParen:
        case TokenType::LeftBracket:

        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Exclamation:
        case TokenType::Not:
        case TokenType::BitNot:
        case TokenType::BitAnd:
        case TokenType::Star:
        case TokenType::PlusPlus:
        case TokenType::MinusMinus:

            return true;

        default:
            return false;
    }
}

bool Parser::isBinaryOperator(
    TokenType type
) const {

    switch (type) {

        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:

        case TokenType::EqualEqual:
        case TokenType::NotEqual:

        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:

        case TokenType::And:
        case TokenType::AndAnd:

        case TokenType::Or:
        case TokenType::OrOr:

        case TokenType::BitAnd:
        case TokenType::BitOr:
        case TokenType::BitXor:

        case TokenType::LeftShift:
        case TokenType::RightShift:

        case TokenType::Is:

            return true;

        default:
            return false;
    }
}

// ============================================================
// Synchronization
// ============================================================

void Parser::synchronize() {

    if (isAtEnd()) {
        return;
    }

    while (!isAtEnd()) {

        if (check(TokenType::Newline)) {

            advance();
            return;
        }

        if (
            check(TokenType::End) ||
            check(TokenType::Else) ||
            check(TokenType::Stop)
        ) {
            return;
        }

        if (
            current_ > 0 &&
            isStatementStart()
        ) {
            return;
        }

        advance();
    }
}

} // namespace zplus::parser