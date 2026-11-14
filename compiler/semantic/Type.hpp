#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zplus::semantic {

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
    Custom,
    TypeParameter
};

struct Type {
    TypeKind kind = TypeKind::Unknown;
    std::string name;
    std::shared_ptr<Type> elementType;
    std::vector<Type> arguments;
    std::vector<Type> parameters;
    std::size_t requiredParameterCount = 0;
    std::shared_ptr<Type> returnType;

    Type() = default;
    explicit Type(TypeKind value) : kind(value) {}

    static Type unknown() { return Type(TypeKind::Unknown); }
    static Type number() { return Type(TypeKind::Number); }
    static Type string() { return Type(TypeKind::String); }
    static Type character() { return Type(TypeKind::Character); }
    static Type boolean() { return Type(TypeKind::Boolean); }
    static Type voidType() { return Type(TypeKind::Void); }
    static Type null() { return Type(TypeKind::Null); }
    static Type typeParameter(std::string name);
    static Type custom(std::string name, std::vector<Type> arguments = {});
    static Type array(const Type& element);
    static Type array(Type&& element);
    static Type pointer(const Type& element);
    static Type pointer(Type&& element);
    static Type function(std::vector<Type> parameters, const Type& returnType);
    static Type function(std::vector<Type> parameters, Type&& returnType);
};

using TypeSubstitution = std::unordered_map<std::string, Type>;

std::string typeName(const Type& type);
bool typesEqual(const Type& left, const Type& right);
bool isAssignable(const Type& target, const Type& value);
Type substitute(const Type& type, const TypeSubstitution& substitutions);
bool unify(const Type& pattern, const Type& actual, TypeSubstitution& substitutions);

} // namespace zplus::semantic
