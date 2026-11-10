#pragma once

#include "Config.hpp"

#include <filesystem>
#include <unordered_map>
#include <string>
#include <vector>

namespace zbuild {

struct BuildOptions {
    std::string target;
    std::string mode;
    std::string backend;
    unsigned jobs = 0;
    bool verbose = false;
    bool cleanFirst = false;
    bool force = false;
};

struct BuildResult {
    std::vector<std::string> built;
    std::vector<std::string> reused;
    std::filesystem::path selectedOutput;
    std::unordered_map<std::string, std::filesystem::path> outputs;
};

class BuildEngine {
public:
    BuildEngine(ProjectConfig project, std::filesystem::path compiler);

    BuildResult build(const BuildOptions& options);
    int run(const BuildOptions& options, const std::vector<std::string>& arguments);
    int test(const BuildOptions& options);
    void clean(const std::string& mode);
    void printTargets(bool json) const;
    void printGraph(bool dot) const;
    void printInfo(bool json) const;
    void package(const BuildOptions& options);

    const ProjectConfig& project() const noexcept { return project_; }

private:
    ProjectConfig project_;
    std::filesystem::path compiler_;
};

} // namespace zbuild
