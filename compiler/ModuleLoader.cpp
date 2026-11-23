#include "ModuleLoader.hpp"

#include "lexer/Lexer.hpp"
#include "parser/Parser.hpp"
#include "../runtime/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace zplus {
namespace {

std::filesystem::path pathFromUtf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(encoded);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

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
#ifdef _WIN32
    const DWORD required = GetEnvironmentVariableW(L"ZPLUS_STDLIB_PATH", nullptr, 0);
    if (required > 0) {
        std::wstring configured(static_cast<std::size_t>(required), L'\0');
        const DWORD length = GetEnvironmentVariableW(
            L"ZPLUS_STDLIB_PATH", configured.data(), required);
        if (length == 0 || length >= required) {
            throw std::runtime_error("cannot read ZPLUS_STDLIB_PATH");
        }
        configured.resize(length);
        return std::filesystem::path(configured);
    }
#else
    if (const char* configured = std::getenv("ZPLUS_STDLIB_PATH");
        configured && *configured) {
        return pathFromUtf8(configured);
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
    return pathFromUtf8(ZPLUS_STDLIB_DIR);
#else
    return {};
#endif
}

const json::Value* objectField(const json::Value& value, std::string_view name) {
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) return nullptr;
    for (const auto& [key, item] : *object) {
        if (key == name) return &item;
    }
    return nullptr;
}

std::string stringField(const json::Value& value, std::string_view name) {
    const auto* field = objectField(value, name);
    const auto* text = field
        ? std::get_if<std::string>(&field->data)
        : nullptr;
    if (!text) return {};
    return *text;
}

std::filesystem::path packageEntry(
    const std::filesystem::path& packageRoot,
    const std::string& moduleName
) {
    if (moduleName.empty() ||
        !std::all_of(moduleName.begin(), moduleName.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-';
        })) {
        return {};
    }
    const auto packageDirectory = packageRoot / pathFromUtf8(moduleName);
    std::error_code error;
    const auto canonicalPackage = std::filesystem::canonical(packageDirectory, error);
    if (error || !std::filesystem::is_directory(canonicalPackage)) return {};
    const auto canonicalRoot = std::filesystem::canonical(packageRoot, error);
    if (error) return {};
    const auto relativePackage = canonicalPackage.lexically_relative(canonicalRoot);
    if (relativePackage.empty() || relativePackage.is_absolute() ||
        *relativePackage.begin() == "..") return {};
    std::ifstream manifestFile(canonicalPackage / "zpm.json", std::ios::binary);
    if (!manifestFile) return {};
    std::string contents;
    contents.resize(1024 * 1024 + 1);
    manifestFile.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    const auto read = manifestFile.gcount();
    if (read < 0 || static_cast<std::size_t>(read) > 1024 * 1024) {
        throw std::runtime_error(
            "package manifest exceeds the 1 MiB limit: '" +
            pathToUtf8(canonicalPackage / "zpm.json") + "'");
    }
    contents.resize(static_cast<std::size_t>(read));
    const auto manifest = json::parse(contents);
    if (stringField(manifest, "name") != moduleName) {
        throw std::runtime_error(
            "installed package directory name does not match package manifest: '" +
            pathToUtf8(canonicalPackage) + "'");
    }
    const std::string main = stringField(manifest, "main");
    if (main.empty()) {
        throw std::runtime_error(
            "installed package manifest has no main module: '" +
            pathToUtf8(canonicalPackage / "zpm.json") + "'");
    }
    const auto relativeMain = pathFromUtf8(main);
    if (relativeMain.empty() || relativeMain.is_absolute() ||
        relativeMain.has_root_name()) {
        throw std::runtime_error(
            "installed package main module must be a relative path");
    }
    const auto normalizedMain = relativeMain.lexically_normal();
    if (normalizedMain.empty() || *normalizedMain.begin() == "..") {
        throw std::runtime_error(
            "installed package main module escapes its package directory");
    }
    const auto entry = std::filesystem::canonical(
        canonicalPackage / normalizedMain, error);
    if (error || entry.extension() != ".zh" ||
        !std::filesystem::is_regular_file(entry)) {
        throw std::runtime_error(
            "installed package main module is not an existing .zh file");
    }
    const auto relativeEntry = entry.lexically_relative(canonicalPackage);
    if (relativeEntry.empty() || relativeEntry.is_absolute() ||
        *relativeEntry.begin() == "..") {
        throw std::runtime_error(
            "installed package main module resolves outside its package directory");
    }
    return entry;
}

std::string pathKey(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    std::string key(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z'
            ? static_cast<char>(ch - 'A' + 'a')
            : static_cast<char>(ch);
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

struct StandardModuleResolution {
    std::filesystem::path path;
    std::string defaultAlias;
    std::string canonicalName;
    bool deprecated = false;
    bool invalidNamespace = false;
};

bool validNamespaceSegment(std::string_view segment) {
    if (segment.empty() ||
        !(std::isalpha(static_cast<unsigned char>(segment.front())) ||
          segment.front() == '_')) {
        return false;
    }
    return std::all_of(segment.begin() + 1, segment.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

std::vector<std::string> splitNamespace(std::string_view name) {
    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const std::size_t separator = name.find('.', begin);
        const std::size_t end = separator == std::string_view::npos
            ? name.size()
            : separator;
        segments.emplace_back(name.substr(begin, end - begin));
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    return segments;
}

std::filesystem::path standardModuleEntry(
    const std::filesystem::path& standardLibrary,
    const std::vector<std::string>& segments
) {
    if (standardLibrary.empty() || segments.empty()) return {};
    std::filesystem::path entry = standardLibrary / "zp";
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        entry /= pathFromUtf8(segments[index]);
    }
    const std::string& leaf = segments.back();
    if (segments.size() == 1) {
        entry /= pathFromUtf8(leaf);
        entry /= pathFromUtf8(leaf + ".zh");
    } else {
        entry /= pathFromUtf8(leaf + ".zh");
    }
    return entry;
}

std::optional<StandardModuleResolution> resolveStandardModuleName(
    const std::filesystem::path& standardLibrary,
    const std::string& modulePath
) {
    if (modulePath == "math") {
        return StandardModuleResolution{
            standardLibrary / "zp" / "math" / "legacy.zh",
            "math",
            "zp.math.legacy",
            true,
            false
        };
    }

    if (modulePath == "zp" || modulePath.rfind("zp.", 0) == 0) {
        const auto names = modulePath == "zp"
            ? std::vector<std::string>{}
            : splitNamespace(std::string_view(modulePath).substr(3));
        if (names.empty() ||
            !std::all_of(names.begin(), names.end(), validNamespaceSegment)) {
            return StandardModuleResolution{{}, {}, {}, false, true};
        }
        return StandardModuleResolution{
            standardModuleEntry(standardLibrary, names),
            names.front(),
            "zp." + modulePath.substr(3),
            false,
            false
        };
    }

    if (!modulePath.empty() && modulePath.front() == 'z') {
        auto names = splitNamespace(modulePath);
        if (names.front().size() > 1) names.front().erase(names.front().begin());
        else return std::nullopt;
        if (!std::all_of(names.begin(), names.end(), validNamespaceSegment)) {
            return std::nullopt;
        }
        std::string canonicalName = "zp";
        for (const auto& name : names) canonicalName += "." + name;
        const auto aliasEnd = modulePath.find('.');
        const std::string legacyAlias = modulePath.substr(0, aliasEnd);
        return StandardModuleResolution{
            standardModuleEntry(standardLibrary, names),
            legacyAlias,
            std::move(canonicalName),
            true,
            false
        };
    }

    return std::nullopt;
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
    packageRoots_.clear();

    std::error_code error;
    entryPath_ = entrySource
        ? std::filesystem::weakly_canonical(entryPath, error)
        : std::filesystem::canonical(entryPath, error);
    if (error) {
        addDiagnostic(
            "cannot resolve entry source file '" + pathToUtf8(entryPath) + "'",
            pathToUtf8(entryPath)
        );
        return std::move(result_);
    }
    if (entrySource && !std::filesystem::is_directory(entryPath_.parent_path(), error)) {
        addDiagnostic(
            "cannot resolve entry source directory '" +
                pathToUtf8(entryPath_.parent_path()) + "'",
            pathToUtf8(entryPath_)
        );
        return std::move(result_);
    }
    for (auto current = entryPath_.parent_path(); !current.empty();) {
        const auto packages = current / "z+_packages";
        error.clear();
        if (std::filesystem::is_directory(packages, error) && !error) {
            std::error_code canonicalError;
            const auto canonicalPackages =
                std::filesystem::canonical(packages, canonicalError);
            if (!canonicalError) packageRoots_.push_back(canonicalPackages);
        }
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    std::string entryExtension = entryPath_.extension().string();
    std::transform(entryExtension.begin(), entryExtension.end(), entryExtension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (entryExtension != ".zp" && !(entrySource && entryExtension == ".zh")) {
        addDiagnostic(
            "entry source file must use the .zp extension",
            pathToUtf8(entryPath_)
        );
        return std::move(result_);
    }

    std::vector<std::string> activePaths;
    if (!loadModule(entryPath_, true, activePaths, {}, entrySource)) {
        return std::move(result_);
    }

    result_.program = std::make_unique<ast::Program>();
    result_.program->location.sourceFile = pathToUtf8(entryPath_);
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
            "module file '" + pathToUtf8(requestedPath) + "' does not exist or cannot be read",
            pathToUtf8(requestedPath)
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
        addDiagnostic(chain.str(), pathToUtf8(canonicalPath));
        return false;
    }

    if (loadedPaths_.contains(identity)) {
        const auto knownName = moduleNamesByPath_.find(identity);
        if (!requestedName.empty() && knownName != moduleNamesByPath_.end() &&
            knownName->second != requestedName) {
            addDiagnostic(
                "module '" + pathToUtf8(canonicalPath) + "' is used with conflicting names '" +
                    knownName->second + "' and '" + requestedName + "'",
                pathToUtf8(canonicalPath)
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
                "module file '" + pathToUtf8(canonicalPath) + "' must use the .zh extension",
                pathToUtf8(canonicalPath)
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
                "cannot open module file '" + pathToUtf8(canonicalPath) + "'",
                pathToUtf8(canonicalPath)
            );
            return false;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        source = buffer.str();
    }

    lexer::Lexer lexer(source, pathToUtf8(canonicalPath));
    auto tokens = lexer.tokenize();
    for (const auto& error : lexer.errors()) {
        addDiagnostic(
            error.message,
            pathToUtf8(canonicalPath),
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
            pathToUtf8(canonicalPath),
            error.line,
            error.column,
            "E1001"
        );
    }
    if (!result_.diagnostics.empty() || !program) return false;

    const std::string moduleName = requestedName.empty()
        ? moduleIdentifier(pathToUtf8(canonicalPath.stem()))
        : moduleIdentifier(requestedName);
    if (!entryModule) {
        const auto sameName = modulePathsByName_.find(moduleName);
        if (sameName != modulePathsByName_.end() && sameName->second != identity) {
            addDiagnostic(
                "ambiguous module name '" + moduleName + "' refers to both '" +
                    sameName->second + "' and '" + identity + "'; use a distinct alias",
                pathToUtf8(canonicalPath)
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
        const std::filesystem::path usePath = pathFromUtf8(modulePath);
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
                pathToUtf8(canonicalPath),
                use->location.line,
                use->location.column
            );
            activePaths.pop_back();
            return false;
        }

        auto resolvedPath = canonicalPath.parent_path() / usePath;
        std::error_code existenceError;
        std::string standardAlias;
        if ((existenceError || !std::filesystem::exists(resolvedPath, existenceError)) &&
            standardModuleName) {
            const auto standardModule = resolveStandardModuleName(
                standardLibraryDirectory(), modulePath);
            if (standardModule && standardModule->invalidNamespace) {
                addDiagnostic(
                    "invalid standard-library namespace '" + use->path +
                        "'; expected 'zp.<library>[.<module>...]'",
                    pathToUtf8(canonicalPath),
                    use->location.line,
                    use->location.column,
                    "E3002"
                );
                activePaths.pop_back();
                return false;
            }
            if (standardModule) {
                existenceError.clear();
                if (std::filesystem::exists(standardModule->path, existenceError) &&
                    !existenceError) {
                    resolvedPath = standardModule->path;
                    standardAlias = standardModule->defaultAlias;
                    if (standardModule->deprecated) {
                        addWarning(
                            "standard library name '" + use->path +
                                "' is deprecated; use '" + standardModule->canonicalName + "'",
                            pathToUtf8(canonicalPath),
                            use->location.line,
                            use->location.column
                        );
                    }
                }
            }
        }
        existenceError.clear();
        if (!std::filesystem::exists(resolvedPath, existenceError) &&
            standardModuleName && modulePath.find('.') == std::string::npos) {
            for (const auto& packageRoot : packageRoots_) {
                const auto packagePath = packageEntry(packageRoot, modulePath);
                if (!packagePath.empty()) {
                    resolvedPath = packagePath;
                    break;
                }
            }
        }
        existenceError.clear();
        if (!std::filesystem::exists(resolvedPath, existenceError) || existenceError) {
            addDiagnostic(
                "module '" + use->path + "' was not found relative to '" +
                    pathToUtf8(canonicalPath.parent_path()) + "'",
                pathToUtf8(canonicalPath),
                use->location.line,
                use->location.column
            );
            activePaths.pop_back();
            return false;
        }
        std::string defaultAlias = standardAlias.empty()
            ? pathToUtf8(pathFromUtf8(modulePath).stem())
            : standardAlias;
        if (standardModuleName && standardAlias.empty()) {
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
                            "module '" + pathToUtf8(canonicalPath) +
                                "' start block may contain declarations only",
                            pathToUtf8(canonicalPath),
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
                "module '" + pathToUtf8(canonicalPath) +
                    "' may contain only module directives and declarations in its start block",
                pathToUtf8(canonicalPath),
                statement->location.line,
                statement->location.column
            );
            return false;
        }
        statements.push_back(std::move(statement));
    }

    loadedPaths_.insert(identity);
    result_.sourceFiles.push_back(canonicalPath);
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

void ModuleLoader::addWarning(
    const std::string& message,
    const std::string& sourceFile,
    int line,
    int column,
    const std::string& code
) {
    result_.warnings.push_back({message, sourceFile, line, column, code});
}

} // namespace zplus
