#include "Analyzer.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace zplus::semantic {

namespace {

// ============================================================
// Helper
// ============================================================

template <typename T, typename U>
const T* as(const U* node) {
    return dynamic_cast<const T*>(node);
}

bool hasTypeParameter(const Type& type) {
    if (type.kind == TypeKind::TypeParameter) return true;
    if (type.elementType && hasTypeParameter(*type.elementType)) return true;
    for (const Type& argument : type.arguments) {
        if (hasTypeParameter(argument)) return true;
    }
    for (const Type& parameter : type.parameters) {
        if (hasTypeParameter(parameter)) return true;
    }
    return type.returnType && hasTypeParameter(*type.returnType);
}

} // namespace


// ============================================================
// Scope management
// ============================================================

void Analyzer::enterScope() {

    scopes_.emplace_back();
}

void Analyzer::exitScope() {

    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

Symbol* Analyzer::findSymbol(
    const std::string& name
) {

    for (auto it = scopes_.rbegin();
         it != scopes_.rend();
         ++it) {

        auto found =
            it->find(name);

        if (found != it->end()) {
            return &found->second;
        }
    }

    return nullptr;
}

const Symbol* Analyzer::findSymbol(
    const std::string& name
) const {

    for (auto it = scopes_.rbegin();
         it != scopes_.rend();
         ++it) {

        auto found =
            it->find(name);

        if (found != it->end()) {
            return &found->second;
        }
    }

    return nullptr;
}

const Symbol* Analyzer::findModuleSymbol(
    const std::string& module,
    const std::string& name
) const {
    return findSymbol(module + "::" + name);
}

std::string Analyzer::moduleNameForFile(const std::string& sourceFile) const {
    if (sourceFile.empty()) return {};
    const auto knownModule = moduleNamesByFile_.find(sourceFile);
    if (knownModule != moduleNamesByFile_.end()) return knownModule->second;
    for (const auto& scope : scopes_) {
        for (const auto& [key, symbol] : scope) {
            if (symbol.sourceFile == sourceFile && !symbol.moduleName.empty()) {
                return symbol.moduleName;
            }
        }
    }
    return {};
}

std::string Analyzer::createBindingName() {
    return "__zplus_binding$" + std::to_string(nextBindingId_++);
}

bool Analyzer::declareSymbol(
    const Symbol& symbol
) {

    if (scopes_.empty()) {
        enterScope();
    }

    auto& scope =
        scopes_.back();

    if (
        scope.find(symbol.name) !=
        scope.end()
    ) {
        return false;
    }

    scope.emplace(
        symbol.name,
        symbol
    );

    return true;
}


// ============================================================
// Errors
// ============================================================

void Analyzer::error(
    const zplus::ast::Node& node,
    const std::string& message,
    zplus::DiagnosticCategory category
) {

    error(
        node.location,
        message,
        category
    );
}

void Analyzer::error(
    const zplus::ast::Location& location,
    const std::string& message,
    zplus::DiagnosticCategory category
) {

    errors_.push_back({
        message,
        location.sourceFile,
        static_cast<std::size_t>(location.line),
        static_cast<std::size_t>(location.column),
        "E2001",
        category
    });
}

void Analyzer::warning(
    const zplus::ast::Node& node,
    const std::string& message
) {
    warnings_.emplace_back(
        message,
        node.location.sourceFile,
        static_cast<std::size_t>(node.location.line),
        static_cast<std::size_t>(node.location.column)
    );
}


// ============================================================
// Type helpers
// ============================================================

bool Analyzer::isUnknown(
    const Type& type
) const {

    return type.kind ==
        TypeKind::Unknown;
}

bool Analyzer::isNumeric(
    const Type& type
) const {

    return type.kind == TypeKind::Number ||
        (type.kind == TypeKind::TypeParameter &&
         std::find(type.constraints.begin(), type.constraints.end(),
                   TypeConstraint::Numeric) != type.constraints.end());
}

bool Analyzer::isBoolean(
    const Type& type
) const {

    return type.kind ==
        TypeKind::Boolean;
}

std::string Analyzer::typeName(
    const Type& type
) const {
    return zplus::semantic::typeName(type);
}

bool Analyzer::typesEqual(
    const Type& left,
    const Type& right
) const {
    return zplus::semantic::typesEqual(left, right);
}

bool Analyzer::isAssignable(
    const Type& target,
    const Type& value
) const {
    return zplus::semantic::isAssignable(target, value);
}


// ============================================================
// Resolve AST type
// ============================================================

Type Analyzer::resolveType(
    const zplus::ast::Type& type
) {

    switch (type.kind) {

        case zplus::ast::Type::Kind::Number:
            return Type::number();

        case zplus::ast::Type::Kind::String:
            return Type::string();

        case zplus::ast::Type::Kind::Character:
            return Type::character();

        case zplus::ast::Type::Kind::Boolean:
            return Type::boolean();

        case zplus::ast::Type::Kind::Void:
            return Type::voidType();

        case zplus::ast::Type::Kind::Custom: {
            if (const Type* parameter = findGenericTypeParameter(type.name)) {
                if (!type.arguments.empty()) {
                    error(type, "type parameter '" + type.name +
                                    "' cannot have type arguments",
                          zplus::DiagnosticCategory::Type);
                }
                return *parameter;
            }
            const std::string sourceModule =
                moduleNameForFile(type.location.sourceFile).empty()
                    ? currentModuleName_
                    : moduleNameForFile(type.location.sourceFile);
            const std::string qualifiedName = sourceModule.empty()
                ? type.name
                : sourceModule + "::" + type.name;
            const auto localType = userTypes_.find(qualifiedName);
            auto resolvedDeclaration = localType;
            if (resolvedDeclaration == userTypes_.end()) {
                resolvedDeclaration = userTypes_.find(type.name);
            }
            if (resolvedDeclaration != userTypes_.end()) {
                const Type& declaration = resolvedDeclaration->second;
                if (type.arguments.size() != declaration.arguments.size()) {
                    const std::string expected =
                        declaration.arguments.empty()
                            ? "no type arguments"
                            : "exactly " +
                                  std::to_string(declaration.arguments.size()) +
                                  " type argument(s)";
                    error(type, "type '" + type.name + "' requires " + expected,
                          zplus::DiagnosticCategory::Type);
                    return Type::unknown();
                }
                std::vector<Type> arguments;
                arguments.reserve(type.arguments.size());
                TypeSubstitution substitutions;
                for (std::size_t index = 0; index < type.arguments.size(); ++index) {
                    if (!type.arguments[index]) {
                        arguments.push_back(Type::unknown());
                        continue;
                    }
                    Type argument = resolveType(*type.arguments[index]);
                    if (!unify(declaration.arguments[index], argument, substitutions)) {
                        error(*type.arguments[index],
                              "type argument '" + typeName(argument) +
                                  "' does not satisfy the constraint for '" +
                                  typeName(declaration.arguments[index]) + "'",
                              zplus::DiagnosticCategory::Type);
                    }
                    arguments.push_back(std::move(argument));
                }
                return Type::custom(declaration.name, std::move(arguments));
            }
            if (ambiguousPublicTypeNames_.contains(type.name)) {
                error(type, "type '" + type.name +
                                "' is ambiguous across imported modules");
                return Type::unknown();
            }
            const auto privateType = privateTypeOwners_.find(type.name);
            if (privateType != privateTypeOwners_.end() &&
                privateType->second != sourceModule) {
                error(type, "type '" + type.name + "' is private to module '" +
                                privateType->second + "'");
                return Type::unknown();
            }
            if (!type.arguments.empty()) {
                error(type, "unknown generic type '" + type.name + "'",
                      zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
            return Type::custom(type.name);
        }

        case zplus::ast::Type::Kind::Array:
            if (!type.elementType) {
                return Type(TypeKind::Array);
            }
            return Type::array(resolveType(*type.elementType));

        case zplus::ast::Type::Kind::Pointer:
            if (!type.elementType) {
                return Type(TypeKind::Pointer);
            }
            return Type::pointer(resolveType(*type.elementType));

        default:
            return Type::unknown();
    }
}

void Analyzer::pushGenericTypeParameters(
    const std::vector<zplus::ast::GenericParameter>& parameters,
    const std::string& owner
) {
    std::unordered_map<std::string, Type> scope;
    std::vector<GenericTypeParameter> orderedParameters;
    orderedParameters.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        std::vector<TypeConstraint> constraints;
        if (parameter.constraint == "number" ||
            parameter.constraint == "numeric") {
            constraints.push_back(TypeConstraint::Numeric);
        } else if (parameter.constraint == "equatable" ||
                   parameter.constraint == "equality") {
            constraints.push_back(TypeConstraint::Equatable);
        } else if (!parameter.constraint.empty()) {
            error(parameter.location,
                  "unknown generic constraint '" + parameter.constraint +
                      "'; supported constraints are 'number' and 'equatable'",
                  zplus::DiagnosticCategory::Type);
        }

        parameter.bindingName = owner + "$type$" + parameter.name;
        Type type = Type::typeParameter(
            parameter.bindingName, parameter.name, std::move(constraints));
        orderedParameters.push_back(
            {type.name, parameter.name, type.constraints});
        if (!scope.emplace(parameter.name, std::move(type)).second) {
            error(parameter.location,
                  "duplicate generic type parameter '" + parameter.name + "'",
                  zplus::DiagnosticCategory::Type);
        }
    }
    genericTypeScopes_.push_back(std::move(scope));
    genericParameterScopes_.push_back(std::move(orderedParameters));
}

void Analyzer::popGenericTypeParameters() {
    if (!genericTypeScopes_.empty()) genericTypeScopes_.pop_back();
    if (!genericParameterScopes_.empty()) genericParameterScopes_.pop_back();
}

std::vector<GenericTypeParameter> Analyzer::activeGenericTypeParameters() const {
    std::vector<GenericTypeParameter> parameters;
    for (const auto& scope : genericParameterScopes_) {
        parameters.insert(parameters.end(), scope.begin(), scope.end());
    }
    return parameters;
}

const Type* Analyzer::findGenericTypeParameter(const std::string& name) const {
    for (auto scope = genericTypeScopes_.rbegin();
         scope != genericTypeScopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) return &found->second;
    }
    return nullptr;
}


// ============================================================
// Program analysis
// ============================================================

Analyzer::Analyzer()
    : currentReturnType_(Type::voidType()) {
}

bool Analyzer::analyze(
    const zplus::ast::Program& program
) {

    scopes_.clear();
    genericTypeScopes_.clear();
    genericParameterScopes_.clear();
    errors_.clear();
    warnings_.clear();
    builtinRegistry_.clear();
    nextBindingId_ = 0;

    userTypes_.clear();
    structDeclarations_.clear();
    classDeclarations_.clear();
    enumDeclarations_.clear();
    currentClassName_.clear();
    moduleNamesByFile_.clear();
    privateTypeOwners_.clear();
    ambiguousPublicTypeNames_.clear();

    loopDepth_ = 0;
    functionDepth_ = 0;

    currentReturnType_ =
        Type::voidType();

    currentFunctionName_.clear();
    expectedExpressionType_.reset();

    enterScope();

    // --------------------------------------------------------
    // Built-ins
    // --------------------------------------------------------

    auto registerBuiltin = [this](
        const std::string& name,
        std::vector<Type> parameters,
        Type result,
        std::size_t requiredParameterCount,
        bool variadic
    ) {
        Symbol symbol;
        symbol.name = name;
        symbol.originalName = name;
        symbol.type = Type::function(std::move(parameters), result);
        symbol.type.requiredParameterCount = requiredParameterCount;
        builtinRegistry_.add(name, symbol.type, variadic);
        symbol.isFunction = true;
        symbol.initialized = true;
        symbol.isBuiltin = true;
        (void)declareSymbol(symbol);
    };
    auto declareBuiltin = [&registerBuiltin](
        const std::string& name,
        std::vector<Type> parameters,
        Type result
    ) {
        const std::size_t requiredParameterCount = parameters.size();
        registerBuiltin(name, std::move(parameters), std::move(result),
                        requiredParameterCount, false);
    };
    auto declareOptionalPromptBuiltin = [&registerBuiltin](
        const std::string& name,
        Type result
    ) {
        registerBuiltin(name, {Type::string()}, std::move(result), 0, false);
    };
    auto declareVariadicBuiltin = [&registerBuiltin](
        const std::string& name,
        std::vector<Type> parameters,
        Type result,
        std::size_t minimumArgumentCount
    ) {
        registerBuiltin(name, std::move(parameters), std::move(result),
                        minimumArgumentCount, true);
    };
    const Type number = Type::number();
    const Type string = Type::string();
    const Type boolean = Type::boolean();
    const Type voidType = Type::voidType();
    declareVariadicBuiltin("say", {Type::unknown()}, voidType, 1);
    declareVariadicBuiltin("printz", {Type::unknown()}, voidType, 1);
    declareVariadicBuiltin("printm", {Type::unknown()}, voidType, 1);
    declareVariadicBuiltin("println", {Type::unknown()}, voidType, 0);
    declareVariadicBuiltin("print", {Type::unknown()}, voidType, 0);
    declareBuiltin("time", {}, number);
    declareOptionalPromptBuiltin("read", string);
    declareOptionalPromptBuiltin("read_number", number);
    declareOptionalPromptBuiltin("read_integer", number);
    declareOptionalPromptBuiltin("read_boolean", boolean);
    declareBuiltin("string", {string}, string);
    declareBuiltin("to_string", {number}, string);
    declareBuiltin("__zgraphics_createWindow", {string, number, number}, number);
    const Type element = Type::typeParameter("T");
    const Type elements = Type::array(element);
    declareBuiltin("length", {elements}, number);
    declareBuiltin("push", {elements, element}, voidType);
    declareBuiltin("pop", {elements}, element);
    declareBuiltin("clear", {elements}, voidType);
    declareBuiltin("insert", {elements, number, element}, voidType);
    declareBuiltin("remove", {elements, number}, element);
    declareBuiltin("contains", {elements, element}, boolean);
    declareBuiltin("__zmemory_heapUsed", {}, number);
    declareBuiltin("__zmemory_heapObjects", {}, number);
    declareBuiltin("__zmemory_collect", {}, voidType);
    declareBuiltin("__zmemory_collectionCount", {}, number);
    declareBuiltin("__zmemory_setThreshold", {number}, voidType);
    for (const char* operation : {
             "abs", "round", "floor", "ceil", "sqrt", "sin", "cos", "tan",
             "asin", "acos", "atan", "log", "log10", "exp"}) {
        declareBuiltin(std::string("__zstdlib_math_") + operation, {number}, number);
    }
    for (const char* operation : {"min", "max", "pow"}) {
        declareBuiltin(std::string("__zstdlib_math_") + operation, {number, number}, number);
    }
    declareBuiltin("__zstdlib_math_pi", {}, number);
    declareBuiltin("__zstdlib_math_e", {}, number);
    for (const char* operation : {"upper", "lower", "trim", "reverse"}) {
        declareBuiltin(std::string("__zstdlib_string_") + operation, {string}, string);
    }
    for (const char* operation : {"contains", "startsWith", "endsWith"}) {
        declareBuiltin(std::string("__zstdlib_string_") + operation,
                       {string, string}, boolean);
    }
    declareBuiltin("__zstdlib_string_replace", {string, string, string}, string);
    declareBuiltin("__zstdlib_string_substring", {string, number, number}, string);
    declareBuiltin("__zstdlib_string_repeat", {string, number}, string);
    declareBuiltin("__zstdlib_string_length", {string}, number);
    declareBuiltin("__zstdlib_string_split", {string, string}, Type::array(string));
    declareBuiltin("__zstdlib_string_join", {Type::array(string), string}, string);
    for (const char* operation : {"sum", "average", "min", "max"}) {
        declareBuiltin(std::string("__zstdlib_collections_") + operation,
                       {Type::array(number)}, number);
    }
    for (const char* operation : {"reverse", "sort", "unique"}) {
        declareBuiltin(std::string("__zstdlib_collections_") + operation,
                       {Type::array(number)}, Type::array(number));
    }
    declareBuiltin("__zstdlib_collections_contains", {Type::array(number), number}, boolean);
    declareBuiltin("__zstdlib_collections_count", {Type::array(number), number}, number);
    declareBuiltin("__zstdlib_collections_first", {Type::array(number)}, number);
    declareBuiltin("__zstdlib_collections_last", {Type::array(number)}, number);
    declareBuiltin("__zstdlib_collections_isEmpty", {Type::array(number)}, boolean);
    declareBuiltin("__zstdlib_random_integer", {number, number}, number);
    declareBuiltin("__zstdlib_random_number", {number, number}, number);
    declareBuiltin("__zstdlib_random_boolean", {}, boolean);
    declareBuiltin("__zstdlib_random_choice", {Type::array(number)}, number);
    declareBuiltin("__zstdlib_random_shuffle", {Type::array(number)}, Type::array(number));
    declareBuiltin("__zstdlib_random_seed", {number}, voidType);
    for (const char* operation : {"normalize", "extension", "filename", "directory",
                                  "absolute", "isAbsolute"}) {
        declareBuiltin(std::string("__zstdlib_path_") + operation,
                       {string}, operation == std::string("isAbsolute") ? boolean : string);
    }
    declareBuiltin("__zstdlib_path_join", {string, string}, string);
    declareBuiltin("__zstdlib_file_read", {string}, string);
    declareBuiltin("__zstdlib_file_readLines", {string}, Type::array(string));
    declareBuiltin("__zstdlib_file_write", {string, string}, voidType);
    declareBuiltin("__zstdlib_file_writeLines", {string, Type::array(string)}, voidType);
    declareBuiltin("__zstdlib_file_append", {string, string}, voidType);
    for (const char* operation : {"exists", "delete", "createDirectory", "isDirectory"}) {
        declareBuiltin(std::string("__zstdlib_file_") + operation, {string}, boolean);
    }
    declareBuiltin("__zstdlib_file_scheduleDeleteOnReboot", {string}, boolean);
    declareBuiltin("__zstdlib_file_listDirectory", {string}, Type::array(string));
    declareBuiltin("__zstdlib_file_size", {string}, number);
    declareBuiltin("__zstdlib_file_copyFile", {string, string, boolean}, boolean);
    declareBuiltin("__zstdlib_file_moveFile", {string, string, boolean}, boolean);
    declareBuiltin("__zstdlib_file_removeEmptyDirectory", {string}, boolean);
    declareBuiltin("__zstdlib_crypto_sha256File", {string}, string);
    declareBuiltin("__zstdlib_process_run",
        {string, Type::array(string), string, number}, string);
    for (const char* operation : {"platform", "architecture", "osVersion", "executablePath"}) {
        declareBuiltin(std::string("__zstdlib_system_") + operation, {}, string);
    }
    declareBuiltin("__zstdlib_system_availableSpace", {string}, number);
    declareBuiltin("__zstdlib_system_selectFolder", {string, string}, string);
    declareBuiltin("__zstdlib_system_messageBox",
        {string, string, string, string}, number);
    declareBuiltin("__zstdlib_registry_read", {string, string}, string);
    declareBuiltin("__zstdlib_registry_valueKind", {string, string}, string);
    declareBuiltin("__zstdlib_registry_write",
        {string, string, string, string}, boolean);
    declareBuiltin("__zstdlib_registry_deleteValue", {string, string}, boolean);
    declareBuiltin("__zstdlib_system_createShortcut",
        {string, string, string, string}, boolean);
    declareBuiltin("__zstdlib_system_shortcutTarget", {string}, string);
    declareBuiltin("__zstdlib_system_shortcutArguments", {string}, string);
    declareBuiltin("__zstdlib_system_removeShortcutIfMatches",
        {string, string, string}, boolean);
    declareBuiltin("__zstdlib_env_get", {string}, string);
    declareBuiltin("__zstdlib_env_set", {string, string}, voidType);
    declareBuiltin("__zstdlib_env_exists", {string}, boolean);
    declareBuiltin("__zstdlib_env_homeDirectory", {}, string);
    declareBuiltin("__zstdlib_env_currentDirectory", {}, string);
    declareBuiltin("__zstdlib_time_now", {}, number);
    declareBuiltin("__zstdlib_time_format", {number, string}, string);
    declareBuiltin("__zstdlib_time_parse", {string, string}, number);
    for (const char* part : {"year", "month", "day", "hour", "minute", "second"}) {
        declareBuiltin(std::string("__zstdlib_time_") + part, {number}, number);
    }
    declareBuiltin("__zstdlib_time_sleep", {number}, voidType);
    declareBuiltin("__zstdlib_args_count", {}, number);
    declareBuiltin("__zstdlib_args_get", {number}, string);
    declareBuiltin("__zstdlib_args_has", {string}, boolean);
    declareBuiltin("__zstdlib_args_all", {}, Type::array(string));
    for (const char* operation : {"debug", "info", "warning", "error"}) {
        declareBuiltin(std::string("__zstdlib_log_") + operation, {string}, voidType);
    }
    declareBuiltin("__zstdlib_log_setLevel", {string}, voidType);
    declareBuiltin("__zstdlib_log_setOutput", {string}, voidType);
    declareBuiltin("__zjson_valid", {string}, boolean);
    declareBuiltin("__zjson_compact", {string}, string);
    declareBuiltin("__zjson_quote", {string}, string);
    declareBuiltin("__zjson_get", {string, string}, string);
    declareBuiltin("__zjson_getString", {string, string}, string);
    declareBuiltin("__zjson_getNumber", {string, string}, number);
    declareBuiltin("__zjson_getBoolean", {string, string}, boolean);
    declareBuiltin("__zjson_arrayLength", {string, string}, number);
    declareBuiltin("__zjson_arrayGet", {string, string, number}, string);
    declareBuiltin("__zjson_kind", {string, string}, string);
    declareBuiltin("__zhttp_serverCreate", {string, number}, number);
    declareBuiltin("__zhttp_serverStart", {number}, boolean);
    declareBuiltin("__zhttp_serverRunning", {number}, boolean);
    declareBuiltin("__zhttp_serverPort", {number}, number);
    declareBuiltin("__zhttp_serverNext", {number, number}, string);
    declareBuiltin("__zhttp_serverRespond", {number, number, number, string, string}, boolean);
    declareBuiltin("__zhttp_serverRespondBinary",
        {number, number, number, string, string}, boolean);
    declareBuiltin("__zhttp_serverStop", {number}, voidType);
    declareBuiltin("__zhttp_serverConfigure", {number, number, number}, voidType);
    declareBuiltin("__zhttp_configureCors", {number, string, string, string}, voidType);
    declareBuiltin("__zhttp_routeParams", {string, string}, string);
    declareBuiltin("__zhttp_queryValue", {string, string}, string);
    declareBuiltin("__zhttp_requestHeader", {string, string}, string);
    declareBuiltin("__zhttp_clientRequest",
        {string, string, string, string, number}, string);
    declareBuiltin("__zhttp_clientRequestBinary",
        {string, string, string, string, number}, string);
    declareBuiltin("__zweb_urlEncode", {string}, string);
    declareBuiltin("__zweb_urlDecode", {string}, string);
    declareBuiltin("__zweb_parseUrl", {string}, string);
    declareBuiltin("__zweb_buildUrl",
        {string, string, string, string, string, string}, string);
    declareBuiltin("__zweb_parseForm", {string}, string);
    declareBuiltin("__zweb_parseCookies", {string}, string);
    declareBuiltin("__zweb_cookieHeader",
        {string, string, number, string, string, boolean, boolean, string}, string);
    declareBuiltin("__zweb_cookieHeaderExpires",
        {string, string, number, string, string, string, boolean, boolean, string}, string);
    declareBuiltin("__zweb_securityHeaders", {string}, string);
    declareBuiltin("__zweb_routeAdd", {string, string, string, string}, string);
    declareBuiltin("__zweb_routeGroup", {string, string}, string);
    declareBuiltin("__zweb_routeMatch", {string, string, string}, string);
    declareBuiltin("__zweb_routeAddConfigured",
        {string, string, string, string, string, string}, string);
    declareBuiltin("__zweb_routeUrl", {string, string, string}, string);
    declareBuiltin("__zweb_response", {number, string, string}, string);
    declareBuiltin("__zweb_jsonResponse", {number, string, string}, string);
    declareBuiltin("__zweb_parseJsonBody", {string}, string);
    declareBuiltin("__zweb_notFoundResponse", {string}, string);
    declareBuiltin("__zweb_methodNotAllowedResponse", {string, string}, string);
    declareBuiltin("__zweb_mimeType", {string}, string);
    declareBuiltin("__zweb_staticFile", {string, string, number}, string);
    declareBuiltin("__zweb_respondFile",
        {number, number, number, string, string, string, number}, boolean);
    declareBuiltin("__zweb_respondStatic",
        {number, number, number, string, string, string, string, number, string}, boolean);
    declareBuiltin("__zweb_parseMultipart", {string, number}, string);
    declareBuiltin("__zweb_saveUpload",
        {string, number, string, string, number}, string);
    declareBuiltin("__zweb_validateForm", {string, string}, string);
    declareBuiltin("__zweb_formHas", {string, string}, boolean);
    declareBuiltin("__zweb_formNumber", {string, string}, number);
    declareBuiltin("__zweb_loadTemplate", {string, string, number}, string);
    declareBuiltin("__zweb_renderTemplate", {string, string}, string);
    declareBuiltin("__zweb_middlewareUse", {string, string, string}, string);
    declareBuiltin("__zweb_middlewareBefore", {string, string}, string);
    declareBuiltin("__zweb_middlewareAfter", {string, string, string}, string);
    declareBuiltin("__zweb_sessionStoreCreate", {}, number);
    declareBuiltin("__zweb_sessionStoreClose", {number}, voidType);
    declareBuiltin("__zweb_sessionCreate", {number, number}, string);
    declareBuiltin("__zweb_sessionSet", {number, string, string, string}, boolean);
    declareBuiltin("__zweb_sessionGet", {number, string, string}, string);
    declareBuiltin("__zweb_sessionHas", {number, string, string}, boolean);
    declareBuiltin("__zweb_sessionInvalidate", {number, string}, boolean);
    declareBuiltin("__zweb_sessionRotate", {number, string, number}, string);
    declareBuiltin("__zweb_sessionCsrfToken", {number, string}, string);
    declareBuiltin("__zweb_sessionValidateCsrf", {number, string, string}, boolean);
    declareBuiltin("__zweb_sessionCookie",
        {string, string, number, boolean, string}, string);
    declareBuiltin("__zsqlite_open", {string}, number);
    declareBuiltin("__zsqlite_close", {number}, voidType);
    declareBuiltin("__zsqlite_execute", {number, string, string}, number);
    declareBuiltin("__zsqlite_query", {number, string, string}, string);
    declareBuiltin("__zsqlite_changes", {number}, number);
    declareBuiltin("__zaudio_initialize", {}, voidType);
    declareBuiltin("__zaudio_initialized", {}, boolean);
    declareBuiltin("__zaudio_shutdown", {}, voidType);
    declareBuiltin("__zaudio_loadMusic", {string}, number);
    declareBuiltin("__zaudio_loadSound", {string}, number);
    declareBuiltin("__zaudio_unload", {number}, voidType);
    declareBuiltin("__zaudio_unloadAll", {}, voidType);
    declareBuiltin("__zaudio_play", {number}, voidType);
    declareBuiltin("__zaudio_pause", {number}, voidType);
    declareBuiltin("__zaudio_resume", {number}, voidType);
    declareBuiltin("__zaudio_stop", {number}, voidType);
    declareBuiltin("__zaudio_isPlaying", {number}, boolean);
    declareBuiltin("__zaudio_isPaused", {number}, boolean);
    declareBuiltin("__zaudio_isStopped", {number}, boolean);
    declareBuiltin("__zaudio_setVolume", {number, number}, voidType);
    declareBuiltin("__zaudio_getVolume", {number}, number);
    declareBuiltin("__zaudio_setLooping", {number, boolean}, voidType);
    declareBuiltin("__zaudio_isLooping", {number}, boolean);
    declareBuiltin("__zaudio_setPlaybackPosition", {number, number}, voidType);
    declareBuiltin("__zaudio_getPlaybackPosition", {number}, number);
    declareBuiltin("__zaudio_getDuration", {number}, number);
    declareBuiltin("__zaudio_setMasterVolume", {number}, voidType);
    declareBuiltin("__zaudio_getMasterVolume", {}, number);
    declareBuiltin("__zaudio_mute", {}, voidType);
    declareBuiltin("__zaudio_unmute", {}, voidType);
    declareBuiltin("__zaudio_isMuted", {}, boolean);
    declareBuiltin("__zaudio_deviceCount", {}, number);
    declareBuiltin("__zaudio_deviceName", {number}, string);
    declareBuiltin("__zaudio_defaultDevice", {}, string);
    declareBuiltin("__zaudio_selectDevice", {string}, voidType);
    declareBuiltin("__zphysics_createWorld", {number, number}, number);
    declareBuiltin("__zphysics_destroyWorld", {number}, voidType);
    declareBuiltin("__zphysics_setGravity", {number, number, number}, voidType);
    declareBuiltin("__zphysics_getGravityX", {number}, number);
    declareBuiltin("__zphysics_getGravityY", {number}, number);
    declareBuiltin("__zphysics_setFixedTimeStep", {number, number}, voidType);
    declareBuiltin("__zphysics_setIterations", {number, number}, voidType);
    declareBuiltin("__zphysics_setPaused", {number, boolean}, voidType);
    declareBuiltin("__zphysics_isPaused", {number}, boolean);
    declareBuiltin("__zphysics_update", {number, number}, voidType);
    declareBuiltin("__zphysics_bodyCount", {number}, number);
    declareBuiltin("__zphysics_shapeCount", {number}, number);
    declareBuiltin("__zphysics_contactCount", {number}, number);
    declareBuiltin("__zphysics_createBody", {number, string, number, number}, number);
    declareBuiltin("__zphysics_destroyBody", {number}, voidType);
    declareBuiltin("__zphysics_setPosition", {number, number, number}, voidType);
    declareBuiltin("__zphysics_getPositionX", {number}, number);
    declareBuiltin("__zphysics_getPositionY", {number}, number);
    declareBuiltin("__zphysics_setRotation", {number, number}, voidType);
    declareBuiltin("__zphysics_getRotation", {number}, number);
    declareBuiltin("__zphysics_setVelocity", {number, number, number}, voidType);
    declareBuiltin("__zphysics_getVelocityX", {number}, number);
    declareBuiltin("__zphysics_getVelocityY", {number}, number);
    declareBuiltin("__zphysics_setAngularVelocity", {number, number}, voidType);
    declareBuiltin("__zphysics_getAngularVelocity", {number}, number);
    declareBuiltin("__zphysics_setMass", {number, number}, voidType);
    declareBuiltin("__zphysics_getMass", {number}, number);
    declareBuiltin("__zphysics_setLinearDamping", {number, number}, voidType);
    declareBuiltin("__zphysics_setAngularDamping", {number, number}, voidType);
    declareBuiltin("__zphysics_applyForce", {number, number, number}, voidType);
    declareBuiltin("__zphysics_applyImpulse", {number, number, number}, voidType);
    declareBuiltin("__zphysics_applyTorque", {number, number}, voidType);
    declareBuiltin("__zphysics_createCircle", {number, number, number}, number);
    declareBuiltin("__zphysics_createBox", {number, number, number, number}, number);
    declareBuiltin("__zphysics_createPolygon", {number, number, string}, number);
    declareBuiltin("__zphysics_createSegment",
        {number, number, number, number, number, number}, number);
    declareBuiltin("__zphysics_destroyShape", {number}, voidType);
    declareBuiltin("__zphysics_setCollisionCategory", {number, number}, voidType);
    declareBuiltin("__zphysics_setCollisionMask", {number, number}, voidType);
    declareBuiltin("__zphysics_setSensor", {number, boolean}, voidType);
    declareBuiltin("__zphysics_setFriction", {number, number}, voidType);
    declareBuiltin("__zphysics_setRestitution", {number, number}, voidType);
    declareBuiltin("__zphysics_setDensity", {number, number}, voidType);
    declareBuiltin("__zphysics_isTouching", {number, number}, boolean);
    declareBuiltin("__zphysics_collisionStarted", {number, number, number}, boolean);
    declareBuiltin("__zphysics_collisionEnded", {number, number, number}, boolean);
    declareBuiltin("__zphysics_createDistanceJoint",
        {number, number, number, number}, number);
    declareBuiltin("__zphysics_createRevoluteJoint",
        {number, number, number, number, number}, number);
    declareBuiltin("__zphysics_createWeldJoint",
        {number, number, number, number, number}, number);
    declareBuiltin("__zphysics_createMouseJoint",
        {number, number, number, number, number}, number);
    declareBuiltin("__zphysics_setMouseJointTarget", {number, number, number}, voidType);
    declareBuiltin("__zphysics_destroyJoint", {number}, voidType);
    declareBuiltin("__zphysics_raycast",
        {number, number, number, number, number}, number);
    declareBuiltin("__zphysics_raycastFraction", {number}, number);
    declareBuiltin("__zphysics_raycastPointX", {number}, number);
    declareBuiltin("__zphysics_raycastPointY", {number}, number);
    declareBuiltin("__zphysics_raycastNormalX", {number}, number);
    declareBuiltin("__zphysics_raycastNormalY", {number}, number);
    declareBuiltin("__zphysics_pointQuery", {number, number, number}, number);
    declareBuiltin("__zphysics_areaQuery",
        {number, number, number, number, number}, number);
    declareBuiltin("__zphysics_queryBodyAt", {number, number}, number);
    declareBuiltin("__zphysics_setQueryFilter", {number, number, number}, voidType);
    declareBuiltin("__zgraphics_numberText", {number}, string);
    declareBuiltin("__zgraphics_setPosition", {number, number, number}, voidType);
    declareBuiltin("__zgraphics_resize", {number, number, number}, voidType);
    declareBuiltin("__zgraphics_setTitle", {number, string}, voidType);
    declareBuiltin("__zgraphics_setBackground", {number, string}, voidType);
    declareBuiltin("__zgraphics_show", {number}, voidType);
    declareBuiltin("__zgraphics_hide", {number}, voidType);
    declareBuiltin("__zgraphics_minimize", {number}, voidType);
    declareBuiltin("__zgraphics_maximize", {number}, voidType);
    declareBuiltin("__zgraphics_restore", {number}, voidType);
    declareBuiltin("__zgraphics_focus", {number}, voidType);
    declareBuiltin("__zgraphics_close", {number}, voidType);
    declareBuiltin("__zgraphics_isOpen", {number}, boolean);
    declareBuiltin("__zgraphics_run", {number}, number);
    declareBuiltin("__zgraphics_drawText", {number, string, number, number, string, number}, voidType);
    declareBuiltin("__zgraphics_drawImage",
                   {number, string, number, number, number, number}, voidType);
    declareBuiltin("__zgraphics_drawLine",
                   {number, number, number, number, number, string, number}, voidType);
    declareBuiltin("__zgraphics_drawRectangle",
                   {number, number, number, number, number, string, boolean, number}, voidType);
    declareBuiltin("__zgraphics_drawCircle",
                   {number, number, number, number, string, boolean, number}, voidType);
    declareBuiltin("__zgraphics_clear", {number}, voidType);
    declareBuiltin("__zgraphics_pollEvent", {number}, number);
    declareBuiltin("__zgraphics_waitEvent", {number}, number);
    declareBuiltin("__zgraphics_eventX", {number}, number);
    declareBuiltin("__zgraphics_eventY", {number}, number);
    declareBuiltin("__zgraphics_eventKey", {number}, number);
    declareBuiltin("__zgraphics_eventText", {number}, string);
    declareBuiltin("__zgraphics_update", {number}, voidType);
    declareBuiltin("__zgraphics_setTargetFPS", {number, number}, voidType);
    declareBuiltin("__zgraphics_deltaTimeMs", {number}, number);
    declareBuiltin("__zgraphics_framesPerSecond", {number}, number);
    declareBuiltin("__zgraphics_keyDown", {number, number}, boolean);
    declareBuiltin("__zgraphics_keyPressed", {number, number}, boolean);
    declareBuiltin("__zgraphics_keyReleased", {number, number}, boolean);
    declareBuiltin("__zgraphics_mouseX", {number}, number);
    declareBuiltin("__zgraphics_mouseY", {number}, number);
    declareBuiltin("__zgraphics_mouseButtonDown", {number, number}, boolean);
    declareBuiltin("__zgraphics_mouseButtonPressed", {number, number}, boolean);
    declareBuiltin("__zgraphics_mouseButtonReleased", {number, number}, boolean);

    // --------------------------------------------------------
    // Predeclare user types/functions
    // --------------------------------------------------------

    predeclareTypes(
        program.statements
    );
    predeclareFunctions(
        program.statements
    );

    // --------------------------------------------------------
    // Analyze statements
    // --------------------------------------------------------

    for (const auto& statement :
         program.statements) {

        if (statement) {
            analyzeStatement(
                *statement
            );
        }
    }

    exitScope();

    return errors_.empty();
}


// ============================================================
// Predeclare types
// ============================================================

void Analyzer::predeclareTypes(
    const std::vector<zplus::ast::Stmt>& statements
) {

    std::unordered_map<std::string, std::size_t> publicTypeCounts;
    for (const auto& statement : statements) {
        if (!statement) continue;
        const zplus::ast::Statement* declaration = statement.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }
        if (const auto* structure =
                as<zplus::ast::StructDeclaration>(declaration);
            structure && !structure->moduleName.empty() && structure->isPublic) {
            ++publicTypeCounts[structure->name];
        }
        if (const auto* klass =
                as<zplus::ast::ClassDeclaration>(declaration);
            klass && !klass->moduleName.empty() && klass->isPublic) {
            ++publicTypeCounts[klass->name];
        }
        if (const auto* enumeration =
                as<zplus::ast::EnumDeclaration>(declaration);
            enumeration && !enumeration->moduleName.empty() && enumeration->isPublic) {
            ++publicTypeCounts[enumeration->name];
        }
    }

    for (const auto& statement :
         statements) {

        if (!statement) {
            continue;
        }

        const zplus::ast::Statement* declaration = statement.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }

        if (const auto* node =
                as<
                    zplus::ast::StructDeclaration
                >(declaration)) {

            const std::string key = node->moduleName.empty()
                ? node->name
                : node->moduleName + "::" + node->name;
            structDeclarations_[key] = node;
            pushGenericTypeParameters(node->genericParameters, key);
            std::vector<Type> genericArguments;
            genericArguments.reserve(node->genericParameters.size());
            for (const auto& parameter : node->genericParameters) {
                if (const Type* resolved =
                        findGenericTypeParameter(parameter.name)) {
                    genericArguments.push_back(*resolved);
                }
            }
            const Type declarationType = Type::custom(key, genericArguments);
            node->resolvedType = declarationType;
            popGenericTypeParameters();
            if (!userTypes_.emplace(key, declarationType).second) {
                error(*node, "duplicate type declaration '" + node->name + "'");
            }
            if (!node->moduleName.empty()) {
                moduleNamesByFile_[node->location.sourceFile] = node->moduleName;
                if (node->isPublic && publicTypeCounts[node->name] == 1) {
                    structDeclarations_[node->name] = node;
                    if (!userTypes_.emplace(node->name, declarationType).second) {
                        error(*node, "duplicate type declaration '" + node->name + "'");
                    }
                } else if (!node->isPublic) {
                    privateTypeOwners_[node->name] = node->moduleName;
                } else {
                    ambiguousPublicTypeNames_.insert(node->name);
                }
            }

            continue;
        }

        if (const auto* node =
                as<
                    zplus::ast::ClassDeclaration
                >(declaration)) {

            const std::string key = node->moduleName.empty()
                ? node->name
                : node->moduleName + "::" + node->name;
            classDeclarations_[key] = node;
            pushGenericTypeParameters(node->genericParameters, key);
            std::vector<Type> genericArguments;
            genericArguments.reserve(node->genericParameters.size());
            for (const auto& parameter : node->genericParameters) {
                if (const Type* resolved =
                        findGenericTypeParameter(parameter.name)) {
                    genericArguments.push_back(*resolved);
                }
            }
            const Type declarationType = Type::custom(key, genericArguments);
            node->resolvedType = declarationType;
            popGenericTypeParameters();
            if (!userTypes_.emplace(key, declarationType).second) {
                error(*node, "duplicate type declaration '" + node->name + "'");
            }
            if (!node->moduleName.empty()) {
                moduleNamesByFile_[node->location.sourceFile] = node->moduleName;
                if (node->isPublic && publicTypeCounts[node->name] == 1) {
                    classDeclarations_[node->name] = node;
                    if (!userTypes_.emplace(node->name, declarationType).second) {
                        error(*node, "duplicate type declaration '" + node->name + "'");
                    }
                } else if (!node->isPublic) {
                    privateTypeOwners_[node->name] = node->moduleName;
                } else {
                    ambiguousPublicTypeNames_.insert(node->name);
                }
            }

            continue;
        }

        if (const auto* node =
                as<
                    zplus::ast::EnumDeclaration
                >(declaration)) {

            const std::string key = node->moduleName.empty()
                ? node->name
                : node->moduleName + "::" + node->name;
            enumDeclarations_[key] = node;
            if (!userTypes_.emplace(key, Type::custom(key)).second) {
                error(*node, "duplicate type declaration '" + node->name + "'");
            }
            if (!node->moduleName.empty()) {
                moduleNamesByFile_[node->location.sourceFile] = node->moduleName;
                if (node->isPublic && publicTypeCounts[node->name] == 1) {
                    enumDeclarations_[node->name] = node;
                    if (!userTypes_.emplace(node->name, Type::custom(key)).second) {
                        error(*node, "duplicate type declaration '" + node->name + "'");
                    }
                } else if (!node->isPublic) {
                    privateTypeOwners_[node->name] = node->moduleName;
                } else {
                    ambiguousPublicTypeNames_.insert(node->name);
                }
            }
        }
    }
}


// ============================================================
// Predeclare functions
// ============================================================

void Analyzer::predeclareFunctions(
    const std::vector<zplus::ast::Stmt>& statements
) {

    for (const auto& statement :
         statements) {

        if (!statement) {
            continue;
        }

        const zplus::ast::Statement* declaration = statement.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }

        const auto* function =
            as<
                zplus::ast::FunctionDeclaration
            >(declaration);

        if (!function) {
            if (const auto* namespaceNode =
                    as<zplus::ast::NamespaceDeclaration>(declaration)) {
                predeclareFunctions(namespaceNode->body);
            }
            continue;
        }

        const std::string functionName = function->internalName.empty()
            ? function->name
            : function->internalName;
        pushGenericTypeParameters(
            function->genericParameters, functionName);
        std::vector<Type> parameters;

        for (const auto& parameter :
             function->parameters) {

            if (
                parameter &&
                parameter->type
            ) {

                parameters.push_back(
                    resolveType(
                        *parameter->type
                    )
                );

            } else {

                parameters.push_back(
                    Type::unknown()
                );
            }
        }

        Type returnType =
            function->returnType
                ? resolveType(
                    *function->returnType
                )
                : Type::voidType();

        Symbol symbol;
        symbol.name = function->internalName.empty()
            ? function->name
            : function->internalName;
        symbol.originalName = function->name;
        symbol.moduleName = function->moduleName;
        symbol.sourceFile = function->location.sourceFile;
        symbol.isPublic = function->moduleName.empty() || function->isPublic;

        symbol.type =
            Type::function(
                std::move(parameters),
                returnType,
                activeGenericTypeParameters()
            );
        symbol.type.requiredParameterCount = 0;
        for (const auto& parameter : function->parameters) {
            if (parameter && parameter->defaultValue) {
                break;
            }
            ++symbol.type.requiredParameterCount;
        }

        symbol.isFunction = true;
        symbol.initialized = true;

        if (!declareSymbol(symbol)) {

            error(
                *function,
                "duplicate declaration '" +
                function->name +
                "'"
            );
        }

        if (!function->moduleName.empty() && function->isPublic) {
            std::size_t publicNameCount = 0;
            for (const auto& candidate : statements) {
                const auto* other = candidate
                    ? as<zplus::ast::FunctionDeclaration>(candidate.get())
                    : nullptr;
                if (other && other->isPublic &&
                    other->name == function->name) {
                    ++publicNameCount;
                }
            }
            if (publicNameCount == 1) {
                Symbol alias = symbol;
                alias.name = function->name;
                const Symbol* existing = findSymbol(alias.name);
                if ((!existing || !existing->isBuiltin) &&
                    !declareSymbol(alias)) {
                    error(*function, "ambiguous unqualified public function '" +
                                         function->name + "'");
                }
            }
        }
        function->resolvedType = symbol.type;
        popGenericTypeParameters();
    }
}


// ============================================================
// Statement dispatcher
// ============================================================

void Analyzer::analyzeStatement(
    const zplus::ast::Statement& statement
) {

    currentModuleName_ =
        moduleNameForFile(statement.location.sourceFile);

    if (
        const auto* node =
            as<
                zplus::ast::StartStatement
            >(&statement)
    ) {

        analyzeStart(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::VariableDeclaration
            >(&statement)
    ) {

        analyzeVariable(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::AssignmentStatement
            >(&statement)
    ) {

        analyzeAssignmentStatement(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ExpressionStatement
            >(&statement)
    ) {

        analyzeExpressionStatement(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ReturnStatement
            >(&statement)
    ) {

        analyzeReturn(*node);
        return;
    }

    if (const auto* node = as<zplus::ast::ThrowStatement>(&statement)) {
        analyzeThrow(*node);
        return;
    }

    if (const auto* node = as<zplus::ast::TryStatement>(&statement)) {
        analyzeTry(*node);
        return;
    }

    if (const auto* node = as<zplus::ast::DeferStatement>(&statement)) {
        analyzeDefer(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::IfStatement
            >(&statement)
    ) {

        analyzeIf(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::WhileStatement
            >(&statement)
    ) {

        analyzeWhile(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ForStatement
            >(&statement)
    ) {

        analyzeFor(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::RepeatStatement
            >(&statement)
    ) {

        analyzeRepeat(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::BreakStatement
            >(&statement)
    ) {

        analyzeBreak(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ContinueStatement
            >(&statement)
    ) {

        analyzeContinue(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::FunctionDeclaration
            >(&statement)
    ) {

        analyzeFunction(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::SwitchStatement
            >(&statement)
    ) {

        analyzeSwitch(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::StructDeclaration
            >(&statement)
    ) {

        analyzeStruct(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ClassDeclaration
            >(&statement)
    ) {

        analyzeClass(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::EnumDeclaration
            >(&statement)
    ) {

        analyzeEnum(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::NamespaceDeclaration
            >(&statement)
    ) {

        analyzeNamespace(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ImportDeclaration
            >(&statement)
    ) {

        analyzeImport(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::UseDeclaration
            >(&statement)
    ) {
        analyzeUse(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::ExportDeclaration
            >(&statement)
    ) {

        analyzeExport(*node);
        return;
    }

    if (
        const auto* node =
            as<
                zplus::ast::BlockStatement
            >(&statement)
    ) {

        analyzeBlock(
            node->statements
        );

        return;
    }

    error(
        statement,
        "unsupported statement"
    );
}

void Analyzer::analyzeThrow(
    const zplus::ast::ThrowStatement& statement
) {
    if (!statement.value) {
        error(statement, "throw requires an error value");
        return;
    }
    expressionType(*statement.value);
}

void Analyzer::analyzeTry(
    const zplus::ast::TryStatement& statement
) {
    enterScope();
    analyzeBlock(statement.tryBody);
    exitScope();

    enterScope();
    Symbol errorSymbol;
    errorSymbol.name = statement.catchName;
    errorSymbol.originalName = statement.catchName;
    errorSymbol.type = Type::custom("Error");
    errorSymbol.isConst = true;
    errorSymbol.initialized = true;
    errorSymbol.bindingName = createBindingName();
    statement.catchBindingName = errorSymbol.bindingName;
    declareSymbol(errorSymbol);
    analyzeBlock(statement.catchBody);
    exitScope();
}

void Analyzer::analyzeDefer(
    const zplus::ast::DeferStatement& statement
) {
    if (statement.body.size() != 1 || !statement.body.front()) {
        error(statement, "defer blocks must contain exactly one function call");
        return;
    }
    const auto* expressionStatement =
        as<zplus::ast::ExpressionStatement>(statement.body.front().get());
    const auto* call = expressionStatement && expressionStatement->expression
        ? as<zplus::ast::CallExpression>(expressionStatement->expression.get())
        : nullptr;
    if (!call || !as<zplus::ast::Identifier>(call->callee.get())) {
        error(statement, "defer blocks currently require one direct function call");
        return;
    }
    enterScope();
    analyzeBlock(statement.body);
    exitScope();
}


// ============================================================
// Start
// ============================================================

void Analyzer::analyzeStart(
    const zplus::ast::StartStatement& statement
) {

    enterScope();
    predeclareTypes(statement.body);
    predeclareFunctions(statement.body);

    analyzeBlock(
        statement.body
    );

    exitScope();
}


// ============================================================
// Block
// ============================================================

void Analyzer::analyzeBlock(
    const std::vector<zplus::ast::Stmt>& statements
) {

    for (const auto& statement :
         statements) {

        if (statement) {
            analyzeStatement(
                *statement
            );
        }
    }
}


// ============================================================
// Variable
// ============================================================

void Analyzer::analyzeVariable(
    const zplus::ast::VariableDeclaration& statement
) {

    if (scopes_.empty()) {
        enterScope();
    }

    const std::string symbolName = statement.internalName.empty()
        ? statement.name
        : statement.internalName;
    statement.bindingName = statement.internalName.empty()
        ? createBindingName()
        : statement.internalName;
    if (findSymbol(symbolName) != nullptr) {

        // Only reject if same scope.
        const auto& scope =
            scopes_.back();

        if (
            scope.find(symbolName) != scope.end()
        ) {

            error(
                statement,
                "duplicate declaration '" +
                statement.name +
                "'"
            );

            return;
        }
    }

    Type declaredType =
        statement.type
            ? resolveType(
                *statement.type
            )
            : Type::unknown();

    const std::optional<Type> previousExpectedType = expectedExpressionType_;
    if (statement.type) expectedExpressionType_ = declaredType;
    Type valueType =
        statement.initializer
            ? expressionType(*statement.initializer)
            : Type::unknown();
    expectedExpressionType_ = previousExpectedType;

    if (
        statement.initializer &&
        statement.type &&
        !isAssignable(
            declaredType,
            valueType
        )
    ) {

        error(
            statement,
            "cannot initialize '" +
            statement.name +
            "' with type '" +
            typeName(valueType) +
            "'; expected '" +
            typeName(declaredType) +
            "'"
        );
    }

    Type finalType =
        statement.type
            ? declaredType
            : valueType;
    statement.resolvedType = finalType;

    Symbol symbol;

    symbol.name =
        symbolName;
    symbol.bindingName = statement.bindingName;
    symbol.originalName = statement.name;
    symbol.moduleName = statement.moduleName;
    symbol.sourceFile = statement.location.sourceFile;
    symbol.isPublic = statement.moduleName.empty() || statement.isPublic;

    symbol.type =
        finalType;

    symbol.isConst =
        statement.isConst;

    symbol.initialized =
        statement.initializer != nullptr;

    if (!declareSymbol(symbol)) {

        error(
            statement,
            "duplicate declaration '" +
            statement.name +
            "'"
        );
        return;
    }

    if (!statement.moduleName.empty() && statement.isPublic) {
        std::size_t publicNameCount = 0;
        for (const auto& scope : scopes_) {
            for (const auto& [key, candidate] : scope) {
                if (!candidate.moduleName.empty() &&
                    candidate.originalName == statement.name &&
                    candidate.isPublic) {
                    ++publicNameCount;
                }
            }
        }
        if (publicNameCount == 1) {
            Symbol alias = symbol;
            alias.name = statement.name;
            const Symbol* existing = findSymbol(alias.name);
            if ((!existing || !existing->isBuiltin) &&
                !declareSymbol(alias)) {
                error(statement, "ambiguous unqualified public declaration '" +
                                     statement.name + "'");
            }
        }
    }
}


// ============================================================
// Assignment statement
// ============================================================

void Analyzer::analyzeAssignmentStatement(
    const zplus::ast::AssignmentStatement& statement
) {

    if (
        !statement.target ||
        !statement.value
    ) {
        error(
            statement,
            "invalid assignment statement"
        );

        return;
    }

    Type target =
        targetType(
            *statement.target
        );

    const auto* arrayValue =
        as<zplus::ast::ArrayExpression>(statement.value.get());
    const std::optional<Type> previousExpectedType = expectedExpressionType_;
    if (arrayValue && arrayValue->elements.empty() &&
        target.kind == TypeKind::Array) {
        expectedExpressionType_ = target;
    }
    Type value =
        expressionType(
            *statement.value
        );
    expectedExpressionType_ = previousExpectedType;

    if (!isAssignable(target, value)) {

        error(
            statement,
            "cannot assign type '" +
            typeName(value) +
            "' to '" +
            typeName(target) +
            "'"
        );
    }

    if (
        const auto* identifier =
            as<
                zplus::ast::Identifier
            >(statement.target.get())
    ) {

        Symbol* symbol =
            findSymbol(
                identifier->name
            );

        if (!symbol) {

            error(
                *identifier,
                "undefined variable '" +
                identifier->name +
                "'"
            );

            return;
        }

        if (symbol->isConst) {

            error(
                *identifier,
                "cannot assign to constant '" +
                identifier->name +
                "'"
            );

            return;
        }

        symbol->initialized = true;
    }
}


// ============================================================
// Expression statement
// ============================================================

void Analyzer::analyzeExpressionStatement(
    const zplus::ast::ExpressionStatement& statement
) {

    if (statement.expression) {

        expressionType(
            *statement.expression
        );
    }
}


// ============================================================
// Return
// ============================================================

void Analyzer::analyzeReturn(
    const zplus::ast::ReturnStatement& statement
) {

    if (!insideFunction()) {

        error(
            statement,
            "'return' is only valid inside a function"
        );

        return;
    }

    const std::optional<Type> previousExpectedType = expectedExpressionType_;
    if (statement.value) expectedExpressionType_ = currentReturnType_;
    Type actual =
        statement.value
            ? expressionType(*statement.value)
            : Type::voidType();
    expectedExpressionType_ = previousExpectedType;

    if (
        !isAssignable(
            currentReturnType_,
            actual
        )
    ) {

        error(
            statement,
            "function '" +
            currentFunctionName_ +
            "' returns '" +
            typeName(currentReturnType_) +
            "' but this return provides '" +
            typeName(actual) +
            "'"
        );
    }
}


// ============================================================
// If
// ============================================================

void Analyzer::analyzeIf(
    const zplus::ast::IfStatement& statement
) {

    if (!statement.condition) {

        error(
            statement,
            "if statement requires a condition"
        );

    } else {

        Type condition =
            expressionType(
                *statement.condition
            );

        if (
            !isBoolean(condition) &&
            !isUnknown(condition)
        ) {

            error(
                *statement.condition,
                "if condition must be boolean"
            );
        }
    }

    enterScope();

    analyzeBlock(
        statement.thenBody
    );

    exitScope();

    if (statement.hasElse) {

        enterScope();

        analyzeBlock(
            statement.elseBody
        );

        exitScope();
    }
}


// ============================================================
// While
// ============================================================

void Analyzer::analyzeWhile(
    const zplus::ast::WhileStatement& statement
) {

    if (statement.condition) {

        Type condition =
            expressionType(
                *statement.condition
            );

        if (
            !isBoolean(condition) &&
            !isUnknown(condition)
        ) {

            error(
                *statement.condition,
                "while condition must be boolean"
            );
        }
    }

    ++loopDepth_;

    enterScope();

    analyzeBlock(
        statement.body
    );

    exitScope();

    --loopDepth_;
}


// ============================================================
// Repeat
// ============================================================

void Analyzer::analyzeRepeat(
    const zplus::ast::RepeatStatement& statement
) {

    if (!statement.count) {

        error(
            statement,
            "repeat requires a count"
        );

    } else {

        Type count =
            expressionType(
                *statement.count
            );

        if (
            !isNumeric(count) &&
            !isUnknown(count)
        ) {

            error(
                *statement.count,
                "repeat count must be a number"
            );
        }
    }

    ++loopDepth_;

    enterScope();

    analyzeBlock(
        statement.body
    );

    exitScope();

    --loopDepth_;
}


// ============================================================
// For
// ============================================================

void Analyzer::analyzeFor(
    const zplus::ast::ForStatement& statement
) {

    enterScope();

    if (statement.initializer) {

        analyzeStatement(
            *statement.initializer
        );
    }

    if (statement.condition) {

        Type condition =
            expressionType(
                *statement.condition
            );

        if (
            !isBoolean(condition) &&
            !isUnknown(condition)
        ) {

            error(
                *statement.condition,
                "for condition must be boolean"
            );
        }
    }

    if (statement.increment) {

        expressionType(
            *statement.increment
        );
    }

    ++loopDepth_;

    analyzeBlock(
        statement.body
    );

    --loopDepth_;

    exitScope();
}


// ============================================================
// Break
// ============================================================

void Analyzer::analyzeBreak(
    const zplus::ast::BreakStatement& statement
) {

    if (!insideLoop()) {

        error(
            statement,
            "'break' is only valid inside a loop"
        );
    }
}


// ============================================================
// Continue
// ============================================================

void Analyzer::analyzeContinue(
    const zplus::ast::ContinueStatement& statement
) {

    if (!insideLoop()) {

        error(
            statement,
            "'continue' is only valid inside a loop"
        );
    }
}


// ============================================================
// Function
// ============================================================

void Analyzer::analyzeFunction(
    const zplus::ast::FunctionDeclaration& statement
) {

    const std::string symbolName = statement.internalName.empty()
        ? statement.name
        : statement.internalName;
    pushGenericTypeParameters(
        statement.genericParameters, symbolName);
    const Symbol* existing = findSymbol(symbolName);

    if (
        !existing
    ) {

        std::vector<Type> parameterTypes;

        for (const auto& parameter :
             statement.parameters) {

            parameterTypes.push_back(
                parameter && parameter->type
                    ? resolveType(
                        *parameter->type
                    )
                    : Type::unknown()
            );
        }

        Type returnType =
            statement.returnType
                ? resolveType(
                    *statement.returnType
                )
                : Type::voidType();

        Symbol symbol;
        symbol.name = statement.internalName.empty()
            ? statement.name
            : statement.internalName;
        symbol.originalName = statement.name;
        symbol.moduleName = statement.moduleName;
        symbol.sourceFile = statement.location.sourceFile;
        symbol.isPublic = statement.moduleName.empty() || statement.isPublic;
        symbol.type = Type::function(
            std::move(parameterTypes), returnType,
            activeGenericTypeParameters());
        symbol.type.requiredParameterCount = 0;
        for (const auto& parameter : statement.parameters) {
            if (parameter && parameter->defaultValue) {
                break;
            }
            ++symbol.type.requiredParameterCount;
        }
        symbol.isFunction = true;
        symbol.initialized = true;
        declareSymbol(symbol);
    }

    if (const Symbol* functionSymbol = findSymbol(symbolName)) {
        statement.resolvedType = functionSymbol->type;
    }

    const Type previousReturnType =
        currentReturnType_;

    const std::string previousFunction =
        currentFunctionName_;
    const std::string previousClass = currentClassName_;
    currentClassName_ = statement.ownerClass;

    currentReturnType_ =
        statement.returnType
            ? resolveType(
                *statement.returnType
            )
            : Type::voidType();

    currentFunctionName_ =
        statement.name;

    ++functionDepth_;

    enterScope();

    if (!statement.ownerClass.empty()) {
        Symbol receiver;
        receiver.name = "this";
        receiver.bindingName = "this";
        receiver.sourceFile = statement.location.sourceFile;
        std::vector<Type> receiverArguments;
        const auto ownerClass = classDeclarations_.find(statement.ownerClass);
        if (ownerClass != classDeclarations_.end() &&
            ownerClass->second->resolvedType) {
            receiverArguments = ownerClass->second->resolvedType->arguments;
        }
        receiver.type = Type::custom(
            statement.ownerClass, std::move(receiverArguments));
        receiver.initialized = true;
        declareSymbol(receiver);
    }

    // --------------------------------------------------------
    // Parameters
    // --------------------------------------------------------

    bool hasDefaultParameter = false;
    for (const auto& parameter :
         statement.parameters) {

        if (!parameter) {
            continue;
        }

        if (parameter->defaultValue) {
            hasDefaultParameter = true;
        } else if (hasDefaultParameter) {
            error(
                *parameter,
                "parameters without defaults cannot follow parameters with defaults"
            );
        }

        Type parameterType =
            parameter->type
                ? resolveType(
                    *parameter->type
                )
                : Type::unknown();
        parameter->resolvedType = parameterType;

        parameter->bindingName = createBindingName();
        Symbol symbol;

        symbol.name =
            parameter->name;
        symbol.bindingName =
            parameter->bindingName;

        symbol.type =
            parameterType;

        symbol.initialized = true;

        symbol.isConst =
            parameter->isConst;

        if (!declareSymbol(symbol)) {

            error(
                *parameter,
                "duplicate parameter '" +
                parameter->name +
                "'"
            );
        }

        if (parameter->defaultValue) {

            Type defaultType =
                expressionType(
                    *parameter->defaultValue
                );

            if (
                !isAssignable(
                    parameterType,
                    defaultType
                )
            ) {

                error(
                    *parameter,
                    "default value for parameter '" +
                    parameter->name +
                    "' has type '" +
                    typeName(defaultType) +
                    "' but expected '" +
                    typeName(parameterType) +
                    "'"
                );
            }
        }
    }

    // --------------------------------------------------------
    // Body
    // --------------------------------------------------------

    analyzeBlock(
        statement.body
    );

    exitScope();

    --functionDepth_;

    currentReturnType_ =
        previousReturnType;

    currentFunctionName_ =
        previousFunction;
    currentClassName_ = previousClass;
    popGenericTypeParameters();
}


// ============================================================
// Switch
// ============================================================

void Analyzer::analyzeSwitch(
    const zplus::ast::SwitchStatement& statement
) {

    Type switchType =
        statement.expression
            ? expressionType(
                *statement.expression
            )
            : Type::unknown();

    for (const auto& caseNode :
         statement.cases) {

        if (!caseNode) {
            continue;
        }

        if (caseNode->value) {

            Type caseType =
                expressionType(
                    *caseNode->value
                );

            if (
                !isAssignable(
                    switchType,
                    caseType
                ) &&
                !isAssignable(
                    caseType,
                    switchType
                )
            ) {

                error(
                    *caseNode,
                    "case value type '" +
                    typeName(caseType) +
                    "' does not match switch type '" +
                    typeName(switchType) +
                    "'"
                );
            }
        }

        enterScope();

        analyzeBlock(
            caseNode->body
        );

        exitScope();
    }

    if (statement.hasDefault) {

        enterScope();

        analyzeBlock(
            statement.defaultBody
        );

        exitScope();
    }
}


// ============================================================
// Struct
// ============================================================

void Analyzer::analyzeStruct(
    const zplus::ast::StructDeclaration& statement
) {

    const std::string declaredTypeName = statement.moduleName.empty()
        ? statement.name
        : statement.moduleName + "::" + statement.name;
    pushGenericTypeParameters(statement.genericParameters, declaredTypeName);
    std::unordered_set<std::string> fieldNames;
    bool optionalFieldSeen = false;
    for (const auto& field :
         statement.fields) {

        if (!field) {
            continue;
        }

        if (field->name.empty() || !fieldNames.insert(field->name).second) {
            error(*field, "duplicate or empty field name in struct '" +
                              statement.name + "'");
            continue;
        }

        if (field->defaultValue) {
            optionalFieldSeen = true;
        } else if (optionalFieldSeen) {
            error(*field,
                  "required fields must precede fields with default values");
        }

        if (field->type) {

            const Type fieldType = resolveType(*field->type);
            field->resolvedType = fieldType;
            if (field->defaultValue) {
                const Type defaultType = expressionType(*field->defaultValue);
                if (!isAssignable(fieldType, defaultType)) {
                    error(*field, "default value for field '" + field->name +
                                      "' has type '" + typeName(defaultType) +
                                      "', expected '" + typeName(fieldType) + "'");
                }
                if (!as<zplus::ast::Literal>(field->defaultValue.get())) {
                    error(*field,
                          "struct field defaults must currently be literals");
                }
            }
        } else if (field->defaultValue) {
            field->resolvedType = expressionType(*field->defaultValue);
        }
    }
    popGenericTypeParameters();
}


// ============================================================
// Class
// ============================================================

void Analyzer::analyzeClass(
    const zplus::ast::ClassDeclaration& statement
) {

    const std::string declaredTypeName = statement.moduleName.empty()
        ? statement.name
        : statement.moduleName + "::" + statement.name;
    pushGenericTypeParameters(statement.genericParameters, declaredTypeName);
    std::unordered_set<std::string> memberNames;
    for (const auto& field :
         statement.fields) {

        if (!field) {
            continue;
        }

        if (!memberNames.insert(field->name).second) {
            error(*field, "duplicate class member '" + field->name + "'");
        }

        if (field->type) {

            const Type fieldType = resolveType(
                *field->type
            );
            field->resolvedType = fieldType;
            if (field->initializer) {
                const Type initializerType = expressionType(*field->initializer);
                if (!isAssignable(fieldType, initializerType)) {
                    error(*field, "initializer for class field '" + field->name +
                                      "' has type '" + typeName(initializerType) +
                                      "', expected '" + typeName(fieldType) + "'");
                }
            }
        } else if (field->initializer) {
            field->resolvedType = expressionType(*field->initializer);
        }

        if (field->initializer) {

            const auto* literal =
                as<zplus::ast::Literal>(field->initializer.get());
            if (!literal) {
                error(*field, "class field initializers must be literals");
            }
        } else if (!field->type) {
            error(*field, "class field '" + field->name +
                              "' requires a type or initializer");
        }
    }

    for (const auto& method :
         statement.methods) {

        if (method) {
            if (!memberNames.insert(method->name).second) {
                error(*method, "duplicate class member '" + method->name + "'");
            }
            analyzeFunction(
                *method
            );
        }
    }
    popGenericTypeParameters();
}


// ============================================================
// Enum
// ============================================================

void Analyzer::analyzeEnum(
    const zplus::ast::EnumDeclaration& statement
) {

    std::unordered_set<std::string> memberNames;
    for (const auto& value :
         statement.values) {

        if (!value) continue;

        if (!memberNames.insert(value->name).second) {
            error(*value, "duplicate enum member '" + value->name + "'");
        }

        if (value->value) {

            Type valueType =
                expressionType(
                    *value->value
                );

            if (
                !isNumeric(valueType) &&
                !isUnknown(valueType)
            ) {

                error(
                    *value,
                    "enum value must be numeric"
                );
            }

            const auto* literal = as<zplus::ast::Literal>(value->value.get());
            const auto* signedLiteral =
                as<zplus::ast::UnaryExpression>(value->value.get());
            const auto* signedNumber = signedLiteral &&
                    (signedLiteral->operatorType == zplus::TokenType::Minus ||
                     signedLiteral->operatorType == zplus::TokenType::Plus)
                ? as<zplus::ast::Literal>(signedLiteral->operand.get())
                : nullptr;
            if ((!literal || literal->kind != zplus::ast::Literal::Kind::Number) &&
                (!signedNumber ||
                 signedNumber->kind != zplus::ast::Literal::Kind::Number)) {
                error(*value, "enum values must be numeric literals");
            }
        }
    }
}


// ============================================================
// Namespace
// ============================================================

void Analyzer::analyzeNamespace(
    const zplus::ast::NamespaceDeclaration& statement
) {

    enterScope();
    predeclareTypes(statement.body);
    predeclareFunctions(statement.body);
    for (const auto& child : statement.body) {
        const zplus::ast::Statement* declaration = child.get();
        while (const auto* exported =
                   declaration
                       ? as<zplus::ast::ExportDeclaration>(declaration)
                       : nullptr) {
            declaration = exported->declaration.get();
        }
        const auto* function =
            declaration
                ? as<zplus::ast::FunctionDeclaration>(declaration)
                : nullptr;
        if (!function || function->namespaceName.empty()) continue;
        const Symbol* existing = findSymbol(function->internalName);
        if (!existing) continue;
        Symbol alias = *existing;
        alias.name = function->name;
        if (!declareSymbol(alias)) {
            error(*function, "duplicate declaration '" + function->name +
                                 "' in namespace '" + statement.name + "'");
        }
    }

    analyzeBlock(
        statement.body
    );

    exitScope();
}


// ============================================================
// Import
// ============================================================

void Analyzer::analyzeImport(
    const zplus::ast::ImportDeclaration&
) {
    // Import resolution belongs to the module system.
}

void Analyzer::analyzeUse(
    const zplus::ast::UseDeclaration& statement
) {
    error(statement, "'use' is only valid at module scope");
}


// ============================================================
// Export
// ============================================================

void Analyzer::analyzeExport(
    const zplus::ast::ExportDeclaration& statement
) {

    if (statement.declaration) {

        analyzeStatement(
            *statement.declaration
        );
    }
}


// ============================================================
// Expression dispatcher
// ============================================================

Type Analyzer::expressionType(
    const zplus::ast::Expression& expression
) {
    Type result;
    if (
        const auto* node =
            as<zplus::ast::Literal>(
                &expression
            )
    ) {
        result = typeOfLiteral(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::Identifier>(
                &expression
            )
    ) {
        result = typeOfIdentifier(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::UnaryExpression>(
                &expression
            )
    ) {
        result = typeOfUnary(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::BinaryExpression>(
                &expression
            )
    ) {
        result = typeOfBinary(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::AssignmentExpression>(
                &expression
            )
    ) {
        result = typeOfAssignment(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::ConditionalExpression>(
                &expression
            )
    ) {
        result = typeOfConditional(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::CallExpression>(
                &expression
            )
    ) {
        result = typeOfCall(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::MemberExpression>(
                &expression
            )
    ) {
        result = typeOfMember(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::IndexExpression>(
                &expression
            )
    ) {
        result = typeOfIndex(
            *node
        );
    } else if (
        const auto* node =
            as<zplus::ast::ArrayExpression>(
                &expression
            )
    ) {
        result = typeOfArray(
            *node
        );
    } else {
        error(expression, "unsupported expression");
        result = Type::unknown();
    }
    expression.resolvedType = result;
    return result;
}

// ============================================================
// Literal
// ============================================================

Type Analyzer::typeOfLiteral(
    const zplus::ast::Literal& expression
) {

    switch (expression.kind) {

        case zplus::ast::Literal::Kind::Number:
            return Type::number();

        case zplus::ast::Literal::Kind::String:
            return Type::string();

        case zplus::ast::Literal::Kind::Character:
            return Type::character();

        case zplus::ast::Literal::Kind::Boolean:
            return Type::boolean();

        case zplus::ast::Literal::Kind::Null:
            return Type::null();
    }

    return Type::unknown();
}


// ============================================================
// Identifier
// ============================================================

Type Analyzer::typeOfIdentifier(
    const zplus::ast::Identifier& expression
) {

    const std::string sourceModule =
        moduleNameForFile(expression.location.sourceFile);
    const Symbol* symbol = nullptr;
    if (!sourceModule.empty()) {
        symbol = findModuleSymbol(sourceModule, expression.name);
    }
    if (!symbol) {
        symbol = findSymbol(expression.name);
    }

    if (!symbol) {
        const Symbol* uniquePublic = nullptr;
        bool ambiguous = false;
        for (const auto& scope : scopes_) {
            for (const auto& [key, candidate] : scope) {
                if (candidate.moduleName.empty() ||
                    candidate.originalName != expression.name ||
                    !candidate.isPublic || key != candidate.name) {
                    continue;
                }
                if (uniquePublic && uniquePublic->moduleName != candidate.moduleName) {
                    ambiguous = true;
                    break;
                }
                uniquePublic = &candidate;
            }
            if (ambiguous) break;
        }
        if (ambiguous) {
            error(expression, "ambiguous unqualified name '" + expression.name +
                                 "'; qualify it with a module name");
            return Type::unknown();
        }
        symbol = uniquePublic;
    }

    if (!symbol) {
        for (const auto& scope : scopes_) {
            for (const auto& [key, candidate] : scope) {
                if (!candidate.moduleName.empty() &&
                    candidate.originalName == expression.name &&
                    !candidate.isPublic &&
                    candidate.moduleName != sourceModule) {
                    error(expression, "declaration '" + expression.name +
                                          "' is private to module '" +
                                          candidate.moduleName + "'");
                    return Type::unknown();
                }
            }
        }
        error(
            expression,
            "undefined identifier '" +
            expression.name +
            "'"
        );

        return Type::unknown();
    }

    if (!symbol->moduleName.empty() && !symbol->isPublic &&
        symbol->moduleName != sourceModule) {
        error(expression, "declaration '" + expression.name +
                              "' is private to module '" + symbol->moduleName + "'");
        return Type::unknown();
    }
    expression.bindingName = symbol->bindingName.empty()
        ? symbol->name
        : symbol->bindingName;
    return symbol->type;
}


// ============================================================
// Unary
// ============================================================

Type Analyzer::typeOfUnary(
    const zplus::ast::UnaryExpression& expression
) {

    if (!expression.operand) {

        error(
            expression,
            "missing operand for unary operator"
        );

        return Type::unknown();
    }

    Type operand =
        expressionType(
            *expression.operand
        );

    switch (expression.operatorType) {

        case zplus::TokenType::Plus:
        case zplus::TokenType::Minus:

            if (
                !isNumeric(operand) &&
                !isUnknown(operand)
            ) {

                error(
                    expression,
                    "unary numeric operator requires a number"
                );

                return Type::unknown();
            }

            return operand.kind == TypeKind::TypeParameter
                ? operand
                : Type::number();

        case zplus::TokenType::Exclamation:
        case zplus::TokenType::Not:

            if (
                !isBoolean(operand) &&
                !isUnknown(operand)
            ) {

                error(
                    expression,
                    "logical NOT requires a boolean"
                );

                return Type::unknown();
            }

            return Type::boolean();

        case zplus::TokenType::BitNot:

            if (
                !isNumeric(operand) &&
                !isUnknown(operand)
            ) {

                error(
                    expression,
                    "bitwise NOT requires a number"
                );

                return Type::unknown();
            }

            return operand.kind == TypeKind::TypeParameter
                ? operand
                : Type::number();

        case zplus::TokenType::PlusPlus:
        case zplus::TokenType::MinusMinus:

            if (
                !isNumeric(operand) &&
                !isUnknown(operand)
            ) {

                error(
                    expression,
                    "increment/decrement requires a number"
                );

                return Type::unknown();
            }

            return operand;

        case zplus::TokenType::BitAnd:

            return Type::pointer(
                operand
            );

        case zplus::TokenType::Star:

            if (
                operand.kind !=
                TypeKind::Pointer &&
                !isUnknown(operand)
            ) {

                error(
                    expression,
                    "dereference requires a pointer"
                );

                return Type::unknown();
            }

            if (
                operand.elementType
            ) {
                return *operand.elementType;
            }

            return Type::unknown();

        default:

            error(
                expression,
                "unsupported unary operator"
            );

            return Type::unknown();
    }
}


// ============================================================
// Binary
// ============================================================

Type Analyzer::typeOfBinary(
    const zplus::ast::BinaryExpression& expression
) {

    if (
        !expression.left ||
        !expression.right
    ) {

        error(
            expression,
            "invalid binary expression"
        );

        return Type::unknown();
    }

    if (expression.operatorType == zplus::TokenType::Is) {
        const Type left = expressionType(*expression.left);
        const auto* typeName =
            as<zplus::ast::Identifier>(expression.right.get());
        if (!typeName) {
            error(*expression.right,
                  "the right operand of 'is' must be a declared type name");
            return Type::boolean();
        }
        const std::string sourceModule =
            moduleNameForFile(expression.location.sourceFile);
        const std::string qualifiedName = sourceModule.empty()
            ? typeName->name
            : sourceModule + "::" + typeName->name;
        if (!userTypes_.contains(qualifiedName) &&
            !userTypes_.contains(typeName->name)) {
            error(*typeName, "unknown type '" + typeName->name + "'");
        }
        if (left.kind != TypeKind::Custom && left.kind != TypeKind::Unknown) {
            return Type::boolean();
        }
        return Type::boolean();
    }

    Type left =
        expressionType(
            *expression.left
        );

    Type right =
        expressionType(
            *expression.right
        );

    switch (expression.operatorType) {

        // ----------------------------------------------------
        // Arithmetic
        // ----------------------------------------------------

        case zplus::TokenType::Plus:

            if (
                left.kind == TypeKind::String &&
                right.kind == TypeKind::String
            ) {
                return Type::string();
            }

            if (isUnknown(left) || isUnknown(right)) {
                return Type::unknown();
            }

            [[fallthrough]];

        case zplus::TokenType::Minus:
        case zplus::TokenType::Star:
        case zplus::TokenType::Slash:
        case zplus::TokenType::Percent:

            if (isUnknown(left) || isUnknown(right)) {
                return Type::unknown();
            }

            if (
                (
                    !isNumeric(left) &&
                    !isUnknown(left)
                ) ||
                (
                    !isNumeric(right) &&
                    !isUnknown(right)
                )
            ) {

                error(
                    expression,
                    "arithmetic operator requires numbers"
                );

                return Type::unknown();
            }

            if (left.kind == TypeKind::TypeParameter &&
                typesEqual(left, right)) {
                return left;
            }
            return Type::number();

        // ----------------------------------------------------
        // Comparison
        // ----------------------------------------------------

        case zplus::TokenType::Less:
        case zplus::TokenType::LessEqual:
        case zplus::TokenType::Greater:
        case zplus::TokenType::GreaterEqual:

            if (
                (
                    !isNumeric(left) &&
                    !isUnknown(left)
                ) ||
                (
                    !isNumeric(right) &&
                    !isUnknown(right)
                )
            ) {

                error(
                    expression,
                    "comparison requires numbers"
                );
            }

            return Type::boolean();

        // ----------------------------------------------------
        // Equality
        // ----------------------------------------------------

        case zplus::TokenType::EqualEqual:
        case zplus::TokenType::NotEqual:

            if (
                !typesEqual(
                    left,
                    right
                )
            ) {

                error(
                    expression,
                    "cannot compare '" +
                    typeName(left) +
                    "' with '" +
                    typeName(right) +
                    "'"
                );
            }

            return Type::boolean();

        // ----------------------------------------------------
        // Logical
        // ----------------------------------------------------

        case zplus::TokenType::And:
        case zplus::TokenType::AndAnd:
        case zplus::TokenType::Or:
        case zplus::TokenType::OrOr:

            if (
                (
                    !isBoolean(left) &&
                    !isUnknown(left)
                ) ||
                (
                    !isBoolean(right) &&
                    !isUnknown(right)
                )
            ) {

                error(
                    expression,
                    "logical operators require booleans"
                );
            }

            return Type::boolean();

        // ----------------------------------------------------
        // Bitwise / shift
        // ----------------------------------------------------

        case zplus::TokenType::BitAnd:
        case zplus::TokenType::BitOr:
        case zplus::TokenType::BitXor:
        case zplus::TokenType::LeftShift:
        case zplus::TokenType::RightShift:

            if (
                (
                    !isNumeric(left) &&
                    !isUnknown(left)
                ) ||
                (
                    !isNumeric(right) &&
                    !isUnknown(right)
                )
            ) {

                error(
                    expression,
                    "bitwise operators require numbers"
                );
            }

            return Type::number();

        // ----------------------------------------------------
        // Type relation
        // ----------------------------------------------------

        default:

            error(
                expression,
                "unsupported binary operator"
            );

            return Type::unknown();
    }
}


// ============================================================
// Assignment expression
// ============================================================

Type Analyzer::typeOfAssignment(
    const zplus::ast::AssignmentExpression& expression
) {

    if (
        !expression.target ||
        !expression.value
    ) {

        error(
            expression,
            "invalid assignment expression"
        );

        return Type::unknown();
    }

    Type target =
        targetType(
            *expression.target
        );

    const auto* arrayValue =
        as<zplus::ast::ArrayExpression>(expression.value.get());
    const std::optional<Type> previousExpectedType = expectedExpressionType_;
    if (arrayValue && arrayValue->elements.empty() &&
        target.kind == TypeKind::Array) {
        expectedExpressionType_ = target;
    }
    Type value =
        expressionType(
            *expression.value
        );
    expectedExpressionType_ = previousExpectedType;

    if (!isValidAssignmentTarget(
            *expression.target
        )) {

        error(
            expression,
            "invalid assignment target"
        );

        return Type::unknown();
    }

    // --------------------------------------------------------
    // Compound assignments
    // --------------------------------------------------------

    if (
        expression.operatorType !=
        zplus::TokenType::Equal
    ) {

        switch (
            expression.operatorType
        ) {

            case zplus::TokenType::PlusEqual:

                if (
                    !(
                        isNumeric(target) &&
                        isNumeric(value)
                    ) &&
                    !(
                        target.kind ==
                            TypeKind::String &&
                        value.kind ==
                            TypeKind::String
                    )
                ) {

                    error(
                        expression,
                        "'+=' requires compatible values",
                        zplus::DiagnosticCategory::Type
                    );
                }

                break;

            case zplus::TokenType::MinusEqual:
            case zplus::TokenType::StarEqual:
            case zplus::TokenType::SlashEqual:
            case zplus::TokenType::PercentEqual:
            case zplus::TokenType::AndEqual:
            case zplus::TokenType::OrEqual:
            case zplus::TokenType::XorEqual:
            case zplus::TokenType::LeftShiftEqual:
            case zplus::TokenType::RightShiftEqual:

                if (
                    !isNumeric(target) ||
                    !isNumeric(value)
                ) {

                    error(
                        expression,
                        "compound assignment requires numbers",
                        zplus::DiagnosticCategory::Type
                    );
                }

                break;

            default:
                break;
        }
    }

    if (
        !isAssignable(
            target,
            value
        )
    ) {

        error(
            expression,
            "cannot assign '" +
            typeName(value) +
            "' to '" +
            typeName(target) +
            "'",
            zplus::DiagnosticCategory::Type
        );

        return Type::unknown();
    }

    if (
        const auto* identifier =
            as<
                zplus::ast::Identifier
            >(expression.target.get())
    ) {

        Symbol* symbol =
            findSymbol(
                identifier->name
            );

        if (!symbol) {

            error(
                *identifier,
                "undefined variable '" +
                identifier->name +
                "'"
            );

            return Type::unknown();
        }

        if (symbol->isConst) {

            error(
                *identifier,
                "cannot assign to constant '" +
                identifier->name +
                "'"
            );

            return Type::unknown();
        }

        symbol->initialized = true;
    }

    return target;
}


// ============================================================
// Conditional
// ============================================================

Type Analyzer::typeOfConditional(
    const zplus::ast::ConditionalExpression& expression
) {

    if (
        !expression.condition ||
        !expression.thenExpression ||
        !expression.elseExpression
    ) {

        error(
            expression,
            "invalid conditional expression"
        );

        return Type::unknown();
    }

    Type condition =
        expressionType(
            *expression.condition
        );

    if (
        !isBoolean(condition) &&
        !isUnknown(condition)
    ) {

        error(
            *expression.condition,
            "conditional expression requires a boolean condition"
        );
    }

    Type thenType =
        expressionType(
            *expression.thenExpression
        );

    Type elseType =
        expressionType(
            *expression.elseExpression
        );

    if (isUnknown(thenType) || isUnknown(elseType)) {
        return Type::unknown();
    }

    if (!typesEqual(
            thenType,
            elseType
        )) {

        error(
            expression,
            "conditional branches must have compatible types"
        );

        return Type::unknown();
    }

    return thenType;
}


// ============================================================
// Function call
// ============================================================

Type Analyzer::typeOfCall(
    const zplus::ast::CallExpression& expression
) {
    const std::optional<Type> expectedReturnType = expectedExpressionType_;
    struct ExpectedTypeRestore {
        std::optional<Type>& slot;
        std::optional<Type> value;
        ~ExpectedTypeRestore() { slot = std::move(value); }
    } restoreExpectedType{expectedExpressionType_, expectedExpressionType_};
    expectedExpressionType_.reset();

    if (!expression.callee) {

        error(
            expression,
            "missing function in call"
        );

        return Type::unknown();
    }

    const auto* calleeIdentifier =
        as<zplus::ast::Identifier>(expression.callee.get());
    if (calleeIdentifier) {
        const std::string sourceModule =
            moduleNameForFile(expression.location.sourceFile);
        std::string structKey = calleeIdentifier->name;
        if (!sourceModule.empty()) {
            const std::string localKey =
                sourceModule + "::" + calleeIdentifier->name;
            if (structDeclarations_.contains(localKey) ||
                classDeclarations_.contains(localKey)) {
                structKey = localKey;
            }
        }

        const auto instantiateRecord =
            [&](const Type& declarationType,
                const std::vector<Type>& fieldTypes,
                std::size_t requiredCount,
                const std::string& recordName) -> Type {
                if (expression.arguments.size() < requiredCount ||
                    expression.arguments.size() > fieldTypes.size()) {
                    error(expression, "constructor for '" + recordName +
                        "' expects " +
                        (requiredCount == fieldTypes.size()
                            ? std::to_string(fieldTypes.size())
                            : std::to_string(requiredCount) + " to " +
                                  std::to_string(fieldTypes.size())) +
                        " arguments");
                    return Type::unknown();
                }

                if (!expression.explicitTypeArguments.empty() &&
                    expression.explicitTypeArguments.size() !=
                        declarationType.arguments.size()) {
                    error(expression, "generic type '" + recordName +
                        "' requires exactly " +
                        std::to_string(declarationType.arguments.size()) +
                        " explicit type argument(s)",
                        zplus::DiagnosticCategory::Type);
                    return Type::unknown();
                }

                TypeSubstitution substitutions;
                std::vector<Type> typeArguments;
                typeArguments.reserve(declarationType.arguments.size());
                if (!expression.explicitTypeArguments.empty()) {
                    for (std::size_t index = 0;
                         index < expression.explicitTypeArguments.size();
                         ++index) {
                        Type actual = resolveType(
                            *expression.explicitTypeArguments[index]);
                        if (!unify(declarationType.arguments[index], actual,
                                   substitutions)) {
                            error(*expression.explicitTypeArguments[index],
                                  "invalid type argument '" + typeName(actual) +
                                      "' for '" + recordName + "'",
                                  zplus::DiagnosticCategory::Type);
                            return Type::unknown();
                        }
                        typeArguments.push_back(std::move(actual));
                    }
                }

                std::vector<Type> argumentTypes;
                argumentTypes.reserve(expression.arguments.size());
                for (std::size_t index = 0;
                     index < expression.arguments.size(); ++index) {
                    const auto& argument = expression.arguments[index];
                    if (!argument) {
                        argumentTypes.push_back(Type::unknown());
                        continue;
                    }
                    const Type expected =
                        substitute(fieldTypes[index], substitutions);
                    const auto* array =
                        as<zplus::ast::ArrayExpression>(argument.get());
                    const std::optional<Type> previousExpectedType =
                        expectedExpressionType_;
                    if (array && array->elements.empty() &&
                        expected.kind == TypeKind::Array) {
                        expectedExpressionType_ = expected;
                    }
                    argumentTypes.push_back(expressionType(*argument));
                    expectedExpressionType_ = previousExpectedType;
                }

                for (std::size_t index = 0; index < argumentTypes.size(); ++index) {
                    const Type expected = substitute(fieldTypes[index], substitutions);
                    const bool matched = expression.explicitTypeArguments.empty()
                        ? unify(fieldTypes[index], argumentTypes[index], substitutions)
                        : isAssignable(expected, argumentTypes[index]);
                    if (!matched) {
                        error(*expression.arguments[index],
                              "constructor argument for field " +
                                  std::to_string(index + 1) + " of '" + recordName +
                                  "' has type '" + typeName(argumentTypes[index]) +
                                  "', expected '" +
                                  typeName(substitute(fieldTypes[index], substitutions)) +
                                  "'",
                              zplus::DiagnosticCategory::Type);
                        return Type::unknown();
                    }
                }

                if (expression.explicitTypeArguments.empty()) {
                    for (const Type& parameter : declarationType.arguments) {
                        Type argument = substitute(parameter, substitutions);
                        if ((argument.kind == TypeKind::TypeParameter &&
                             argument.name == parameter.name) ||
                            argument.kind == TypeKind::Unknown) {
                            error(expression, "cannot infer type parameter '" +
                                typeName(parameter) + "' for '" + recordName + "'",
                                zplus::DiagnosticCategory::Type);
                            return Type::unknown();
                        }
                        typeArguments.push_back(std::move(argument));
                    }
                }
                expression.resolvedTypeArguments = typeArguments;
                return Type::custom(declarationType.name, std::move(typeArguments));
            };

        const auto structure = structDeclarations_.find(structKey);
        if (structure != structDeclarations_.end()) {
            const auto& declaration = *structure->second;
            const auto type = userTypes_.find(structKey);
            const Type declarationType = type == userTypes_.end()
                ? Type::custom(structKey)
                : type->second;
            pushGenericTypeParameters(
                declaration.genericParameters, declarationType.name);
            std::vector<Type> fieldTypes;
            std::size_t requiredCount = declaration.fields.size();
            for (const auto& field : declaration.fields) {
                if (!field) {
                    fieldTypes.push_back(Type::unknown());
                    continue;
                }
                fieldTypes.push_back(
                    field->type
                        ? resolveType(*field->type)
                        : (field->defaultValue
                            ? expressionType(*field->defaultValue)
                            : Type::unknown()));
            }
            while (requiredCount > 0 &&
                   declaration.fields[requiredCount - 1] &&
                   declaration.fields[requiredCount - 1]->defaultValue) {
                --requiredCount;
            }
            Type result = instantiateRecord(
                declarationType, fieldTypes, requiredCount, declaration.name);
            popGenericTypeParameters();
            return result;
        }

        const auto klass = classDeclarations_.find(structKey);
        if (klass != classDeclarations_.end()) {
            const auto& declaration = *klass->second;
            const std::string canonicalName = declaration.moduleName.empty()
                ? declaration.name
                : declaration.moduleName + "::" + declaration.name;
            const auto type = userTypes_.find(structKey);
            const Type declarationType = type == userTypes_.end()
                ? Type::custom(canonicalName)
                : type->second;
            pushGenericTypeParameters(
                declaration.genericParameters, declarationType.name);
            std::vector<Type> fieldTypes;
            std::size_t requiredCount = declaration.fields.size();
            for (const auto& field : declaration.fields) {
                if (!field) {
                    fieldTypes.push_back(Type::unknown());
                    continue;
                }
                fieldTypes.push_back(
                    field->type
                        ? resolveType(*field->type)
                        : (field->initializer
                            ? expressionType(*field->initializer)
                            : Type::unknown()));
            }
            while (requiredCount > 0 &&
                   declaration.fields[requiredCount - 1] &&
                   declaration.fields[requiredCount - 1]->initializer) {
                --requiredCount;
            }
            Type result = instantiateRecord(
                declarationType, fieldTypes, requiredCount, declaration.name);
            popGenericTypeParameters();
            return result;
        }
    }

    Type calleeType =
        expressionType(
            *expression.callee
        );

    if (calleeIdentifier && calleeIdentifier->name == "printm") {
        warning(
            *calleeIdentifier,
            "printm() is deprecated; use printz() instead"
        );
    }

    // --------------------------------------------------------
    // Evaluate arguments
    // --------------------------------------------------------

    std::vector<Type> argumentTypes;

    const BuiltinMetadata* calledBuiltin = nullptr;
    if (calleeIdentifier) {
        calledBuiltin = builtinRegistry_.find(calleeIdentifier->name);
    }

    std::vector<Type> resolvedExplicitTypeArguments;
    TypeSubstitution argumentContextSubstitutions;
    if (calleeType.kind == TypeKind::Function &&
        !calleeType.genericParameters.empty() &&
        expression.explicitTypeArguments.size() ==
            calleeType.genericParameters.size()) {
        resolvedExplicitTypeArguments.reserve(
            expression.explicitTypeArguments.size());
        for (std::size_t index = 0;
             index < expression.explicitTypeArguments.size();
             ++index) {
            Type argument = expression.explicitTypeArguments[index]
                ? resolveType(*expression.explicitTypeArguments[index])
                : Type::unknown();
            argumentContextSubstitutions.emplace(
                calleeType.genericParameters[index].id, argument);
            resolvedExplicitTypeArguments.push_back(std::move(argument));
        }
    }

    for (std::size_t index = 0;
         index < expression.arguments.size();
         ++index) {
        const auto& argument = expression.arguments[index];
        if (!argument) {
            argumentTypes.push_back(Type::unknown());
            continue;
        }

        const Type* expectedType = nullptr;
        std::optional<Type> contextualExpectedType;
        if (calledBuiltin &&
            !calledBuiltin->signature.parameters.empty()) {
            const std::size_t parameter =
                std::min(index, calledBuiltin->signature.parameters.size() - 1);
            expectedType = &calledBuiltin->signature.parameters[parameter];
        } else if (calleeType.kind == TypeKind::Function &&
                   index < calleeType.parameters.size()) {
            contextualExpectedType =
                substitute(calleeType.parameters[index],
                           argumentContextSubstitutions);
            expectedType = &*contextualExpectedType;
        }

        const auto* array =
            as<zplus::ast::ArrayExpression>(argument.get());
        const std::optional<Type> previousExpectedType =
            expectedExpressionType_;
        if (array && array->elements.empty() &&
            expectedType && expectedType->kind == TypeKind::Array &&
            !hasTypeParameter(*expectedType)) {
            expectedExpressionType_ = *expectedType;
        }
        argumentTypes.push_back(expressionType(*argument));
        expectedExpressionType_ = previousExpectedType;
    }

    if (calleeIdentifier) {
        if (const BuiltinMetadata* builtin =
                builtinRegistry_.find(calleeIdentifier->name)) {
            const std::size_t parameterCount =
                builtin->signature.parameters.size();
            if (argumentTypes.size() < builtin->minimumArgumentCount()) {
                error(expression, builtin->name + " expects at least " +
                    std::to_string(builtin->minimumArgumentCount()) +
                    (builtin->minimumArgumentCount() == 1 ? " argument" : " arguments"),
                    zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
            if ((!builtin->variadic && argumentTypes.size() > parameterCount) ||
                (parameterCount == 0 && !argumentTypes.empty())) {
                error(expression, "too many arguments to " + builtin->name,
                    zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }

            TypeSubstitution substitutions;
            bool validArguments = true;
            for (std::size_t i = 0; i < argumentTypes.size(); ++i) {
                if (parameterCount == 0) break;
                const Type& expected =
                    builtin->signature.parameters[std::min(i, parameterCount - 1)];
                if (i == 0 && expected.kind == TypeKind::Array &&
                    argumentTypes[i].kind != TypeKind::Array &&
                    argumentTypes[i].kind != TypeKind::Unknown) {
                    error(expression, builtin->name +
                        " expects an array as its first argument",
                        zplus::DiagnosticCategory::Type);
                    validArguments = false;
                    continue;
                }
                if (!unify(expected, argumentTypes[i], substitutions)) {
                    if ((builtin->name == "insert" || builtin->name == "remove") &&
                        i == 1) {
                        error(expression, builtin->name +
                            " index must be a number",
                            zplus::DiagnosticCategory::Type);
                    } else if (i == 0 && parameterCount == 1) {
                        error(expression, builtin->name + " expects a " +
                            typeName(expected) + ", got '" +
                            typeName(argumentTypes[i]) + "'",
                            zplus::DiagnosticCategory::Type);
                    } else if (expected.kind == TypeKind::TypeParameter) {
                        error(expression, builtin->name + " value has type '" +
                            typeName(argumentTypes[i]) + "', expected '" +
                            typeName(substitute(expected, substitutions)) + "'",
                            zplus::DiagnosticCategory::Type);
                    } else {
                        error(expression, builtin->name + " argument " +
                            std::to_string(i + 1) + " has type '" +
                            typeName(argumentTypes[i]) + "', expected '" +
                            typeName(substitute(expected, substitutions)) + "'",
                            zplus::DiagnosticCategory::Type);
                    }
                    validArguments = false;
                }
            }
            if (!validArguments) return Type::unknown();
            if (!builtin->signature.returnType) return Type::voidType();
            return substitute(*builtin->signature.returnType, substitutions);
        }
    }

    if (calleeType.kind == TypeKind::Unknown) {
        return Type::unknown();
    }

    if (calleeType.kind != TypeKind::Function) {
        error(expression, "expression is not callable");
        return Type::unknown();
    }

    const auto* memberCallee =
        as<zplus::ast::MemberExpression>(expression.callee.get());
    if (memberCallee && memberCallee->requiresReceiver) {
        expression.resolvedTypeArguments =
            memberCallee->resolvedGenericTypeArguments;
    } else {
        expression.resolvedTypeArguments.clear();
    }

    if (!calleeType.genericParameters.empty()) {
        const auto& genericParameters = calleeType.genericParameters;
        if (argumentTypes.size() > calleeType.parameters.size()) {
            error(expression, "too many arguments to generic function",
                  zplus::DiagnosticCategory::Type);
            return Type::unknown();
        }
        if (argumentTypes.size() < calleeType.requiredParameterCount) {
            error(expression, "generic function expects at least " +
                std::to_string(calleeType.requiredParameterCount) +
                (calleeType.requiredParameterCount == 1
                    ? " argument" : " arguments"),
                zplus::DiagnosticCategory::Type);
            return Type::unknown();
        }
        if (!expression.explicitTypeArguments.empty() &&
            expression.explicitTypeArguments.size() != genericParameters.size()) {
            error(expression, "generic function expects exactly " +
                std::to_string(genericParameters.size()) +
                " explicit type argument(s)",
                zplus::DiagnosticCategory::Type);
            return Type::unknown();
        }

        TypeSubstitution substitutions;
        if (!expression.explicitTypeArguments.empty()) {
            for (std::size_t index = 0;
                 index < expression.explicitTypeArguments.size(); ++index) {
                const Type& argument =
                    resolvedExplicitTypeArguments[index];
                const auto& parameter = genericParameters[index];
                bool satisfiesConstraints = true;
                for (const TypeConstraint constraint : parameter.constraints) {
                    if (argument.kind == TypeKind::TypeParameter) {
                        satisfiesConstraints =
                            std::find(argument.constraints.begin(),
                                      argument.constraints.end(),
                                      constraint) != argument.constraints.end();
                    } else if (constraint == TypeConstraint::Numeric) {
                        satisfiesConstraints =
                            argument.kind == TypeKind::Number;
                    } else {
                        satisfiesConstraints =
                            argument.kind == TypeKind::Number ||
                            argument.kind == TypeKind::String ||
                            argument.kind == TypeKind::Character ||
                            argument.kind == TypeKind::Boolean ||
                            argument.kind == TypeKind::Null ||
                            argument.kind == TypeKind::Custom ||
                            argument.kind == TypeKind::Array ||
                            argument.kind == TypeKind::Pointer;
                    }
                    if (!satisfiesConstraints) break;
                }
                if (!satisfiesConstraints) {
                    error(*expression.explicitTypeArguments[index],
                          "type argument '" + typeName(argument) +
                              "' does not satisfy constraints for '" +
                              parameter.name + "'",
                          zplus::DiagnosticCategory::Type);
                    return Type::unknown();
                }
                substitutions.emplace(parameter.id, std::move(argument));
            }
        }

        const std::size_t count = std::min(
            argumentTypes.size(), calleeType.parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (!unify(calleeType.parameters[index], argumentTypes[index],
                       substitutions)) {
                error(*expression.arguments[index],
                      "generic argument " + std::to_string(index + 1) +
                          " has type '" + typeName(argumentTypes[index]) +
                          "', which conflicts with parameter type '" +
                          typeName(calleeType.parameters[index]) + "'",
                      zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
        }
        if (expectedReturnType &&
            expectedReturnType->kind != TypeKind::Unknown &&
            calleeType.returnType &&
            !unify(*calleeType.returnType, *expectedReturnType,
                   substitutions)) {
            error(expression, "generic return type cannot be inferred as '" +
                typeName(*expectedReturnType) + "'",
                zplus::DiagnosticCategory::Type);
            return Type::unknown();
        }

        expression.resolvedTypeArguments.reserve(
            expression.resolvedTypeArguments.size() + genericParameters.size());
        for (const auto& parameter : genericParameters) {
            Type argument = substitute(
                Type::typeParameter(parameter.id, parameter.name,
                                    parameter.constraints),
                substitutions);
            if ((argument.kind == TypeKind::TypeParameter &&
                 argument.name == parameter.id) ||
                argument.kind == TypeKind::Unknown) {
                error(expression, "cannot infer generic type parameter '" +
                    parameter.name + "'",
                    zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
            expression.resolvedTypeArguments.push_back(std::move(argument));
        }
        if (calleeType.returnType) {
            return substitute(*calleeType.returnType, substitutions);
        }
        return Type::voidType();
    }

    if (!expression.explicitTypeArguments.empty()) {
        error(expression, "type arguments were provided to a non-generic function",
              zplus::DiagnosticCategory::Type);
        return Type::unknown();
    }

    // --------------------------------------------------------
    // Parameter count
    // --------------------------------------------------------

    if (
        argumentTypes.size() >
        calleeType.parameters.size()
    ) {

        error(
            expression,
            "too many arguments"
        );

    } else if (argumentTypes.size() < calleeType.requiredParameterCount) {

        error(
            expression,
            "not enough arguments"
        );
    }

    // --------------------------------------------------------
    // Parameter types
    // --------------------------------------------------------

    const std::size_t count =
        argumentTypes.size() <
            calleeType.parameters.size()
            ? argumentTypes.size()
            : calleeType.parameters.size();

    for (
        std::size_t i = 0;
        i < count;
        ++i
    ) {

        if (
            !isAssignable(
                calleeType.parameters[i],
                argumentTypes[i]
            )
        ) {

            error(
                expression,
                "argument " +
                std::to_string(i + 1) +
                " has type '" +
                typeName(argumentTypes[i]) +
                "', expected '" +
                typeName(
                    calleeType.parameters[i]
                ) +
                "'"
            );
        }
    }

    if (
        calleeType.returnType
    ) {

        return *
            calleeType.returnType;
    }

    return Type::voidType();
}


// ============================================================
// Member
// ============================================================

Type Analyzer::typeOfMember(
    const zplus::ast::MemberExpression& expression
) {

    if (!expression.object) {

        error(
            expression,
            "missing object in member access"
        );

        return Type::unknown();
    }

    const auto* moduleIdentifier =
        as<zplus::ast::Identifier>(expression.object.get());

    if (moduleIdentifier) {
        const std::string namespaceFunctionName =
            moduleIdentifier->name + "::" + expression.member;
        if (const Symbol* namespaceFunction =
                findSymbol(namespaceFunctionName);
            namespaceFunction && namespaceFunction->isFunction) {
            const std::string sourceModule =
                moduleNameForFile(expression.location.sourceFile);
            if (!namespaceFunction->isPublic &&
                namespaceFunction->moduleName != sourceModule) {
                error(expression, "declaration '" + expression.member +
                                      "' is private to module '" +
                                      namespaceFunction->moduleName + "'");
                return Type::unknown();
            }
            expression.resolvedName = namespaceFunctionName;
            return namespaceFunction->type;
        }

        const std::string sourceModule =
            moduleNameForFile(expression.location.sourceFile);
        std::string enumKey = moduleIdentifier->name;
        if (!sourceModule.empty()) {
            const std::string localKey =
                sourceModule + "::" + moduleIdentifier->name;
            if (enumDeclarations_.contains(localKey)) enumKey = localKey;
        }

        const auto enumeration = enumDeclarations_.find(enumKey);
        if (enumeration != enumDeclarations_.end()) {
            const auto member = std::find_if(
                enumeration->second->values.begin(),
                enumeration->second->values.end(),
                [&expression](const auto& value) {
                    return value && value->name == expression.member;
                });
            if (member == enumeration->second->values.end()) {
                error(expression, "enum '" + enumeration->second->name +
                                      "' has no member '" + expression.member + "'");
                return Type::unknown();
            }
            return Type::custom(
                enumeration->second->moduleName.empty()
                    ? enumeration->second->name
                    : enumeration->second->moduleName + "::" +
                          enumeration->second->name
            );
        }

        for (const auto& [key, candidate] : enumDeclarations_) {
            (void)key;
            if (candidate && candidate->moduleName == moduleIdentifier->name &&
                candidate->name == expression.member) {
                return Type::custom(candidate->moduleName + "::" + candidate->name);
            }
        }
    }

    if (moduleIdentifier) {
        const Symbol* symbol =
            findModuleSymbol(moduleIdentifier->name, expression.member);
        if (symbol) {
            const std::string sourceModule =
                moduleNameForFile(expression.location.sourceFile);
            if (!symbol->isPublic &&
                moduleIdentifier->name != sourceModule) {
                error(expression, "declaration '" + expression.member +
                                      "' is private to module '" +
                                      moduleIdentifier->name + "'");
                return Type::unknown();
            }
            return symbol->type;
        }
    }

    Type objectType =
        expressionType(*expression.object);

    if (objectType.kind == TypeKind::Custom &&
        objectType.name == "Error" &&
        expression.member == "message") {
        return Type::string();
    }

    if (
        objectType.kind ==
        TypeKind::Unknown
    ) {
        return Type::unknown();
    }

    if (objectType.kind == TypeKind::Custom) {
        const auto enumeration = enumDeclarations_.find(objectType.name);
        if (enumeration != enumDeclarations_.end()) {
            const auto member = std::find_if(
                enumeration->second->values.begin(),
                enumeration->second->values.end(),
                [&expression](const auto& value) {
                    return value && value->name == expression.member;
                });
            if (member == enumeration->second->values.end()) {
                error(expression, "enum '" + enumeration->second->name +
                                      "' has no member '" + expression.member + "'");
                return Type::unknown();
            }
            return Type::custom(objectType.name);
        }
        const auto klass = classDeclarations_.find(objectType.name);
        if (klass != classDeclarations_.end()) {
            TypeSubstitution classSubstitution;
            const Type declarationType =
                klass->second->resolvedType.value_or(
                    Type::custom(objectType.name));
            const std::size_t classParameterCount =
                declarationType.arguments.size();
            if (objectType.arguments.size() != classParameterCount) {
                error(expression, "class type '" + typeName(objectType) +
                                      "' has invalid generic arguments",
                      zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
            for (std::size_t index = 0; index < classParameterCount; ++index) {
                if (!unify(declarationType.arguments[index],
                           objectType.arguments[index], classSubstitution)) {
                    error(expression, "invalid type arguments for class '" +
                                          klass->second->name + "'",
                          zplus::DiagnosticCategory::Type);
                    return Type::unknown();
                }
            }
            expression.resolvedGenericTypeArguments = objectType.arguments;
            const auto rejectInaccessible = [this, &expression, &klass](
                zplus::ast::MemberVisibility visibility,
                const std::string& memberName
            ) {
                if (visibility ==
                        zplus::ast::MemberVisibility::Public ||
                    currentClassName_ == klass->first) {
                    return false;
                }
                const bool protectedMember = visibility ==
                    zplus::ast::MemberVisibility::Protected;
                error(expression,
                      std::string(protectedMember ? "protected" : "private") +
                          " member '" + memberName + "' of class '" +
                          klass->second->name + "' is not accessible here",
                      zplus::DiagnosticCategory::Type);
                return true;
            };
            for (const auto& field : klass->second->fields) {
                if (field && field->name == expression.member) {
                    if (rejectInaccessible(field->visibility, field->name)) {
                        return Type::unknown();
                    }
                    const Type fieldType = field->resolvedType
                        ? *field->resolvedType
                        : field->type
                        ? resolveType(*field->type)
                        : (field->initializer
                               ? expressionType(*field->initializer)
                               : Type::unknown());
                    return substitute(fieldType, classSubstitution);
                }
            }
            for (const auto& method : klass->second->methods) {
                if (!method || method->name != expression.member) continue;
                if (rejectInaccessible(method->memberVisibility, method->name)) {
                    return Type::unknown();
                }
                Type methodType = method->resolvedType.value_or(Type::function(
                    {}, method->returnType
                            ? resolveType(*method->returnType)
                            : Type::voidType()));
                for (Type& parameter : methodType.parameters) {
                    parameter = substitute(parameter, classSubstitution);
                }
                if (methodType.returnType) {
                    methodType.returnType = std::make_shared<Type>(
                        substitute(*methodType.returnType, classSubstitution));
                }
                if (methodType.genericParameters.size() >= classParameterCount) {
                    methodType.genericParameters.erase(
                        methodType.genericParameters.begin(),
                        methodType.genericParameters.begin() +
                            static_cast<std::ptrdiff_t>(classParameterCount));
                }
                expression.resolvedName = method->internalName.empty()
                    ? klass->second->name + "::" + method->name
                    : method->internalName;
                expression.requiresReceiver = true;
                return methodType;
            }
            error(expression, "class '" + klass->second->name +
                                  "' has no field or method '" +
                                  expression.member + "'");
            return Type::unknown();
        }
        const auto structure = structDeclarations_.find(objectType.name);
        if (structure != structDeclarations_.end()) {
            const Type declarationType =
                structure->second->resolvedType.value_or(
                    Type::custom(objectType.name));
            if (objectType.arguments.size() != declarationType.arguments.size()) {
                error(expression, "struct type '" + typeName(objectType) +
                                      "' has invalid generic arguments",
                      zplus::DiagnosticCategory::Type);
                return Type::unknown();
            }
            TypeSubstitution substitutions;
            for (std::size_t index = 0;
                 index < declarationType.arguments.size(); ++index) {
                if (!unify(declarationType.arguments[index],
                           objectType.arguments[index], substitutions)) {
                    error(expression, "invalid type arguments for struct '" +
                                          structure->second->name + "'",
                          zplus::DiagnosticCategory::Type);
                    return Type::unknown();
                }
            }
            for (const auto& field : structure->second->fields) {
                if (field && field->name == expression.member) {
                    const Type fieldType = field->resolvedType
                        ? *field->resolvedType
                        : field->type
                        ? resolveType(*field->type)
                        : Type::unknown();
                    return substitute(fieldType, substitutions);
                }
            }
            error(expression, "struct '" + structure->second->name +
                                  "' has no field '" + expression.member + "'");
            return Type::unknown();
        }
    }

    error(
        expression,
        "member access is not supported for type '" +
            typeName(objectType) + "'"
    );
    return Type::unknown();
}


// ============================================================
// Index
// ============================================================

Type Analyzer::typeOfIndex(
    const zplus::ast::IndexExpression& expression
) {

    if (
        !expression.object ||
        !expression.index
    ) {

        error(
            expression,
            "invalid index expression"
        );

        return Type::unknown();
    }

    Type objectType =
        expressionType(
            *expression.object
        );

    Type indexType =
        expressionType(
            *expression.index
        );

    if (
        !isNumeric(indexType) &&
        !isUnknown(indexType)
    ) {

        error(
            *expression.index,
            "array index must be a number"
        );
    }

    if (
        objectType.kind ==
        TypeKind::Array
    ) {

        if (objectType.elementType) {
            return *objectType.elementType;
        }
    }

    if (
        objectType.kind ==
        TypeKind::Pointer
    ) {

        if (objectType.elementType) {
            return *objectType.elementType;
        }
    }

    if (
        objectType.kind !=
        TypeKind::Unknown
    ) {

        error(
            expression,
            "value is not indexable"
        );
    }

    return Type::unknown();
}


// ============================================================
// Array
// ============================================================

Type Analyzer::typeOfArray(
    const zplus::ast::ArrayExpression& expression
) {

    if (expression.elements.empty()) {
        if (expectedExpressionType_ &&
            expectedExpressionType_->kind == TypeKind::Array) {
            return *expectedExpressionType_;
        }
        return Type::array(
            Type::unknown()
        );
    }

    Type elementType = Type::unknown();
    bool hasConcreteElement = false;
    bool hasUnknownElement = false;

    for (const auto& element :
         expression.elements) {

        if (!element) {
            continue;
        }

        Type currentType =
            expressionType(
                *element
            );

        if (isUnknown(currentType)) {
            hasUnknownElement = true;
            continue;
        }

        if (!hasConcreteElement) {
            elementType = currentType;
            hasConcreteElement = true;
            continue;
        }

        if (
            !typesEqual(
                elementType,
                currentType
            )
        ) {

            error(
                *element,
                "array elements must have compatible types"
            );
        }
    }

    return Type::array(
        hasUnknownElement ? Type::unknown() : std::move(elementType)
    );
}


// ============================================================
// Assignment target
// ============================================================

bool Analyzer::isValidAssignmentTarget(
    const zplus::ast::Expression& target
) const {

    if (
        as<
            zplus::ast::Identifier
        >(&target)
    ) {
        return true;
    }

    if (
        as<
            zplus::ast::MemberExpression
        >(&target)
    ) {
        return true;
    }

    if (
        as<
            zplus::ast::IndexExpression
        >(&target)
    ) {
        return true;
    }

    return false;
}

Type Analyzer::targetType(
    const zplus::ast::Expression& target
) {

    if (
        const auto* identifier =
            as<
                zplus::ast::Identifier
            >(&target)
    ) {

        Symbol* symbol =
            findSymbol(
                identifier->name
            );

        if (!symbol) {

            error(
                *identifier,
                "undefined variable '" +
                identifier->name +
                "'"
            );

            return Type::unknown();
        }

        identifier->bindingName = symbol->bindingName.empty()
            ? symbol->name
            : symbol->bindingName;
        return symbol->type;
    }

    if (
        const auto* index =
            as<
                zplus::ast::IndexExpression
            >(&target)
    ) {
        const Type result = typeOfIndex(*index);
        index->resolvedType = result;
        return result;
    }

    if (
        const auto* member =
            as<
                zplus::ast::MemberExpression
            >(&target)
    ) {
        const Type result = typeOfMember(*member);
        member->resolvedType = result;
        return result;
    }

    return Type::unknown();
}

} // namespace zplus::semantic
