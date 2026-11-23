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
    struct SelectedPackage {
        PackageManifest manifest;
        std::filesystem::path source;
        bool local = false;
    };
    std::map<std::string, std::string> preferredVersions;

    const auto resolveFromRegistry = [&](const std::string& name,
                                         const std::string& constraint) {
        if (constraint.size() > 512) {
            throw std::runtime_error(
                "combined dependency constraints exceed the registry's 512-character limit");
        }
        const std::string encodedName = encodePathComponent(name);
        const std::string encodedRange = encodePathComponent(constraint);
        const JsonValue selection = responseJson(registryRequest(
            "GET", "/packages/" + encodedName + "/resolve?range=" + encodedRange),
            "resolve package '" + name + "'");
        const std::string version = stringField(
            selection, "version", "registry resolution");
        const std::string archiveIntegrity = stringField(
            selection, "integrity", "registry resolution");
        if (!packageVersionSatisfies(version, constraint)) {
            throw std::runtime_error(
                "registry selected a version that does not satisfy '" + constraint + "'");
        }
        return std::pair{version, downloadAndCache(
            root, name, version, archiveIntegrity, {}, compilerVersion)};
    };

    for (unsigned pass = 0; pass < 64; ++pass) {
        std::map<std::string, SelectedPackage> selected;
        std::map<std::string, std::vector<std::string>> constraints;
        std::set<std::string> active;
        bool restart = false;

        const auto combinedConstraint = [&](const std::string& name,
                                            const std::string& constraint) {
            auto& terms = constraints[name];
            terms.push_back(constraint);
            std::string combined;
            for (const std::string& term : terms) {
                if (!combined.empty()) combined.push_back(' ');
                combined += term;
            }
            if (combined.size() > 512) {
                throw std::runtime_error(
                    "combined dependency constraints for '" + name +
                    "' exceed the registry's 512-character limit");
            }
            return combined;
        };

        const auto chooseSource = [&](const std::string& name,
                                      const std::string& constraint) {
            std::filesystem::path source;
            std::string version;
            if (locked) {
                const auto selection = lockedEntries.find(name);
                if (selection == lockedEntries.end()) {
                    throw std::runtime_error(
                        "package '" + name + "' is missing from zpm-lock.json");
                }
                version = selection->second.version;
                if (!packageVersionSatisfies(version, constraint)) {
                    throw std::runtime_error(
                        "locked package '" + name + "' does not satisfy '" +
                        constraint + "'");
                }
                source = cachedPackageVersion(name, version);
                if (source.empty() ||
                    packageTreeIntegrity(source) != selection->second.integrity) {
                    source = downloadAndCache(root, name, version, {},
                        selection->second.integrity, compilerVersion);
                }
            } else {
                const auto preferred = preferredVersions.find(name);
                if (preferred != preferredVersions.end() &&
                    packageVersionSatisfies(preferred->second, constraint)) {
                    source = cachedPackageVersion(name, preferred->second);
                    if (!source.empty()) version = preferred->second;
                }
                if (source.empty() && !forceUpdate) {
                    source = cachedPackage(name, constraint);
                    if (!source.empty()) version = loadPackageManifest(source).version;
                }
                if (source.empty()) {
                    auto registrySelection = resolveFromRegistry(name, constraint);
                    version = std::move(registrySelection.first);
                    source = std::move(registrySelection.second);
                }
            }
            return std::pair{version, source};
        };

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

            const std::string constraint =
                combinedConstraint(name, dependency.constraint);
            const auto prior = selected.find(name);
            if (prior != selected.end()) {
                if (dependency.localPath) {
                    std::error_code error;
                    const auto requested = std::filesystem::canonical(
                        parent / dependency.path, error);
                    if (error || requested != prior->second.source) {
                        throw std::runtime_error(
                            "conflicting local sources for dependency '" + name + "'");
                    }
                }
                if (packageVersionSatisfies(
                        prior->second.manifest.version, constraint)) {
                    return;
                }
                if (locked || prior->second.local) {
                    throw std::runtime_error("dependency version conflict for '" + name +
                        "': selected " + prior->second.manifest.version +
                        " does not satisfy '" + constraint + "'");
                }
                auto replacement = chooseSource(name, constraint);
                const PackageManifest replacementManifest =
                    loadPackageManifest(replacement.second);
                if (replacementManifest.name != name ||
                    !packageVersionSatisfies(replacementManifest.version, constraint)) {
                    throw std::runtime_error(
                        "replacement package does not satisfy the combined constraints for '" +
                        name + "'");
                }
                preferredVersions[name] = replacementManifest.version;
                restart = true;
                return;
            }

            std::filesystem::path source;
            std::string selectedVersion;
            bool isLocal = dependency.localPath;
            if (dependency.localPath) {
                std::error_code error;
                source = std::filesystem::canonical(parent / dependency.path, error);
                if (error) throw std::filesystem::filesystem_error(
                    "cannot resolve local package dependency",
                    parent / dependency.path, error);
                selectedVersion = loadPackageManifest(source).version;
            } else {
                auto chosen = chooseSource(name, constraint);
                selectedVersion = std::move(chosen.first);
                source = std::move(chosen.second);
            }

            if (!packageVersionSatisfies(selectedVersion, constraint)) {
                throw std::runtime_error("package '" + name + "' version " +
                    selectedVersion + " does not satisfy '" + constraint + "'");
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
            selected.emplace(name, SelectedPackage{manifest, source, isLocal});
            active.insert(name);
            for (const auto& [childName, childDependency] : manifest.dependencies) {
                if (restart) break;
                self(self, source, childName, childDependency, depth + 1);
            }
            active.erase(name);
        };

        for (const auto& [name, dependency] : project.dependencies) {
            if (restart) break;
            resolve(resolve, root, name, dependency, 0);
        }
        if (!restart) return;
    }
    throw std::runtime_error(
        "package dependency resolution did not converge after 64 constraint updates");
}

} // namespace zbuild
