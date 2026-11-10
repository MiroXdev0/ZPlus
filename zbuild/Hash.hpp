#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace zbuild {

std::string sha256(std::string_view value);
std::string sha256File(const std::filesystem::path& path);

} // namespace zbuild
