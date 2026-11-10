#pragma once

namespace mpp::semantic {

enum class TypeKind {
    Unknown,
    Void,
    Number,
    Boolean,
    String
};

struct Type {
    TypeKind kind = TypeKind::Unknown;
};

const char* typeName(TypeKind kind);

}