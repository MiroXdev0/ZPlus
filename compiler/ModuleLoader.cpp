#include "ModuleLoader.hpp"

#include "lexer/Lexer.hpp"
#include "parser/Parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace zplus {
namespace {

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path.parent_path();
#else
    return {};
#endif
}

std::filesystem::path standardLibraryDirectory() {
#ifdef _MSC_VER
    char* configured = nullptr;
    std::size_t configuredLength = 0;
    if (_dupenv_s(&configured, &configuredLength, "ZPLUS_STDLIB_PATH") == 0 &&
        configured) {
        const std::string value(configured);
        std::free(configured);
        if (!value.empty()) return std::filesystem::path(value);
    }
#else
    if (const char* configured = std::getenv("ZPLUS_STDLIB_PATH");
        configured && *configured) {
        return std::filesystem::path(configured);
    }
#endif
    const auto executableDir = executableDirectory();
    if (!executableDir.empty()) {
        const auto adjacentLibrary = executableDir / "stdlib";
        std::error_code directoryError;
        if (std::filesystem::is_directory(adjacentLibrary, directoryError) &&
            !directoryError) {
            return adjacentLibrary;
        }
    }
#ifdef ZPLUS_STDLIB_DIR
    return std::filesystem::path(ZPLUS_STDLIB_DIR);
#else
    return {};
#endif
}

std::string pathKey(const std::filesystem::path& path) {
    std::string key = path.generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return key;
}

bool isModuleDeclaration(const ast::Statement& statement) {
    return
        dynamic_cast<const ast::FunctionDeclaration*>(&statement) ||
        dynamic_cast<const ast::VariableDeclaration*>(&statement) ||
        dynamic_cast<const ast::StructDeclaration*>(&statement) ||
        dynamic_cast<const ast::ClassDeclaration*>(&statement) ||
        dynamic_cast<const ast::EnumDeclaration*>(&statement) ||
        dynamic_cast<const ast::NamespaceDeclaration*>(&statement) ||
        dynamic_cast<const ast::ExportDeclaration*>(&statement) ||
        dynamic_cast<const ast::ImportDeclaration*>(&statement);
}

bool hasDrivePrefix(const std::string& path) {
    return path.size() >= 2 &&
        std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':';
}

std::string moduleIdentifier(std::string name) {
    for (char& ch : name) {
        const auto value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value) && ch != '_') ch = '_';
    }
    if (name.empty()) name = "module";
    if (std::isdigit(static_cast<unsigned char>(name.front()))) {
        name.insert(name.begin(), '_');
    }
    return name;
}

} // namespace

LoadedProgram ModuleLoader::load(
    const std::filesystem::path& entryPath,
    const std::string* entrySource
) {
    result_ = {};
    moduleStatements_.clear();
    loadedPaths_.clear();
    moduleNamesByPath_.clear();
    modulePathsByName_.clear();

    std::error_code error;
    entryPath_ = entrySource
        ? std::filesystem::weakly_canonical(entryPath, error)
        : std::filesystem::canonical(entryPath, error);
    if (error) {
        addDiagnostic(
            "cannot resolve entry source file '" + entryPath.string() + "'",
            entryPath.string()
        );
        return std::move(result_);
    }
    if (entrySource && !std::filesystem::is_directory(entryPath_.parent_path(), error)) {
        addDiagnostic(
            "cannot resolve entry source directory '" + entryPath_.parent_path().string() + "'",
            entryPath_.string()
        );
        return std::move(result_);
    }
    std::string entryExtension = entryPath_.extension().string();
    std::transform(entryExtension.begin(), entryExtension.end(), entryExtension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (entryExtension != ".zp" && !(entrySource && entryExtension == ".zh")) {
        addDiagnostic(
            "entry source file must use the .zp extension",
            entryPath_.string()
        );
        return std::move(result_);
    }

    std::vector<std::string> activePaths;
    if (!loadModule(entryPath_, true, activePaths, {}, entrySource)) {
        return std::move(result_);
    }

    result_.program = std::make_unique<ast::Program>();
    result_.program->location.sourceFile = entryPath_.string();
    for (auto& statements : moduleStatements_) {
        for (auto& statement : statements) {
            result_.program->statements.push_back(std::move(statement));
        }
    }
    return std::move(result_);
}

bool ModuleLoader::loadModule(
    const std::filesystem::path& requestedPath,
    bool entryModule,
    std::vector<std::string>& activePaths,
    const std::string& requestedName,
    const std::string* sourceOverride
) {
    std::error_code pathError;
    const auto canonicalPath = entryModule && sourceOverride
        ? std::filesystem::weakly_canonical(requestedPath, pathError)
        : std::filesystem::canonical(requestedPath, pathError);
    if (pathError) {
        addDiagnostic(
            "module file '" + requestedPath.string() + "' does not exist or cannot be read",
            requestedPath.string()
        );
        return false;
    }

    const std::string identity = pathKey(canonicalPath);
    const auto cycleStart = std::find(activePaths.begin(), activePaths.end(), identity);
    if (cycleStart != activePaths.end()) {
        std::ostringstream chain;
        chain << "circular module dependency: ";
        for (auto current = cycleStart; current != activePaths.end(); ++current) {
            if (current != cycleStart) chain << " -> ";
            chain << *current;
        }
        chain << " -> " << identity;
        addDiagnostic(chain.str(), canonicalPath.string());
        return false;
    }

    if (loadedPaths_.contains(identity)) {
        const auto knownName = moduleNamesByPath_.find(identity);
        if (!requestedName.empty() && knownName != moduleNamesByPath_.end() &&
            knownName->second != requestedName) {
            addDiagnostic(
                "module '" + canonicalPath.string() + "' is used with conflicting names '" +
                    knownName->second + "' and '" + requestedName + "'",
                canonicalPath.string()
            );
            return false;
        }
        return true;
    }

    if (!entryModule) {
        std::string extension = canonicalPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (extension != ".zh") {
            addDiagnostic(
                "module file '" + canonicalPath.string() + "' must use the .zh extension",
                canonicalPath.string()
            );
            return false;
        }
    }

    std::string source;
    if (entryModule && sourceOverride) {
        source = *sourceOverride;
    } else {
        std::ifstream input(canonicalPath, std::ios::binary);
        if (!input) {
            addDiagnostic(
                "cannot open module file '" + canonicalPath.string() + "'",
                canonicalPath.string()
            );
            return false;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        source = buffer.str();
    }

    lexer::Lexer lexer(source, canonicalPath.string());
    auto tokens = lexer.tokenize();
    for (const auto& error : lexer.errors()) {
        addDiagnostic(
            error.message,
            canonicalPath.string(),
            error.line,
            error.column,
            "E1000"
        );
    }
    if (!result_.diagnostics.empty()) return false;

    parser::Parser parser(std::move(tokens));
    auto program = parser.parse();
    for (const auto& error : parser.errors()) {
        addDiagnostic(
            error.message,
            canonicalPath.string(),
            error.line,
            error.column,
            "E1001"
        );
    }
    if (!result_.diagnostics.empty() || !program) return false;

    const std::string moduleName = requestedName.empty()
        ? moduleIdentifier(canonicalPath.stem().string())
        : moduleIdentifier(requestedName);
    if (!entryModule) {
        const auto sameName = modulePathsByName_.find(moduleName);
        if (sameName != modulePathsByName_.end() && sameName->second != identity) {
            addDiagnostic(
                "ambiguous module name '" + moduleName + "' refers to both '" +
                    sameName->second + "' and '" + identity + "'; use a distinct alias",
                canonicalPath.string()
            );
            return false;
        }
        modulePathsByName_[moduleName] = identity;
        moduleNamesByPath_[identity] = moduleName;
    }

    activePaths.push_back(identity);
    for (const auto& statement : program->statements) {
        const auto* use = dynamic_cast<const ast::UseDeclaration*>(statement.get());
        if (!use) continue;

        std::string modulePath = use->path;
        std::replace(modulePath.begin(), modulePath.end(), '\\', '/');
        const std::filesystem::path usePath(modulePath);
        const bool bareDottedModule =
            usePath.parent_path().empty() &&
            modulePath.find('.') != std::string::npos &&
            usePath.extension() != ".zh";
        const bool standardModuleName =
            usePath.parent_path().empty() &&
            (usePath.extension().empty() || bareDottedModule);
        if (modulePath.empty() ||
            usePath.is_absolute() ||
            modulePath.front() == '/' ||
            hasDrivePrefix(modulePath)) {
            addDiagnostic(
                "absolute module paths are not permitted; use a path relative to the importing file",
                canonicalPath.string(),
                use->location.line,
                use->location.column
            );
            activePaths.pop_back();
            return false;
        }

        auto resolvedPath = canonicalPath.parent_path() / usePath;
        std::error_code existenceError;
        if ((existenceError || !std::filesystem::exists(resolvedPath, existenceError)) &&
            standardModuleName) {
            std::filesystem::path standardModule = standardLibraryDirectory();
            if (modulePath.find('.') == std::string::npos) {
                standardModule /= modulePath;
                standardModule /= modulePath + ".zh";
            } else {
                std::size_t begin = 0;
                std::size_t separator = modulePath.find('.');
                while (separator != std::string::npos) {
                    standardModule /= modulePath.substr(begin, separator - begin);
                    begin = separator + 1;
                    separator = modulePath.find('.', begin);
                }
                standardModule /= modulePath.substr(begin) + ".zh";
            }
            existenceError.clear();
            if (std::filesystem::exists(standardModule, existenceError) && !existenceError) {
                resolvedPath = standardModule;
            }
        }
        existenceError.clear();
        if (!std::filesystem::exists(resolvedPath, existenceError) || existenceError) {
            addDiagnostic(
                "module '" + use->path + "' was not found relative to '" +
                    canonicalPath.parent_path().string() + "'",
                canonicalPath.string(),
                use->location.line,
                use->location.column
            );
            activePaths.pop_back();
            return false;
        }
        std::string defaultAlias = std::filesystem::path(modulePath).stem().string();
        if (standardModuleName) {
            const auto separator = modulePath.find('.');
            if (bareDottedModule && separator != std::string::npos) {
                defaultAlias = modulePath.substr(0, separator);
            }
        }
        const std::string alias = use->alias.empty()
            ? moduleIdentifier(defaultAlias)
            : moduleIdentifier(use->alias);
        if (!loadModule(resolvedPath, false, activePaths, alias)) {
            activePaths.pop_back();
            return false;
        }
    }
    activePaths.pop_back();

    std::vector<ast::Stmt> statements;
    statements.reserve(program->statements.size());
    const auto tagModuleDeclaration = [&](ast::Statement& declaration) {
        ast::Statement* current = &declaration;
        if (auto* exported = dynamic_cast<ast::ExportDeclaration*>(current)) {
            current = exported->declaration.get();
        }
        if (auto* function = dynamic_cast<ast::FunctionDeclaration*>(current)) {
            function->moduleName = moduleName;
            function->internalName = moduleName + "::" + function->name;
        } else if (auto* variable = dynamic_cast<ast::VariableDeclaration*>(current)) {
            variable->moduleName = moduleName;
            variable->internalName = moduleName + "::" + variable->name;
        } else if (auto* structure = dynamic_cast<ast::StructDeclaration*>(current)) {
            structure->moduleName = moduleName;
        } else if (auto* klass = dynamic_cast<ast::ClassDeclaration*>(current)) {
            klass->moduleName = moduleName;
            const std::string className = moduleName + "::" + klass->name;
            for (auto& method : klass->methods) {
                if (!method) continue;
                method->moduleName = moduleName;
                method->ownerClass = className;
                method->internalName = className + "::" + method->name;
            }
        } else if (auto* enumeration = dynamic_cast<ast::EnumDeclaration*>(current)) {
            enumeration->moduleName = moduleName;
        }
    };

    for (auto& statement : program->statements) {
        if (!statement || dynamic_cast<ast::UseDeclaration*>(statement.get())) continue;

        if (entryModule) {
            statements.push_back(std::move(statement));
            continue;
        }

        if (auto* start = dynamic_cast<ast::StartStatement*>(statement.get())) {
            for (auto& declaration : start->body) {
                if (!declaration || !isModuleDeclaration(*declaration)) {
                    if (declaration) {
                        addDiagnostic(
                            "module '" + canonicalPath.string() +
                                "' start block may contain declarations only",
                            canonicalPath.string(),
                            declaration->location.line,
                            declaration->location.column
                        );
                    }
                    return false;
                }
                tagModuleDeclaration(*declaration);
                statements.push_back(std::move(declaration));
            }
            continue;
        }

        if (!dynamic_cast<ast::ImportDeclaration*>(statement.get())) {
            addDiagnostic(
                "module '" + canonicalPath.string() +
                    "' may contain only module directives and declarations in its start block",
                canonicalPath.string(),
                statement->location.line,
                statement->location.column
            );
            return false;
        }
        statements.push_back(std::move(statement));
    }

    loadedPaths_.insert(identity);
    moduleStatements_.push_back(std::move(statements));
    return true;
}

void ModuleLoader::addDiagnostic(
    const std::string& message,
    const std::string& sourceFile,
    int line,
    int column,
    const std::string& code
) {
    result_.diagnostics.push_back({message, sourceFile, line, column, code});
}

} // namespace zplus
