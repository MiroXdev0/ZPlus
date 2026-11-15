#pragma once

#include <filesystem>
#include <string>

namespace zbuild {

void prepareRegistryPackages(
    const std::filesystem::path& projectRoot,
    const std::string& compilerVersion,
    bool locked,
    bool forceUpdate = false
);

} // namespace zbuild
