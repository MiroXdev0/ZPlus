#pragma once

#include "Config.hpp"

#include <string>
#include <vector>

namespace zbuild {

std::vector<std::string> targetBuildOrder(
    const std::vector<TargetConfig>& targets,
    const std::string& selectedTarget = {}
);

} // namespace zbuild
