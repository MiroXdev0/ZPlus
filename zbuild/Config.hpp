#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace zbuild {

struct TargetConfig {
    std::string name;
    std::string type = "executable";
    std::string entry;
    std::vector<std::string> sources;
    std::vector<std::string> dependencies;
    std::string backend;
    std::string mode;
    std::string outputDirectory;
};

struct ProjectConfig {
    std::filesystem::path root;
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    std::string entry;
    std::vector<std::string> sourceDirectories;
    std::vector<std::string> includeDirectories;
    std::string backend = "native";
    std::string mode = "debug";
    std::string outputDirectory = "build";
    int optimization = 0;
    unsigned jobs = 0;
    std::string compiler;
    bool warnings = true;
    bool debugSymbols = false;
    bool windows = true;
    bool linux = true;
    std::vector<std::string> assetDirectories;
    bool preserveAssetStructure = true;
    std::vector<std::string> testTargets;
    std::vector<std::string> testArguments;
    std::unordered_map<std::string, std::filesystem::path> localDependencies;
    std::vector<TargetConfig> targets;
    bool implicitTarget = false;
};

ProjectConfig loadProject(const std::filesystem::path& file);
std::filesystem::path discoverProject(const std::filesystem::path& start);
void writeLockfile(const ProjectConfig& project);

} // namespace zbuild
