#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace zbuild {

struct PackageDependency {
    std::string constraint;
    std::filesystem::path path;
    bool localPath = false;
};

struct PackageManifest {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    std::string repository;
    std::string homepage;
    std::string main;
    std::string compiler;
    std::map<std::string, PackageDependency> dependencies;
};

struct InstalledPackage {
    std::string name;
    std::string version;
    std::string integrity;
    std::filesystem::path directory;
};

PackageManifest loadPackageManifest(const std::filesystem::path& directory);
void initializePackage(const std::filesystem::path& directory, std::string name);
void addLocalPackage(const std::filesystem::path& projectRoot,
    const std::filesystem::path& packageSource);
void addPackageDependency(
    const std::filesystem::path& projectRoot,
    const std::string& name,
    const std::string& constraint
);
void removePackage(const std::filesystem::path& projectRoot, const std::string& name);
bool packageVersionSatisfies(
    const std::string& version,
    const std::string& constraint
);
std::filesystem::path cachedPackage(
    const std::string& name,
    const std::string& constraint
);
std::filesystem::path cachedPackageVersion(
    const std::string& name,
    const std::string& version
);
std::string packageTreeIntegrity(const std::filesystem::path& directory);
std::vector<InstalledPackage> installPackages(
    const std::filesystem::path& projectRoot,
    const std::string& compilerVersion,
    bool locked = false
);
std::vector<InstalledPackage> listPackages(const std::filesystem::path& projectRoot);
std::filesystem::path packageCacheDirectory();
void cachePackage(const std::filesystem::path& packageDirectory);
std::size_t cleanPackageCache();
void createPackageArchive(
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& archivePath
);
void installPackageArchive(
    const std::filesystem::path& archivePath,
    const std::filesystem::path& destination
);

} // namespace zbuild
