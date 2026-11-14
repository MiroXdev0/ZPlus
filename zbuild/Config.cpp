#include "Config.hpp"

#include "BuildGraph.hpp"
#include "Filesystem.hpp"
#include "Hash.hpp"
#include "Template.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace zbuild {
namespace {

using Value = std::variant<std::string, std::int64_t, bool, std::vector<std::string>>;

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string parseString(const std::string& value, std::size_t line) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw std::runtime_error("zproject.toml:" + std::to_string(line) +
            ": expected a double-quoted string");
    }
    std::string result;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] != '\\') {
            result.push_back(value[i]);
            continue;
        }
        if (++i + 1 >= value.size()) {
            throw std::runtime_error("zproject.toml:" + std::to_string(line) +
                ": invalid string escape");
        }
        switch (value[i]) {
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            default:
                throw std::runtime_error("zproject.toml:" + std::to_string(line) +
                    ": unsupported string escape");
        }
    }
    return result;
}

Value parseValue(const std::string& source, std::size_t line) {
    const std::string value = trim(source);
    if (value.empty()) {
        throw std::runtime_error("zproject.toml:" + std::to_string(line) +
            ": missing value");
    }
    if (value.front() == '"') return parseString(value, line);
    if (value == "true") return true;
    if (value == "false") return false;
    if (value.front() == '[') {
        if (value.back() != ']') {
            throw std::runtime_error("zproject.toml:" + std::to_string(line) +
                ": unterminated array");
        }
        std::vector<std::string> result;
        std::string item;
        bool quoted = false;
        bool escaped = false;
        for (std::size_t i = 1; i + 1 < value.size(); ++i) {
            const char ch = value[i];
            if (quoted) {
                item.push_back(ch);
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') quoted = false;
            } else if (ch == '"') {
                quoted = true;
                item.push_back(ch);
            } else if (ch == ',') {
                if (!trim(item).empty()) result.push_back(parseString(trim(item), line));
                item.clear();
            } else {
                item.push_back(ch);
            }
        }
        if (quoted) {
            throw std::runtime_error("zproject.toml:" + std::to_string(line) +
                ": unterminated string in array");
        }
        if (!trim(item).empty()) result.push_back(parseString(trim(item), line));
        return result;
    }
    std::size_t consumed = 0;
    try {
        const auto number = std::stoll(value, &consumed);
        if (consumed == value.size()) return static_cast<std::int64_t>(number);
    } catch (const std::exception&) {
    }
    throw std::runtime_error("zproject.toml:" + std::to_string(line) +
        ": unsupported value '" + value + "'");
}

std::string removeComment(const std::string& line) {
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (line[i] == '\\') escaped = true;
            else if (line[i] == '"') quoted = false;
        } else if (line[i] == '"') {
            quoted = true;
        } else if (line[i] == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string requireString(
    const std::map<std::string, Value>& table,
    const std::string& key,
    std::string fallback = {}
) {
    const auto found = table.find(key);
    if (found == table.end()) return fallback;
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    throw std::runtime_error("zproject.toml: '" + key + "' must be a string");
}

bool requireBool(
    const std::map<std::string, Value>& table,
    const std::string& key,
    bool fallback
) {
    const auto found = table.find(key);
    if (found == table.end()) return fallback;
    if (const auto* value = std::get_if<bool>(&found->second)) return *value;
    throw std::runtime_error("zproject.toml: '" + key + "' must be a boolean");
}

std::int64_t requireInteger(
    const std::map<std::string, Value>& table,
    const std::string& key,
    std::int64_t fallback
) {
    const auto found = table.find(key);
    if (found == table.end()) return fallback;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) return *value;
    throw std::runtime_error("zproject.toml: '" + key + "' must be an integer");
}

std::vector<std::string> requireArray(
    const std::map<std::string, Value>& table,
    const std::string& key
) {
    const auto found = table.find(key);
    if (found == table.end()) return {};
    if (const auto* value = std::get_if<std::vector<std::string>>(&found->second)) {
        return *value;
    }
    throw std::runtime_error("zproject.toml: '" + key + "' must be an array of strings");
}

std::string quoteToml(const std::string& value) {
    std::string result = "\"";
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') result.push_back('\\');
        if (ch == '\n') result += "\\n";
        else result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

void rejectUnknownKeys(
    const std::map<std::string, Value>& table,
    const std::set<std::string>& supported,
    const std::string& section
) {
    for (const auto& [key, value] : table) {
        (void)value;
        if (!supported.contains(key)) {
            throw std::runtime_error(
                "zproject.toml: unsupported key '" + key + "' in " + section);
        }
    }
}

void validateProjectPath(const std::string& value, const std::string& setting) {
    const std::filesystem::path path = pathFromUtf8(value);
    if (path.empty() || path.is_absolute()) {
        throw std::runtime_error(setting + " must be a non-empty project-relative path");
    }
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || *normalized.begin() == "..") {
        throw std::runtime_error(setting + " must stay inside the project directory");
    }
}

} // namespace

ProjectConfig loadProject(const std::filesystem::path& file) {
    try {
    std::ifstream input(file);
    if (!input) {
        throw std::runtime_error(
            "cannot open project configuration: " + pathToUtf8(file));
    }

    std::map<std::string, std::map<std::string, Value>> tables;
    std::vector<std::map<std::string, Value>> targets;
    std::set<std::string> declaredTables;
    std::string section;
    std::size_t lineNumber = 0;
    std::string rawLine;
    while (std::getline(input, rawLine)) {
        ++lineNumber;
        const std::string line = trim(removeComment(rawLine));
        if (line.empty()) continue;
        if (line.starts_with("[[") && line.ends_with("]]")) {
            section = trim(line.substr(2, line.size() - 4));
            if (section != "target") {
                throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                    ": only [[target]] array tables are supported");
            }
            targets.emplace_back();
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            if (section.empty()) {
                throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                    ": empty table name");
            }
            if (section == "target" ||
                (section.starts_with("target.") && section.size() == 7)) {
                throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                    ": named targets use [target.NAME]");
            }
            if (!declaredTables.insert(section).second) {
                throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                    ": duplicate table [" + section + "]");
            }
            continue;
        }
        bool quoted = false;
        bool escaped = false;
        std::size_t equals = std::string::npos;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (quoted) {
                if (escaped) escaped = false;
                else if (line[i] == '\\') escaped = true;
                else if (line[i] == '"') quoted = false;
            } else if (line[i] == '"') quoted = true;
            else if (line[i] == '=') {
                equals = i;
                break;
            }
        }
        if (equals == std::string::npos) {
            throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                ": expected key = value");
        }
        const std::string key = trim(line.substr(0, equals));
        if (section.empty()) {
            throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                ": keys must be inside a table");
        }
        if (key.empty() || key.find_first_of(" \t") != std::string::npos) {
            throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                ": invalid key");
        }
        auto& table = section == "target" && !targets.empty()
            ? targets.back()
            : tables[section];
        if (!table.emplace(key, parseValue(line.substr(equals + 1), lineNumber)).second) {
            throw std::runtime_error("zproject.toml:" + std::to_string(lineNumber) +
                ": duplicate key '" + key + "'");
        }
    }
    if (!input.eof()) throw std::runtime_error("failed while reading project configuration");

    const auto table = [&](const std::string& name)
        -> const std::map<std::string, Value>& {
        static const std::map<std::string, Value> empty;
        const auto found = tables.find(name);
        return found == tables.end() ? empty : found->second;
    };

    ProjectConfig project;
    project.root = std::filesystem::absolute(file).parent_path();
    const auto& metadata = table("project");
    rejectUnknownKeys(metadata,
        {"name", "version", "description", "entry", "sources", "include_dirs"},
        "[project]");
    project.name = requireString(metadata, "name");
    if (project.name.empty()) throw std::runtime_error("zproject.toml: [project].name is required");
    project.version = requireString(metadata, "version", "0.1.0");
    project.description = requireString(metadata, "description");
    project.entry = requireString(metadata, "entry");
    const auto legacySourceDirectories = requireArray(metadata, "sources");
    project.includeDirectories = requireArray(metadata, "include_dirs");

    const auto& build = table("build");
    rejectUnknownKeys(build,
        {"backend", "mode", "default_profile", "output", "output_dir",
            "optimization", "jobs", "windows_gui"},
        "[build]");
    project.backend = requireString(build, "backend", "native");
    const std::string configuredMode = requireString(build, "mode");
    const std::string configuredProfile = requireString(build, "default_profile");
    if (!configuredMode.empty() && !configuredProfile.empty() &&
        configuredMode != configuredProfile) {
        throw std::runtime_error(
            "[build].mode and [build].default_profile cannot select different profiles");
    }
    project.mode = !configuredProfile.empty() ? configuredProfile
        : !configuredMode.empty() ? configuredMode : "debug";
    const std::string outputDirectory = requireString(build, "output");
    const std::string legacyOutputDirectory = requireString(build, "output_dir");
    if (!outputDirectory.empty() && !legacyOutputDirectory.empty() &&
        outputDirectory != legacyOutputDirectory) {
        throw std::runtime_error(
            "[build].output and [build].output_dir cannot specify different directories");
    }
    project.outputDirectory = !outputDirectory.empty() ? outputDirectory
        : !legacyOutputDirectory.empty() ? legacyOutputDirectory : "build";
    const std::int64_t configuredOptimization =
        requireInteger(build, "optimization", 0);
    if (configuredOptimization != 0) {
        throw std::runtime_error(
            "build.optimization is unsupported: the current zp compiler has no optimization levels");
    }
    project.optimization = 0;
    project.windowsGui = requireBool(build, "windows_gui", false);
    const auto jobs = requireInteger(build, "jobs", 0);
    if (jobs < 0 || jobs > 1024) throw std::runtime_error("build.jobs must be from 0 to 1024");
    project.jobs = static_cast<unsigned>(jobs);
    validateProjectPath(project.outputDirectory, "build.output_dir");

    const auto& sources = table("sources");
    rejectUnknownKeys(sources, {"directories", "exclude"}, "[sources]");
    project.sourceDirectories = requireArray(sources, "directories");
    project.sourceExcludes = requireArray(sources, "exclude");
    if (project.sourceDirectories.empty()) {
        project.sourceDirectories = legacySourceDirectories;
    } else if (!legacySourceDirectories.empty() &&
               legacySourceDirectories != project.sourceDirectories) {
        throw std::runtime_error(
            "configure source directories either in [sources].directories or the legacy "
            "[project].sources key, not both");
    }
    if (project.sourceDirectories.empty()) {
        const auto entryParent = pathFromUtf8(project.entry).parent_path();
        if (!project.entry.empty()) {
            project.sourceDirectories.push_back(
                entryParent.empty() ? "src" : pathToUtf8(entryParent));
        }
    }
    project.profiles.emplace("debug", BuildProfile{});
    project.profiles.emplace("release", BuildProfile{});

    const auto& compiler = table("compiler");
    rejectUnknownKeys(compiler, {"path", "warnings", "debug_symbols"}, "[compiler]");
    project.compiler = requireString(compiler, "path");
    project.warnings = requireBool(compiler, "warnings", true);
    project.debugSymbols = requireBool(compiler, "debug_symbols", false);

    const auto& platform = table("platform");
    rejectUnknownKeys(platform, {"windows", "linux"}, "[platform]");
    project.windows = requireBool(platform, "windows", true);
    project.linux = requireBool(platform, "linux", true);

    const auto& assets = table("assets");
    rejectUnknownKeys(assets, {"directories", "preserve_structure"}, "[assets]");
    project.assetDirectories = requireArray(assets, "directories");
    project.preserveAssetStructure = requireBool(assets, "preserve_structure", true);

    const auto& tests = table("tests");
    rejectUnknownKeys(tests, {"targets", "arguments"}, "[tests]");
    project.testTargets = requireArray(tests, "targets");
    project.testArguments = requireArray(tests, "arguments");

    const auto& targetsTable = table("targets");
    rejectUnknownKeys(targetsTable, {"default"}, "[targets]");
    project.defaultTarget = requireString(targetsTable, "default");

    if (project.backend != "native" && project.backend != "interpreter") {
        throw std::runtime_error("build.backend must be 'native' or 'interpreter'");
    }
    if (project.windowsGui && project.backend != "native") {
        throw std::runtime_error("build.windows_gui requires build.backend = 'native'");
    }
#ifndef _WIN32
    if (project.windowsGui) {
        throw std::runtime_error("build.windows_gui is supported only by Windows builds");
    }
#endif
    if (project.mode != "debug" && project.mode != "release") {
        throw std::runtime_error("build.mode must be 'debug' or 'release'");
    }
    if (project.debugSymbols) {
        throw std::runtime_error(
            "compiler.debug_symbols=true is unsupported: the current compiler has no debug-symbol option");
    }
    if (!project.warnings) {
        throw std::runtime_error(
            "compiler.warnings=false is unsupported: the current zp compiler has no warning-disable option");
    }
    if (!project.includeDirectories.empty()) {
        throw std::runtime_error(
            "compiler include_dirs are not supported by the current zp module resolver; use relative .zh imports");
    }
    const auto outputPath =
        (project.root / pathFromUtf8(project.outputDirectory)).lexically_normal();
    for (const auto& sourceDirectory : project.sourceDirectories) {
        validateProjectPath(sourceDirectory, "project.sources");
        const auto resolved =
            (project.root / pathFromUtf8(sourceDirectory)).lexically_normal();
        if (!std::filesystem::is_directory(resolved)) {
            throw std::runtime_error(
                "[sources].directories contains a missing directory: '" +
                pathToUtf8(resolved) + "'");
        }
    }
    for (const auto& excludedPath : project.sourceExcludes) {
        validateProjectPath(excludedPath, "sources.exclude");
    }
    for (const auto& assetDirectory : project.assetDirectories) {
        validateProjectPath(assetDirectory, "assets.directories");
        const auto assetPath =
            (project.root / pathFromUtf8(assetDirectory)).lexically_normal();
        const auto outputWithinAssets = outputPath.lexically_relative(assetPath);
        if (!outputWithinAssets.empty() && *outputWithinAssets.begin() != "..") {
            throw std::runtime_error(
                "build.output_dir cannot be inside an asset source directory");
        }
        if (!std::filesystem::is_directory(assetPath)) {
            throw std::runtime_error(
                "asset directory does not exist: '" + pathToUtf8(assetPath) + "'");
        }
    }

    for (const auto& [name, values] : tables) {
        if (name.starts_with("build.profiles.")) {
            const std::string profileName = name.substr(15);
            if (profileName != "debug" && profileName != "release") {
                throw std::runtime_error(
                    "unsupported build profile '" + profileName +
                    "'; only debug and release are currently supported");
            }
            rejectUnknownKeys(values, {"optimization", "debug_symbols"},
                "[" + name + "]");
            BuildProfile profile;
            const std::int64_t profileOptimization =
                requireInteger(values, "optimization", 0);
            if (profileOptimization != 0) {
                throw std::runtime_error(
                    "[" + name + "].optimization is unsupported: zp has no optimization-level option");
            }
            profile.optimization = 0;
            profile.debugSymbols = requireBool(values, "debug_symbols", false);
            if (profile.debugSymbols) {
                throw std::runtime_error(
                    "[" + name + "].debug_symbols=true is unsupported: zp has no debug-symbol option");
            }
            project.profiles[profileName] = profile;
        } else if (name == "build.profiles") {
            if (!values.empty()) {
                throw std::runtime_error(
                    "[build.profiles] must be configured with [build.profiles.debug] "
                    "or [build.profiles.release]");
            }
        } else if (name.starts_with("dependencies.")) {
            rejectUnknownKeys(values, {"path"}, "[" + name + "]");
            const std::string dependencyName = name.substr(13);
            const std::string path = requireString(values, "path");
            if (path.empty()) {
                throw std::runtime_error("local dependency '" + dependencyName + "' requires a path");
            }
            project.localDependencies.emplace(
                dependencyName, pathFromUtf8(path));
        } else if (name.empty() || name == "project" || name == "build" ||
                   name == "compiler" || name == "platform" || name == "assets" ||
                   name == "tests" || name == "sources" || name == "targets" ||
                   name == "dependencies" || name.starts_with("target.")) {
            if (name == "dependencies") {
                for (const auto& [dependencyName, value] : values) {
                    const auto* path = std::get_if<std::string>(&value);
                    if (!path) {
                        throw std::runtime_error(
                            "dependency '" + dependencyName + "' must be a local path string");
                    }
                    project.localDependencies.emplace(
                        dependencyName, pathFromUtf8(*path));
                }
            }
        } else {
            throw std::runtime_error("zproject.toml contains unsupported table [" + name + "]");
        }
    }

    const auto addTarget = [&](const std::map<std::string, Value>& targetTable,
                               const std::string& tableName) {
        rejectUnknownKeys(targetTable,
            {"name", "type", "entry", "sources", "dependencies", "backend",
                "mode", "output", "output_dir", "command"},
            tableName);
        TargetConfig target;
        const std::string tableTargetName = tableName.starts_with("[target.")
            ? tableName.substr(8, tableName.size() - 9)
            : std::string{};
        target.name = requireString(targetTable, "name", tableTargetName);
        if (!tableTargetName.empty() && target.name != tableTargetName) {
            throw std::runtime_error(
                tableName + ".name must match its table name '" + tableTargetName + "'");
        }
        target.type = requireString(targetTable, "type", "executable");
        target.entry = requireString(targetTable, "entry");
        target.sources = requireArray(targetTable, "sources");
        target.dependencies = requireArray(targetTable, "dependencies");
        target.backend = requireString(targetTable, "backend");
        target.mode = requireString(targetTable, "mode");
        const std::string targetOutput = requireString(targetTable, "output");
        const std::string legacyTargetOutput = requireString(targetTable, "output_dir");
        if (!targetOutput.empty() && !legacyTargetOutput.empty() &&
            targetOutput != legacyTargetOutput) {
            throw std::runtime_error(
                tableName + " cannot specify different output and output_dir values");
        }
        target.outputDirectory = !targetOutput.empty() ? targetOutput : legacyTargetOutput;
        target.command = requireArray(targetTable, "command");
        if (target.name.empty()) {
            throw std::runtime_error(tableName + ".name is required");
        }
        if (target.type != "executable" && target.type != "test" &&
            target.type != "custom") {
            throw std::runtime_error(
                "target '" + target.name + "' uses unsupported type '" + target.type +
                "'; supported target types are executable, test, and custom. "
                "The zp compiler does not support linkable library targets.");
        }
        if (target.type == "custom") {
            if (target.command.empty()) {
                throw std::runtime_error(
                    "custom target '" + target.name + "' requires a non-empty command array");
            }
            if (!target.entry.empty()) {
                throw std::runtime_error(
                    "custom target '" + target.name + "' must not specify an entry point");
            }
        } else if (target.entry.empty()) {
            throw std::runtime_error(
                "target '" + target.name +
                "' requires an entry file; the current compiler builds one program entry at a time");
        }
        if (!target.backend.empty() &&
            target.backend != "native" && target.backend != "interpreter") {
            throw std::runtime_error(
                tableName + ".backend must be 'native' or 'interpreter'");
        }
        if (!target.mode.empty() &&
            target.mode != "debug" && target.mode != "release") {
            throw std::runtime_error(
                tableName + ".mode must be 'debug' or 'release'");
        }
        if (!target.entry.empty()) {
            validateProjectPath(target.entry, "target '" + target.name + "' entry");
        }
        for (const auto& source : target.sources) {
            validateProjectPath(source, "target '" + target.name + "' sources");
        }
        if (!target.outputDirectory.empty()) {
            validateProjectPath(target.outputDirectory, "target '" + target.name + "' output_dir");
        }
        const auto entry = project.root / pathFromUtf8(target.entry);
        if (!target.entry.empty() && !std::filesystem::is_regular_file(entry)) {
            throw std::runtime_error(
                "target '" + target.name + "' entry does not exist: '" +
                pathToUtf8(entry) + "'");
        }
        project.targets.push_back(std::move(target));
    };
    for (const auto& targetTable : targets) {
        addTarget(targetTable, "[[target]]");
    }
    for (const auto& [name, values] : tables) {
        if (name.starts_with("target.")) {
            addTarget(values, "[" + name + "]");
        }
    }
    if (project.targets.empty() && !project.entry.empty()) {
        project.targets.push_back({"app", "executable", project.entry});
        project.implicitTarget = true;
    }
    std::set<std::string> names;
    std::set<std::string> foldedNames;
    for (const TargetConfig& target : project.targets) {
        if (!std::all_of(target.name.begin(), target.name.end(), [](unsigned char ch) {
                return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            })) {
            throw std::runtime_error(
                "target name '" + target.name +
                "' may contain only ASCII letters, digits, '_' and '-'");
        }
        std::string caseFolded = target.name;
        std::transform(caseFolded.begin(), caseFolded.end(), caseFolded.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!names.insert(target.name).second) {
            throw std::runtime_error("duplicate target name '" + target.name + "'");
        }
        if (!foldedNames.insert(caseFolded).second) {
            throw std::runtime_error(
                "target names must be unique without regard to case (duplicate '" +
                target.name + "')");
        }
    }
    if (project.targets.empty()) {
        throw std::runtime_error(
            "project must define project.entry or at least one [[target]]/[target.NAME] table");
    }
    (void)targetBuildOrder(project.targets);
    std::set<std::string> targetNames;
    for (const TargetConfig& target : project.targets) targetNames.insert(target.name);
    if (!project.defaultTarget.empty() &&
        !targetNames.contains(project.defaultTarget)) {
        throw std::runtime_error(
            "[targets].default names unknown target '" + project.defaultTarget + "'");
    }
    if (project.defaultTarget.empty()) {
        const auto defaultTarget = std::find_if(
            project.targets.begin(), project.targets.end(),
            [](const TargetConfig& target) { return target.type == "executable"; });
        project.defaultTarget = defaultTarget == project.targets.end()
            ? project.targets.front().name
            : defaultTarget->name;
    }
    for (const std::string& testTarget : project.testTargets) {
        const auto target = std::find_if(
            project.targets.begin(), project.targets.end(),
            [&](const TargetConfig& item) { return item.name == testTarget; });
        if (!names.contains(testTarget) || target == project.targets.end() ||
            target->type != "test") {
            throw std::runtime_error("[tests].targets names unknown target '" + testTarget + "'");
        }
    }
    return project;
    } catch (const std::exception& error) {
        throw std::runtime_error(pathToUtf8(file) + ": " + error.what());
    }
}

std::filesystem::path discoverProject(const std::filesystem::path& start) {
    std::error_code error;
    auto current = std::filesystem::absolute(start, error);
    if (error) throw std::runtime_error("cannot resolve working directory");
    if (!std::filesystem::is_directory(current, error)) current = current.parent_path();
    while (!current.empty()) {
        if (std::filesystem::is_regular_file(current / "zproject.toml", error) && !error) {
            return current / "zproject.toml";
        }
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    throw std::runtime_error(
        "no zproject.toml found in this directory or its parents; "
        "run 'zpbuild init' to create a project");
}

void writeLockfile(const ProjectConfig& project) {
    std::vector<std::pair<std::string, std::string>> dependencies;
    for (const auto& [name, relativePath] : project.localDependencies) {
        const auto path = std::filesystem::weakly_canonical(project.root / relativePath);
        const auto manifest = path / "zproject.toml";
        if (!std::filesystem::is_regular_file(manifest)) {
            throw std::runtime_error(
                "local dependency '" + name + "' has no zproject.toml at '" +
                pathToUtf8(path) + "'");
        }
        const ProjectConfig dependency = loadProject(manifest);
        dependencies.emplace_back(name, dependency.version + ":" + sha256File(manifest));
    }
    std::sort(dependencies.begin(), dependencies.end());
    const auto lockfile = project.root / "zproject.lock";
    std::ostringstream output;
    output << "format = 1\n";
    output << "project = " << quoteToml(project.name) << '\n';
    output << "version = " << quoteToml(project.version) << '\n';
    for (const auto& [name, integrity] : dependencies) {
        output << "\n[[dependency]]\nname = " << quoteToml(name)
               << "\nversion_integrity = " << quoteToml(integrity) << '\n';
    }
    writeFileIfChangedAtomically(lockfile, output.str());
}

} // namespace zbuild
