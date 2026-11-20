#include "Config.hpp"
#include "BuildGraph.hpp"
#include "Hash.hpp"
#include "Packages.hpp"
#include "RegistryClient.hpp"
#include "Template.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireThrows(Function&& function, const std::string& expected) {
    try {
        function();
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(expected) == std::string::npos) {
            throw std::runtime_error(
                "expected diagnostic containing '" + expected +
                "', got '" + error.what() + "'");
        }
        return;
    }
    throw std::runtime_error("expected operation to fail");
}

void writePackage(
    const std::filesystem::path& directory,
    const std::string& name,
    const std::string& version,
    const std::string& dependencies = "{}"
) {
    std::filesystem::create_directories(directory / "src");
    std::ofstream source(directory / "src" / "main.zh", std::ios::binary);
    source << "start {\n} stop\n";
    std::ofstream manifest(directory / "zpm.json", std::ios::binary);
    manifest << "{\"name\":\"" << name << "\",\"version\":\"" << version
             << "\",\"main\":\"src/main.zh\",\"compiler\":\">=1.2.0\","
                "\"dependencies\":"
             << dependencies << "}";
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

std::optional<std::string> environmentValue(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
}

void setEnvironment(const char* name, const std::optional<std::string>& value) {
#ifdef _WIN32
    _putenv_s(name, value ? value->c_str() : "");
#else
    if (value) setenv(name, value->c_str(), 1);
    else unsetenv(name);
#endif
}

} // namespace

int main(int, char** argv) {
    try {
        require(
            zbuild::sha256("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-input vector failed");
        require(
            zbuild::sha256("abc") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc vector failed");

        const auto temporary = std::filesystem::temp_directory_path() /
            ("zbuild-unit-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        try {
            const std::vector<std::pair<std::string, std::optional<std::string>>> savedEnvironment{
                {"ZPM_REGISTRY_URL", environmentValue("ZPM_REGISTRY_URL")},
#ifdef _WIN32
                {"LOCALAPPDATA", environmentValue("LOCALAPPDATA")},
                {"APPDATA", environmentValue("APPDATA")}
#else
                {"XDG_CONFIG_HOME", environmentValue("XDG_CONFIG_HOME")}
#endif
            };
            setEnvironment("ZPM_REGISTRY_URL",
                "https://zpm-unit.invalid/api/registry");
#ifdef _WIN32
            setEnvironment("LOCALAPPDATA",
                (temporary / "local-app-data").string());
            setEnvironment("APPDATA",
                (temporary / "app-data").string());
#else
            setEnvironment("XDG_CONFIG_HOME",
                (temporary / "config").string());
#endif
            const std::string registryToken =
                "unit-test-token-1234567890-abcde";
            zbuild::storeRegistryToken(registryToken);
            require(zbuild::loadRegistryToken() == registryToken,
                "saved registry token should round-trip through protected storage");
            zbuild::removeRegistryToken();
            require(zbuild::loadRegistryToken().empty(),
                "removed registry token should not remain in storage");
            requireThrows(
                [] { zbuild::storeRegistryToken("invalid"); },
                "invalid session token");
            for (const auto& [name, value] : savedEnvironment) {
                setEnvironment(name.c_str(), value);
            }

#ifdef _WIN32
            _putenv_s("ZBUILD_TEMPLATES_DIR",
                (temporary / "custom-templates").string().c_str());
#else
            setenv("ZBUILD_TEMPLATES_DIR",
                (temporary / "custom-templates").string().c_str(), 1);
#endif
            zbuild::TemplateRegistry templates(std::filesystem::absolute(argv[0]));
            const auto available = templates.list();
            require(available.size() == 6, "six built-in templates should be discoverable");
            const auto& game = templates.find("gAmE");
            require(game.name == "Game", "template lookup should be case-insensitive");
            require(!templates.find("webserver").available,
                "HTTP server template should be marked unavailable");
            templates.initialize("exe", temporary / "UnitProject", "UnitProject", "1.2.3");
            const auto project =
                zbuild::loadProject(temporary / "UnitProject" / "zproject.toml");
            require(project.implicitTarget, "generated console project should use a valid implicit target");
            require(project.entry == "src/main.zp", "generated project entry should be configured");
            require(!project.windowsGui, "generated console projects should keep the console subsystem");

            const auto schemaProject = temporary / "Project With Spaces";
            std::filesystem::create_directories(schemaProject / "src");
            {
                std::ofstream source(schemaProject / "src" / "main.zp");
                source << "start:\nstop\n";
            }
            {
                std::ofstream config(schemaProject / "zproject.toml");
                config << "[project]\nname = \"SchemaProject\"\nentry = \"src/main.zp\"\n"
                          "\n[build]\nbackend = \"native\"\ndefault_profile = \"release\"\n"
                          "output = \"build\"\n"
                          "\n[build.profiles.debug]\noptimization = 0\n"
                          "\n[build.profiles.release]\noptimization = 0\n"
                          "\n[sources]\ndirectories = [\"src\"]\nexclude = [\"vendor\"]\n"
                          "\n[targets]\ndefault = \"application\"\n"
                          "\n[target.application]\ntype = \"executable\"\n"
                          "entry = \"src/main.zp\"\ndependencies = [\"prepare\", \"prepare\"]\n"
                          "\n[target.prepare]\ntype = \"custom\"\n"
                          "command = [\"cmake\", \"-E\", \"true\"]\n";
            }
            const auto schema =
                zbuild::loadProject(schemaProject / "zproject.toml");
            require(schema.mode == "release", "configured default profile should be selected");
            require(schema.defaultTarget == "application",
                "configured default target should be parsed");
            require(schema.sourceDirectories == std::vector<std::string>{"src"},
                "configured source directories should be parsed");
            require(schema.sourceExcludes == std::vector<std::string>{"vendor"},
                "configured source exclusions should be parsed");
            require(schema.targets.size() == 2,
                "named executable and custom targets should be parsed");

            zbuild::TargetConfig application;
            application.name = "application";
            application.dependencies = {"prepare", "prepare"};
            zbuild::TargetConfig prepare;
            prepare.name = "prepare";
            prepare.type = "custom";
            zbuild::TargetConfig tool;
            tool.name = "tool";
            require(
                zbuild::targetBuildOrder({tool, application, prepare}, "application") ==
                    std::vector<std::string>{"prepare", "application"},
                "build graph should order dependencies first and deduplicate edges");
            require(
                zbuild::targetBuildOrder({tool, application, prepare}) ==
                    std::vector<std::string>{"prepare", "application", "tool"},
                "build graph order should be deterministic for independent targets");
            requireThrows(
                [&] { (void)zbuild::targetBuildOrder({application}, "application"); },
                "missing target");
            prepare.dependencies = {"application"};
            requireThrows(
                [&] { (void)zbuild::targetBuildOrder({application, prepare}, "application"); },
                "circular target dependency");
            requireThrows(
                [&] { (void)zbuild::targetBuildOrder({application, prepare}, "unknown"); },
                "unknown target");

#ifdef _WIN32
            const auto guiProject = temporary / "windows-gui";
            std::filesystem::create_directories(guiProject / "src");
            {
                std::ofstream source(guiProject / "src" / "main.zp");
                source << "start:\nstop\n";
            }
            {
                std::ofstream config(guiProject / "zproject.toml");
                config << "[project]\nname = \"WindowsGui\"\nentry = \"src/main.zp\"\n"
                          "\n[build]\nbackend = \"native\"\nwindows_gui = true\n"
                          "\n[platform]\nwindows = true\nlinux = false\n";
            }
            const auto guiConfig =
                zbuild::loadProject(guiProject / "zproject.toml");
            require(guiConfig.windowsGui, "Windows GUI subsystem setting should parse");
#endif

            const auto localProject = temporary / "local";
            const auto consumerProject = temporary / "consumer";
            templates.initialize("Library", localProject, "LocalProject");
            templates.initialize("Empty", consumerProject, "ConsumerProject");
            {
                std::ofstream config(consumerProject / "zproject.toml", std::ios::app);
                config << "\n[dependencies]\nlocal = \"../local\"\n";
            }
            const auto consumer =
                zbuild::loadProject(consumerProject / "zproject.toml");
            require(consumer.localDependencies.size() == 1, "local dependency should parse");
            zbuild::writeLockfile(consumer);
            std::ifstream lockfile(consumerProject / "zproject.lock");
            const std::string lockContents(
                (std::istreambuf_iterator<char>(lockfile)),
                std::istreambuf_iterator<char>());
            require(lockContents.find("project = \"ConsumerProject\"") != std::string::npos,
                "lockfile should identify the consumer project");
            require(lockContents.find("name = \"local\"") != std::string::npos,
                "lockfile should contain the local dependency");
            require(lockContents.find("version_integrity = \"0.1.0:") != std::string::npos,
                "lockfile should record the dependency version and manifest hash");
            lockfile.close();

            const auto packageCache = temporary / "package-cache";
#ifdef _WIN32
            _putenv_s("ZPM_CACHE_DIR", packageCache.string().c_str());
#else
            setenv("ZPM_CACHE_DIR", packageCache.string().c_str(), 1);
#endif
            const auto semanticOld = temporary / "semantic-old";
            const auto semanticNew = temporary / "semantic-new";
            const auto semanticLatest = temporary / "semantic-latest";
            writePackage(semanticOld, "semantic-lib", "1.0.0-alpha.2");
            writePackage(semanticNew, "semantic-lib", "1.0.0-alpha.10");
            writePackage(semanticLatest, "semantic-lib", "1.0.0-beta.1");
            zbuild::cachePackage(semanticOld);
            zbuild::cachePackage(semanticNew);
            const auto foreignPackage = temporary / "foreign-package";
            writePackage(foreignPackage, "foreign-package", "0.1.0");
            {
                std::ofstream foreignSource(
                    foreignPackage / "src" / "implementation.py");
                foreignSource << "print('not Z+')\n";
            }
            requireThrows(
                [&] { (void)zbuild::loadPackageManifest(foreignPackage); },
                "alternative-language implementations");
            const auto invalidVersionPackage = temporary / "invalid-version-package";
            writePackage(invalidVersionPackage, "invalid-version-package", "1.0.0-alpha.01");
            requireThrows(
                [&] { (void)zbuild::loadPackageManifest(invalidVersionPackage); },
                "invalid semantic version");
            const auto packageConsumer = temporary / "package-consumer";
            zbuild::initializePackage(packageConsumer, "package-consumer");
            {
                std::ofstream manifest(packageConsumer / "zpm.json", std::ios::trunc);
                manifest << "{\"name\":\"package-consumer\",\"version\":\"0.1.0\","
                    "\"main\":\"src/main.zh\",\"compiler\":\">=1.2.0\","
                    "\"dependencies\":{\"semantic-lib\":\"^1.0.0-alpha.2\"}}";
            }
            auto resolvedPackages = zbuild::installPackages(
                packageConsumer, "1.2.0");
            require(resolvedPackages.size() == 1 &&
                    resolvedPackages.front().version == "1.0.0-alpha.10",
                "semantic versions must order prerelease numeric identifiers numerically");
            zbuild::cachePackage(semanticLatest);
            resolvedPackages = zbuild::installPackages(packageConsumer, "1.2.0", true);
            require(resolvedPackages.size() == 1 &&
                    resolvedPackages.front().version == "1.0.0-alpha.10",
                "locked installs must retain their exact version when newer cache versions exist");
            {
                std::ofstream installed(
                    packageConsumer / "z+_packages" / "semantic-lib" /
                        "src" / "main.zh",
                    std::ios::trunc);
                installed << "tampered\n";
            }
            (void)zbuild::installPackages(packageConsumer, "1.2.0", true);
            {
                std::ifstream restored(
                    packageConsumer / "z+_packages" / "semantic-lib" /
                        "src" / "main.zh");
                const std::string contents(
                    (std::istreambuf_iterator<char>(restored)),
                    std::istreambuf_iterator<char>());
                require(contents.find("tampered") == std::string::npos,
                    "locked install should restore a modified package from the cache");
            }
            (void)zbuild::installPackages(packageConsumer, "1.2.0");

            const auto sharedV1 = temporary / "shared-v1";
            const auto sharedV2 = temporary / "shared-v2";
            const auto parent = temporary / "parent";
            writePackage(sharedV1, "shared-lib", "1.0.0");
            writePackage(sharedV2, "shared-lib", "2.0.0");
            writePackage(parent, "parent-lib", "1.0.0",
                "{\"shared-lib\":\"^2.0.0\"}");
            zbuild::cachePackage(sharedV1);
            zbuild::cachePackage(sharedV2);
            zbuild::cachePackage(parent);
            {
                std::ofstream manifest(packageConsumer / "zpm.json", std::ios::trunc);
                manifest << "{\"name\":\"package-consumer\",\"version\":\"0.1.0\","
                    "\"main\":\"src/main.zh\",\"compiler\":\">=1.2.0\","
                    "\"dependencies\":{\"parent-lib\":\"^1.0.0\","
                    "\"shared-lib\":\"^1.0.0\"}}";
            }
            requireThrows(
                [&] { (void)zbuild::installPackages(packageConsumer, "1.2.0"); },
                "dependency version conflict");

            const auto compatibleConsumer = temporary / "compatible-consumer";
            const auto resolverParent = temporary / "a-resolver-parent";
            const auto resolverOld = temporary / "resolver-common-1.0.0";
            const auto resolverCompatible = temporary / "resolver-common-1.4.0";
            const auto resolverNewest = temporary / "resolver-common-1.9.0";
            zbuild::initializePackage(compatibleConsumer, "compatible-consumer");
            writePackage(resolverParent, "a-resolver-parent", "1.0.0",
                "{\"z-resolver-common\":\">=1.0.0 <2.0.0\"}");
            writePackage(resolverOld, "z-resolver-common", "1.0.0");
            writePackage(resolverCompatible, "z-resolver-common", "1.4.0");
            writePackage(resolverNewest, "z-resolver-common", "1.9.0");
            zbuild::cachePackage(resolverParent);
            zbuild::cachePackage(resolverOld);
            zbuild::cachePackage(resolverCompatible);
            zbuild::cachePackage(resolverNewest);
            {
                std::ofstream manifest(
                    compatibleConsumer / "zpm.json", std::ios::trunc);
                manifest << "{\"name\":\"compatible-consumer\",\"version\":\"0.1.0\","
                    "\"main\":\"src/main.zh\",\"compiler\":\">=1.2.0\","
                    "\"dependencies\":{\"a-resolver-parent\":\"^1.0.0\","
                    "\"z-resolver-common\":\"<1.5.0\"}}";
            }
            const auto compatibleResolution =
                zbuild::installPackages(compatibleConsumer, "1.2.0");
            require(compatibleResolution.size() == 2 &&
                    compatibleResolution.back().name == "z-resolver-common" &&
                    compatibleResolution.back().version == "1.4.0",
                "resolver must backtrack to the highest cached version that satisfies all constraints");

            const auto cycleConsumer = temporary / "cycle-consumer";
            const auto cycleHigh = temporary / "cycle-high";
            const auto cycleLow = temporary / "cycle-low";
            const auto cycleBridge = temporary / "cycle-bridge";
            zbuild::initializePackage(cycleConsumer, "cycle-consumer");
            writePackage(cycleHigh, "cycle-lib", "1.1.0",
                "{\"cycle-bridge\":\"^1.0.0\"}");
            writePackage(cycleLow, "cycle-lib", "1.0.0");
            writePackage(cycleBridge, "cycle-bridge", "1.0.0",
                "{\"cycle-lib\":\"^1.0.0\"}");
            zbuild::cachePackage(cycleHigh);
            zbuild::cachePackage(cycleLow);
            zbuild::cachePackage(cycleBridge);
            {
                std::ofstream manifest(cycleConsumer / "zpm.json", std::ios::trunc);
                manifest << "{\"name\":\"cycle-consumer\",\"version\":\"0.1.0\","
                    "\"main\":\"src/main.zh\",\"compiler\":\">=1.2.0\","
                    "\"dependencies\":{\"cycle-lib\":\"^1.0.0\"}}";
            }
            const auto cycleResolution = zbuild::installPackages(cycleConsumer, "1.2.0");
            require(cycleResolution.size() == 1 &&
                    cycleResolution.front().name == "cycle-lib" &&
                    cycleResolution.front().version == "1.0.0",
                "resolver must backtrack when the newest candidate introduces a dependency cycle");

            {
                std::ofstream manifest(packageConsumer / "zpm.json", std::ios::trunc);
                manifest << "{\"name\":\"package-consumer\",\"version\":\"0.1.0\","
                    "\"main\":\"src/main.zh\",\"compiler\":\">=1.3.0\","
                    "\"dependencies\":{}}";
            }
            requireThrows(
                [&] { (void)zbuild::installPackages(packageConsumer, "1.2.0"); },
                "requires Z+ compiler");

            const auto traversalArchive = temporary / "traversal.zpm";
            const auto traversalDestination = temporary / "traversal-extracted";
            {
                std::ofstream archive(traversalArchive, std::ios::binary);
                archive.write("ZPM1", 4);
                writeU32(archive, 1);
                writeU32(archive, 7);
                writeU64(archive, 0);
                archive.write("../../x", 7);
            }
            requireThrows(
                [&] {
                    zbuild::installPackageArchive(
                        traversalArchive, traversalDestination);
                },
                "traversal paths");
            require(!std::filesystem::exists(traversalDestination),
                "archive traversal rejection must not create its destination");
            const auto unmanagedCache = packageCache / "unmanaged-user-data";
            std::filesystem::create_directories(unmanagedCache);
            {
                std::ofstream keep(unmanagedCache / "keep.txt");
                keep << "preserve";
            }
            {
                std::ofstream keep(packageCache / "user-file.txt");
                keep << "preserve";
            }
            require(zbuild::cleanPackageCache() >= 5,
                "cache clean should remove the managed versioned packages");
            require(std::filesystem::exists(unmanagedCache / "keep.txt") &&
                    std::filesystem::exists(packageCache / "user-file.txt"),
                "cache clean must preserve unmarked user data");

            bool refusedOverwrite = false;
            try {
                templates.initialize("Empty", temporary / "UnitProject", "ChangedName");
            } catch (const std::runtime_error&) {
                refusedOverwrite = true;
            }
            require(refusedOverwrite, "init must refuse to overwrite project files");

            const auto badProject = temporary / "bad";
            std::filesystem::create_directories(badProject / "src");
            {
                std::ofstream source(badProject / "src" / "main.zp");
                source << "start:\nstop\n";
            }
            {
                std::ofstream config(badProject / "zproject.toml");
                config << "[project]\nname = \"Bad\"\nentry = \"src/main.zp\"\n"
                          "unknown_setting = true\n";
            }
            bool rejectedUnknown = false;
            try {
                (void)zbuild::loadProject(badProject / "zproject.toml");
            } catch (const std::runtime_error&) {
                rejectedUnknown = true;
            }
            require(rejectedUnknown, "configuration parser must reject unknown keys");
            {
                std::ofstream config(badProject / "zproject.toml", std::ios::trunc);
                config << "[project]\nname = \"Bad\"\nentry = \"src/main.zp\"\n"
                          "\n[profile.performance]\noptimization = 0\n";
            }
            bool rejectedProfile = false;
            try {
                (void)zbuild::loadProject(badProject / "zproject.toml");
            } catch (const std::runtime_error&) {
                rejectedProfile = true;
            }
            require(rejectedProfile, "unsupported custom profiles must be rejected");
            {
                std::ofstream config(badProject / "zproject.toml", std::ios::trunc);
                config << "[project]\nname = \"Bad\"\nentry = \"src/main.zp\"\n"
                          "\n[build]\noptimization = 4294967296\n";
            }
            requireThrows(
                [&] { (void)zbuild::loadProject(badProject / "zproject.toml"); },
                "optimization is unsupported");
            requireThrows(
                [&] { (void)zbuild::loadProject(temporary / "missing" / "zproject.toml"); },
                "cannot open project configuration");

            bool rejectedProjectName = false;
            try {
                templates.initialize("Empty", temporary / "bad-name", "../bad");
            } catch (const std::runtime_error&) {
                rejectedProjectName = true;
            }
            require(rejectedProjectName, "project names must not allow path traversal");
            std::filesystem::remove_all(temporary);
        } catch (...) {
            std::filesystem::remove_all(temporary);
            throw;
        }

        std::cout << "ZBuild unit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ZBuild unit test failure: " << error.what() << '\n';
        return 1;
    }
}
