#pragma once

#include "parser/AST.hpp"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace zplus {

struct ModuleDiagnostic {
    std::string message;
    std::string sourceFile;
    int line = 0;
    int column = 0;
    std::string code = "E3001";
};

struct LoadedProgram {
    std::unique_ptr<ast::Program> program;
    std::vector<ModuleDiagnostic> diagnostics;
    std::vector<std::filesystem::path> sourceFiles;
};

class ModuleLoader {
public:
    LoadedProgram load(
        const std::filesystem::path& entryPath,
        const std::string* entrySource = nullptr
    );

private:
    bool loadModule(
        const std::filesystem::path& path,
        bool entryModule,
        std::vector<std::string>& activePaths,
        const std::string& requestedName = {},
        const std::string* sourceOverride = nullptr
    );

    void addDiagnostic(
        const std::string& message,
        const std::string& sourceFile,
        int line = 0,
        int column = 0,
        const std::string& code = "E3001"
    );

    std::filesystem::path entryPath_;
    LoadedProgram result_;
    std::vector<std::vector<ast::Stmt>> moduleStatements_;
    std::unordered_set<std::string> loadedPaths_;
    std::unordered_map<std::string, std::string> moduleNamesByPath_;
    std::unordered_map<std::string, std::string> modulePathsByName_;
};

} // namespace zplus
