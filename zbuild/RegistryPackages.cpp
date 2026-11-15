#include "RegistryPackages.hpp"

#include "Filesystem.hpp"
#include "Hash.hpp"
#include "Packages.hpp"
#include "RegistryClient.hpp"

#include "../runtime/Json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace zbuild {
namespace {

using JsonValue = zplus::json::Value;
using JsonObject = JsonValue::Object;
using JsonArray = JsonValue::Array;

struct LockedPackage {
    std::string version;
    std::string integrity;
};

const JsonValue* field(const JsonValue& value, std::string_view name) {
    const auto* object = std::get_if<JsonObject>(&value.data);
    if (!object) throw std::runtime_error("expected a JSON object in zpm-lock.json");
    const JsonValue* result = nullptr;
    for (const auto& [key, item] : *object) {
        if (key == name) {
            if (result) throw std::runtime_error(
                "duplicate JSON property '" + std::string(name) + "'");
            result = &item;
        }
    }
    return result;
}

std::string stringField(
    const JsonValue& value,
    std::string_view name,
    const std::string& context
) {
    const JsonValue* item = field(value, name);
    const auto* result = item ? std::get_if<std::string>(&item->data) : nullptr;
    if (!result || result->empty()) {
        throw std::runtime_error(context + " requires a non-empty string '" +
            std::string(name) + "'");
    }
    return *result;
}

std::string readText(
    const std::filesystem::path& path,
    std::size_t maximumBytes
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(
        "cannot read '" + pathToUtf8(path) + "'");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximumBytes) {
        throw std::runtime_error(
            "'" + pathToUtf8(path) + "' exceeds the allowed size");
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.seekg(0, std::ios::beg);
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    if (!input && !input.eof()) {
        throw std::runtime_error("failed reading '" + pathToUtf8(path) + "'");
    }
    return contents;
}

std::map<std::string, LockedPackage> lockedPackages(
    const std::filesystem::path& root
) {
    const auto lockPath = root / "zpm-lock.json";
    if (!std::filesystem::is_regular_file(lockPath)) {
        throw std::runtime_error(
            "zpm-lock.json is required for 'zpm install --locked'");
    }
    const JsonValue lock = zplus::json::parse(readText(lockPath, 1024 * 1024));
    if (stringField(lock, "manifestHash", "package lock") !=
        sha256File(root / "zpm.json")) {
        throw std::runtime_error(
            "zpm-lock.json does not match zpm.json; run 'zpm install'");
    }
    const JsonValue* packageValue = field(lock, "packages");
    const auto* packages = packageValue
        ? std::get_if<JsonArray>(&packageValue->data) : nullptr;
    if (!packages) throw std::runtime_error("zpm-lock.json packages must be an array");
    std::map<std::string, LockedPackage> result;
    for (const auto& item : *packages) {
        const std::string name = stringField(item, "name", "lock package");
        const std::string version = stringField(item, "version", "lock package");
        const std::string integrity = stringField(item, "integrity", "lock package");
        if (!result.emplace(name, LockedPackage{version, integrity}).second) {
            throw std::runtime_error("duplicate package in zpm-lock.json: '" + name + "'");
        }
    }
    return result;
}

JsonValue responseJson(
    const RegistryResponse& response,
    std::string_view operation
) {
    if (response.status < 200 || response.status >= 300) {
        std::string message = std::string(operation) + " failed with HTTP " +
            std::to_string(response.status);
        try {
            const JsonValue error = zplus::json::parse(response.body);
            const JsonValue* errorValue = field(error, "error");
            if (errorValue) {
                const JsonValue* messageValue = field(*errorValue, "message");
                if (messageValue) {
                    if (const auto* text = std::get_if<std::string>(&messageValue->data)) {
                        message = *text;
                    }
                }
            }
        } catch (const std::exception&) {
        }
        throw std::runtime_error(message);
    }
    return zplus::json::parse(response.body);
}

std::string encodePathComponent(std::string_view value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(digits[ch >> 4]);
            encoded.push_back(digits[ch & 0x0F]);
        }
    }
    return encoded;
}

std::string stringFieldOrEmpty(
    const JsonValue& value,
    std::string_view name
) {
    const JsonValue* item = field(value, name);
    if (!item) return {};
    const auto* text = std::get_if<std::string>(&item->data);
    if (!text) throw std::runtime_error(
        "registry response field '" + std::string(name) + "' is invalid");
    return *text;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto timestamp = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            path_ = base / pathFromUtf8(
                "zpm-registry-" + std::to_string(timestamp) + "-" +
                std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error(
                    "cannot create temporary package directory", path_, error);
            }
        }
        throw std::runtime_error("cannot allocate a unique temporary package directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::filesystem::path downloadAndCache(
    const std::filesystem::path& projectRoot,
    const std::string& name,
    const std::string& version,
    const std::string& expectedArchiveIntegrity,
    const std::string& expectedTreeIntegrity,
    const std::string& compilerVersion
) {
    const std::string encodedName = encodePathComponent(name);
    const std::string encodedVersion = encodePathComponent(version);
    const auto metadata = responseJson(registryRequest(
        "GET", "/packages/" + encodedName + "/versions/" + encodedVersion),
        "fetch package metadata");
    const std::string apiIntegrity = stringField(
        metadata, "integrity", "registry version metadata");
    const std::string expectedIntegrity = expectedArchiveIntegrity.empty()
        ? apiIntegrity : expectedArchiveIntegrity;
    if (apiIntegrity != expectedIntegrity ||
        !expectedIntegrity.starts_with("sha256:") ||
        expectedIntegrity.size() != 71) {
        throw std::runtime_error(
            "registry integrity does not match the package lock or version metadata");
    }
    const std::string metadataVersion = stringField(
        metadata, "version", "registry version metadata");
    if (metadataVersion != version) {
        throw std::runtime_error("registry returned a different package version");
    }
    std::cout << "[packages] downloading " << name << ' ' << version << '\n';
    const auto archiveResponse = registryRequest(
        "GET",
        "/packages/" + encodedName + "/versions/" + encodedVersion + "/download",
        {}, {}, 4 * 1024 * 1024);
    if (archiveResponse.status != 200) {
        throw std::runtime_error("package download failed with HTTP " +
            std::to_string(archiveResponse.status));
    }
    if ("sha256:" + sha256(archiveResponse.body) != expectedIntegrity) {
        throw std::runtime_error(
            "downloaded package archive failed its SHA-256 integrity check");
    }

    TemporaryDirectory temporary;
    const auto archivePath = temporary.path() / "package.zpm";
    writeFileAtomically(archivePath, archiveResponse.body);
    const auto extraction = temporary.path() / "package";
    installPackageArchive(archivePath, extraction);
    const PackageManifest manifest = loadPackageManifest(extraction);
    if (manifest.name != name || manifest.version != version) {
        throw std::runtime_error(
            "package archive manifest does not match its registry version");
    }
    if (!manifest.compiler.empty() &&
        !packageVersionSatisfies(compilerVersion, manifest.compiler)) {
        throw std::runtime_error("package '" + name + "' requires Z+ compiler '" +
            manifest.compiler + "', but the selected compiler is " + compilerVersion);
    }
    if (!expectedTreeIntegrity.empty() &&
        packageTreeIntegrity(extraction) != expectedTreeIntegrity) {
        throw std::runtime_error(
            "downloaded package contents do not match zpm-lock.json");
    }
    cachePackage(extraction);
    const auto cached = cachedPackageVersion(name, version);
    if (cached.empty()) {
        throw std::runtime_error("verified package was not added to the ZPM cache");
    }
    (void)projectRoot;
    return cached;
}

} // namespace

void prepareRegistryPackages(
    const std::filesystem::path& projectRoot,
    const std::string& compilerVersion,
    bool locked,
    bool forceUpdate
) {
    const auto root = std::filesystem::canonical(projectRoot);
    if (!std::filesystem::is_regular_file(root / "zpm.json")) return;
    const PackageManifest project = loadPackageManifest(root);
    const auto lockedEntries = locked ? lockedPackages(root)
                                      : std::map<std::string, LockedPackage>{};
    std::map<std::string, std::string> resolved;
    std::set<std::string> active;

    const auto resolve = [&](const auto& self,
                             const std::filesystem::path& parent,
                             const std::string& name,
                             const PackageDependency& dependency,
                             unsigned depth) -> void {
        if (depth > 64) {
            throw std::runtime_error("package dependency graph exceeds 64 levels");
        }
        if (active.contains(name)) {
            throw std::runtime_error(
                "circular package dependency involving '" + name + "'");
        }
        const auto prior = resolved.find(name);
        if (prior != resolved.end()) {
            if (!packageVersionSatisfies(prior->second, dependency.constraint)) {
                throw std::runtime_error("dependency version conflict for '" + name +
                    "': selected " + prior->second + " does not satisfy '" +
                    dependency.constraint + "'");
            }
            return;
        }

        std::filesystem::path source;
        std::string selectedVersion;
        if (dependency.localPath) {
            std::error_code error;
            source = std::filesystem::canonical(parent / dependency.path, error);
            if (error) throw std::filesystem::filesystem_error(
                "cannot resolve local package dependency", parent / dependency.path, error);
            const PackageManifest local = loadPackageManifest(source);
            if (local.name != name ||
                !packageVersionSatisfies(local.version, dependency.constraint)) {
                throw std::runtime_error(
                    "local package source does not satisfy dependency '" + name + "'");
            }
            selectedVersion = local.version;
        } else if (locked) {
            const auto selection = lockedEntries.find(name);
            if (selection == lockedEntries.end()) {
                throw std::runtime_error(
                    "package '" + name + "' is missing from zpm-lock.json");
            }
            selectedVersion = selection->second.version;
            if (!packageVersionSatisfies(selectedVersion, dependency.constraint)) {
                throw std::runtime_error(
                    "locked package '" + name + "' does not satisfy '" +
                    dependency.constraint + "'");
            }
            source = cachedPackageVersion(name, selectedVersion);
            if (source.empty() ||
                packageTreeIntegrity(source) != selection->second.integrity) {
                source = downloadAndCache(root, name, selectedVersion, {}, 
                    selection->second.integrity, compilerVersion);
            }
        } else {
            if (!forceUpdate) source = cachedPackage(name, dependency.constraint);
            if (!source.empty()) {
                selectedVersion = loadPackageManifest(source).version;
            } else {
                const std::string encodedName = encodePathComponent(name);
                const std::string encodedRange =
                    encodePathComponent(dependency.constraint);
                const JsonValue selection = responseJson(registryRequest(
                    "GET", "/packages/" + encodedName + "/resolve?range=" +
                        encodedRange),
                    "resolve package '" + name + "'");
                selectedVersion = stringField(
                    selection, "version", "registry resolution");
                const std::string archiveIntegrity = stringField(
                    selection, "integrity", "registry resolution");
                if (!packageVersionSatisfies(
                        selectedVersion, dependency.constraint)) {
                    throw std::runtime_error(
                        "registry selected a version that does not satisfy '" +
                        dependency.constraint + "'");
                }
                source = downloadAndCache(root, name, selectedVersion,
                    archiveIntegrity, {}, compilerVersion);
            }
        }
        if (selectedVersion.empty()) {
            selectedVersion = loadPackageManifest(source).version;
        }
        if (!packageVersionSatisfies(selectedVersion, dependency.constraint)) {
            throw std::runtime_error("package '" + name + "' version " +
                selectedVersion + " does not satisfy '" + dependency.constraint + "'");
        }
        const PackageManifest manifest = loadPackageManifest(source);
        if (manifest.name != name) {
            throw std::runtime_error("package key '" + name +
                "' does not match its manifest name '" + manifest.name + "'");
        }
        if (!manifest.compiler.empty() &&
            !packageVersionSatisfies(compilerVersion, manifest.compiler)) {
            throw std::runtime_error("package '" + name + "' requires Z+ compiler '" +
                manifest.compiler + "', but the selected compiler is " + compilerVersion);
        }
        resolved.emplace(name, selectedVersion);
        active.insert(name);
        for (const auto& [childName, childDependency] : manifest.dependencies) {
            self(self, source, childName, childDependency, depth + 1);
        }
        active.erase(name);
    };

    for (const auto& [name, dependency] : project.dependencies) {
        resolve(resolve, root, name, dependency, 0);
    }
}

} // namespace zbuild
