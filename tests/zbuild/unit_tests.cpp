#include "Config.hpp"
#include "BuildGraph.hpp"
#include "Hash.hpp"
#include "Template.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
        require(std::string(error.what()).find(expected) != std::string::npos,
            "exception did not contain the expected diagnostic");
        return;
    }
    throw std::runtime_error("expected operation to fail");
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
