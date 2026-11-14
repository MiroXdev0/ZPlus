#include "Type.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace zplus::semantic {
namespace {

bool containsTypeParameter(const Type& type, const std::string& name) {
    if (type.kind == TypeKind::TypeParameter && type.name == name) {
        return true;
    }
    if (type.elementType && containsTypeParameter(*type.elementType, name)) {
        return true;
    }
    for (const Type& argument : type.arguments) {
        if (containsTypeParameter(argument, name)) return true;
    }
    for (const Type& parameter : type.parameters) {
        if (containsTypeParameter(parameter, name)) return true;
    }
    return type.returnType && containsTypeParameter(*type.returnType, name);
}

Type substituteImpl(
    const Type& type,
    const TypeSubstitution& substitutions,
    std::unordered_set<std::string>& expanding
) {
    if (type.kind == TypeKind::TypeParameter) {
        const auto found = substitutions.find(type.name);
        if (found == substitutions.end()) return type;
        if (!expanding.insert(type.name).second) return Type::unknown();
        Type result = substituteImpl(found->second, substitutions, expanding);
        expanding.erase(type.name);
        return result;
    }

    Type result = type;
    if (type.elementType) {
        result.elementType =
            std::make_shared<Type>(
                substituteImpl(*type.elementType, substitutions, expanding));
    }
    for (Type& argument : result.arguments) {
        argument = substituteImpl(argument, substitutions, expanding);
    }
    for (Type& parameter : result.parameters) {
        parameter = substituteImpl(parameter, substitutions, expanding);
    }
    if (type.returnType) {
        result.returnType =
            std::make_shared<Type>(
                substituteImpl(*type.returnType, substitutions, expanding));
    }
    return result;
}

bool satisfiesConstraint(const Type& type, TypeConstraint constraint) {
    if (type.kind == TypeKind::TypeParameter) {
        return std::find(type.constraints.begin(), type.constraints.end(),
                         constraint) != type.constraints.end();
    }
    if (constraint == TypeConstraint::Numeric) {
        return type.kind == TypeKind::Number;
    }
    return type.kind == TypeKind::Number ||
        type.kind == TypeKind::String ||
        type.kind == TypeKind::Character ||
        type.kind == TypeKind::Boolean ||
        type.kind == TypeKind::Null ||
        type.kind == TypeKind::Custom ||
        type.kind == TypeKind::Array ||
        type.kind == TypeKind::Pointer;
}

bool unifyImpl(
    const Type& pattern,
    const Type& actual,
    TypeSubstitution& substitutions,
    std::unordered_set<std::string>& resolving
) {
    if (pattern.kind == TypeKind::Unknown || actual.kind == TypeKind::Unknown) {
        return true;
    }
    if (pattern.kind == TypeKind::TypeParameter) {
        for (const TypeConstraint constraint : pattern.constraints) {
            if (!satisfiesConstraint(actual, constraint)) return false;
        }
        if (actual.kind == TypeKind::TypeParameter &&
            actual.name == pattern.name) {
            return true;
        }
        const auto found = substitutions.find(pattern.name);
        if (found != substitutions.end()) {
            if (!resolving.insert(pattern.name).second) return false;
            const bool compatible =
                unifyImpl(found->second, actual, substitutions, resolving);
            resolving.erase(pattern.name);
            return compatible;
        }
        if (containsTypeParameter(actual, pattern.name)) {
            return false;
        }
        substitutions.emplace(pattern.name, actual);
        return true;
    }
    if (pattern.kind != actual.kind) return false;

    if (pattern.kind == TypeKind::Array || pattern.kind == TypeKind::Pointer) {
        return !pattern.elementType || !actual.elementType ||
            unifyImpl(*pattern.elementType, *actual.elementType,
                      substitutions, resolving);
    }
    if (pattern.kind == TypeKind::Custom) {
        if (pattern.name != actual.name ||
            pattern.arguments.size() != actual.arguments.size()) {
            return false;
        }
        for (std::size_t i = 0; i < pattern.arguments.size(); ++i) {
            if (!unifyImpl(pattern.arguments[i], actual.arguments[i],
                           substitutions, resolving)) {
                return false;
            }
        }
        return true;
    }
    if (pattern.kind == TypeKind::Function) {
        if (pattern.parameters.size() != actual.parameters.size() ||
            pattern.requiredParameterCount != actual.requiredParameterCount ||
            !pattern.returnType || !actual.returnType) {
            return false;
        }
        for (std::size_t i = 0; i < pattern.parameters.size(); ++i) {
            if (!unifyImpl(pattern.parameters[i], actual.parameters[i],
                           substitutions, resolving)) {
                return false;
            }
        }
        return unifyImpl(*pattern.returnType, *actual.returnType,
                         substitutions, resolving);
    }
    return true;
}

} // namespace

Type Type::typeParameter(
    std::string parameterName,
    std::string parameterDisplayName,
    std::vector<TypeConstraint> parameterConstraints
) {
    Type type(TypeKind::TypeParameter);
    type.name = std::move(parameterName);
    type.displayName = parameterDisplayName.empty()
        ? type.name
        : std::move(parameterDisplayName);
    type.constraints = std::move(parameterConstraints);
    return type;
}

Type Type::custom(std::string typeName, std::vector<Type> typeArguments) {
    Type type(TypeKind::Custom);
    type.name = std::move(typeName);
    type.arguments = std::move(typeArguments);
    return type;
}

Type Type::array(const Type& element) {
    Type type(TypeKind::Array);
    type.elementType = std::make_shared<Type>(element);
    return type;
}

Type Type::array(Type&& element) {
    Type type(TypeKind::Array);
    type.elementType = std::make_shared<Type>(std::move(element));
    return type;
}

Type Type::pointer(const Type& element) {
    Type type(TypeKind::Pointer);
    type.elementType = std::make_shared<Type>(element);
    return type;
}

Type Type::pointer(Type&& element) {
    Type type(TypeKind::Pointer);
    type.elementType = std::make_shared<Type>(std::move(element));
    return type;
}

Type Type::function(
    std::vector<Type> functionParameters,
    const Type& result,
    std::vector<GenericTypeParameter> typeParameters
) {
    Type type(TypeKind::Function);
    type.parameters = std::move(functionParameters);
    type.genericParameters = std::move(typeParameters);
    type.requiredParameterCount = type.parameters.size();
    type.returnType = std::make_shared<Type>(result);
    return type;
}

Type Type::function(
    std::vector<Type> functionParameters,
    Type&& result,
    std::vector<GenericTypeParameter> typeParameters
) {
    Type type(TypeKind::Function);
    type.parameters = std::move(functionParameters);
    type.genericParameters = std::move(typeParameters);
    type.requiredParameterCount = type.parameters.size();
    type.returnType = std::make_shared<Type>(std::move(result));
    return type;
}

std::string typeName(const Type& type) {
    switch (type.kind) {
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Character: return "char";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Void: return "void";
        case TypeKind::Null: return "null";
        case TypeKind::Custom: {
            std::string result = type.name;
            if (!type.arguments.empty()) {
                result += "<";
                for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                    if (i != 0) result += ", ";
                    result += typeName(type.arguments[i]);
                }
                result += ">";
            }
            return result;
        }
        case TypeKind::TypeParameter:
            return type.displayName.empty() ? type.name : type.displayName;
        case TypeKind::Array:
            return (type.elementType ? typeName(*type.elementType) : "unknown") + "[]";
        case TypeKind::Pointer:
            return (type.elementType ? typeName(*type.elementType) : "unknown") + "*";
        case TypeKind::Function:
            return "function";
    }
    return "unknown";
}

bool typesEqual(const Type& left, const Type& right) {
    if (left.kind == TypeKind::Unknown || right.kind == TypeKind::Unknown) {
        return true;
    }
    if (left.kind != right.kind) return false;

    if (left.kind == TypeKind::Custom || left.kind == TypeKind::TypeParameter) {
        if (left.name != right.name || left.arguments.size() != right.arguments.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.arguments.size(); ++i) {
            if (!typesEqual(left.arguments[i], right.arguments[i])) return false;
        }
        return true;
    }
    if (left.kind == TypeKind::Array || left.kind == TypeKind::Pointer) {
        return !left.elementType || !right.elementType ||
            typesEqual(*left.elementType, *right.elementType);
    }
    if (left.kind == TypeKind::Function) {
        if (left.parameters.size() != right.parameters.size() ||
            left.requiredParameterCount != right.requiredParameterCount ||
            left.genericParameters.size() != right.genericParameters.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.parameters.size(); ++i) {
            if (!typesEqual(left.parameters[i], right.parameters[i])) return false;
        }
        for (std::size_t i = 0; i < left.genericParameters.size(); ++i) {
            if (left.genericParameters[i].constraints !=
                right.genericParameters[i].constraints) {
                return false;
            }
        }
        return !left.returnType || !right.returnType ||
            typesEqual(*left.returnType, *right.returnType);
    }
    return true;
}

bool isAssignable(const Type& target, const Type& value) {
    return target.kind == TypeKind::Unknown ||
        value.kind == TypeKind::Unknown ||
        typesEqual(target, value) ||
        (value.kind == TypeKind::Null && target.kind == TypeKind::Pointer);
}

Type substitute(const Type& type, const TypeSubstitution& substitutions) {
    std::unordered_set<std::string> expanding;
    return substituteImpl(type, substitutions, expanding);
}

bool unify(const Type& pattern, const Type& actual, TypeSubstitution& substitutions) {
    std::unordered_set<std::string> resolving;
    TypeSubstitution candidate = substitutions;
    if (!unifyImpl(pattern, actual, candidate, resolving)) return false;
    substitutions = std::move(candidate);
    return true;
}

} // namespace zplus::semantic
