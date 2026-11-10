#pragma once

#include "Token.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace zplus::lexer {

class Lexer {
public:

    struct Error {
        std::string message;
        int line = 1;
        int column = 1;
    };

    explicit Lexer(
        const std::string& source,
        std::string sourceFile = {}
    );

    std::vector<zplus::Token> tokenize();

    const std::vector<Error>& errors() const;

private:

    bool isAtEnd() const;

    char current() const;

    char peek(
        std::size_t offset = 1
    ) const;

    void advance();

    bool match(
        char expected
    );

    zplus::Token makeToken(
        zplus::TokenType type,
        const std::string& value,
        int line,
        int column
    ) const;

    zplus::TokenType keywordType(
        const std::string& word
    ) const;

    void skipWhitespace();

    void lexIdentifierOrKeyword(
        std::vector<zplus::Token>& tokens
    );

    void lexNumber(
        std::vector<zplus::Token>& tokens
    );

    void lexString(
        std::vector<zplus::Token>& tokens
    );

    void lexCharacter(
        std::vector<zplus::Token>& tokens
    );

    void lexOperatorOrPunctuation(
        std::vector<zplus::Token>& tokens
    );

    static bool isIdentifierStart(char c);
    static bool isIdentifierPart(char c);
    static bool isDigit(char c);
    static bool isHexDigit(char c);

    void error(
        const std::string& message,
        int line,
        int column
    );

private:

    const std::string& source_;
    std::string sourceFile_;

    std::size_t position_ = 0;

    int line_ = 1;
    int column_ = 1;

    std::vector<Error> errors_;
};

} // namespace zplus::lexer