#include "Parser.hpp"

#include <functional>
#include <initializer_list>
#include <unordered_map>
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
        type == TokenType::Stop ||
        type == TokenType::RightBrace;
}

bool isBlockTerminator(TokenType type) {

    return
        type == TokenType::End ||
        type == TokenType::Else ||
        type == TokenType::Stop ||
        type == TokenType::EndOfFile;
}

std::string blockName(TokenType type) {
    switch (type) {
        case TokenType::Fn: return "fn";
        case TokenType::If: return "if";
        case TokenType::While: return "while";
        case TokenType::For: return "for";
        case TokenType::Repeat: return "repeat";
        case TokenType::Switch: return "switch";
        case TokenType::Stop: return "stop";
        case TokenType::Try: return "try";
        case TokenType::Defer: return "defer";
        case TokenType::Struct: return "struct";
        case TokenType::Class: return "class";
        case TokenType::Enum: return "enum";
        case TokenType::Namespace: return "namespace";
        default: return zplus::tokenTypeName(type);
    }
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

    std::size_t startCount = 0;
    std::function<void(const std::vector<zplus::ast::Stmt>&, bool, const std::string&)>
        validateScope;
    std::function<void(const zplus::ast::Statement&, bool, const std::string&)>
        validateStatement;

    std::function<std::pair<std::string, std::string>(
        const zplus::ast::Statement&)> declarationName;
    declarationName = [&](const zplus::ast::Statement& statement)
        -> std::pair<std::string, std::string> {
        if (const auto* node = dynamic_cast<const zplus::ast::FunctionDeclaration*>(&statement)) {
            return {"fn", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::NamespaceDeclaration*>(&statement)) {
            return {"namespace", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::ClassDeclaration*>(&statement)) {
            return {"class", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::EnumDeclaration*>(&statement)) {
            return {"enum", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::StructDeclaration*>(&statement)) {
            return {"struct", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::VariableDeclaration*>(&statement)) {
            return {"variable", node->name};
        }
        if (const auto* node = dynamic_cast<const zplus::ast::ExportDeclaration*>(&statement)) {
            return node->declaration ? declarationName(*node->declaration)
                                     : std::pair<std::string, std::string>{};
        }
        return {};
    };

    const auto isConstrainedDeclaration = [](const zplus::ast::Statement& statement) {
        if (const auto* node = dynamic_cast<const zplus::ast::ExportDeclaration*>(&statement)) {
            return node->declaration &&
                (dynamic_cast<const zplus::ast::FunctionDeclaration*>(node->declaration.get()) ||
                 dynamic_cast<const zplus::ast::NamespaceDeclaration*>(node->declaration.get()) ||
                 dynamic_cast<const zplus::ast::ClassDeclaration*>(node->declaration.get()) ||
                 dynamic_cast<const zplus::ast::EnumDeclaration*>(node->declaration.get()) ||
                 dynamic_cast<const zplus::ast::StructDeclaration*>(node->declaration.get()));
        }
        return dynamic_cast<const zplus::ast::FunctionDeclaration*>(&statement) ||
            dynamic_cast<const zplus::ast::NamespaceDeclaration*>(&statement) ||
            dynamic_cast<const zplus::ast::ClassDeclaration*>(&statement) ||
            dynamic_cast<const zplus::ast::EnumDeclaration*>(&statement) ||
            dynamic_cast<const zplus::ast::StructDeclaration*>(&statement);
    };

    validateScope = [&](const std::vector<zplus::ast::Stmt>& statements,
                        bool declarationsAllowed,
                        const std::string& scopeName) {
        std::unordered_map<std::string, zplus::ast::Location> names;
        for (const auto& statement : statements) {
            if (!statement) continue;

            if (isConstrainedDeclaration(*statement)) {
                if (!declarationsAllowed) {
                    const auto [kind, name] = declarationName(*statement);
                    (void)name;
                    errorAt(Token{TokenType::Unknown, {}, statement->location.line,
                                  statement->location.column, statement->location.sourceFile},
                            "'" + kind + "' declarations may only appear directly inside "
                            "the program's start block or a namespace");
                    continue;
                }

                const auto [kind, name] = declarationName(*statement);
                if (!name.empty() && names.contains(name)) {
                    errorAt(Token{TokenType::Unknown, {}, statement->location.line,
                                  statement->location.column, statement->location.sourceFile},
                            "duplicate declaration '" + name + "' in " + scopeName);
                } else if (!name.empty()) {
                    names.emplace(name, statement->location);
                }
                validateStatement(*statement, true, scopeName);
                continue;
            }

            if (const auto* variable =
                    dynamic_cast<const zplus::ast::VariableDeclaration*>(statement.get())) {
                if (!variable->name.empty() && names.contains(variable->name)) {
                    errorAt(Token{TokenType::Unknown, {}, variable->location.line,
                                  variable->location.column, variable->location.sourceFile},
                            "duplicate declaration '" + variable->name + "' in " + scopeName);
                } else if (!variable->name.empty()) {
                    names.emplace(variable->name, variable->location);
                }
            }

            validateStatement(*statement, false, scopeName);
        }
    };

    validateStatement = [&](const zplus::ast::Statement& statement,
                            bool declarationAllowed,
                            const std::string& scopeName) {
        if (const auto* start =
                dynamic_cast<const zplus::ast::StartStatement*>(&statement)) {
            if (!declarationAllowed) {
                errorAt(Token{TokenType::Unknown, {}, start->location.line,
                              start->location.column, start->location.sourceFile},
                        "a program may contain only one top-level start block");
            } else {
                validateScope(start->body, true, "start block");
            }
            return;
        }

        if (const auto* node =
                dynamic_cast<const zplus::ast::FunctionDeclaration*>(&statement)) {
            validateScope(node->body, false, "function body");
            return;
        }
        if (const auto* node =
                dynamic_cast<const zplus::ast::NamespaceDeclaration*>(&statement)) {
            validateScope(node->body, true, "namespace '" + node->name + "'");
            return;
        }
        if (const auto* node =
                dynamic_cast<const zplus::ast::ClassDeclaration*>(&statement)) {
            for (const auto& method : node->methods) {
                if (method) validateScope(method->body, false, "method body");
            }
            return;
        }
        if (const auto* node =
                dynamic_cast<const zplus::ast::ExportDeclaration*>(&statement)) {
            if (node->declaration) {
                validateStatement(*node->declaration, declarationAllowed, scopeName);
            }
            return;
        }

        const auto inspectBody = [&](const std::vector<zplus::ast::Stmt>& body) {
            validateScope(body, false, scopeName);
        };
        if (const auto* blockNode =
                dynamic_cast<const zplus::ast::BlockStatement*>(&statement)) {
            inspectBody(blockNode->statements);
        } else if (const auto* ifNode =
                       dynamic_cast<const zplus::ast::IfStatement*>(&statement)) {
            inspectBody(ifNode->thenBody);
            inspectBody(ifNode->elseBody);
        } else if (const auto* whileNode =
                       dynamic_cast<const zplus::ast::WhileStatement*>(&statement)) {
            inspectBody(whileNode->body);
        } else if (const auto* forNode =
                       dynamic_cast<const zplus::ast::ForStatement*>(&statement)) {
            if (forNode->initializer) {
                validateStatement(*forNode->initializer, false, scopeName);
            }
            inspectBody(forNode->body);
        } else if (const auto* repeatNode =
                       dynamic_cast<const zplus::ast::RepeatStatement*>(&statement)) {
            inspectBody(repeatNode->body);
        } else if (const auto* switchNode =
                       dynamic_cast<const zplus::ast::SwitchStatement*>(&statement)) {
            for (const auto& branch : switchNode->cases) {
                if (branch) inspectBody(branch->body);
            }
            inspectBody(switchNode->defaultBody);
        } else if (const auto* caseNode =
                       dynamic_cast<const zplus::ast::CaseStatement*>(&statement)) {
            inspectBody(caseNode->body);
        } else if (const auto* tryNode =
                       dynamic_cast<const zplus::ast::TryStatement*>(&statement)) {
            inspectBody(tryNode->tryBody);
            inspectBody(tryNode->catchBody);
        } else if (const auto* deferNode =
                       dynamic_cast<const zplus::ast::DeferStatement*>(&statement)) {
            inspectBody(deferNode->body);
        } else if (dynamic_cast<const zplus::ast::UseDeclaration*>(&statement) ||
                   dynamic_cast<const zplus::ast::ImportDeclaration*>(&statement)) {
            if (scopeName != "module scope") {
                errorAt(Token{TokenType::Unknown, {}, statement.location.line,
                              statement.location.column, statement.location.sourceFile},
                        "module directives must remain outside the program's start block");
            }
        } else if (dynamic_cast<const zplus::ast::StartStatement*>(&statement)) {
            errorAt(Token{TokenType::Unknown, {}, statement.location.line,
                          statement.location.column, statement.location.sourceFile},
                    "a nested start block is not allowed");
        }
    };

    for (const auto& statement : program->statements) {
        if (!statement) continue;
        if (const auto* start =
                dynamic_cast<const zplus::ast::StartStatement*>(statement.get())) {
            ++startCount;
            if (startCount > 1) {
                errorAt(Token{TokenType::Unknown, {}, start->location.line,
                              start->location.column, start->location.sourceFile},
                        "a source file must contain exactly one top-level start block");
            }
            validateScope(start->body, true, "start block");
        } else if (!dynamic_cast<const zplus::ast::UseDeclaration*>(statement.get()) &&
                   !dynamic_cast<const zplus::ast::ImportDeclaration*>(statement.get())) {
            const auto [kind, name] = declarationName(*statement);
            const std::string message = isConstrainedDeclaration(*statement)
                ? "'" + kind + "' declarations must be inside the program's start block"
                : "top-level statements must be inside the program's start block";
            errorAt(Token{TokenType::Unknown, {}, statement->location.line,
                          statement->location.column, statement->location.sourceFile},
                    message);
            (void)name;
        }
    }

    if (startCount == 0) {
        errorAt(tokens_.empty() ? Token{} : tokens_.front(),
                "a source file must contain one top-level start block");
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
            if (current().type != TokenType::Newline &&
                current().type != TokenType::EndOfFile) {
                errorAt(current(), "unexpected block terminator 'end " +
                    blockName(current().type) + "'");
                advance();
            }
            return nullptr;

        case TokenType::RightBrace:
            error("unexpected closing brace '}'");
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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->body = parseBracedBody(opening, "start");
        consumeExplicitTerminator(TokenType::Stop, "start", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(TokenType::Colon, "expected '{' after 'start' (or ':' for legacy syntax)");
    skipNewlines();
    blockSyntaxStack_.push_back(BlockSyntax::Legacy);
    while (!isAtEnd() && !check(TokenType::Stop)) {
        if (check(TokenType::End) || check(TokenType::RightBrace)) {
            error("unexpected block terminator inside legacy 'start:'; expected 'stop'");
            advance();
            skipNewlines();
            continue;
        }
        auto child = parseStatement();
        if (child) statement->body.push_back(std::move(child));
        else synchronize();
        skipNewlines();
    }
    blockSyntaxStack_.pop_back();
    consume(TokenType::Stop, "expected 'stop' to close 'start:'");

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->thenBody = parseBracedBody(opening, "if", true);
        auto* currentIf = statement.get();

        while (match(TokenType::Else)) {
            currentIf->hasElse = true;
            if (match(TokenType::If)) {
                const Token elseIfToken = previous();
                auto elseIf = std::make_unique<zplus::ast::IfStatement>();
                setLocation(*elseIf, elseIfToken);
                elseIf->condition = parseCondition();
                if (!elseIf->condition) return nullptr;
                if (!match(TokenType::LeftBrace)) {
                    error("expected '{' after else-if condition");
                    return nullptr;
                }
                const Token elseIfOpening = previous();
                elseIf->thenBody = parseBracedBody(elseIfOpening, "if", true);
                currentIf->elseBody.push_back(std::move(elseIf));
                currentIf = static_cast<zplus::ast::IfStatement*>(
                    currentIf->elseBody.back().get());
                continue;
            }

            if (!match(TokenType::LeftBrace)) {
                error("expected '{' after 'else'");
                return nullptr;
            }
            const Token elseOpening = previous();
            currentIf->elseBody = parseBracedBody(elseOpening, "else branch");
            break;
        }

        consumeExplicitTerminator(TokenType::If, "if", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(TokenType::Colon,
        "expected '{' after if condition (or ':' for legacy syntax)");
    skipNewlines();
    statement->thenBody = parseUntil(TokenType::Else);
    if (match(TokenType::Else)) {
        statement->hasElse = true;
        consume(TokenType::Colon, "expected ':' after 'else'");
        skipNewlines();
        statement->elseBody = parseUntil(TokenType::End);
    }
    consume(TokenType::End, "expected 'end' after if");

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->body = parseBracedBody(opening, "while");
        consumeExplicitTerminator(TokenType::While, "while", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(TokenType::Colon,
        "expected '{' after while condition (or ':' for legacy syntax)");
    skipNewlines();
    statement->body = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after while");

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->body = parseBracedBody(opening, "repeat");
        consumeExplicitTerminator(TokenType::Repeat, "repeat", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(TokenType::Colon,
        "expected '{' after repeat count (or ':' for legacy syntax)");
    skipNewlines();
    statement->body = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after repeat");

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

    if (!check(TokenType::LeftParen)) {
        if (!check(TokenType::Identifier)) {
            error("expected loop variable or '(' after 'for'");
            return nullptr;
        }

        const Token variableToken = advance();
        if (!check(TokenType::Identifier) || current().value != "in") {
            error("expected 'in' after for-loop variable");
            return nullptr;
        }
        advance();

        auto first = parseExpression();
        if (!first) {
            return nullptr;
        }

        auto makeIdentifier = [](const std::string& name, const Token& source) {
            auto identifier = std::make_unique<zplus::ast::Identifier>(name);
            setLocation(*identifier, source);
            return identifier;
        };
        auto makeInternalName = [this](const char* prefix) {
            while (true) {
                const std::string candidate =
                    std::string(prefix) + std::to_string(syntheticNameCounter_++);
                bool exists = false;
                for (const Token& existing : tokens_) {
                    if (existing.type == TokenType::Identifier &&
                        existing.value == candidate) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) return candidate;
            }
        };
        auto makeDeclaration = [](const std::string& name,
                                  std::unique_ptr<zplus::ast::Expression> initializer,
                                  const Token& source) {
            auto declaration =
                std::make_unique<zplus::ast::VariableDeclaration>(name);
            setLocation(*declaration, source);
            declaration->initializer = std::move(initializer);
            return declaration;
        };
        auto makeNumber = [](const std::string& value, const Token& source) {
            auto literal = std::make_unique<zplus::ast::Literal>(
                zplus::ast::Literal::Kind::Number, value);
            literal->isNumber = true;
            setLocation(*literal, source);
            return literal;
        };
        auto makeIncrement = [&](const std::string& name) {
            return std::make_unique<zplus::ast::UnaryExpression>(
                TokenType::PlusPlus, makeIdentifier(name, variableToken), false,
                zplus::ast::Location(variableToken.line, variableToken.column,
                                     variableToken.sourceFile));
        };

        auto wrapper = std::make_unique<zplus::ast::BlockStatement>();
        setLocation(*wrapper, token);
        auto loop = std::make_unique<zplus::ast::ForStatement>();
        setLocation(*loop, token);

        if (match(TokenType::Range)) {
            auto last = parseExpression();
            if (!last) return nullptr;

            const std::string firstName = makeInternalName("__zp_range_start_");
            const std::string lastName = makeInternalName("__zp_range_end_");
            wrapper->statements.push_back(
                makeDeclaration(firstName, std::move(first), variableToken));
            wrapper->statements.push_back(
                makeDeclaration(lastName, std::move(last), variableToken));

            loop->initializer = makeDeclaration(
                variableToken.value, makeIdentifier(firstName, variableToken),
                variableToken);
            loop->condition = std::make_unique<zplus::ast::BinaryExpression>(
                makeIdentifier(variableToken.value, variableToken),
                TokenType::Less,
                makeIdentifier(lastName, variableToken),
                zplus::ast::Location(variableToken.line, variableToken.column,
                                     variableToken.sourceFile));
            loop->increment = makeIncrement(variableToken.value);
        } else {
            const std::string iterableName = makeInternalName("__zp_iterable_");
            const std::string indexName = makeInternalName("__zp_index_");
            wrapper->statements.push_back(
                makeDeclaration(iterableName, std::move(first), variableToken));

            loop->initializer = makeDeclaration(
                indexName, makeNumber("0", variableToken), variableToken);
            auto lengthCallee = std::make_unique<zplus::ast::Identifier>("length");
            setLocation(*lengthCallee, variableToken);
            auto lengthCall = std::make_unique<zplus::ast::CallExpression>(
                std::move(lengthCallee));
            setLocation(*lengthCall, variableToken);
            lengthCall->arguments.push_back(
                makeIdentifier(iterableName, variableToken));
            loop->condition = std::make_unique<zplus::ast::BinaryExpression>(
                makeIdentifier(indexName, variableToken), TokenType::Less,
                std::move(lengthCall),
                zplus::ast::Location(variableToken.line, variableToken.column,
                                     variableToken.sourceFile));
            loop->increment = std::make_unique<zplus::ast::UnaryExpression>(
                TokenType::PlusPlus, makeIdentifier(indexName, variableToken), false,
                zplus::ast::Location(variableToken.line, variableToken.column,
                                     variableToken.sourceFile));

            loop->body.push_back(makeDeclaration(
                variableToken.value,
                std::make_unique<zplus::ast::IndexExpression>(
                    makeIdentifier(iterableName, variableToken),
                    makeIdentifier(indexName, variableToken),
                    zplus::ast::Location(variableToken.line, variableToken.column,
                                         variableToken.sourceFile)),
                variableToken));
        }

        if (!match(TokenType::LeftBrace)) {
            error("expected '{' after for-in header");
            return nullptr;
        }
        const Token opening = previous();
        auto body = parseBracedBody(opening, "for");
        consumeExplicitTerminator(TokenType::For, "for", opening);
        for (auto& bodyStatement : body) {
            loop->body.push_back(std::move(bodyStatement));
        }
        wrapper->statements.push_back(std::move(loop));
        return wrapper;
    }

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->body = parseBracedBody(opening, "for");
        consumeExplicitTerminator(TokenType::For, "for", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(TokenType::Colon,
        "expected '{' after for header (or ':' for legacy syntax)");
    skipNewlines();
    statement->body = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after for");

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

    function->genericParameters = parseGenericParameters();
    function->parameters =
        parseParameters();

    // --------------------------------------------------------
    // Return type
    //
    // -> number
    // --------------------------------------------------------

    bool hasColonReturnType = false;
    if (match(TokenType::Arrow)) {

        function->returnType =
            parseType();

        if (!function->returnType) {
            return nullptr;
        }
    } else if (check(TokenType::Colon) &&
               (peek(1).type == TokenType::NumberType ||
                peek(1).type == TokenType::StringType ||
                peek(1).type == TokenType::CharacterType ||
                peek(1).type == TokenType::BooleanType ||
                peek(1).type == TokenType::VoidType ||
                peek(1).type == TokenType::Identifier)) {
        advance();
        function->returnType = parseType();
        if (!function->returnType) return nullptr;
        hasColonReturnType = true;
    }

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        function->body = parseBracedBody(opening, "fn");
        consumeExplicitTerminator(TokenType::Fn, "fn", opening);
        return function;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    if (hasColonReturnType) {
        error("a function return type written with ':' must be followed by '{'");
    }
    consume(TokenType::Colon,
        "expected '{' after function signature (or ':' for legacy syntax)");
    skipNewlines();
    function->body = parseUntil(TokenType::End);
    consume(TokenType::End, "expected 'end' after function");

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->tryBody = parseBracedBody(opening, "try", false, true);
        if (!match(TokenType::Catch)) {
            error("expected 'catch' after closing brace of 'try' block opened at line " +
                std::to_string(opening.line));
            return statement;
        }
        const bool parenthesizedCatch = match(TokenType::LeftParen);
        if (!check(TokenType::Identifier)) {
            error("expected error variable name in 'catch'");
            return nullptr;
        }
        statement->catchName = advance().value;
        if (parenthesizedCatch) {
            consume(TokenType::RightParen, "expected ')' after catch variable");
        }
        if (!match(TokenType::LeftBrace)) {
            error("expected '{' after catch clause");
            return nullptr;
        }
        const Token catchOpening = previous();
        statement->catchBody = parseBracedBody(catchOpening, "catch");
        consumeExplicitTerminator(TokenType::Try, "try", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        statement->body = parseBracedBody(opening, "defer");
        consumeExplicitTerminator(TokenType::Defer, "defer", opening);
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
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

    blockSyntaxStack_.push_back(BlockSyntax::Legacy);
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

    blockSyntaxStack_.pop_back();
    return statements;
}

void Parser::validateBlockSyntax(
    BlockSyntax syntax,
    const Token& opening
) {
    if (blockSyntaxStack_.empty() || blockSyntaxStack_.back() == syntax) {
        return;
    }

    const std::string syntaxName = syntax == BlockSyntax::Braced
        ? "brace-based"
        : "legacy colon-based";
    const std::string enclosingName = blockSyntaxStack_.back() == BlockSyntax::Braced
        ? "brace-based"
        : "legacy colon-based";
    errorAt(opening, "cannot mix " + syntaxName +
        " block syntax inside a " + enclosingName + " block");
}

std::vector<std::unique_ptr<zplus::ast::Statement>>
Parser::parseBracedBody(
    const Token& opening,
    const std::string& name,
    bool stopAtElse,
    bool stopAtCatch
) {
    std::vector<std::unique_ptr<zplus::ast::Statement>> statements;
    validateBlockSyntax(BlockSyntax::Braced, opening);
    blockSyntaxStack_.push_back(BlockSyntax::Braced);
    skipNewlines();

    while (!isAtEnd() && !check(TokenType::RightBrace)) {
        if ((stopAtElse && check(TokenType::Else)) ||
            (stopAtCatch && check(TokenType::Catch))) {
            break;
        }
        if (check(TokenType::End) || check(TokenType::Stop) ||
            check(TokenType::Else) || check(TokenType::Catch)) {
            errorAt(current(), "expected '}' to close '" + name +
                "' block opened at line " + std::to_string(opening.line) +
                " before " + describeToken(current()));
            break;
        }

        auto statement = parseStatement();
        if (statement) {
            statements.push_back(std::move(statement));
        } else {
            synchronize();
        }
        skipNewlines();
    }

    if (check(TokenType::RightBrace)) {
        advance();
    } else {
        errorAt(current(), "expected '}' to close '" + name +
            "' block opened at line " + std::to_string(opening.line) +
            "; found " + describeToken(current()));
    }
    blockSyntaxStack_.pop_back();
    return statements;
}

void Parser::consumeExplicitTerminator(
    TokenType expected,
    const std::string& name,
    const Token& opening
) {
    const std::string expectedText = expected == TokenType::Stop
        ? "'stop'"
        : "'end " + name + "'";
    if (expected == TokenType::Stop) {
        if (match(TokenType::Stop)) return;
        error("expected " + expectedText + " to close '" + name +
            "' block opened at line " + std::to_string(opening.line) +
            "; found " + describeToken(current()));
        return;
    }

    if (!match(TokenType::End)) {
        error("expected " + expectedText + " after closing brace of '" +
            name + "' block opened at line " + std::to_string(opening.line) +
            "; found " + describeToken(current()));
        return;
    }

    if (match(expected)) return;

    if (current().type == TokenType::Fn || current().type == TokenType::If ||
        current().type == TokenType::While || current().type == TokenType::For ||
        current().type == TokenType::Repeat || current().type == TokenType::Switch ||
        current().type == TokenType::Try || current().type == TokenType::Defer ||
        current().type == TokenType::Struct || current().type == TokenType::Class ||
        current().type == TokenType::Enum || current().type == TokenType::Namespace) {
        const Token actual = advance();
        errorAt(actual, "block opened as '" + name + "' at line " +
            std::to_string(opening.line) + " was closed with 'end " +
            blockName(actual.type) + "'; expected " + expectedText);
        return;
    }

    error("expected " + expectedText + " after closing brace of '" +
        name + "' block opened at line " + std::to_string(opening.line) +
        "; found " + describeToken(current()));
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

        if (check(TokenType::Less) && looksLikeGenericCall()) {
            std::vector<std::unique_ptr<zplus::ast::Type>> typeArguments =
                parseTypeArguments();
            if (!check(TokenType::LeftParen)) {
                error("expected '(' after generic type arguments");
                return nullptr;
            }
            auto call = parseCall(std::move(expression));
            if (auto* callExpression =
                    dynamic_cast<zplus::ast::CallExpression*>(call.get())) {
                callExpression->explicitTypeArguments =
                    std::move(typeArguments);
            }
            expression = std::move(call);
            continue;
        }

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

    if (match(TokenType::This)) {
        auto identifier =
            std::make_unique<zplus::ast::Identifier>();
        setLocation(*identifier, token);
        identifier->name = "this";
        return identifier;
    }

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
            if (match(TokenType::Less)) {
                do {
                    auto argument = parseType();
                    if (!argument) return nullptr;
                    type->arguments.push_back(std::move(argument));
                } while (match(TokenType::Comma));
                consumeGenericGreater("expected '>' after generic type arguments");
            }
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

std::vector<zplus::ast::GenericParameter>
Parser::parseGenericParameters() {
    std::vector<zplus::ast::GenericParameter> parameters;
    if (!match(TokenType::Less)) return parameters;

    do {
        if (!check(TokenType::Identifier)) {
            error("expected a type parameter name");
            return parameters;
        }
        zplus::ast::GenericParameter parameter;
        parameter.name = current().value;
        parameter.location = {
            current().line,
            current().column,
            current().sourceFile
        };
        advance();
        if (match(TokenType::Colon)) {
            if (check(TokenType::NumberType)) {
                parameter.constraint = "number";
                advance();
            } else if (check(TokenType::Identifier)) {
                parameter.constraint = current().value;
                advance();
            } else {
                error("expected a generic constraint after ':'");
                return parameters;
            }
        }
        parameters.push_back(std::move(parameter));
    } while (match(TokenType::Comma));

    consumeGenericGreater("expected '>' after generic parameter list");
    return parameters;
}

std::vector<std::unique_ptr<zplus::ast::Type>>
Parser::parseTypeArguments() {
    std::vector<std::unique_ptr<zplus::ast::Type>> arguments;
    consume(TokenType::Less, "expected '<'");
    do {
        auto argument = parseType();
        if (!argument) return arguments;
        arguments.push_back(std::move(argument));
    } while (match(TokenType::Comma));
    consumeGenericGreater("expected '>' after generic type arguments");
    return arguments;
}

const zplus::Token& Parser::consumeGenericGreater(
    const std::string& message
) {
    if (check(TokenType::Greater)) return advance();
    if (check(TokenType::RightShift)) {
        zplus::Token second = current();
        second.type = TokenType::Greater;
        second.value = ">";
        ++second.column;
        tokens_[current_].type = TokenType::Greater;
        tokens_[current_].value = ">";
        tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(current_ + 1),
                       std::move(second));
        return advance();
    }
    return consume(TokenType::Greater, message);
}

bool Parser::looksLikeGenericCall() const {
    if (!check(TokenType::Less)) return false;
    std::size_t depth = 0;
    for (std::size_t index = current_; index < tokens_.size(); ++index) {
        const TokenType token = tokens_[index].type;
        if (token == TokenType::Less) {
            ++depth;
        } else if (token == TokenType::Greater) {
            if (depth == 0) return false;
            if (--depth == 0) {
                return index + 1 < tokens_.size() &&
                    tokens_[index + 1].type == TokenType::LeftParen;
            }
        } else if (token == TokenType::RightShift) {
            if (depth < 2) return false;
            depth -= 2;
            if (depth == 0) {
                return index + 1 < tokens_.size() &&
                    tokens_[index + 1].type == TokenType::LeftParen;
            }
        } else if (token == TokenType::EndOfFile ||
                   token == TokenType::Newline ||
                   token == TokenType::Semicolon) {
            return false;
        }
    }
    return false;
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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        validateBlockSyntax(BlockSyntax::Braced, opening);
        blockSyntaxStack_.push_back(BlockSyntax::Braced);
        skipNewlines();
        while (!isAtEnd() && !check(TokenType::RightBrace)) {
            if (match(TokenType::Case)) {
                const Token caseToken = previous();
                auto caseNode = std::make_unique<zplus::ast::CaseStatement>();
                setLocation(*caseNode, caseToken);
                caseNode->value = parseExpression();
                if (!caseNode->value) {
                    synchronize();
                    continue;
                }
                if (!match(TokenType::LeftBrace)) {
                    error("expected '{' after switch case value");
                    synchronize();
                    continue;
                }
                const Token caseOpening = previous();
                caseNode->body = parseBracedBody(caseOpening, "case");
                statement->cases.push_back(std::move(caseNode));
            } else if (match(TokenType::Default)) {
                if (statement->hasDefault) {
                    errorAt(previous(), "duplicate default clause in switch");
                }
                statement->hasDefault = true;
                if (!match(TokenType::LeftBrace)) {
                    error("expected '{' after 'default'");
                    synchronize();
                    continue;
                }
                const Token defaultOpening = previous();
                statement->defaultBody = parseBracedBody(defaultOpening, "default");
            } else {
                error("expected 'case', 'default', or '}' in switch");
                synchronize();
            }
            skipNewlines();
        }
        if (match(TokenType::RightBrace)) {
            blockSyntaxStack_.pop_back();
            consumeExplicitTerminator(TokenType::Switch, "switch", opening);
        } else {
            blockSyntaxStack_.pop_back();
            errorAt(current(), "expected '}' to close 'switch' block opened at line " +
                std::to_string(opening.line) + "; found " + describeToken(current()));
            consumeExplicitTerminator(TokenType::Switch, "switch", opening);
        }
        return statement;
    }

    validateBlockSyntax(BlockSyntax::Legacy, token);
    consume(
        TokenType::Colon,
        "expected '{' after switch expression (or ':' for legacy syntax)"
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

    declaration->genericParameters = parseGenericParameters();
    const bool braced = match(TokenType::LeftBrace);
    const Token opening = braced ? previous() : token;
    if (braced) {
        validateBlockSyntax(BlockSyntax::Braced, opening);
        blockSyntaxStack_.push_back(BlockSyntax::Braced);
    } else {
        validateBlockSyntax(BlockSyntax::Legacy, token);
        consume(TokenType::Colon, "expected '{' after struct name");
    }

    skipNewlines();

    while (
        !isAtEnd() &&
        !(braced ? check(TokenType::RightBrace) : check(TokenType::End))
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

    if (braced) {
        if (match(TokenType::RightBrace)) {
            blockSyntaxStack_.pop_back();
            consumeExplicitTerminator(TokenType::Struct, "struct", opening);
        } else {
            blockSyntaxStack_.pop_back();
            errorAt(current(), "expected '}' to close 'struct' block opened at line " +
                std::to_string(opening.line) + "; found " + describeToken(current()));
            consumeExplicitTerminator(TokenType::Struct, "struct", opening);
        }
    } else {
        consume(TokenType::End, "expected 'end' after struct");
    }

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

    declaration->genericParameters = parseGenericParameters();
    const bool braced = match(TokenType::LeftBrace);
    const Token opening = braced ? previous() : token;
    if (braced) {
        validateBlockSyntax(BlockSyntax::Braced, opening);
        blockSyntaxStack_.push_back(BlockSyntax::Braced);
    } else {
        validateBlockSyntax(BlockSyntax::Legacy, token);
        consume(TokenType::Colon, "expected '{' after class name");
    }

    skipNewlines();

    while (
        !isAtEnd() &&
        !(braced ? check(TokenType::RightBrace) : check(TokenType::End))
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
                    function->ownerClass = declaration->name;
                    function->internalName =
                        declaration->name + "::" + function->name;

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

    if (braced) {
        if (match(TokenType::RightBrace)) {
            blockSyntaxStack_.pop_back();
            consumeExplicitTerminator(TokenType::Class, "class", opening);
        } else {
            blockSyntaxStack_.pop_back();
            errorAt(current(), "expected '}' to close 'class' block opened at line " +
                std::to_string(opening.line) + "; found " + describeToken(current()));
            consumeExplicitTerminator(TokenType::Class, "class", opening);
        }
    } else {
        consume(TokenType::End, "expected 'end' after class");
    }

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

    const bool braced = match(TokenType::LeftBrace);
    const Token opening = braced ? previous() : token;
    if (braced) {
        validateBlockSyntax(BlockSyntax::Braced, opening);
        blockSyntaxStack_.push_back(BlockSyntax::Braced);
    } else {
        validateBlockSyntax(BlockSyntax::Legacy, token);
        consume(TokenType::Colon, "expected '{' after enum name");
    }

    skipNewlines();

    while (
        !isAtEnd() &&
        !(braced ? check(TokenType::RightBrace) : check(TokenType::End))
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

    if (braced) {
        if (match(TokenType::RightBrace)) {
            blockSyntaxStack_.pop_back();
            consumeExplicitTerminator(TokenType::Enum, "enum", opening);
        } else {
            blockSyntaxStack_.pop_back();
            errorAt(current(), "expected '}' to close 'enum' block opened at line " +
                std::to_string(opening.line) + "; found " + describeToken(current()));
            consumeExplicitTerminator(TokenType::Enum, "enum", opening);
        }
    } else {
        consume(TokenType::End, "expected 'end' after enum");
    }

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

    if (match(TokenType::LeftBrace)) {
        const Token opening = previous();
        declaration->body = parseBracedBody(opening, "namespace");
        consumeExplicitTerminator(TokenType::Namespace, "namespace", opening);
    } else {
        validateBlockSyntax(BlockSyntax::Legacy, token);
        consume(TokenType::Colon, "expected '{' after namespace name");
        skipNewlines();
        declaration->body = parseUntil(TokenType::End);
        consume(TokenType::End, "expected 'end' after namespace");
    }

    std::function<void(std::vector<zplus::ast::Stmt>&, const std::string&)>
        assignNamespaceNames;
    assignNamespaceNames = [&](std::vector<zplus::ast::Stmt>& statements,
                               const std::string& namespaceName) {
        for (auto& child : statements) {
            if (!child) continue;
            zplus::ast::Statement* nested = child.get();
            if (auto* exported =
                    dynamic_cast<zplus::ast::ExportDeclaration*>(nested)) {
                nested = exported->declaration.get();
            }
            if (auto* function =
                    dynamic_cast<zplus::ast::FunctionDeclaration*>(nested)) {
                function->namespaceName = namespaceName;
                function->internalName =
                    namespaceName + "::" + function->name;
            } else if (auto* nestedNamespace =
                           dynamic_cast<zplus::ast::NamespaceDeclaration*>(nested)) {
                assignNamespaceNames(
                    nestedNamespace->body,
                    namespaceName + "::" + nestedNamespace->name
                );
            }
        }
    };
    assignNamespaceNames(declaration->body, declaration->name);

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
               check(TokenType::Range) ||
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