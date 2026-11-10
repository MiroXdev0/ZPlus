#include "Lexer.hpp"
#include "../NumericLiteral.hpp"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace zplus::lexer {

// ============================================================
// Constructor
// ============================================================

Lexer::Lexer(const std::string& source, std::string sourceFile)
    : source_(source),
      sourceFile_(std::move(sourceFile)) {
}

// ============================================================
// Character helpers
// ============================================================

bool Lexer::isAtEnd() const {
    return position_ >= source_.size();
}

char Lexer::current() const {
    if (isAtEnd()) {
        return '\0';
    }

    return source_[position_];
}

char Lexer::peek(std::size_t offset) const {
    const std::size_t index = position_ + offset;

    if (index >= source_.size()) {
        return '\0';
    }

    return source_[index];
}

void Lexer::advance() {
    if (isAtEnd()) {
        return;
    }

    if (current() == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }

    ++position_;
}

bool Lexer::match(char expected) {
    if (current() != expected) {
        return false;
    }

    advance();
    return true;
}

// ============================================================
// Token creation
// ============================================================

zplus::Token Lexer::makeToken(
    zplus::TokenType type,
    const std::string& value,
    int line,
    int column
) const {
    return zplus::Token{
        type,
        value,
        line,
        column,
        sourceFile_
    };
}

// ============================================================
// Keyword lookup
// ============================================================

zplus::TokenType Lexer::keywordType(
    const std::string& word
) const {

    static const std::unordered_map<
        std::string,
        zplus::TokenType
    > keywords = {

        // Program
        {"start", zplus::TokenType::Start},
        {"stop", zplus::TokenType::Stop},

        // Variables
        {"let", zplus::TokenType::Let},
        {"const", zplus::TokenType::Const},

        // Types
        {"number", zplus::TokenType::NumberType},
        {"string", zplus::TokenType::StringType},
        {"char", zplus::TokenType::CharacterType},
        {"character", zplus::TokenType::CharacterType},
        {"boolean", zplus::TokenType::BooleanType},
        {"bool", zplus::TokenType::BooleanType},
        {"void", zplus::TokenType::VoidType},

        // Functions
        {"fn", zplus::TokenType::Fn},
        {"return", zplus::TokenType::Return},
        {"try", zplus::TokenType::Try},
        {"catch", zplus::TokenType::Catch},
        {"throw", zplus::TokenType::Throw},
        {"defer", zplus::TokenType::Defer},

        // Control flow
        {"if", zplus::TokenType::If},
        {"else", zplus::TokenType::Else},
        {"end", zplus::TokenType::End},

        // Loops
        {"repeat", zplus::TokenType::Repeat},
        {"while", zplus::TokenType::While},
        {"for", zplus::TokenType::For},

        {"break", zplus::TokenType::Break},
        {"continue", zplus::TokenType::Continue},

        // Switch
        {"switch", zplus::TokenType::Switch},
        {"case", zplus::TokenType::Case},
        {"default", zplus::TokenType::Default},

        // Structures
        {"struct", zplus::TokenType::Struct},
        {"class", zplus::TokenType::Class},
        {"enum", zplus::TokenType::Enum},

        {"public", zplus::TokenType::Public},
        {"pub", zplus::TokenType::Pub},
        {"private", zplus::TokenType::Private},
        {"protected", zplus::TokenType::Protected},
        {"this", zplus::TokenType::This},

        // Modules
        {"namespace", zplus::TokenType::Namespace},
        {"import", zplus::TokenType::Import},
        {"use", zplus::TokenType::Use},
        {"export", zplus::TokenType::Export},

        // Built-ins
        {"say", zplus::TokenType::Say},
        {"printz", zplus::TokenType::Printz},

        // Constants
        {"true", zplus::TokenType::True},
        {"false", zplus::TokenType::False},
        {"null", zplus::TokenType::Null},
        {"nullptr", zplus::TokenType::Null},

        // Logical keywords
        {"and", zplus::TokenType::And},
        {"or", zplus::TokenType::Or},
        {"not", zplus::TokenType::Not},

        // Type operations
        {"as", zplus::TokenType::As},
        {"is", zplus::TokenType::Is}
    };

    const auto it = keywords.find(word);

    if (it != keywords.end()) {
        return it->second;
    }

    return zplus::TokenType::Identifier;
}

// ============================================================
// Whitespace and comments
// ============================================================

void Lexer::skipWhitespace() {

    while (!isAtEnd()) {

        const char c = current();

        if (
            c == ' ' ||
            c == '\t' ||
            c == '\r'
        ) {
            advance();
            continue;
        }

        // Single-line comment
        if (
            c == '/' &&
            peek() == '/'
        ) {
            advance();
            advance();

            while (
                !isAtEnd() &&
                current() != '\n'
            ) {
                advance();
            }

            continue;
        }

        // Block comment
        if (
            c == '/' &&
            peek() == '*'
        ) {

            const int startLine = line_;
            const int startColumn = column_;

            advance();
            advance();

            bool closed = false;

            while (!isAtEnd()) {

                if (
                    current() == '*' &&
                    peek() == '/'
                ) {
                    advance();
                    advance();
                    closed = true;
                    break;
                }

                advance();
            }

            if (!closed) {
                error(
                    "unterminated block comment",
                    startLine,
                    startColumn
                );
            }

            continue;
        }

        break;
    }
}

// ============================================================
// Character classification
// ============================================================

bool Lexer::isIdentifierStart(char c) {

    return
        std::isalpha(
            static_cast<unsigned char>(c)
        ) ||
        c == '_';
}

bool Lexer::isIdentifierPart(char c) {

    return
        std::isalnum(
            static_cast<unsigned char>(c)
        ) ||
        c == '_';
}

bool Lexer::isDigit(char c) {

    return std::isdigit(
        static_cast<unsigned char>(c)
    );
}

bool Lexer::isHexDigit(char c) {

    return std::isxdigit(
        static_cast<unsigned char>(c)
    );
}

// ============================================================
// Identifier / keyword
// ============================================================

void Lexer::lexIdentifierOrKeyword(
    std::vector<zplus::Token>& tokens
) {

    const int startLine = line_;
    const int startColumn = column_;

    const std::size_t start = position_;

    while (
        !isAtEnd() &&
        isIdentifierPart(current())
    ) {
        advance();
    }

    const std::string word =
        source_.substr(
            start,
            position_ - start
        );

    tokens.push_back(
        makeToken(
            keywordType(word),
            word,
            startLine,
            startColumn
        )
    );
}

// ============================================================
// Number
// ============================================================

void Lexer::lexNumber(
    std::vector<zplus::Token>& tokens
) {

    const int startLine = line_;
    const int startColumn = column_;

    const std::size_t start = position_;
    bool valid = true;

    const auto emitNumber = [&]() {
        const std::string spelling = source_.substr(start, position_ - start);
        double parsed = 0.0;
        if (valid && !zplus::numeric::parseLiteral(spelling, parsed)) {
            error("malformed numeric literal", startLine, startColumn);
            valid = false;
        }
        tokens.push_back(makeToken(
            valid ? zplus::TokenType::Number : zplus::TokenType::Unknown,
            spelling,
            startLine,
            startColumn
        ));
    };

    // Hexadecimal
    if (
        current() == '0' &&
        (peek() == 'x' || peek() == 'X')
    ) {

        advance();
        advance();

        const std::size_t digitStart = position_;

        while (
            !isAtEnd() &&
            isHexDigit(current())
        ) {
            advance();
        }

        if (position_ == digitStart) {

            error(
                "expected hexadecimal digits after '0x'",
                startLine,
                startColumn
            );
            valid = false;
        }

        if (isIdentifierPart(current())) {
            while (!isAtEnd() && isIdentifierPart(current())) advance();
            error("invalid character in hexadecimal numeric literal", startLine, startColumn);
            valid = false;
        }

        emitNumber();
        return;
    }

    // Binary
    if (
        current() == '0' &&
        (peek() == 'b' || peek() == 'B')
    ) {

        advance();
        advance();

        const std::size_t digitStart = position_;

        while (
            !isAtEnd() &&
            (
                current() == '0' ||
                current() == '1'
            )
        ) {
            advance();
        }

        if (position_ == digitStart) {

            error(
                "expected binary digits after '0b'",
                startLine,
                startColumn
            );
            valid = false;
        }

        if (isIdentifierPart(current())) {
            while (!isAtEnd() && isIdentifierPart(current())) advance();
            error("invalid character in binary numeric literal", startLine, startColumn);
            valid = false;
        }

        emitNumber();
        return;
    }

    // Decimal
    while (
        !isAtEnd() &&
        isDigit(current())
    ) {
        advance();
    }

    // Fraction
    if (
        current() == '.' &&
        isDigit(peek())
    ) {

        advance();

        while (
            !isAtEnd() &&
            isDigit(current())
        ) {
            advance();
        }
    }

    // Scientific notation
    if (
        current() == 'e' ||
        current() == 'E'
    ) {

        advance();

        if (
            current() == '+' ||
            current() == '-'
        ) {
            advance();
        }

        const std::size_t exponentStart = position_;

        while (
            !isAtEnd() &&
            isDigit(current())
        ) {
            advance();
        }

        if (position_ == exponentStart) {

            error(
                "expected exponent digits",
                startLine,
                startColumn
            );
            valid = false;
        }
    }

    if (isIdentifierStart(current())) {
        while (!isAtEnd() && isIdentifierPart(current())) advance();
        error("invalid character after numeric literal", startLine, startColumn);
        valid = false;
    }

    emitNumber();
}

// ============================================================
// String
// ============================================================

void Lexer::lexString(
    std::vector<zplus::Token>& tokens
) {

    const int startLine = line_;
    const int startColumn = column_;

    advance();

    std::string value;

    while (!isAtEnd()) {

        if (current() == '"') {

            advance();

            tokens.push_back(
                makeToken(
                    zplus::TokenType::String,
                    value,
                    startLine,
                    startColumn
                )
            );

            return;
        }

        if (current() == '\n') {

            error(
                "unterminated string literal",
                startLine,
                startColumn
            );

            tokens.push_back(
                makeToken(
                    zplus::TokenType::Unknown,
                    value,
                    startLine,
                    startColumn
                )
            );

            return;
        }

        if (current() == '\\') {

            advance();

            if (isAtEnd()) {
                break;
            }

            switch (current()) {

                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'v': value += '\v'; break;
                case 'a': value += '\a'; break;
                case '0': value += '\0'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;

                default:
                    error(
                        "unknown escape sequence",
                        line_,
                        column_
                    );

                    value += current();
                    break;
            }

            advance();
            continue;
        }

        value += current();
        advance();
    }

    error(
        "unterminated string literal",
        startLine,
        startColumn
    );

    tokens.push_back(
        makeToken(
            zplus::TokenType::Unknown,
            value,
            startLine,
            startColumn
        )
    );
}

// ============================================================
// Character
// ============================================================

void Lexer::lexCharacter(
    std::vector<zplus::Token>& tokens
) {

    const int startLine = line_;
    const int startColumn = column_;

    advance();

    std::string value;

    if (
        isAtEnd() ||
        current() == '\n'
    ) {

        error(
            "unterminated character literal",
            startLine,
            startColumn
        );

        tokens.push_back(
            makeToken(
                zplus::TokenType::Unknown,
                "",
                startLine,
                startColumn
            )
        );

        return;
    }

    if (current() == '\\') {

        advance();

        if (isAtEnd()) {
            error(
                "unterminated character literal",
                startLine,
                startColumn
            );
            tokens.push_back(makeToken(
                zplus::TokenType::Unknown,
                "",
                startLine,
                startColumn
            ));
            return;
        }

        switch (current()) {

            case 'n': value = "\n"; break;
            case 't': value = "\t"; break;
            case 'r': value = "\r"; break;
            case 'b': value = "\b"; break;
            case 'f': value = "\f"; break;
            case 'v': value = "\v"; break;
            case 'a': value = "\a"; break;
            case '0': value = "\0"; break;
            case '\\': value = "\\"; break;
            case '\'': value = "'"; break;
            case '"': value = "\""; break;

            default:
                error(
                    "unknown escape sequence",
                    line_,
                    column_
                );
                value += current();
                break;
        }

        advance();

    } else {

        value += current();
        advance();
    }

    if (current() != '\'') {

        error(
            "character literal must contain exactly one character",
            startLine,
            startColumn
        );

        while (
            !isAtEnd() &&
            current() != '\'' &&
            current() != '\n'
        ) {
            advance();
        }

        if (current() == '\'') {
            advance();
        }

        tokens.push_back(
            makeToken(
                zplus::TokenType::Unknown,
                value,
                startLine,
                startColumn
            )
        );

        return;
    }

    advance();

    tokens.push_back(
        makeToken(
            zplus::TokenType::Character,
            value,
            startLine,
            startColumn
        )
    );
}

// ============================================================
// Operators / punctuation
// ============================================================

void Lexer::lexOperatorOrPunctuation(
    std::vector<zplus::Token>& tokens
) {

    const int tokenLine = line_;
    const int tokenColumn = column_;

    const char c = current();

    advance();

    switch (c) {

        case '(':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::LeftParen,
                    "(",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case ')':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::RightParen,
                    ")",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case '[':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::LeftBracket,
                    "[",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case ']':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::RightBracket,
                    "]",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case '{':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::LeftBrace,
                    "{",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case '}':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::RightBrace,
                    "}",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case ',':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::Comma,
                    ",",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case ':':
            if (match(':')) {
                tokens.push_back(
                    makeToken(
                        zplus::TokenType::DoubleColon,
                        "::",
                        tokenLine,
                        tokenColumn
                    )
                );
            } else {
                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Colon,
                        ":",
                        tokenLine,
                        tokenColumn
                    )
                );
            }
            break;

        case '.':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::Dot,
                    ".",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case ';':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::Semicolon,
                    ";",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        case '+':

            if (match('+')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::PlusPlus,
                        "++",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::PlusEqual,
                        "+=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Plus,
                        "+",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '-':

            if (match('>')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Arrow,
                        "->",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('-')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::MinusMinus,
                        "--",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::MinusEqual,
                        "-=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Minus,
                        "-",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '*':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::StarEqual,
                        "*=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Star,
                        "*",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '/':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::SlashEqual,
                        "/=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Slash,
                        "/",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '%':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::PercentEqual,
                        "%=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Percent,
                        "%",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '=':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::EqualEqual,
                        "==",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Equal,
                        "=",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '!':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::NotEqual,
                        "!=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Exclamation,
                        "!",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '<':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::LessEqual,
                        "<=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('<')) {

                if (match('=')) {

                    tokens.push_back(
                        makeToken(
                            zplus::TokenType::LeftShiftEqual,
                            "<<=",
                            tokenLine,
                            tokenColumn
                        )
                    );

                } else {

                    tokens.push_back(
                        makeToken(
                            zplus::TokenType::LeftShift,
                            "<<",
                            tokenLine,
                            tokenColumn
                        )
                    );
                }

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Less,
                        "<",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '>':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::GreaterEqual,
                        ">=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('>')) {

                if (match('=')) {

                    tokens.push_back(
                        makeToken(
                            zplus::TokenType::RightShiftEqual,
                            ">>=",
                            tokenLine,
                            tokenColumn
                        )
                    );

                } else {

                    tokens.push_back(
                        makeToken(
                            zplus::TokenType::RightShift,
                            ">>",
                            tokenLine,
                            tokenColumn
                        )
                    );
                }

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::Greater,
                        ">",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '&':

            if (match('&')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::AndAnd,
                        "&&",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::AndEqual,
                        "&=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::BitAnd,
                        "&",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '|':

            if (match('|')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::OrOr,
                        "||",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::OrEqual,
                        "|=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::BitOr,
                        "|",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '^':

            if (match('=')) {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::XorEqual,
                        "^=",
                        tokenLine,
                        tokenColumn
                    )
                );

            } else {

                tokens.push_back(
                    makeToken(
                        zplus::TokenType::BitXor,
                        "^",
                        tokenLine,
                        tokenColumn
                    )
                );
            }

            break;

        case '~':

            tokens.push_back(
                makeToken(
                    zplus::TokenType::BitNot,
                    "~",
                    tokenLine,
                    tokenColumn
                )
            );

            break;

        case '?':

            tokens.push_back(
                makeToken(
                    zplus::TokenType::Question,
                    "?",
                    tokenLine,
                    tokenColumn
                )
            );

            break;

        case '#':

            tokens.push_back(
                makeToken(
                    zplus::TokenType::Hash,
                    "#",
                    tokenLine,
                    tokenColumn
                )
            );

            break;

        case '@':

            tokens.push_back(
                makeToken(
                    zplus::TokenType::At,
                    "@",
                    tokenLine,
                    tokenColumn
                )
            );

            break;

        case '\\':
            tokens.push_back(
                makeToken(
                    zplus::TokenType::Backslash,
                    "\\",
                    tokenLine,
                    tokenColumn
                )
            );
            break;

        default:

            error(
                "unexpected character '" +
                std::string(1, c) +
                "'",
                tokenLine,
                tokenColumn
            );

            tokens.push_back(
                makeToken(
                    zplus::TokenType::Unknown,
                    std::string(1, c),
                    tokenLine,
                    tokenColumn
                )
            );

            break;
    }
}

// ============================================================
// Error handling
// ============================================================

void Lexer::error(
    const std::string& message,
    int line,
    int column
) {

    errors_.push_back({
        message,
        line,
        column
    });
}

// ============================================================
// Tokenize
// ============================================================

std::vector<zplus::Token> Lexer::tokenize() {

    std::vector<zplus::Token> tokens;

    while (!isAtEnd()) {

        skipWhitespace();

        if (isAtEnd()) {
            break;
        }

        // Newline
        if (current() == '\n') {

            const int line = line_;
            const int column = column_;

            advance();

            tokens.push_back(
                makeToken(
                    zplus::TokenType::Newline,
                    "\\n",
                    line,
                    column
                )
            );

            continue;
        }

        // Identifier / keyword
        if (isIdentifierStart(current())) {

            lexIdentifierOrKeyword(tokens);
            continue;
        }

        // Number
        if (isDigit(current())) {

            lexNumber(tokens);
            continue;
        }

        // String
        if (current() == '"') {

            lexString(tokens);
            continue;
        }

        // Character
        if (current() == '\'') {

            lexCharacter(tokens);
            continue;
        }

        // Operators / punctuation
        lexOperatorOrPunctuation(tokens);
    }

    tokens.push_back(
        makeToken(
            zplus::TokenType::EndOfFile,
            "",
            line_,
            column_
        )
    );

    return tokens;
}

// ============================================================
// Errors
// ============================================================

const std::vector<Lexer::Error>&
Lexer::errors() const {
    return errors_;
}

} // namespace zplus::lexer