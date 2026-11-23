#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace zbuild {

std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(std::string_view value);
void writeFileAtomically(const std::filesystem::path& path, std::string_view contents);
void replaceFileAtomically(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
);
void writeFileIfChangedAtomically(
    const std::filesystem::path& path,
    std::string_view contents
);

} // namespace zbuild
