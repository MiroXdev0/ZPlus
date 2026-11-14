#include "Template.hpp"
#include "Filesystem.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace zbuild {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string genericPathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string parseQuoted(const std::string& value, std::size_t line) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw std::runtime_error("template.toml:" + std::to_string(line) +
            ": expected a quoted string");
    }
    std::string result;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] != '\\') {
            result.push_back(value[i]);
            continue;
        }
        if (++i + 1 >= value.size()) {
            throw std::runtime_error("template.toml:" + std::to_string(line) +
                ": invalid string escape");
        }
        switch (value[i]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default:
                throw std::runtime_error("template.toml:" + std::to_string(line) +
                    ": unsupported string escape");
        }
    }
    return result;
}

std::vector<std::string> parseArray(const std::string& value, std::size_t line) {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw std::runtime_error("template.toml:" + std::to_string(line) +
            ": expected an array of quoted strings");
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
            const auto part = trim(item);
            if (!part.empty()) result.push_back(parseQuoted(part, line));
            item.clear();
        } else {
            item.push_back(ch);
        }
    }
    if (quoted) {
        throw std::runtime_error("template.toml:" + std::to_string(line) +
            ": unterminated string in array");
    }
    const auto part = trim(item);
    if (!part.empty()) result.push_back(parseQuoted(part, line));
    return result;
}

bool parseBoolean(const std::string& value, std::size_t line) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("template.toml:" + std::to_string(line) +
        ": expected true or false");
}

std::map<std::string, std::string> parseManifestValues(
    const std::filesystem::path& path
) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error(
        "cannot open template manifest '" + pathToUtf8(path) + "'");
    std::map<std::string, std::string> values;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        bool quoted = false;
        bool escaped = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (quoted) {
                if (escaped) escaped = false;
                else if (line[i] == '\\') escaped = true;
                else if (line[i] == '"') quoted = false;
            } else if (line[i] == '"') quoted = true;
            else if (line[i] == '#') {
                line.resize(i);
                break;
            }
        }
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']' &&
            line.find('=') == std::string::npos) {
            throw std::runtime_error("template.toml:" + std::to_string(lineNumber) +
                ": tables are not supported; use the documented flat manifest schema");
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("template.toml:" + std::to_string(lineNumber) +
                ": expected key = value");
        }
        const std::string key = trim(line.substr(0, equals));
        if (key.empty() || !values.emplace(key, trim(line.substr(equals + 1))).second) {
            throw std::runtime_error("template.toml:" + std::to_string(lineNumber) +
                ": empty or duplicate key");
        }
    }
    if (!input.eof()) throw std::runtime_error(
        "failed reading template manifest '" + pathToUtf8(path) + "'");
    return values;
}

std::string requiredString(
    const std::map<std::string, std::string>& values,
    const std::string& key
) {
    const auto found = values.find(key);
    if (found == values.end()) {
        throw std::runtime_error("template manifest is missing required key '" + key + "'");
    }
    return parseQuoted(found->second, 0);
}

std::vector<std::string> optionalArray(
    const std::map<std::string, std::string>& values,
    const std::string& key
) {
    const auto found = values.find(key);
    return found == values.end() ? std::vector<std::string>{}
        : parseArray(found->second, 0);
}

std::string environmentVariable(const char* name) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::char_traits<char>::length(name));
    const DWORD required = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (required == 0) return {};
    std::wstring wideValue(static_cast<std::size_t>(required), L'\0');
    const DWORD length = GetEnvironmentVariableW(
        wideName.c_str(), wideValue.data(), required);
    if (length == 0 || length >= required) {
        throw std::runtime_error(
            "cannot read environment variable '" + std::string(name) + "'");
    }
    wideValue.resize(length);
    const int utf8Length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(),
        static_cast<int>(wideValue.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        throw std::runtime_error(
            "environment variable '" + std::string(name) + "' is not valid Unicode");
    }
    std::string value(static_cast<std::size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(),
            static_cast<int>(wideValue.size()), value.data(), utf8Length,
            nullptr, nullptr) != utf8Length) {
        throw std::runtime_error(
            "failed converting environment variable '" + std::string(name) + "'");
    }
    return value;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

TemplateInfo readTemplate(const std::filesystem::path& directory, bool builtIn) {
    const auto manifest = directory / "template.toml";
    const auto values = parseManifestValues(manifest);
    const std::set<std::string> supported{
        "name", "description", "version", "required_zplus_version",
        "default_target_type", "platforms", "required_modules", "aliases",
        "files", "directories", "configuration", "available", "unavailable_reason"
    };
    for (const auto& [key, value] : values) {
        (void)value;
        if (!supported.contains(key)) {
            throw std::runtime_error("template manifest has unsupported key '" + key +
                "' in '" + pathToUtf8(manifest) + "'");
        }
    }

    TemplateInfo result;
    result.name = requiredString(values, "name");
    result.description = requiredString(values, "description");
    result.version = requiredString(values, "version");
    result.requiredZPlusVersion = requiredString(values, "required_zplus_version");
    result.defaultTargetType = requiredString(values, "default_target_type");
    result.platforms = optionalArray(values, "platforms");
    result.requiredModules = optionalArray(values, "required_modules");
    result.aliases = optionalArray(values, "aliases");
    result.files = optionalArray(values, "files");
    result.directories = optionalArray(values, "directories");
    result.configuration = optionalArray(values, "configuration");
    result.root = std::filesystem::canonical(directory);
    result.builtIn = builtIn;
    if (const auto available = values.find("available"); available != values.end()) {
        result.available = parseBoolean(available->second, 0);
    }
    if (const auto reason = values.find("unavailable_reason"); reason != values.end()) {
        result.unavailableReason = parseQuoted(reason->second, 0);
    }
    if (result.name.empty() || result.description.empty() || result.version.empty() ||
        result.requiredZPlusVersion.empty() || result.defaultTargetType.empty()) {
        throw std::runtime_error("template manifest contains an empty required value: '" +
            pathToUtf8(manifest) + "'");
    }
    if (!result.available && result.unavailableReason.empty()) {
        throw std::runtime_error("unavailable template '" + result.name +
            "' must specify unavailable_reason");
    }
    if (result.available && result.files.empty() && result.directories.empty()) {
        throw std::runtime_error("available template '" + result.name +
            "' must define files or directories");
    }
    if (result.defaultTargetType != "executable" &&
        result.defaultTargetType != "test" &&
        result.defaultTargetType != "source_module") {
        throw std::runtime_error("template '" + result.name +
            "' declares unsupported default target type '" +
            result.defaultTargetType + "'");
    }
    const auto validateRelative = [&](const std::string& value, const char* field) {
        const std::filesystem::path relative = pathFromUtf8(value);
        if (relative.empty() || relative.is_absolute() ||
            relative.has_root_name() || relative.has_root_directory()) {
            throw std::runtime_error("template '" + result.name + "' has an unsafe " +
                field + " path '" + value + "'");
        }
        const auto normalized = relative.lexically_normal();
        if (normalized.empty() || *normalized.begin() == "..") {
            throw std::runtime_error("template '" + result.name + "' has an unsafe " +
                field + " path '" + value + "'");
        }
        const auto normalizedUtf8 = relative.generic_u8string();
        if (std::string(
                reinterpret_cast<const char*>(normalizedUtf8.data()), normalizedUtf8.size()) != value) {
            throw std::runtime_error("template '" + result.name + "' has a non-normalized " +
                field + " path '" + value + "'");
        }
    };
    std::set<std::string> paths;
    std::set<std::string> filePaths;
    for (const auto& file : result.files) {
        validateRelative(file, "file");
        if (!paths.insert(file).second) {
            throw std::runtime_error("template '" + result.name +
                "' lists a file more than once: '" + file + "'");
        }
        filePaths.insert(file);
        const auto candidate = directory / pathFromUtf8(file);
        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(candidate, statusError);
        std::error_code fileError;
        const auto canonical = std::filesystem::canonical(candidate, fileError);
        const auto insideRoot = canonical.lexically_relative(result.root);
        if (statusError || fileError || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(canonical) || insideRoot.empty() ||
            *insideRoot.begin() == "..") {
            throw std::runtime_error("template '" + result.name +
                "' references a missing or unsafe file '" + file + "'");
        }
    }
    for (const auto& directoryPath : result.directories) {
        validateRelative(directoryPath, "directory");
        if (!paths.insert(directoryPath).second) {
            throw std::runtime_error("template '" + result.name +
                "' lists a path more than once: '" + directoryPath + "'");
        }
    }
    for (const auto& file : filePaths) {
        const std::string prefix = file + '/';
        for (const auto& path : paths) {
            if (path != file && path.starts_with(prefix)) {
                throw std::runtime_error("template '" + result.name +
                    "' declares file '" + file + "' as a parent of another path '" +
                    path + "'");
            }
        }
    }
    std::set<std::string> aliasSet;
    aliasSet.insert(lower(result.name));
    for (const auto& alias : result.aliases) {
        if (alias.empty() || !aliasSet.insert(lower(alias)).second) {
            throw std::runtime_error("template '" + result.name +
                "' has an empty or duplicate alias '" + alias + "'");
        }
    }
    for (const auto& setting : result.configuration) {
        const auto equals = setting.find('=');
        if (equals == std::string::npos || trim(setting.substr(0, equals)).empty()) {
            throw std::runtime_error("template '" + result.name +
                "' has an invalid configuration value '" + setting + "'");
        }
    }
    return result;
}

std::filesystem::path defaultCustomDirectory() {
#ifdef _WIN32
    if (const std::string appData = environmentVariable("APPDATA"); !appData.empty()) {
        return pathFromUtf8(appData) / "ZBuild" / "templates";
    }
#else
    if (const std::string xdg = environmentVariable("XDG_CONFIG_HOME"); !xdg.empty()) {
        return pathFromUtf8(xdg) / "zbuild" / "templates";
    }
#endif
    const std::string home = environmentVariable(
#ifdef _WIN32
            "USERPROFILE"
#else
            "HOME"
#endif
        );
    if (!home.empty()) {
#ifdef _WIN32
        return pathFromUtf8(home) / "AppData" / "Roaming" /
            "ZBuild" / "templates";
#else
        return pathFromUtf8(home) / ".config" / "zbuild" / "templates";
#endif
    }
    return {};
}

std::string currentYear() {
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm localTime{};
#ifdef _WIN32
    if (localtime_s(&localTime, &now) != 0) {
        throw std::runtime_error("cannot determine the current year");
    }
#else
    if (!localtime_r(&now, &localTime)) {
        throw std::runtime_error("cannot determine the current year");
    }
#endif
    return std::to_string(localTime.tm_year + 1900);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(
        "cannot read template file '" + pathToUtf8(path) + "'");
    std::ostringstream result;
    result << input.rdbuf();
    if (input.bad()) throw std::runtime_error(
        "failed reading template file '" + pathToUtf8(path) + "'");
    return result.str();
}

std::string replaceVariables(
    std::string contents,
    const std::unordered_map<std::string, std::string>& variables,
    const std::string& file
) {
    for (const auto& [name, value] : variables) {
        const std::string token = "{{" + name + "}}";
        std::size_t position = 0;
        while ((position = contents.find(token, position)) != std::string::npos) {
            contents.replace(position, token.size(), value);
            position += value.size();
        }
    }
    const auto unresolved = contents.find("{{");
    if (unresolved != std::string::npos) {
        const auto end = contents.find("}}", unresolved + 2);
        const std::string token = end == std::string::npos
            ? contents.substr(unresolved)
            : contents.substr(unresolved, end - unresolved + 2);
        throw std::runtime_error("template file '" + file +
            "' contains an unresolved or invalid variable " + token);
    }
    return contents;
}

std::string validateProjectName(std::string value) {
    if (value.empty() || value.size() > 64 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        }) || !(std::isalnum(static_cast<unsigned char>(value.front())) ||
            value.front() == '_')) {
        throw std::runtime_error(
            "project name must start with an ASCII letter, digit, or '_' and contain only "
            "ASCII letters, digits, '_' or '-' (maximum 64 characters)");
    }
    return value;
}

void validateVersion(const std::string& version) {
    if (version.empty() || version.find_first_not_of("0123456789.") != std::string::npos ||
        version.front() == '.' || version.back() == '.' ||
        version.find("..") != std::string::npos) {
        throw std::runtime_error("project version must use numeric dotted form, for example 0.1.0");
    }
}

std::filesystem::path resolvedTemplateFile(
    const TemplateInfo& info,
    const std::string& relative
) {
    const auto path = info.root / pathFromUtf8(relative);
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (!statusError && std::filesystem::is_symlink(status)) {
        throw std::runtime_error(
            "template file symlinks are not allowed: '" + pathToUtf8(path) + "'");
    }
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (statusError || error || !std::filesystem::is_regular_file(canonical)) {
        throw std::runtime_error("template file is missing or not a regular file: '" +
            pathToUtf8(path) + "'");
    }
    const auto relativeToRoot = canonical.lexically_relative(info.root);
    if (relativeToRoot.empty() || *relativeToRoot.begin() == "..") {
        throw std::runtime_error(
            "template file escapes its root: '" + pathToUtf8(path) + "'");
    }
    return canonical;
}

} // namespace

TemplateRegistry::TemplateRegistry(std::filesystem::path executablePath)
    : executablePath_(std::move(executablePath)) {
    if (const std::string overrideDirectory = environmentVariable("ZBUILD_TEMPLATES_DIR");
        !overrideDirectory.empty()) {
        customDirectory_ = pathFromUtf8(overrideDirectory);
    } else {
        customDirectory_ = defaultCustomDirectory();
    }
#ifdef ZBUILD_BUILTIN_TEMPLATE_DIR
    builtInDirectory_ = ZBUILD_BUILTIN_TEMPLATE_DIR;
#endif
    if (!executablePath_.empty()) {
        const auto adjacentTemplates =
            executablePath_.parent_path() / "zbuild-templates";
        if (std::filesystem::is_directory(adjacentTemplates)) {
            builtInDirectory_ = adjacentTemplates;
        }
    }
}

std::vector<TemplateInfo> TemplateRegistry::list() const {
    std::vector<TemplateInfo> builtIns;
    std::vector<TemplateInfo> custom;
    const auto discover = [](const std::filesystem::path& directory, bool builtIn,
                             std::vector<TemplateInfo>& destination) {
        if (directory.empty() || !std::filesystem::is_directory(directory)) return;
        std::vector<std::filesystem::path> childDirectories;
        for (std::filesystem::directory_iterator it(directory), end; it != end; ++it) {
            if (it->is_symlink()) {
                throw std::runtime_error("template directories may not contain symlinked entries: '" +
                    pathToUtf8(it->path()) + "'");
            }
            if (it->is_directory()) childDirectories.push_back(it->path());
        }
        std::sort(childDirectories.begin(), childDirectories.end());
        for (const auto& child : childDirectories) {
            if (!std::filesystem::is_regular_file(child / "template.toml")) continue;
            destination.push_back(readTemplate(child, builtIn));
        }
    };
    discover(builtInDirectory_, true, builtIns);
    if (customDirectory_ != builtInDirectory_) discover(customDirectory_, false, custom);

    std::set<std::string> builtInNames;
    for (const auto& item : builtIns) {
        const auto key = lower(item.name);
        if (!builtInNames.insert(key).second) {
            throw std::runtime_error(
                "duplicate built-in template name or alias '" + item.name + "'");
        }
        for (const auto& alias : item.aliases) {
            if (!builtInNames.insert(lower(alias)).second) {
                throw std::runtime_error(
                    "duplicate built-in template name or alias '" + alias + "'");
            }
        }
    }
    std::vector<TemplateInfo> result = std::move(builtIns);
    std::set<std::string> customNames;
    for (auto& item : custom) {
        const auto key = lower(item.name);
        if (builtInNames.contains(key)) {
            std::cerr << "zbuild: warning: custom template '" << item.name
                      << "' is shadowed by a built-in template\n";
            continue;
        }
        std::set<std::string> itemNames{key};
        if (customNames.contains(key)) {
            throw std::runtime_error("duplicate custom template name '" + item.name +
                "' in '" + pathToUtf8(item.root) + "'");
        }
        bool shadowsBuiltIn = false;
        for (const auto& alias : item.aliases) {
            const auto aliasKey = lower(alias);
            if (builtInNames.contains(lower(alias))) {
                std::cerr << "zbuild: warning: custom template alias '" << alias
                          << "' is shadowed by a built-in template\n";
                shadowsBuiltIn = true;
            }
            if (customNames.contains(aliasKey) || !itemNames.insert(aliasKey).second) {
                throw std::runtime_error("duplicate custom template name or alias '" +
                    alias + "' in '" + pathToUtf8(item.root) + "'");
            }
        }
        if (shadowsBuiltIn) continue;
        customNames.insert(itemNames.begin(), itemNames.end());
        result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return lower(left.name) < lower(right.name);
    });
    return result;
}

TemplateInfo TemplateRegistry::find(const std::string& requested) const {
    const auto templates = list();
    const std::string key = lower(requested);
    auto found = std::find_if(templates.begin(), templates.end(),
        [&](const TemplateInfo& item) { return lower(item.name) == key; });
    if (found == templates.end()) {
        found = std::find_if(templates.begin(), templates.end(),
            [&](const TemplateInfo& item) {
                return std::any_of(item.aliases.begin(), item.aliases.end(),
                    [&](const std::string& alias) { return lower(alias) == key; });
            });
    }
    if (found != templates.end()) {
        return *found;
    }
    std::ostringstream message;
    message << "unknown template '" << requested << "'. Available templates:";
    for (const auto& item : templates) {
        message << "\n  " << item.name;
        if (!item.available) message << " (unavailable: " << item.unavailableReason << ')';
    }
    if (templates.empty()) message << "\n  (none found)";
    throw std::runtime_error(message.str());
}

void TemplateRegistry::printList() const {
    for (const auto& item : list()) {
        std::cout << item.name << " - " << item.description << '\n'
                  << "  Target: " << item.defaultTargetType << "; Modules: ";
        if (item.requiredModules.empty()) std::cout << "none";
        else {
            for (std::size_t i = 0; i < item.requiredModules.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << item.requiredModules[i];
            }
        }
        std::cout << "; Platforms: ";
        if (item.platforms.empty()) std::cout << "unspecified";
        else {
            for (std::size_t i = 0; i < item.platforms.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << item.platforms[i];
            }
        }
        if (!item.available) std::cout << "; UNAVAILABLE: " << item.unavailableReason;
        std::cout << '\n';
    }
}

void TemplateRegistry::printInfo(const std::string& name) const {
    const auto item = find(name);
    std::cout << item.name << " " << item.version << '\n'
              << item.description << '\n'
              << "Required Z+ version: " << item.requiredZPlusVersion << '\n'
              << "Default target type: " << item.defaultTargetType << '\n'
              << "Platforms: ";
    for (std::size_t i = 0; i < item.platforms.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << item.platforms[i];
    }
    std::cout << "\nRequired modules: ";
    if (item.requiredModules.empty()) std::cout << "none";
    else {
        for (std::size_t i = 0; i < item.requiredModules.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << item.requiredModules[i];
        }
    }
    if (!item.aliases.empty()) {
        std::cout << "\nAliases: ";
        for (std::size_t i = 0; i < item.aliases.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << item.aliases[i];
        }
    }
    if (!item.available) std::cout << "\nUnavailable: " << item.unavailableReason;
    std::cout << '\n';
}

void TemplateRegistry::initialize(
    const std::string& templateName,
    const std::filesystem::path& destination,
    const std::string& requestedProjectName,
    const std::string& projectVersion,
    const std::string& author
) const {
    const TemplateInfo info = find(templateName);
    if (!info.available) {
        throw std::runtime_error("template '" + info.name +
            "' is currently unavailable: " + info.unavailableReason);
    }
    const std::string projectName = validateProjectName(requestedProjectName);
    validateVersion(projectVersion);
    if (author.find_first_of("\r\n{}") != std::string::npos) {
        throw std::runtime_error("author must be a single line and may not contain template delimiters");
    }
    if (std::filesystem::exists(destination) &&
        !std::filesystem::is_directory(destination)) {
        throw std::runtime_error("project destination exists and is not a directory");
    }
    const auto absoluteDestination = std::filesystem::absolute(destination).lexically_normal();
    const auto parent = absoluteDestination.parent_path();
    if (parent.empty()) throw std::runtime_error("cannot determine project destination parent");

    std::unordered_map<std::string, std::string> variables{
        {"PROJECT_NAME", projectName},
        {"PROJECT_DIRECTORY", genericPathToUtf8(absoluteDestination)},
        {"PROJECT_VERSION", projectVersion},
        {"AUTHOR", author},
        {"YEAR", currentYear()},
        {"BUILD_BACKEND", "native"},
        {"BUILD_MODE", "debug"}
    };
    for (const auto& setting : info.configuration) {
        const auto equals = setting.find('=');
        const auto key = trim(setting.substr(0, equals));
        const auto value = trim(setting.substr(equals + 1));
        if (key == "backend") variables["BUILD_BACKEND"] = value;
        else if (key == "mode") variables["BUILD_MODE"] = value;
        else throw std::runtime_error("template '" + info.name +
            "' has unsupported configuration substitution '" + key + "'");
    }

    const std::string suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto staging = parent / (".zbuild-template-stage-" + suffix);
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    if (error) throw std::runtime_error("cannot create template staging directory: " + error.message());

    std::vector<std::filesystem::path> installedFiles;
    std::vector<std::filesystem::path> createdDirectories;
    try {
        const auto entryExists = [](const std::filesystem::path& path) {
            std::error_code statusError;
            const auto status = std::filesystem::symlink_status(path, statusError);
            if (statusError == std::errc::no_such_file_or_directory) return false;
            if (statusError) {
                throw std::filesystem::filesystem_error(
                    "cannot inspect template destination", path, statusError);
            }
            return status.type() != std::filesystem::file_type::not_found;
        };
        const auto ensureDirectories = [&](const std::filesystem::path& relative) {
            const auto target = absoluteDestination / relative;
            std::vector<std::filesystem::path> missing;
            for (auto current = target; !current.empty() &&
                 current != absoluteDestination.parent_path(); current = current.parent_path()) {
                if (entryExists(current)) {
                    const auto status = std::filesystem::symlink_status(current);
                    if (std::filesystem::is_symlink(status)) {
                        throw std::runtime_error("template destination may not traverse symlink '" +
                            pathToUtf8(current) + "'");
                    }
                    if (!std::filesystem::is_directory(status)) {
                        throw std::runtime_error("template directory conflicts with existing file '" +
                            pathToUtf8(current) + "'");
                    }
                    break;
                }
                missing.push_back(current);
            }
            for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
                if (std::filesystem::create_directory(*it)) createdDirectories.push_back(*it);
            }
        };

        for (const auto& directory : info.directories) {
            ensureDirectories(pathFromUtf8(directory));
            std::filesystem::create_directories(staging / pathFromUtf8(directory));
        }
        std::set<std::string> uniqueFiles;
        for (const auto& relative : info.files) {
            if (!uniqueFiles.insert(relative).second) {
                throw std::runtime_error("template '" + info.name +
                    "' lists file more than once: '" + relative + "'");
            }
            const auto source = resolvedTemplateFile(info, relative);
            const auto output = absoluteDestination / pathFromUtf8(relative);
            if (entryExists(output)) {
                throw std::runtime_error("refusing to overwrite existing file '" +
                    pathToUtf8(output) + "'");
            }
            const auto destinationStage = staging / pathFromUtf8(relative);
            std::filesystem::create_directories(destinationStage.parent_path());
            const std::string contents = replaceVariables(
                readText(source), variables, relative);
            std::ofstream staged(destinationStage, std::ios::binary | std::ios::trunc);
            if (!staged) throw std::runtime_error("cannot stage template file '" + relative + "'");
            staged.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!staged) throw std::runtime_error("failed writing staged template file '" + relative + "'");
        }

        if (!info.files.empty()) {
            ensureDirectories(pathFromUtf8(info.files.front()).parent_path());
        }
        for (const auto& relative : info.files) {
            const auto output = absoluteDestination / pathFromUtf8(relative);
            ensureDirectories(pathFromUtf8(relative).parent_path());
            if (entryExists(output)) {
                throw std::runtime_error("refusing to overwrite existing file '" +
                    pathToUtf8(output) + "'");
            }
            std::filesystem::rename(staging / pathFromUtf8(relative), output);
            installedFiles.push_back(output);
        }
        std::filesystem::remove_all(staging);
    } catch (...) {
        for (auto it = installedFiles.rbegin(); it != installedFiles.rend(); ++it) {
            std::error_code ignored;
            std::filesystem::remove(*it, ignored);
        }
        for (auto it = createdDirectories.rbegin(); it != createdDirectories.rend(); ++it) {
            std::error_code ignored;
            std::filesystem::remove(*it, ignored);
        }
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
}

} // namespace zbuild
