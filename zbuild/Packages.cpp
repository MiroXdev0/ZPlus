#include "Packages.hpp"

#include "Filesystem.hpp"
#include "Hash.hpp"
#include "Process.hpp"

#include "../runtime/Json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace zbuild {
namespace {

using JsonValue = zplus::json::Value;
using JsonObject = JsonValue::Object;
using JsonArray = JsonValue::Array;

std::string genericPathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

constexpr std::uint64_t maximumPackageBytes = 64ull * 1024 * 1024;
constexpr std::uint64_t maximumPackageFileBytes = 16ull * 1024 * 1024;
constexpr std::uint32_t maximumArchiveEntries = 10000;
constexpr std::size_t maximumManifestBytes = 1024 * 1024;

std::string environmentVariable(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

struct SemanticVersion {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::vector<std::string> prerelease;
};

struct PackageSource {
    PackageManifest manifest;
    std::filesystem::path directory;
    std::string source;
    std::string integrity;
};

const JsonValue* objectField(const JsonValue& value, std::string_view name) {
    const auto* object = std::get_if<JsonObject>(&value.data);
    if (!object) throw std::runtime_error("expected a JSON object");
    const JsonValue* result = nullptr;
    for (const auto& [key, item] : *object) {
        if (key != name) continue;
        if (result) throw std::runtime_error(
            "duplicate JSON property '" + std::string(name) + "'");
        result = &item;
    }
    return result;
}

std::string requiredString(
    const JsonValue& value,
    std::string_view name,
    const std::string& context
) {
    const JsonValue* field = objectField(value, name);
    const auto* text = field ? std::get_if<std::string>(&field->data) : nullptr;
    if (!text || text->empty()) {
        throw std::runtime_error(context + " requires a non-empty string '" +
            std::string(name) + "'");
    }
    return *text;
}

std::string optionalString(const JsonValue& value, std::string_view name) {
    const JsonValue* field = objectField(value, name);
    if (!field) return {};
    const auto* text = std::get_if<std::string>(&field->data);
    if (!text) {
        throw std::runtime_error("package manifest field '" +
            std::string(name) + "' must be a string");
    }
    return *text;
}

std::string readTextFile(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(
        "cannot read '" + pathToUtf8(path) + "'");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > limit) {
        throw std::length_error(
            "'" + pathToUtf8(path) + "' exceeds the allowed size");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    input.seekg(0, std::ios::beg);
    if (!result.empty()) input.read(result.data(), static_cast<std::streamsize>(result.size()));
    if (!input && !input.eof()) {
        throw std::runtime_error("failed reading '" + pathToUtf8(path) + "'");
    }
    return result;
}

void validateName(const std::string& name) {
    if (name.empty() || name.size() > 64 ||
        name.front() < 'a' || name.front() > 'z' ||
        !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '-';
        }) || name.back() == '-') {
        throw std::runtime_error(
            "package name must be 1-64 lowercase ASCII letters, digits, or hyphens, "
            "starting with a letter and not ending with a hyphen");
    }
}

bool validIdentifiers(std::string_view value, bool prerelease) {
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find('.', start);
        const auto identifier = value.substr(start,
            end == std::string_view::npos ? value.size() - start : end - start);
        if (identifier.empty()) return false;
        bool numeric = true;
        for (const unsigned char ch : identifier) {
            if (!((ch >= '0' && ch <= '9') ||
                  (ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z') || ch == '-')) {
                return false;
            }
            numeric = numeric && ch >= '0' && ch <= '9';
        }
        if (prerelease && numeric && identifier.size() > 1 &&
            identifier.front() == '0') {
            return false;
        }
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
    return false;
}

bool validVersionCore(std::string_view value, SemanticVersion& result) {
    const auto plus = value.find('+');
    if (plus != std::string_view::npos) {
        const auto build = value.substr(plus + 1);
        if (value.find('+', plus + 1) != std::string_view::npos ||
            !validIdentifiers(build, false)) {
            return false;
        }
        value = value.substr(0, plus);
    }
    const auto dash = value.find('-');
    if (dash != std::string_view::npos) {
        const auto prerelease = value.substr(dash + 1);
        if (!validIdentifiers(prerelease, true)) return false;
        std::size_t start = 0;
        while (start < prerelease.size()) {
            const auto end = prerelease.find('.', start);
            result.prerelease.emplace_back(prerelease.substr(start,
                end == std::string_view::npos
                    ? prerelease.size() - start : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        value = value.substr(0, dash);
    }
    std::array<std::uint64_t*, 3> components{
        &result.major, &result.minor, &result.patch};
    std::size_t start = 0;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto end = index == 2 ? value.size() : value.find('.', start);
        if (end == std::string_view::npos || end == start) return false;
        const auto part = value.substr(start, end - start);
        if (part.size() > 1 && part.front() == '0') return false;
        std::uint64_t number = 0;
        for (const unsigned char ch : part) {
            if (ch < '0' || ch > '9') return false;
            const auto digit = static_cast<std::uint64_t>(ch - '0');
            if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                return false;
            }
            number = number * 10 + digit;
        }
        *components[index] = number;
        start = end + 1;
    }
    return true;
}

SemanticVersion parseVersion(const std::string& version) {
    SemanticVersion parsed;
    if (!validVersionCore(version, parsed)) {
        throw std::runtime_error("invalid semantic version '" + version + "'");
    }
    return parsed;
}

bool validIntegrity(const std::string& integrity) {
    constexpr std::string_view prefix = "sha256:";
    return integrity.starts_with(prefix) &&
        integrity.size() == prefix.size() + 64 &&
        std::all_of(integrity.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
            integrity.end(), [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') ||
                    (ch >= 'a' && ch <= 'f');
            });
}

int compareVersion(const SemanticVersion& left, const SemanticVersion& right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    for (std::size_t index = 0;
         index < std::min(left.prerelease.size(), right.prerelease.size());
         ++index) {
        const auto& leftIdentifier = left.prerelease[index];
        const auto& rightIdentifier = right.prerelease[index];
        const bool leftNumeric = std::all_of(
            leftIdentifier.begin(), leftIdentifier.end(),
            [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
        const bool rightNumeric = std::all_of(
            rightIdentifier.begin(), rightIdentifier.end(),
            [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
        if (leftNumeric && rightNumeric) {
            if (leftIdentifier.size() != rightIdentifier.size()) {
                return leftIdentifier.size() < rightIdentifier.size() ? -1 : 1;
            }
            if (leftIdentifier != rightIdentifier) {
                return leftIdentifier < rightIdentifier ? -1 : 1;
            }
        } else if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        } else if (leftIdentifier != rightIdentifier) {
            return leftIdentifier < rightIdentifier ? -1 : 1;
        }
    }
    if (left.prerelease.size() == right.prerelease.size()) return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

bool compareOperator(
    const SemanticVersion& candidate,
    const SemanticVersion& required,
    std::string_view operation
) {
    const int comparison = compareVersion(candidate, required);
    if (operation == "=" || operation.empty()) return comparison == 0;
    if (operation == ">") return comparison > 0;
    if (operation == ">=") return comparison >= 0;
    if (operation == "<") return comparison < 0;
    if (operation == "<=") return comparison <= 0;
    if (operation == "^") {
        if (comparison < 0) return false;
        if (required.major != 0) return candidate.major == required.major;
        if (required.minor != 0) {
            return candidate.major == 0 && candidate.minor == required.minor;
        }
        return candidate.major == 0 && candidate.minor == 0 &&
            candidate.patch == required.patch;
    }
    if (operation == "~") {
        return comparison >= 0 && candidate.major == required.major &&
            candidate.minor == required.minor;
    }
    throw std::runtime_error("unsupported semantic-version operator");
}

bool satisfiesVersion(const std::string& version, const std::string& constraint) {
    const SemanticVersion candidate = parseVersion(version);
    if (constraint.empty() || constraint == "*") return candidate.prerelease.empty();
    std::istringstream input(constraint);
    std::string term;
    bool hasTerm = false;
    while (input >> term) {
        std::string_view operation;
        std::size_t offset = 0;
        if (term.starts_with(">=") || term.starts_with("<=")) {
            operation = std::string_view(term).substr(0, 2);
            offset = 2;
        } else if (term.front() == '>' || term.front() == '<' ||
                   term.front() == '=' || term.front() == '^' ||
                   term.front() == '~') {
            operation = std::string_view(term).substr(0, 1);
            offset = 1;
        }
        const std::string versionTerm = term.substr(offset);
        const SemanticVersion required = parseVersion(versionTerm);
        if (!compareOperator(candidate, required, operation)) return false;
        hasTerm = true;
    }
    return hasTerm;
}

std::filesystem::path checkedRelativePath(
    const std::filesystem::path& root,
    const std::string& value,
    const char* description
) {
    const auto relative = pathFromUtf8(value);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) {
        throw std::runtime_error(
            std::string(description) + " must be a relative path");
    }
    const auto normalized = relative.lexically_normal();
    if (normalized.empty() || *normalized.begin() == "..") {
        throw std::runtime_error(
            std::string(description) + " must stay inside its package directory");
    }
    const auto result = std::filesystem::weakly_canonical(root / normalized);
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    const auto inside = result.lexically_relative(canonicalRoot);
    if (inside.empty() || inside.is_absolute() || *inside.begin() == "..") {
        throw std::runtime_error(
            std::string(description) + " resolves outside its package directory");
    }
    return result;
}

std::vector<std::pair<std::string, std::filesystem::path>> packageFiles(
    const std::filesystem::path& directory
) {
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::uint64_t totalBytes = 0;
    bool hasZplusSource = false;
    const std::set<std::string> ignoredDirectories{
        ".git", ".hg", ".svn", "build", "z+_packages"};
    for (std::filesystem::recursive_directory_iterator item(
             directory, std::filesystem::directory_options::none),
         end;
         item != end; ++item) {
        const auto status = item->symlink_status();
        if (std::filesystem::is_symlink(status)) {
            throw std::runtime_error("package contains a symbolic link: '" +
                pathToUtf8(item->path()) + "'");
        }
        if (std::filesystem::is_directory(status)) {
            if (ignoredDirectories.contains(pathToUtf8(item->path().filename()))) {
                item.disable_recursion_pending();
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            throw std::runtime_error("package contains a non-regular file: '" +
                pathToUtf8(item->path()) + "'");
        }
        const auto relative = item->path().lexically_relative(directory);
        if (relative.empty() || relative.is_absolute() ||
            *relative.begin() == "..") {
            throw std::runtime_error("package file escapes its package directory");
        }
        if (relative == ".zpm-installed.json" ||
            relative == ".zpm-cache.json") continue;
        const auto extension = pathToUtf8(item->path().extension());
        std::string lowerExtension = extension;
        std::transform(lowerExtension.begin(), lowerExtension.end(),
            lowerExtension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        static const std::set<std::string> foreignCodeExtensions{
            ".a", ".bat", ".c", ".cc", ".cmd", ".cpp", ".cxx", ".class",
            ".cs", ".dll", ".dylib", ".exe", ".go", ".h", ".hh", ".hpp",
            ".hxx", ".jar", ".java", ".js", ".jsx", ".kt", ".lua", ".mjs",
            ".node", ".o", ".obj", ".php", ".pl", ".ps1", ".py", ".rb",
            ".rs", ".scala", ".sh", ".so", ".swift", ".ts", ".tsx", ".vb",
            ".wasm"};
        if (foreignCodeExtensions.contains(lowerExtension)) {
            throw std::runtime_error("Z+ packages cannot contain alternative-language "
                "implementations: '" + pathToUtf8(item->path()) + "'");
        }
        const auto size = item->file_size();
        if (size > maximumPackageFileBytes ||
            totalBytes > maximumPackageBytes - size) {
            throw std::length_error("package exceeds the 64 MiB source-size limit");
        }
        totalBytes += size;
        if (lowerExtension == ".zh" || lowerExtension == ".zp") {
            hasZplusSource = true;
        }
        files.emplace_back(genericPathToUtf8(relative), item->path());
    }
    if (!hasZplusSource) {
        throw std::runtime_error("package must contain at least one .zh or .zp Z+ source file");
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return files;
}

std::string packageIntegrity(const std::filesystem::path& directory) {
    std::string contents;
    const auto files = packageFiles(directory);
    const auto appendLength = [&contents](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            contents.push_back(static_cast<char>(value >> shift));
        }
    };
    appendLength(files.size());
    for (const auto& [relative, file] : files) {
        const std::string fileContents =
            readTextFile(file, static_cast<std::size_t>(maximumPackageFileBytes));
        appendLength(relative.size());
        contents += relative;
        appendLength(fileContents.size());
        contents += fileContents;
    }
    return "sha256:" + sha256(contents);
}

std::string manifestJson(const PackageManifest& manifest) {
    JsonObject dependencies;
    for (const auto& [name, dependency] : manifest.dependencies) {
        if (dependency.localPath) {
            dependencies.emplace_back(name, JsonValue{JsonObject{
                {"path", JsonValue{pathToUtf8(dependency.path)}},
                {"version", JsonValue{dependency.constraint}}
            }});
        } else {
            dependencies.emplace_back(name, JsonValue{dependency.constraint});
        }
    }
    return zplus::json::serialize(JsonValue{JsonObject{
        {"name", JsonValue{manifest.name}},
        {"version", JsonValue{manifest.version}},
        {"description", JsonValue{manifest.description}},
        {"author", JsonValue{manifest.author}},
        {"license", JsonValue{manifest.license}},
        {"repository", JsonValue{manifest.repository}},
        {"homepage", JsonValue{manifest.homepage}},
        {"main", JsonValue{manifest.main}},
        {"compiler", JsonValue{manifest.compiler}},
        {"dependencies", JsonValue{std::move(dependencies)}}
    }});
}

std::filesystem::path rootCachePath() {
    const std::string configured = environmentVariable("ZPM_CACHE_DIR");
    if (!configured.empty()) return pathFromUtf8(configured);
#ifdef _WIN32
    const std::string local = environmentVariable("LOCALAPPDATA");
    if (!local.empty()) return pathFromUtf8(local) / "ZPlus" / "zpm" / "cache";
    const std::string profile = environmentVariable("USERPROFILE");
    if (!profile.empty()) {
        return pathFromUtf8(profile) / "AppData" / "Local" /
            "ZPlus" / "zpm" / "cache";
    }
#else
    const std::string xdg = environmentVariable("XDG_CACHE_HOME");
    if (!xdg.empty()) return pathFromUtf8(xdg) / "zplus" / "zpm";
    const std::string home = environmentVariable("HOME");
    if (!home.empty()) return pathFromUtf8(home) / ".cache" / "zplus" / "zpm";
#endif
    throw std::runtime_error("cannot determine a user cache directory; set ZPM_CACHE_DIR");
}

std::filesystem::path createTemporaryDirectory(
    const std::filesystem::path& parent,
    const std::string& prefix
) {
    const auto resolvedParent = parent.empty()
        ? std::filesystem::current_path() : parent;
    std::filesystem::create_directories(resolvedParent);
    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        const auto candidate = resolvedParent / pathFromUtf8(
            prefix + "." + std::to_string(timestamp) + "." +
            std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error(
                "cannot create package staging directory", candidate, error);
        }
    }
    throw std::runtime_error("could not allocate a unique package staging directory");
}

std::filesystem::path unusedTemporaryPath(
    const std::filesystem::path& parent,
    const std::string& prefix
) {
    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        const auto candidate = parent / pathFromUtf8(
            prefix + "." + std::to_string(timestamp) + "." +
            std::to_string(attempt));
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) return candidate;
        if (error) {
            throw std::filesystem::filesystem_error(
                "cannot inspect package temporary path", candidate, error);
        }
    }
    throw std::runtime_error("could not allocate a unique package temporary path");
}

void ensurePackageStore(const std::filesystem::path& projectRoot) {
    const auto packagesRoot = projectRoot / "z+_packages";
    std::error_code error;
    const auto status = std::filesystem::symlink_status(packagesRoot, error);
    if (!error && std::filesystem::is_symlink(status)) {
        throw std::runtime_error("refusing to install through package-store symlink '" +
            pathToUtf8(packagesRoot) + "'");
    }
    if (!error && std::filesystem::exists(status) &&
        !std::filesystem::is_directory(status)) {
        throw std::runtime_error("package store is not a directory: '" +
            pathToUtf8(packagesRoot) + "'");
    }
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        if (!std::filesystem::create_directory(packagesRoot, error) && error) {
            throw std::filesystem::filesystem_error(
                "cannot create package store", packagesRoot, error);
        }
    } else if (error) {
        throw std::filesystem::filesystem_error(
            "cannot inspect package store", packagesRoot, error);
    }
    const auto canonicalRoot = std::filesystem::canonical(projectRoot);
    const auto canonicalStore = std::filesystem::canonical(packagesRoot);
    const auto relative = canonicalStore.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..") {
        throw std::runtime_error("package store resolves outside the project root");
    }
}

std::filesystem::path findCachedPackage(
    const std::string& name,
    const std::string& constraint
) {
    const auto packageCache = packageCacheDirectory() / pathFromUtf8(name);
    std::error_code error;
    if (!std::filesystem::is_directory(packageCache, error) || error) return {};
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(packageCache)) {
        if (!entry.is_directory()) continue;
        try {
            if (satisfiesVersion(pathToUtf8(entry.path().filename()), constraint)) {
                candidates.push_back(entry.path());
            }
        } catch (const std::runtime_error&) {
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) {
            return compareVersion(parseVersion(pathToUtf8(left.filename())),
                parseVersion(pathToUtf8(right.filename()))) > 0;
        });
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

std::filesystem::path findCachedPackageVersion(
    const std::string& name,
    const std::string& version
) {
    const auto candidate = packageCacheDirectory() /
        pathFromUtf8(name) / pathFromUtf8(version);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(candidate, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && !std::filesystem::is_directory(status))) {
        return {};
    }
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot inspect locked package cache entry", candidate, error);
    }
    if (std::filesystem::is_symlink(status)) {
        throw std::runtime_error("locked package cache entry is a symlink: '" +
            pathToUtf8(candidate) + "'");
    }
    return candidate;
}

void copyDirectoryContents(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
) {
    const auto files = packageFiles(source);
    std::filesystem::create_directories(destination);
    for (const auto& [relative, file] : files) {
        const auto output = checkedRelativePath(destination, relative, "package file");
        std::filesystem::create_directories(output.parent_path());
        std::filesystem::copy_file(
            file, output, std::filesystem::copy_options::none);
    }
}

void writeInstalledMarker(
    const std::filesystem::path& directory,
    const PackageSource& package
) {
    const std::string marker = zplus::json::serialize(JsonValue{JsonObject{
        {"name", JsonValue{package.manifest.name}},
        {"version", JsonValue{package.manifest.version}},
        {"integrity", JsonValue{package.integrity}}
    }});
    writeFileIfChangedAtomically(directory / ".zpm-installed.json", marker);
}

void writeCacheMarker(
    const std::filesystem::path& directory,
    const PackageManifest& manifest,
    const std::string& integrity
) {
    const std::string marker = zplus::json::serialize(JsonValue{JsonObject{
        {"name", JsonValue{manifest.name}},
        {"version", JsonValue{manifest.version}},
        {"integrity", JsonValue{integrity}}
    }});
    writeFileIfChangedAtomically(directory / ".zpm-cache.json", marker);
}

std::vector<InstalledPackage> resolvePackages(
    const std::filesystem::path& projectRoot,
    const PackageManifest& project,
    bool locked,
    const std::string& compilerVersion
) {
    std::map<std::string, PackageSource> resolved;
    std::map<std::string, std::pair<std::string, std::string>> lockedPackages;
    std::set<std::string> active;
    const auto packagesRoot = projectRoot / "z+_packages";
    if (locked) {
        const auto lockPath = projectRoot / "zpm-lock.json";
        const JsonValue lock = zplus::json::parse(
            readTextFile(lockPath, maximumManifestBytes));
        if (requiredString(lock, "manifestHash", "package lock") !=
            sha256File(projectRoot / "zpm.json")) {
            throw std::runtime_error(
                "zpm-lock.json does not match zpm.json; run 'zpm install'");
        }
        const JsonValue* packagesValue = objectField(lock, "packages");
        const auto* packages = packagesValue
            ? std::get_if<JsonArray>(&packagesValue->data) : nullptr;
        if (!packages) throw std::runtime_error(
            "zpm-lock.json packages must be an array");
        for (const auto& item : *packages) {
            const std::string name = requiredString(item, "name", "lock package");
            const std::string version = requiredString(item, "version", "lock package");
            const std::string integrity = requiredString(item, "integrity", "lock package");
            validateName(name);
            (void)parseVersion(version);
            if (!validIntegrity(integrity)) {
                throw std::runtime_error(
                    "invalid SHA-256 integrity in lock entry for '" + name + "'");
            }
            if (!lockedPackages.emplace(name, std::pair{version, integrity}).second) {
                throw std::runtime_error(
                    "duplicate package in zpm-lock.json: '" + name + "'");
            }
        }
    }
    const auto resolveDependency = [&](const auto& self,
                                       const std::filesystem::path& parent,
                                       const std::string& name,
                                       const PackageDependency& dependency,
                                       unsigned depth) -> void {
        if (depth > 64) throw std::runtime_error(
            "package dependency graph exceeds 64 levels");
        validateName(name);
        if (active.contains(name)) {
            throw std::runtime_error(
                "circular package dependency involving '" + name + "'");
        }
        const auto existing = resolved.find(name);
        if (existing != resolved.end()) {
            if (!satisfiesVersion(existing->second.manifest.version,
                    dependency.constraint)) {
                throw std::runtime_error("dependency version conflict for '" + name +
                    "': selected " + existing->second.manifest.version +
                    " does not satisfy '" + dependency.constraint + "'");
            }
            if (dependency.localPath) {
                std::error_code error;
                const auto requested = std::filesystem::canonical(
                    parent / dependency.path, error);
                if (error || requested != existing->second.directory) {
                    throw std::runtime_error(
                        "conflicting local sources for dependency '" + name + "'");
                }
            }
            return;
        }

        std::filesystem::path sourceDirectory;
        std::string sourceDescription;
        if (dependency.localPath) {
            sourceDirectory = std::filesystem::canonical(parent / dependency.path);
            sourceDescription = pathToUtf8(dependency.path);
        } else {
            const auto lockedPackage = lockedPackages.find(name);
            if (locked && lockedPackage == lockedPackages.end()) {
                throw std::runtime_error(
                    "package '" + name + "' is missing from zpm-lock.json");
            }
            const auto installed = packagesRoot / pathFromUtf8(name);
            std::string installedPackageProblem;
            std::error_code error;
            if (std::filesystem::is_directory(installed, error) && !error &&
                std::filesystem::is_regular_file(
                    installed / ".zpm-installed.json")) {
                try {
                    const JsonValue marker = zplus::json::parse(readTextFile(
                        installed / ".zpm-installed.json", maximumManifestBytes));
                    const std::string markerName =
                        requiredString(marker, "name", "installed package marker");
                    const std::string markerVersion =
                        requiredString(marker, "version", "installed package marker");
                    const std::string markerIntegrity =
                        requiredString(marker, "integrity", "installed package marker");
                    const std::string installedIntegrity = packageIntegrity(installed);
                    if (markerName == name && markerIntegrity == installedIntegrity) {
                        const auto installedManifest = loadPackageManifest(installed);
                        const bool matchesLock = !locked ||
                            (markerVersion == installedManifest.version &&
                             installedManifest.version == lockedPackage->second.first &&
                             markerIntegrity == lockedPackage->second.second);
                        if (matchesLock &&
                            markerVersion == installedManifest.version &&
                            satisfiesVersion(
                                installedManifest.version, dependency.constraint)) {
                            sourceDirectory = installed;
                            sourceDescription = "installed:" + name;
                        }
                    }
                } catch (const std::exception& validationError) {
                    installedPackageProblem = validationError.what();
                }
            }
            if (sourceDirectory.empty()) {
                sourceDirectory = locked
                    ? findCachedPackageVersion(name, lockedPackage->second.first)
                    : findCachedPackage(name, dependency.constraint);
                if (sourceDirectory.empty()) {
                    if (locked) {
                        throw std::runtime_error("locked package '" + name + " " +
                            lockedPackage->second.first +
                            "' is not installed or cached" +
                            (installedPackageProblem.empty()
                                ? std::string{}
                                : "; installed copy is invalid: " +
                                    installedPackageProblem));
                    }
                    throw std::runtime_error("package '" + name + "' satisfying '" +
                        dependency.constraint + "' is not installed or cached; "
                        "configure a registry or provide a local source" +
                        (installedPackageProblem.empty()
                            ? std::string{}
                            : "; installed copy is invalid: " +
                                installedPackageProblem));
                }
                sourceDescription = "cache:" + name;
            }
        }
        const PackageManifest manifest = loadPackageManifest(sourceDirectory);
        if (manifest.name != name) {
            throw std::runtime_error("dependency key '" + name +
                "' does not match package name '" + manifest.name + "'");
        }
        if (!manifest.compiler.empty() &&
            !satisfiesVersion(compilerVersion, manifest.compiler)) {
            throw std::runtime_error("package '" + name + "' requires Z+ compiler '" +
                manifest.compiler + "', but the selected compiler is " +
                compilerVersion);
        }
        if (!satisfiesVersion(manifest.version, dependency.constraint)) {
            throw std::runtime_error("package '" + name + "' version " +
                manifest.version + " does not satisfy '" + dependency.constraint + "'");
        }
        const std::string integrity = packageIntegrity(sourceDirectory);
        PackageSource package{manifest, sourceDirectory, sourceDescription, integrity};
        resolved.emplace(name, package);
        active.insert(name);
        for (const auto& [childName, childDependency] : manifest.dependencies) {
            self(self, sourceDirectory, childName, childDependency, depth + 1);
        }
        active.erase(name);
    };

    for (const auto& [name, dependency] : project.dependencies) {
        resolveDependency(resolveDependency, projectRoot, name, dependency, 0);
    }

    std::vector<InstalledPackage> result;
    result.reserve(resolved.size());
    for (const auto& [name, package] : resolved) {
        (void)name;
        result.push_back({package.manifest.name, package.manifest.version,
            package.integrity, package.directory});
    }
    if (locked) {
        if (lockedPackages.size() != result.size()) {
            throw std::runtime_error(
                "zpm-lock.json package set does not match resolved dependencies");
        }
        for (const auto& package : result) {
            const auto item = lockedPackages.find(package.name);
            if (item == lockedPackages.end() ||
                item->second.first != package.version ||
                item->second.second != package.integrity) {
                throw std::runtime_error("installed package '" + package.name +
                    "' differs from zpm-lock.json");
            }
        }
    }
    return result;
}

void writeLockfile(
    const std::filesystem::path& projectRoot,
    const std::vector<InstalledPackage>& packages
) {
    JsonArray values;
    for (const auto& package : packages) {
        values.emplace_back(JsonObject{
            {"name", JsonValue{package.name}},
            {"version", JsonValue{package.version}},
            {"integrity", JsonValue{package.integrity}}
        });
    }
    const std::string lock = zplus::json::serialize(JsonValue{JsonObject{
        {"format", JsonValue{1.0}},
        {"manifestHash", JsonValue{sha256File(projectRoot / "zpm.json")}},
        {"packages", JsonValue{std::move(values)}}
    }});
    writeFileIfChangedAtomically(projectRoot / "zpm-lock.json", lock);
}

std::uint32_t readU32(std::istream& input) {
    std::array<unsigned char, 4> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!input) throw std::runtime_error("truncated ZPM archive");
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint64_t readU64(std::istream& input) {
    std::array<unsigned char, 8> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!input) throw std::runtime_error("truncated ZPM archive");
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
    }
    return result;
}

void writeU32(std::ostream& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>(value >> shift));
    }
}

void writeU64(std::ostream& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.put(static_cast<char>(value >> shift));
    }
}

std::filesystem::path safeArchiveEntry(
    const std::filesystem::path& root,
    const std::string& name
) {
    if (name.empty() || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        throw std::runtime_error("invalid ZPM archive path");
    }
    const auto relative = pathFromUtf8(name);
    if (relative.is_absolute() || relative.has_root_name()) {
        throw std::runtime_error("absolute paths are not allowed in ZPM archives");
    }
    for (const auto& component : relative) {
        if (component == "." || component == ".." || component.empty()) {
            throw std::runtime_error("traversal paths are not allowed in ZPM archives");
        }
    }
    const auto output = (root / relative).lexically_normal();
    const auto inside = output.lexically_relative(root.lexically_normal());
    if (inside.empty() || inside.is_absolute() || *inside.begin() == "..") {
        throw std::runtime_error("ZPM archive entry escapes its extraction directory");
    }
    return output;
}

void writeManifestToRoot(const std::filesystem::path& root, const PackageManifest& manifest) {
    writeFileAtomically(root / "zpm.json", manifestJson(manifest));
}

} // namespace

bool packageVersionSatisfies(
    const std::string& version,
    const std::string& constraint
) {
    return satisfiesVersion(version, constraint);
}

std::filesystem::path cachedPackage(
    const std::string& name,
    const std::string& constraint
) {
    validateName(name);
    return findCachedPackage(name, constraint);
}

std::filesystem::path cachedPackageVersion(
    const std::string& name,
    const std::string& version
) {
    validateName(name);
    (void)parseVersion(version);
    return findCachedPackageVersion(name, version);
}

std::string packageTreeIntegrity(const std::filesystem::path& directory) {
    return packageIntegrity(directory);
}

PackageManifest loadPackageManifest(const std::filesystem::path& directory) {
    const auto root = std::filesystem::canonical(directory);
    const JsonValue json = zplus::json::parse(
        readTextFile(root / "zpm.json", maximumManifestBytes));
    PackageManifest manifest;
    manifest.name = requiredString(json, "name", "package manifest");
    manifest.version = requiredString(json, "version", "package manifest");
    validateName(manifest.name);
    (void)parseVersion(manifest.version);
    manifest.description = optionalString(json, "description");
    manifest.author = optionalString(json, "author");
    manifest.license = optionalString(json, "license");
    manifest.repository = optionalString(json, "repository");
    manifest.homepage = optionalString(json, "homepage");
    manifest.main = requiredString(json, "main", "package manifest");
    manifest.compiler = optionalString(json, "compiler");
    const auto main = checkedRelativePath(root, manifest.main, "package main");
    if (main.extension() != ".zh" || !std::filesystem::is_regular_file(main)) {
        throw std::runtime_error(
            "package main must name an existing .zh Z+ module");
    }
    const JsonValue* dependencies = objectField(json, "dependencies");
    if (dependencies) {
        const auto* object = std::get_if<JsonObject>(&dependencies->data);
        if (!object) throw std::runtime_error("package dependencies must be an object");
        for (const auto& [name, value] : *object) {
            validateName(name);
            PackageDependency dependency;
            if (const auto* constraint = std::get_if<std::string>(&value.data)) {
                dependency.constraint = *constraint;
                if (dependency.constraint.empty()) {
                    throw std::runtime_error("dependency constraint cannot be empty");
                }
                if (dependency.constraint != "*") {
                    std::istringstream terms(dependency.constraint);
                    std::string term;
                    while (terms >> term) {
                        std::size_t offset = term.starts_with(">=") ||
                            term.starts_with("<=") ? 2 :
                            (!term.empty() && std::string_view("=<>^~").find(term[0]) !=
                                std::string_view::npos ? 1 : 0);
                        (void)parseVersion(term.substr(offset));
                    }
                }
            } else if (std::holds_alternative<JsonObject>(value.data)) {
                dependency.localPath = true;
                dependency.path = pathFromUtf8(requiredString(value, "path",
                    "local package dependency"));
                dependency.constraint = optionalString(value, "version");
                if (dependency.constraint.empty()) dependency.constraint = "*";
                if (dependency.constraint != "*") {
                    std::istringstream terms(dependency.constraint);
                    std::string term;
                    while (terms >> term) {
                        std::size_t offset = term.starts_with(">=") ||
                            term.starts_with("<=") ? 2 :
                            (!term.empty() && std::string_view("=<>^~").find(term[0]) !=
                                std::string_view::npos ? 1 : 0);
                        (void)parseVersion(term.substr(offset));
                    }
                }
            } else {
                throw std::runtime_error(
                    "dependency '" + name + "' must be a version string or local path object");
            }
            if (!manifest.dependencies.emplace(name, std::move(dependency)).second) {
                throw std::runtime_error("duplicate dependency '" + name + "'");
            }
        }
    }
    (void)packageFiles(root);
    return manifest;
}

void initializePackage(const std::filesystem::path& directory, std::string name) {
    if (name.empty()) name = pathToUtf8(std::filesystem::current_path().filename());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    validateName(name);
    std::filesystem::create_directories(directory);
    const auto manifestPath = directory / "zpm.json";
    if (std::filesystem::exists(manifestPath)) {
        throw std::runtime_error("zpm.json already exists; refusing to overwrite it");
    }
    PackageManifest manifest;
    manifest.name = std::move(name);
    manifest.version = "0.1.0";
    manifest.description = "A Z+ package.";
    manifest.main = "src/main.zh";
    manifest.compiler = ">=1.2.0";
    std::filesystem::create_directories(directory / "src");
    std::filesystem::create_directories(directory / "tests");
    const auto mainPath = directory / pathFromUtf8(manifest.main);
    if (!std::filesystem::exists(mainPath)) {
        writeFileAtomically(mainPath,
            "start {\n"
            "    pub fn packageVersion(): string {\n"
            "        return \"0.1.0\"\n"
            "    } end fn\n"
            "} stop\n");
    }
    const auto entryPath = directory / "src" / "main.zp";
    if (!std::filesystem::exists(entryPath)) {
        writeFileAtomically(entryPath,
            "use \"main.zh\" as package\n"
            "start {\n} stop\n");
    }
    const auto testPath = directory / "tests" / "main.zp";
    if (!std::filesystem::exists(testPath)) {
        writeFileAtomically(testPath,
            "use \"../src/main.zh\" as package\n"
            "start {\n} stop\n");
    }
    writeManifestToRoot(directory, manifest);
    const auto projectFile = directory / "zproject.toml";
    if (!std::filesystem::exists(projectFile)) {
        const std::string projectName = manifest.name;
        writeFileAtomically(projectFile,
            "[project]\nname = \"" + projectName +
            "\"\nversion = \"0.1.0\"\nentry = \"src/main.zp\"\n"
            "\n[build]\nbackend = \"native\"\ndefault_profile = \"debug\"\n"
            "output = \"build\"\n"
            "\n[sources]\ndirectories = [\"src\", \"tests\"]\n"
            "exclude = [\"build\", \"z+_packages\"]\n"
            "\n[tests]\ntargets = [\"package_tests\"]\n"
            "\n[targets]\ndefault = \"app\"\n"
            "\n[target.app]\ntype = \"executable\"\n"
            "entry = \"src/main.zp\"\n"
            "\n[target.package_tests]\ntype = \"test\"\n"
            "entry = \"tests/main.zp\"\n");
    }
}

void addLocalPackage(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& packageSource
) {
    const auto root = std::filesystem::canonical(projectRoot);
    const auto source = std::filesystem::canonical(packageSource);
    if (source == root) {
        throw std::runtime_error("a project cannot depend on itself as a local package");
    }
    const PackageManifest package = loadPackageManifest(source);
    const auto relative = std::filesystem::relative(source, root);
    if (relative.empty()) {
        throw std::runtime_error("cannot express local package path relative to project");
    }
    const auto manifestPath = root / "zpm.json";
    JsonValue json = zplus::json::parse(readTextFile(manifestPath, maximumManifestBytes));
    auto* object = std::get_if<JsonObject>(&json.data);
    if (!object) throw std::runtime_error("project zpm.json must be a JSON object");
    JsonValue* dependencyObject = nullptr;
    for (auto& [key, value] : *object) {
        if (key == "dependencies") dependencyObject = &value;
    }
    if (!dependencyObject) {
        object->emplace_back("dependencies", JsonValue{JsonObject{}});
        dependencyObject = &object->back().second;
    }
    auto* dependencies = std::get_if<JsonObject>(&dependencyObject->data);
    if (!dependencies) throw std::runtime_error("zpm.json dependencies must be an object");
    const auto existing = std::find_if(
        dependencies->begin(), dependencies->end(), [&](const auto& item) {
            return item.first == package.name;
        });
    JsonValue value{JsonObject{
        {"path", JsonValue{pathToUtf8(relative)}},
        {"version", JsonValue{"^" + package.version}}
    }};
    if (existing == dependencies->end()) {
        dependencies->emplace_back(package.name, std::move(value));
    } else {
        existing->second = std::move(value);
    }
    writeFileIfChangedAtomically(manifestPath, zplus::json::serialize(json));
}

void addPackageDependency(
    const std::filesystem::path& projectRoot,
    const std::string& name,
    const std::string& constraint
) {
    validateName(name);
    bool validConstraint = constraint == "*";
    if (!constraint.empty() && constraint != "*") {
        std::istringstream input(constraint);
        std::string term;
        validConstraint = true;
        bool hasTerm = false;
        while (input >> term) {
            std::size_t offset = term.starts_with(">=") ||
                term.starts_with("<=") ? 2 :
                (!term.empty() && std::string_view("=<>^~").find(term[0]) !=
                    std::string_view::npos ? 1 : 0);
            try {
                (void)parseVersion(term.substr(offset));
                hasTerm = true;
            } catch (const std::runtime_error&) {
                validConstraint = false;
                break;
            }
        }
        validConstraint = validConstraint && hasTerm;
    }
    if (!validConstraint) {
        throw std::runtime_error("invalid semantic-version constraint '" +
            constraint + "'");
    }
    const auto root = std::filesystem::canonical(projectRoot);
    const auto manifestPath = root / "zpm.json";
    JsonValue json = zplus::json::parse(
        readTextFile(manifestPath, maximumManifestBytes));
    auto* object = std::get_if<JsonObject>(&json.data);
    if (!object) throw std::runtime_error("project zpm.json must be a JSON object");
    JsonValue* dependencyObject = nullptr;
    for (auto& [key, value] : *object) {
        if (key == "dependencies") dependencyObject = &value;
    }
    if (!dependencyObject) {
        object->emplace_back("dependencies", JsonValue{JsonObject{}});
        dependencyObject = &object->back().second;
    }
    auto* dependencies = std::get_if<JsonObject>(&dependencyObject->data);
    if (!dependencies) throw std::runtime_error("zpm.json dependencies must be an object");
    const auto existing = std::find_if(
        dependencies->begin(), dependencies->end(), [&](const auto& item) {
            return item.first == name;
        });
    if (existing == dependencies->end()) {
        dependencies->emplace_back(name, JsonValue{constraint});
    } else {
        existing->second = JsonValue{constraint};
    }
    writeFileIfChangedAtomically(manifestPath, zplus::json::serialize(json));
}

void removePackage(const std::filesystem::path& projectRoot, const std::string& name) {
    validateName(name);
    const auto root = std::filesystem::canonical(projectRoot);
    const auto manifestPath = root / "zpm.json";
    JsonValue json = zplus::json::parse(readTextFile(manifestPath, maximumManifestBytes));
    auto* object = std::get_if<JsonObject>(&json.data);
    if (!object) throw std::runtime_error("project zpm.json must be a JSON object");
    auto dependency = std::find_if(object->begin(), object->end(), [](const auto& field) {
        return field.first == "dependencies";
    });
    if (dependency == object->end()) {
        throw std::runtime_error("package '" + name + "' is not a project dependency");
    }
    auto* dependencies = std::get_if<JsonObject>(&dependency->second.data);
    if (!dependencies) throw std::runtime_error("zpm.json dependencies must be an object");
    const auto found = std::find_if(
        dependencies->begin(), dependencies->end(), [&](const auto& item) {
            return item.first == name;
        });
    if (found == dependencies->end()) {
        throw std::runtime_error("package '" + name + "' is not a project dependency");
    }
    dependencies->erase(found);
    writeFileIfChangedAtomically(manifestPath, zplus::json::serialize(json));
}

std::vector<InstalledPackage> installPackages(
    const std::filesystem::path& projectRoot,
    const std::string& compilerVersion,
    bool locked
) {
    const auto root = std::filesystem::canonical(projectRoot);
    const auto manifestPath = root / "zpm.json";
    if (!std::filesystem::is_regular_file(manifestPath)) return {};
    const PackageManifest project = loadPackageManifest(root);
    if (!project.compiler.empty() &&
        !satisfiesVersion(compilerVersion, project.compiler)) {
        throw std::runtime_error("project requires Z+ compiler '" + project.compiler +
            "', but the selected compiler is " + compilerVersion);
    }
    auto packages = resolvePackages(root, project, locked, compilerVersion);
    const auto packagesRoot = root / "z+_packages";
    if (!packages.empty()) ensurePackageStore(root);
    for (const auto& package : packages) {
        const auto destination = packagesRoot / pathFromUtf8(package.name);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(destination, error);
        if (!error && std::filesystem::is_symlink(status)) {
            throw std::runtime_error("refusing to install through package symlink '" +
                pathToUtf8(destination) + "'");
        }
        if (std::filesystem::is_directory(status)) {
            const auto markerPath = destination / ".zpm-installed.json";
            if (!std::filesystem::is_regular_file(markerPath)) {
                throw std::runtime_error("package destination already exists and is not "
                    "managed by ZPM: '" + pathToUtf8(destination) + "'");
            }
            const JsonValue marker = zplus::json::parse(
                readTextFile(markerPath, maximumManifestBytes));
            const std::string previousIntegrity =
                requiredString(marker, "integrity", "installed package marker");
            bool destinationValid = false;
            if (previousIntegrity == package.integrity) {
                try {
                    destinationValid =
                        packageIntegrity(destination) == package.integrity;
                } catch (const std::exception&) {
                    destinationValid = false;
                }
            }
            if (destinationValid) {
                continue;
            }
            std::cerr << "[packages] restoring '" << package.name
                      << "' from its verified source\n";
        } else if (error && error != std::errc::no_such_file_or_directory) {
            throw std::filesystem::filesystem_error(
                "cannot inspect package destination", destination, error);
        }
        const auto staging = createTemporaryDirectory(
            packagesRoot, "." + package.name + ".zpm-stage");
        std::error_code cleanupError;
        const auto backup = unusedTemporaryPath(
            packagesRoot, "." + package.name + ".zpm-backup");
        bool movedOld = false;
        try {
            copyDirectoryContents(package.directory, staging);
            writeInstalledMarker(staging, PackageSource{
                loadPackageManifest(package.directory), package.directory, {}, package.integrity});
            if (std::filesystem::exists(destination)) {
                std::filesystem::rename(destination, backup);
                movedOld = true;
            }
            std::filesystem::rename(staging, destination);
            if (movedOld) std::filesystem::remove_all(backup);
        } catch (...) {
            std::filesystem::remove_all(staging, cleanupError);
            if (movedOld && !std::filesystem::exists(destination)) {
                std::filesystem::rename(backup, destination, cleanupError);
            }
            throw;
        }
    }
    if (!locked) writeLockfile(root, packages);
    std::set<std::string> activeNames;
    for (const auto& package : packages) activeNames.insert(package.name);
    if (std::filesystem::is_directory(packagesRoot)) {
        for (const auto& entry : std::filesystem::directory_iterator(packagesRoot)) {
            if (!entry.is_directory() || entry.is_symlink()) continue;
            const std::string name = pathToUtf8(entry.path().filename());
            if (name.starts_with(".")) continue;
            if (activeNames.contains(name)) continue;
            const auto marker = entry.path() / ".zpm-installed.json";
            if (std::filesystem::is_regular_file(marker)) {
                std::filesystem::remove_all(entry.path());
            }
        }
    }
    return packages;
}

std::vector<InstalledPackage> listPackages(const std::filesystem::path& projectRoot) {
    std::vector<InstalledPackage> result;
    const auto packagesRoot = projectRoot / "z+_packages";
    if (!std::filesystem::is_directory(packagesRoot)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(packagesRoot)) {
        if (!entry.is_directory() || entry.is_symlink()) continue;
        const auto markerPath = entry.path() / ".zpm-installed.json";
        if (!std::filesystem::is_regular_file(markerPath)) continue;
        const JsonValue marker = zplus::json::parse(
            readTextFile(markerPath, maximumManifestBytes));
        result.push_back({
            requiredString(marker, "name", "installed package marker"),
            requiredString(marker, "version", "installed package marker"),
            requiredString(marker, "integrity", "installed package marker"),
            entry.path()
        });
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return result;
}

std::filesystem::path packageCacheDirectory() {
    return rootCachePath();
}

void cachePackage(const std::filesystem::path& packageDirectory) {
    const auto source = std::filesystem::canonical(packageDirectory);
    const PackageManifest manifest = loadPackageManifest(source);
    const std::string integrity = packageIntegrity(source);
    const auto cache = packageCacheDirectory() /
        pathFromUtf8(manifest.name) / pathFromUtf8(manifest.version);
    std::filesystem::create_directories(cache.parent_path());
    if (std::filesystem::is_directory(cache)) {
        if (packageIntegrity(cache) == integrity) {
            writeCacheMarker(cache, manifest, integrity);
            return;
        }
        throw std::runtime_error("cache already contains different contents for " +
            manifest.name + " " + manifest.version);
    }
    const auto staging = createTemporaryDirectory(
        cache.parent_path(), "." + manifest.version + ".zpm-stage");
    try {
        copyDirectoryContents(source, staging);
        writeCacheMarker(staging, manifest, integrity);
        std::filesystem::rename(staging, cache);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
}

std::size_t cleanPackageCache() {
    const auto cache = packageCacheDirectory();
    std::error_code error;
    const auto cacheStatus = std::filesystem::symlink_status(cache, error);
    if (error == std::errc::no_such_file_or_directory) return 0;
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot inspect package cache", cache, error);
    }
    if (std::filesystem::is_symlink(cacheStatus)) {
        throw std::runtime_error("refusing to clean a symlinked package cache");
    }
    if (!std::filesystem::is_directory(cacheStatus)) {
        throw std::runtime_error("package cache path is not a directory");
    }
    const auto canonicalCache = std::filesystem::canonical(cache);
    std::size_t removed = 0;
    for (const auto& packageEntry : std::filesystem::directory_iterator(cache)) {
        if (!packageEntry.is_directory() || packageEntry.is_symlink()) continue;
        const auto packageDirectory = packageEntry.path();
        const auto packageName = pathToUtf8(packageDirectory.filename());
        try {
            validateName(packageName);
        } catch (const std::runtime_error&) {
            continue;
        }
        for (const auto& versionEntry :
             std::filesystem::directory_iterator(packageDirectory)) {
            if (!versionEntry.is_directory() || versionEntry.is_symlink()) continue;
            const auto directory = versionEntry.path();
            const auto relative = std::filesystem::canonical(directory)
                .lexically_relative(canonicalCache);
            if (relative.empty() || relative.is_absolute() ||
                *relative.begin() == "..") {
                throw std::runtime_error("package cache entry resolves outside its root");
            }
            const auto markerPath = directory / ".zpm-cache.json";
            if (!std::filesystem::is_regular_file(markerPath)) continue;
            const JsonValue marker = zplus::json::parse(
                readTextFile(markerPath, maximumManifestBytes));
            const std::string markerName =
                requiredString(marker, "name", "package cache marker");
            const std::string markerVersion =
                requiredString(marker, "version", "package cache marker");
            const std::string markerIntegrity =
                requiredString(marker, "integrity", "package cache marker");
            if (markerName != packageName ||
                markerVersion != pathToUtf8(directory.filename()) ||
                markerIntegrity != packageIntegrity(directory)) {
                throw std::runtime_error(
                    "refusing to clean modified package cache entry '" +
                    pathToUtf8(directory) + "'");
            }
            std::filesystem::remove_all(directory);
            ++removed;
        }
        std::filesystem::remove(packageDirectory, error);
        if (error && error != std::errc::directory_not_empty) {
            throw std::filesystem::filesystem_error(
                "cannot remove empty package cache directory",
                packageDirectory, error);
        }
        error.clear();
    }
    return removed;
}

void createPackageArchive(
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& archivePath
) {
    const auto root = std::filesystem::canonical(packageDirectory);
    (void)loadPackageManifest(root);
    const auto files = packageFiles(root);
    if (files.size() > maximumArchiveEntries) {
        throw std::length_error("package archive exceeds the file-count limit");
    }
    if (std::filesystem::exists(archivePath)) {
        throw std::runtime_error(
            "package archive already exists; refusing to overwrite it");
    }
    const auto temporary = unusedTemporaryPath(
        archivePath.parent_path(), "." + pathToUtf8(archivePath.filename()) + ".zpm-tmp");
    std::ofstream output(temporary, std::ios::binary | std::ios::out);
    if (!output) throw std::runtime_error(
        "cannot create package archive '" + pathToUtf8(temporary) + "'");
    try {
        output.write("ZPM1", 4);
        writeU32(output, static_cast<std::uint32_t>(files.size()));
        for (const auto& [relative, file] : files) {
            if (relative.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw std::length_error("package archive path is too long");
            }
            const std::string contents = readTextFile(
                file, static_cast<std::size_t>(maximumPackageFileBytes));
            writeU32(output, static_cast<std::uint32_t>(relative.size()));
            writeU64(output, contents.size());
            output.write(relative.data(), static_cast<std::streamsize>(relative.size()));
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!output) throw std::runtime_error("failed writing package archive");
        }
        output.close();
        if (!output) throw std::runtime_error("failed finalizing package archive");
        std::filesystem::rename(temporary, archivePath);
    } catch (...) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void installPackageArchive(
    const std::filesystem::path& archivePath,
    const std::filesystem::path& destination
) {
    std::ifstream input(archivePath, std::ios::binary);
    if (!input) throw std::runtime_error(
        "cannot read package archive '" + pathToUtf8(archivePath) + "'");
    char magic[4]{};
    input.read(magic, sizeof(magic));
    if (!input || std::string_view(magic, sizeof(magic)) != "ZPM1") {
        throw std::runtime_error("unsupported package archive format");
    }
    const auto count = readU32(input);
    if (count == 0 || count > maximumArchiveEntries) {
        throw std::runtime_error("invalid package archive entry count");
    }
    if (std::filesystem::exists(destination)) {
        throw std::runtime_error("archive extraction destination already exists");
    }
    const auto staging = createTemporaryDirectory(destination.parent_path(),
        "." + pathToUtf8(destination.filename()) + ".zpm-extract");
    std::error_code ignored;
    std::set<std::string> seen;
    std::uint64_t totalBytes = 0;
    try {
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto pathLength = readU32(input);
            const auto size = readU64(input);
            if (pathLength == 0 || pathLength > 4096 ||
                size > maximumPackageFileBytes ||
                totalBytes > maximumPackageBytes - size) {
                throw std::runtime_error("package archive exceeds its path or size limits");
            }
            std::string name(pathLength, '\0');
            input.read(name.data(), static_cast<std::streamsize>(name.size()));
            if (!input) throw std::runtime_error("truncated ZPM archive path");
            const auto output = safeArchiveEntry(staging, name);
            std::string key = name;
#ifdef _WIN32
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
#endif
            if (!seen.insert(key).second) {
                throw std::runtime_error("duplicate path in ZPM archive: '" + name + "'");
            }
            std::filesystem::create_directories(output.parent_path());
            std::ofstream file(output, std::ios::binary | std::ios::out);
            if (!file) throw std::runtime_error(
                "cannot create extracted package file '" + pathToUtf8(output) + "'");
            std::array<char, 8192> buffer{};
            std::uint64_t remaining = size;
            while (remaining > 0) {
                const auto chunk = static_cast<std::size_t>(
                    std::min<std::uint64_t>(remaining, buffer.size()));
                input.read(buffer.data(), static_cast<std::streamsize>(chunk));
                if (!input) throw std::runtime_error("truncated ZPM archive file");
                file.write(buffer.data(), static_cast<std::streamsize>(chunk));
                if (!file) throw std::runtime_error("failed writing extracted package file");
                remaining -= chunk;
            }
            totalBytes += size;
        }
        if (input.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("package archive has trailing data");
        }
        (void)loadPackageManifest(staging);
        std::filesystem::rename(staging, destination);
    } catch (...) {
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
}

} // namespace zbuild
