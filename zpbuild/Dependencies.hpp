#pragma once

#include <filesystem>
#include <vector>

namespace zbuild {

std::vector<std::filesystem::path> compilerDependencies(
    const std::filesystem::path& compiler,
    const std::filesystem::path& entry,
    const std::filesystem::path& projectRoot
);

} // namespace zbuild
