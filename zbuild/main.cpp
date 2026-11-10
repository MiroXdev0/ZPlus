#include "Build.hpp"
#include "Config.hpp"
#include "Process.hpp"
#include "Template.hpp"

#include <charconv>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr const char* version = "0.1.0";

void printHelp() {
    std::cout
        << "ZBuild " << version << " - Z+ project build tool\n\n"
        << "Usage: zbuild <command> [options] [-- program-args]\n\n"
        << "Commands:\n"
        << "  init [name]           Create a Console project (use -template NAME)\n"
        << "  templates             List built-in and custom project templates\n"
        << "  template info NAME    Show project template metadata\n"
        << "  configure             Validate the project and write zproject.lock\n"
        << "  build                 Build project targets\n"
        << "  run                   Build and run an executable target\n"
        << "  test                  Build and run configured test targets\n"
        << "  clean                 Remove generated outputs\n"
        << "  rebuild               Clean and build\n"
        << "  info                  Show project metadata\n"
        << "  targets               List configured targets\n"
        << "  graph                 Show target dependencies (--dot for Graphviz)\n"
        << "  package               Create a release directory\n\n"
        << "Build options:\n"
        << "  --target NAME         Select a target\n"
        << "  --debug | --release   Select the output profile\n"
        << "  --jobs N              Maximum concurrent target builds\n"
        << "  --verbose             Print compiler and program output\n"
        << "  --clean-first         Clean the selected profile before building\n"
        << "  --json                Emit machine-readable info or targets\n"
        << "  --dot                 Emit the graph in Graphviz DOT format\n"
        << "  -h, --help            Show help\n"
        << "  --version             Show the ZBuild version\n";
}

void printTemplateHelp() {
    std::cout << "Available templates: Game, WebServer (unavailable), Console, "
                 "Library, Desktop, Empty\n"
              << "Use: zbuild init ProjectName -template TemplateName\n"
              << "Aliases are listed by 'zbuild templates'.\n";
}

unsigned parseJobs(const std::string& value) {
    unsigned jobs = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), jobs);
    if (error != std::errc{} || end != value.data() + value.size() ||
        jobs == 0 || jobs > 1024) {
        throw std::runtime_error("--jobs must be an integer from 1 to 1024");
    }
    return jobs;
}

std::string inferredProjectName(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    for (char& ch : name) {
        const auto value = static_cast<unsigned char>(ch);
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                (value >= '0' && value <= '9') || ch == '_' || ch == '-')) {
            ch = '_';
        }
    }
    if (name.empty()) name = "ZPlusProject";
    if (!(std::isalnum(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        name.insert(name.begin(), '_');
    }
    if (name.size() > 64) name.resize(64);
    return name;
}

std::filesystem::path compilerPath(
    const zbuild::ProjectConfig& project,
    const std::filesystem::path& executable
) {
    if (!project.compiler.empty()) {
        const std::filesystem::path configured(project.compiler);
        const auto projectRelative = configured.is_absolute()
            ? configured
            : project.root / configured;
        if (std::filesystem::is_regular_file(projectRelative)) {
            return std::filesystem::absolute(projectRelative);
        }
        const auto onPath = zbuild::findExecutable(project.compiler);
        if (!onPath.empty()) return onPath;
        throw std::runtime_error(
            "configured compiler was not found: '" + project.compiler + "'");
    }

#ifdef _WIN32
    const std::string compilerName = "zp.exe";
#else
    const std::string compilerName = "zp";
#endif
    std::vector<std::filesystem::path> candidates{
        executable.parent_path() / compilerName,
        project.root / "bin" / compilerName,
        std::filesystem::current_path() / "bin" / compilerName,
        std::filesystem::current_path() / compilerName
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    const auto onPath = zbuild::findExecutable("zp");
    if (!onPath.empty()) return onPath;
    throw std::runtime_error(
        "cannot find the Z+ compiler; set compiler.path in zproject.toml or add zp to PATH");
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        if (argc < 2) {
            printHelp();
            return 2;
        }
        const std::string command = argv[1];
        if (command == "--version" || command == "version") {
            std::cout << "ZBuild " << version << '\n';
            return 0;
        }
        if (command == "--help" || command == "-h" || command == "help") {
            printHelp();
            return 0;
        }

        const auto executablePath = std::filesystem::absolute(argv[0]);
        zbuild::TemplateRegistry templates(executablePath);
        if (command == "templates") {
            if (argc != 2) throw std::runtime_error("'zbuild templates' takes no arguments");
            templates.printList();
            return 0;
        }
        if (command == "template") {
            if (argc == 3 && std::string(argv[2]) == "--help") {
                printTemplateHelp();
                return 0;
            }
            if (argc != 4 || std::string(argv[2]) != "info") {
                throw std::runtime_error("usage: zbuild template info NAME");
            }
            templates.printInfo(argv[3]);
            return 0;
        }

        zbuild::BuildOptions options;
        bool json = false;
        bool dot = false;
        std::string projectTemplate = "Console";
        std::string projectName;
        std::string projectVersion = "0.1.0";
        std::string author;
        std::filesystem::path initDestination;
        std::vector<std::string> programArguments;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (command == "init") {
                if (argument == "--template") {
                    if (++i >= argc) throw std::runtime_error("--template requires a value");
                    projectTemplate = argv[i];
                } else if (argument == "-template") {
                    if (++i >= argc) throw std::runtime_error("-template requires a value");
                    projectTemplate = argv[i];
                } else if (argument.starts_with("--template=")) {
                    projectTemplate = argument.substr(11);
                } else if (argument.starts_with("-template=")) {
                    projectTemplate = argument.substr(10);
                } else if (argument == "--path") {
                    if (++i >= argc) throw std::runtime_error("--path requires a value");
                    initDestination = argv[i];
                } else if (argument.starts_with("--path=")) {
                    initDestination = argument.substr(7);
                } else if (argument == "--version") {
                    if (++i >= argc) throw std::runtime_error("--version requires a value for init");
                    projectVersion = argv[i];
                } else if (argument.starts_with("--version=")) {
                    projectVersion = argument.substr(10);
                } else if (argument == "--author") {
                    if (++i >= argc) throw std::runtime_error("--author requires a value");
                    author = argv[i];
                } else if (argument.starts_with("--author=")) {
                    author = argument.substr(9);
                } else if (argument.starts_with('-')) {
                    throw std::runtime_error("unknown init option '" + argument + "'");
                } else {
                    if (!projectName.empty()) {
                        throw std::runtime_error("init accepts at most one project name");
                    }
                    projectName = argument;
                }
                continue;
            }
            if (argument == "--") {
                if (command != "run") {
                    throw std::runtime_error("program arguments are supported only by 'zbuild run'");
                }
                for (++i; i < argc; ++i) programArguments.emplace_back(argv[i]);
                break;
            }
            if (argument == "--release") {
                if (!options.mode.empty()) throw std::runtime_error("choose only one of --debug and --release");
                options.mode = "release";
            } else if (argument == "--debug") {
                if (!options.mode.empty()) throw std::runtime_error("choose only one of --debug and --release");
                options.mode = "debug";
            } else if (argument == "--target") {
                if (++i >= argc) throw std::runtime_error("--target requires a name");
                options.target = argv[i];
            } else if (argument.starts_with("--target=")) {
                options.target = argument.substr(9);
            } else if (argument == "--jobs") {
                if (++i >= argc) throw std::runtime_error("--jobs requires a number");
                options.jobs = parseJobs(argv[i]);
            } else if (argument.starts_with("--jobs=")) {
                options.jobs = parseJobs(argument.substr(7));
            } else if (argument == "--verbose" || argument == "-v") {
                options.verbose = true;
            } else if (argument == "--clean-first") {
                options.cleanFirst = true;
            } else if (argument == "--json") {
                json = true;
            } else if (argument == "--dot") {
                dot = true;
            } else if (argument == "--force") {
                options.force = true;
            } else {
                throw std::runtime_error("unknown option '" + argument + "'");
            }
        }

        if (command == "init") {
            const auto workingDirectory = std::filesystem::current_path();
            const std::filesystem::path positionalPath(projectName);
            const bool positionalPathIsDestination =
                !projectName.empty() && projectName != "." &&
                (positionalPath.is_absolute() || positionalPath.has_parent_path());
            std::filesystem::path destination;
            if (!initDestination.empty()) {
                destination = initDestination.is_absolute()
                    ? initDestination
                    : workingDirectory / initDestination;
            } else if (projectName.empty() || projectName == ".") {
                destination = workingDirectory;
            } else if (positionalPathIsDestination) {
                destination = positionalPath.is_absolute()
                    ? positionalPath
                    : workingDirectory / positionalPath;
            } else {
                destination = workingDirectory / projectName;
            }
            const std::string name = projectName.empty() || projectName == "." ||
                (positionalPathIsDestination && initDestination.empty())
                ? inferredProjectName(destination)
                : projectName;
            templates.initialize(
                projectTemplate, destination, name, projectVersion, author);
            std::cout << "[init] created " << destination.string() << '\n';
            return 0;
        }

        const auto manifest = zbuild::discoverProject(std::filesystem::current_path());
        zbuild::ProjectConfig project = zbuild::loadProject(manifest);
        if (command == "configure") {
            zbuild::writeLockfile(project);
            std::cout << "[configure] " << manifest.string() << " is valid\n";
            std::cout << "[configure] wrote " << (project.root / "zproject.lock").string() << '\n';
            return 0;
        }

        if (command == "clean") {
            zbuild::BuildEngine engine(std::move(project), {});
            engine.clean(options.mode);
            return 0;
        }
        if (command == "info" || command == "targets" || command == "graph") {
            if ((command == "graph" && json) ||
                (command != "graph" && dot)) {
                throw std::runtime_error(
                    "--json is for info/targets and --dot is for graph");
            }
            zbuild::BuildEngine engine(std::move(project), {});
            if (command == "info") engine.printInfo(json);
            else if (command == "targets") engine.printTargets(json);
            else engine.printGraph(dot);
            return 0;
        }

        if (command != "build" && command != "rebuild" &&
            command != "run" && command != "test" && command != "package") {
            throw std::runtime_error("unknown command '" + command + "'");
        }
        if (json || dot) {
            throw std::runtime_error("--json and --dot are only valid with info, targets, and graph");
        }
        if (command == "rebuild") {
            options.force = true;
            options.cleanFirst = true;
        }
        const auto compiler = compilerPath(project, executablePath);
        zbuild::BuildEngine engine(std::move(project), compiler);
        if (command == "build" || command == "rebuild") {
            engine.build(options);
            return 0;
        }
        if (command == "run") return engine.run(options, programArguments);
        if (command == "test") return engine.test(options);
        engine.package(options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zbuild: error: " << error.what() << '\n';
        return 1;
    }
}
