#include "Token.hpp"

namespace zplus {

// ============================================================
// Token Type Names
// ============================================================

const char* tokenTypeName(TokenType type) {

    switch (type) {

        // ====================================================
        // Special
        // ====================================================

        case TokenType::EndOfFile: return "EOF";
        case TokenType::Unknown:   return "Unknown";
        case TokenType::Newline:   return "Newline";


        // ====================================================
        // Literals
        // ====================================================

        case TokenType::Identifier: return "Identifier";
        case TokenType::Number:     return "Number";
        case TokenType::String:     return "String";
        case TokenType::Character:  return "Character";


        // ====================================================
        // Constants
        // ====================================================

        case TokenType::True:  return "True";
        case TokenType::False: return "False";
        case TokenType::Null:  return "Null";


        // ====================================================
        // Program structure
        // ====================================================

        case TokenType::Start: return "Start";
        case TokenType::Stop:  return "Stop";


        // ====================================================
        // Variables
        // ====================================================

        case TokenType::Let:   return "Let";
        case TokenType::Const: return "Const";


        // ====================================================
        // Types
        // ====================================================

        case TokenType::NumberType:
            return "NumberType";

        case TokenType::StringType:
            return "StringType";

        case TokenType::CharacterType:
            return "CharacterType";

        case TokenType::BooleanType:
            return "BooleanType";

        case TokenType::VoidType:
            return "VoidType";


        // ====================================================
        // Functions
        // ====================================================

        case TokenType::Fn:     return "Fn";
        case TokenType::Return: return "Return";
        case TokenType::Try:    return "Try";
        case TokenType::Catch:  return "Catch";
        case TokenType::Throw:  return "Throw";
        case TokenType::Defer:  return "Defer";


        // ====================================================
        // Control flow
        // ====================================================

        case TokenType::If:       return "If";
        case TokenType::Else:     return "Else";
        case TokenType::Repeat:   return "Repeat";
        case TokenType::While:    return "While";
        case TokenType::For:      return "For";
        case TokenType::End:      return "End";
        case TokenType::Break:    return "Break";
        case TokenType::Continue: return "Continue";


        // ====================================================
        // Switch
        // ====================================================

        case TokenType::Switch:  return "Switch";
        case TokenType::Case:    return "Case";
        case TokenType::Default: return "Default";


        // ====================================================
        // Structures / classes
        // ====================================================

        case TokenType::Struct:    return "Struct";
        case TokenType::Class:     return "Class";
        case TokenType::Enum:      return "Enum";
        case TokenType::Public:    return "Public";
        case TokenType::Pub:       return "Pub";
        case TokenType::Private:   return "Private";
        case TokenType::Protected: return "Protected";
        case TokenType::This:      return "This";


        // ====================================================
        // Modules / namespaces
        // ====================================================

        case TokenType::Namespace: return "Namespace";
        case TokenType::Import:    return "Import";
        case TokenType::Use:       return "Use";
        case TokenType::Export:    return "Export";


        // ====================================================
        // Built-in functions
        // ====================================================

        case TokenType::Say:    return "Say";
        case TokenType::Printz: return "Printz";


        // ====================================================
        // Logical keywords
        // ====================================================

        case TokenType::And: return "And";
        case TokenType::Or:  return "Or";
        case TokenType::Not: return "Not";


        // ====================================================
        // Parentheses
        // ====================================================

        case TokenType::LeftParen:
            return "LeftParen";

        case TokenType::RightParen:
            return "RightParen";


        // ====================================================
        // Brackets
        // ====================================================

        case TokenType::LeftBracket:
            return "LeftBracket";

        case TokenType::RightBracket:
            return "RightBracket";


        // ====================================================
        // Braces
        // ====================================================

        case TokenType::LeftBrace:
            return "LeftBrace";

        case TokenType::RightBrace:
            return "RightBrace";


        // ====================================================
        // Punctuation
        // ====================================================

        case TokenType::Comma:     return "Comma";
        case TokenType::Colon:     return "Colon";
        case TokenType::Dot:       return "Dot";
        case TokenType::Semicolon: return "Semicolon";


        // ====================================================
        // Arithmetic
        // ====================================================

        case TokenType::Plus:    return "Plus";
        case TokenType::Minus:   return "Minus";
        case TokenType::Star:    return "Star";
        case TokenType::Slash:   return "Slash";
        case TokenType::Percent: return "Percent";


        // ====================================================
        // Increment / decrement
        // ====================================================

        case TokenType::PlusPlus:
            return "PlusPlus";

        case TokenType::MinusMinus:
            return "MinusMinus";


        // ====================================================
        // Assignment
        // ====================================================

        case TokenType::Equal:
            return "Equal";

        case TokenType::PlusEqual:
            return "PlusEqual";

        case TokenType::MinusEqual:
            return "MinusEqual";

        case TokenType::StarEqual:
            return "StarEqual";

        case TokenType::SlashEqual:
            return "SlashEqual";

        case TokenType::PercentEqual:
            return "PercentEqual";

        case TokenType::AndEqual:
            return "AndEqual";

        case TokenType::OrEqual:
            return "OrEqual";

        case TokenType::XorEqual:
            return "XorEqual";

        case TokenType::LeftShiftEqual:
            return "LeftShiftEqual";

        case TokenType::RightShiftEqual:
            return "RightShiftEqual";


        // ====================================================
        // Comparison
        // ====================================================

        case TokenType::EqualEqual:
            return "EqualEqual";

        case TokenType::NotEqual:
            return "NotEqual";

        case TokenType::Less:
            return "Less";

        case TokenType::Greater:
            return "Greater";

        case TokenType::LessEqual:
            return "LessEqual";

        case TokenType::GreaterEqual:
            return "GreaterEqual";


        // ====================================================
        // Logical symbols
        // ====================================================

        case TokenType::AndAnd:
            return "AndAnd";

        case TokenType::OrOr:
            return "OrOr";

        case TokenType::Exclamation:
            return "Exclamation";


        // ====================================================
        // Bitwise
        // ====================================================

        case TokenType::BitAnd:
            return "BitAnd";

        case TokenType::BitOr:
            return "BitOr";

        case TokenType::BitXor:
            return "BitXor";

        case TokenType::BitNot:
            return "BitNot";

        case TokenType::LeftShift:
            return "LeftShift";

        case TokenType::RightShift:
            return "RightShift";


        // ====================================================
        // Pointer / member operators
        // ====================================================

        case TokenType::Arrow:
            return "Arrow";

        case TokenType::DoubleColon:
            return "DoubleColon";


        // ====================================================
        // Conditional
        // ====================================================

        case TokenType::Question:
            return "Question";


        // ====================================================
        // Type operations
        // ====================================================

        case TokenType::As:
            return "As";

        case TokenType::Is:
            return "Is";


        // ====================================================
        // Advanced syntax
        // ====================================================

        case TokenType::Hash:
            return "Hash";

        case TokenType::At:
            return "At";

        case TokenType::Backslash:
            return "Backslash";
    }

    return "Unknown";
}

} // namespace zplus