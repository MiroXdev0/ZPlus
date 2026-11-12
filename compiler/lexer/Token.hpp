#pragma once

#include <string>

namespace zplus {

// ============================================================
// Token Types
// ============================================================

enum class TokenType {

    // ========================================================
    // Special
    // ========================================================

    EndOfFile,
    Unknown,
    Newline,


    // ========================================================
    // Literals
    // ========================================================

    Identifier,
    Number,
    String,
    Character,


    // ========================================================
    // Constants
    // ========================================================

    True,
    False,
    Null,


    // ========================================================
    // Program structure
    // ========================================================

    Start,
    Stop,


    // ========================================================
    // Variables
    // ========================================================

    Let,
    Const,


    // ========================================================
    // Types
    // ========================================================

    NumberType,
    StringType,
    CharacterType,
    BooleanType,
    VoidType,


    // ========================================================
    // Functions
    // ========================================================

    Fn,
    Return,
    Try,
    Catch,
    Throw,
    Defer,


    // ========================================================
    // Control flow
    // ========================================================

    If,
    Else,

    Repeat,
    While,
    For,

    End,

    Break,
    Continue,


    // ========================================================
    // Switch
    // ========================================================

    Switch,
    Case,
    Default,


    // ========================================================
    // Structures / classes
    // ========================================================

    Struct,
    Class,
    Enum,

    Public,
    Pub,
    Private,
    Protected,

    This,


    // ========================================================
    // Modules / namespaces
    // ========================================================

    Namespace,
    Import,
    Use,
    Export,


    // ========================================================
    // Built-in functions
    // ========================================================

    Say,
    Printz,


    // ========================================================
    // Logical keywords
    // ========================================================

    And,
    Or,
    Not,


    // ========================================================
    // Parentheses
    // ========================================================

    LeftParen,
    RightParen,


    // ========================================================
    // Brackets
    // ========================================================

    LeftBracket,
    RightBracket,


    // ========================================================
    // Braces
    // ========================================================

    LeftBrace,
    RightBrace,


    // ========================================================
    // Punctuation
    // ========================================================

    Comma,
    Colon,
    Dot,
    Range,
    Semicolon,


    // ========================================================
    // Arithmetic operators
    // ========================================================

    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    PlusPlus,
    MinusMinus,


    // ========================================================
    // Assignment operators
    // ========================================================

    Equal,

    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,

    AndEqual,
    OrEqual,
    XorEqual,

    LeftShiftEqual,
    RightShiftEqual,


    // ========================================================
    // Comparison operators
    // ========================================================

    EqualEqual,
    NotEqual,

    Less,
    Greater,

    LessEqual,
    GreaterEqual,


    // ========================================================
    // Logical symbol operators
    // ========================================================

    AndAnd,
    OrOr,
    Exclamation,


    // ========================================================
    // Bitwise operators
    // ========================================================

    BitAnd,
    BitOr,
    BitXor,
    BitNot,

    LeftShift,
    RightShift,


    // ========================================================
    // Pointer / member operators
    // ========================================================

    Arrow,
    DoubleColon,


    // ========================================================
    // Conditional operator
    // ========================================================

    Question,


    // ========================================================
    // Type operators
    // ========================================================

    As,
    Is,


    // ========================================================
    // Advanced syntax
    // ========================================================

    Hash,
    At,
    Backslash
};


// ============================================================
// Token
// ============================================================

struct Token {

    TokenType type = TokenType::Unknown;

    std::string value;

    int line = 1;
    int column = 1;
    std::string sourceFile;


    Token() = default;


    Token(
        TokenType tokenType,
        std::string tokenValue,
        int tokenLine = 1,
        int tokenColumn = 1,
        std::string tokenSourceFile = {}
    )
        : type(tokenType),
          value(std::move(tokenValue)),
          line(tokenLine),
          column(tokenColumn),
          sourceFile(std::move(tokenSourceFile)) {
    }
};


// ============================================================
// Token utilities
// ============================================================

const char* tokenTypeName(TokenType type);


// ============================================================
// Token helpers
// ============================================================

inline bool isEnd(const Token& token) {
    return token.type == TokenType::EndOfFile;
}

inline bool isNewline(const Token& token) {
    return token.type == TokenType::Newline;
}

inline bool isIdentifier(const Token& token) {
    return token.type == TokenType::Identifier;
}

} // namespace zplus