#include "Type.hpp"

#include <utility>

namespace zplus::semantic {

Type Type::typeParameter(std::string parameterName) {
    Type type(TypeKind::TypeParameter);
    type.name = std::move(parameterName);
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

Type Type::function(std::vector<Type> functionParameters, const Type& result) {
    Type type(TypeKind::Function);
    type.parameters = std::move(functionParameters);
    type.requiredParameterCount = type.parameters.size();
    type.returnType = std::make_shared<Type>(result);
    return type;
}

Type Type::function(std::vector<Type> functionParameters, Type&& result) {
    Type type(TypeKind::Function);
    type.parameters = std::move(functionParameters);
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
        case TypeKind::TypeParameter: return type.name;
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
            left.requiredParameterCount != right.requiredParameterCount) {
            return false;
        }
        for (std::size_t i = 0; i < left.parameters.size(); ++i) {
            if (!typesEqual(left.parameters[i], right.parameters[i])) return false;
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
    if (type.kind == TypeKind::TypeParameter) {
        const auto found = substitutions.find(type.name);
        return found == substitutions.end() ? type : found->second;
    }

    Type result = type;
    if (type.elementType) {
        result.elementType =
            std::make_shared<Type>(substitute(*type.elementType, substitutions));
    }
    for (Type& argument : result.arguments) {
        argument = substitute(argument, substitutions);
    }
    for (Type& parameter : result.parameters) {
        parameter = substitute(parameter, substitutions);
    }
    if (type.returnType) {
        result.returnType =
            std::make_shared<Type>(substitute(*type.returnType, substitutions));
    }
    return result;
}

bool unify(const Type& pattern, const Type& actual, TypeSubstitution& substitutions) {
    if (pattern.kind == TypeKind::Unknown || actual.kind == TypeKind::Unknown) {
        return true;
    }
    if (pattern.kind == TypeKind::TypeParameter) {
        const auto [found, inserted] =
            substitutions.emplace(pattern.name, actual);
        return inserted || typesEqual(found->second, actual);
    }
    if (pattern.kind != actual.kind) return false;

    if (pattern.kind == TypeKind::Array || pattern.kind == TypeKind::Pointer) {
        return !pattern.elementType || !actual.elementType ||
            unify(*pattern.elementType, *actual.elementType, substitutions);
    }
    if (pattern.kind == TypeKind::Custom) {
        if (pattern.name != actual.name ||
            pattern.arguments.size() != actual.arguments.size()) {
            return false;
        }
        for (std::size_t i = 0; i < pattern.arguments.size(); ++i) {
            if (!unify(pattern.arguments[i], actual.arguments[i], substitutions)) {
                return false;
            }
        }
        return true;
    }
    if (pattern.kind == TypeKind::Function) {
        if (pattern.parameters.size() != actual.parameters.size() ||
            !pattern.returnType || !actual.returnType) {
            return false;
        }
        for (std::size_t i = 0; i < pattern.parameters.size(); ++i) {
            if (!unify(pattern.parameters[i], actual.parameters[i], substitutions)) {
                return false;
            }
        }
        return unify(*pattern.returnType, *actual.returnType, substitutions);
    }
    return true;
}

} // namespace zplus::semantic
