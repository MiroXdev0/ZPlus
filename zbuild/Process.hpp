#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace zbuild {

struct ProcessResult {
    int exitCode = -1;
    std::string output;
};

ProcessResult runProcess(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& workingDirectory
);

std::filesystem::path findExecutable(const std::string& name);

} // namespace zbuild
