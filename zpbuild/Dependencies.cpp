#include "Dependencies.hpp"

#include "Filesystem.hpp"
#include "Process.hpp"

#include "../runtime/Json.hpp"

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace zbuild {

std::vector<std::filesystem::path> compilerDependencies(
    const std::filesystem::path& compiler,
    const std::filesystem::path& entry,
    const std::filesystem::path& projectRoot
) {
    const ProcessResult dependencies = runProcess(
        {pathToUtf8(compiler), "dependencies", pathToUtf8(entry)}, projectRoot);
    if (dependencies.exitCode != 0) {
        throw std::runtime_error("compiler dependency discovery failed:\n" +
            dependencies.output);
    }
    const zplus::json::Value root = zplus::json::parse(dependencies.output);
    const auto* values = std::get_if<zplus::json::Value::Array>(&root.data);
    if (values == nullptr) {
        throw std::runtime_error("compiler returned a non-array dependency result");
    }
    std::vector<std::filesystem::path> result;
    result.reserve(values->size());
    for (const auto& value : *values) {
        const auto* path = std::get_if<std::string>(&value.data);
        if (path == nullptr || path->empty()) {
            throw std::runtime_error(
                "compiler returned a non-string or empty dependency path");
        }
        result.push_back(pathFromUtf8(*path));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace zbuild
