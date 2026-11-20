#include "Build.hpp"
#include "Config.hpp"
#include "Filesystem.hpp"
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

#ifndef ZBUILD_VERSION
#define ZBUILD_VERSION "1.0.0"
#endif

constexpr const char* version = ZBUILD_VERSION;

std::filesystem::path executablePath(const char* argumentZero) {
#ifdef _WIN32
    (void)argumentZero;
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        throw std::runtime_error("cannot determine the ZpBuild executable path");
    return std::filesystem::path(std::wstring(path.data(), length));
#else
    return std::filesystem::absolute(zbuild::pathFromUtf8(argumentZero));
#endif
}

void printHelp() {
    std::cout
        << "ZPBuild " << version << " - Z+ project build system\n\n"
        << "Usage: zpbuild <command> [target] [options] [-- program-args]\n\n"
        << "Commands:\n"
        << "  init [name]           Initialize this directory or create a Console project\n"
        << "  new NAME              Create a project directory (alias for init NAME)\n"
        << "  templates             List built-in and custom project templates\n"
        << "  template info NAME    Show project template metadata\n"
        << "  configure             Validate the project and write zproject.lock\n"
        << "  build [target]        Build configured targets\n"
        << "  plan [target]         Show rebuild decisions without changing outputs\n"
        << "  run                   Build and run an executable target\n"
        << "  check [target]        Check configured Z+ entry points\n"
        << "  test                  Build and run configured test targets\n"
        << "  clean                 Remove generated outputs\n"
        << "  rebuild               Clean and build\n"
        << "  info                  Show project metadata\n"
        << "  targets               List configured targets\n"
        << "  graph                 Show target dependencies (--dot for Graphviz)\n"
        << "  cache info|clean|verify  Inspect or maintain the project cache\n"
        << "  package               Create a release directory\n\n"
        << "Build options:\n"
        << "  --target NAME         Select a target\n"
        << "  --debug | --release   Select the output profile\n"
        << "  --profile NAME        Select debug or release\n"
        << "  --backend NAME        Select native or interpreter\n"
        << "  --jobs N              Maximum concurrent target builds\n"
        << "  --parallel            Use the automatic bounded worker count\n"
        << "  --no-cache            Bypass cache reads and writes for this invocation\n"
        << "  --dry-run             Print the build plan without compiling\n"
        << "  --verbose             Print compiler and program output\n"
        << "  --clean-first         Clean the selected profile before building\n"
        << "  --json                Emit machine-readable info or targets\n"
        << "  --diagnostics=json    Emit machine-readable ZPBuild errors\n"
        << "  --dot                 Emit the graph in Graphviz DOT format\n"
        << "  -h, --help            Show help\n"
        << "  --version             Show the ZPBuild version\n";
}

std::string escapeJson(const std::string& value) {
    std::string result;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex[character >> 4];
                    result += hex[character & 0x0f];
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    return result;
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
    std::string name = zbuild::pathToUtf8(path.filename());
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
        const std::filesystem::path configured =
            zbuild::pathFromUtf8(project.compiler);
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

int runCli(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    bool diagnosticsJson = false;
    try {
        if (argc < 2) {
            printHelp();
            return 2;
        }
        const std::string command = std::string(argv[1]) == "new"
            ? "init"
            : argv[1];
        if (command == "--version" || command == "version") {
            std::cout << "ZPBuild " << version << '\n';
            return 0;
        }
        if (command == "--help" || command == "-h" || command == "help") {
            printHelp();
            return 0;
        }

        zbuild::TemplateRegistry templates(executablePath(argv[0]));
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
        std::string positionalTarget;
        std::filesystem::path initDestination;
        std::vector<std::string> programArguments;
        std::string cacheAction;
        int firstOption = 2;
        if (command == "cache") {
            if (argc != 3) {
                throw std::runtime_error("usage: zpbuild cache info|clean|verify");
            }
            cacheAction = argv[2];
            firstOption = argc;
        }
        for (int i = firstOption; i < argc; ++i) {
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
                    initDestination = zbuild::pathFromUtf8(argv[i]);
                } else if (argument.starts_with("--path=")) {
                    initDestination = zbuild::pathFromUtf8(argument.substr(7));
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
            } else if (argument == "--profile") {
                if (++i >= argc) throw std::runtime_error("--profile requires a name");
                if (!options.mode.empty()) throw std::runtime_error("choose only one profile option");
                options.mode = argv[i];
            } else if (argument.starts_with("--profile=")) {
                if (!options.mode.empty()) throw std::runtime_error("choose only one profile option");
                options.mode = argument.substr(10);
            } else if (argument == "--backend") {
                if (++i >= argc) throw std::runtime_error("--backend requires a name");
                options.backend = argv[i];
            } else if (argument.starts_with("--backend=")) {
                options.backend = argument.substr(10);
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
            } else if (argument == "--parallel") {
                options.parallel = true;
            } else if (argument == "--no-cache") {
                options.noCache = true;
            } else if (argument == "--dry-run") {
                options.dryRun = true;
            } else if (argument == "--json") {
                json = true;
            } else if (argument == "--diagnostics=json") {
                diagnosticsJson = true;
            } else if (argument == "--dot") {
                dot = true;
            } else if (argument == "--force") {
                options.force = true;
            } else if (!argument.starts_with('-') &&
                       (command == "build" || command == "rebuild" ||
                        command == "plan" ||
                        command == "run" || command == "test" ||
                        command == "check" || command == "package")) {
                if (!positionalTarget.empty()) {
                    throw std::runtime_error(command + " accepts at most one target name");
                }
                positionalTarget = argument;
            } else {
                throw std::runtime_error("unknown option '" + argument + "'");
            }
        }
        if (!positionalTarget.empty()) {
            if (!options.target.empty() && options.target != positionalTarget) {
                throw std::runtime_error(
                    "positional target and --target select different targets");
            }
            options.target = positionalTarget;
        }

        if (command == "init") {
            const auto workingDirectory = std::filesystem::current_path();
            const std::filesystem::path positionalPath =
                zbuild::pathFromUtf8(projectName);
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
            std::cout << "[init] created " << zbuild::pathToUtf8(destination) << '\n';
            return 0;
        }

        const auto manifest = zbuild::discoverProject(std::filesystem::current_path());
        zbuild::ProjectConfig project = zbuild::loadProject(manifest);
        if (command == "configure") {
            zbuild::writeLockfile(project);
            std::cout << "[configure] " << zbuild::pathToUtf8(manifest) << " is valid\n";
            std::cout << "[configure] wrote "
                      << zbuild::pathToUtf8(project.root / "zproject.lock") << '\n';
            return 0;
        }

        if (command == "cache") {
            zbuild::BuildEngine engine(std::move(project), {});
            return engine.cacheCommand(cacheAction);
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
            command != "plan" &&
            command != "run" && command != "test" && command != "check" &&
            command != "package") {
            throw std::runtime_error("unknown command '" + command + "'");
        }
        if (json || dot) {
            throw std::runtime_error("--json and --dot are only valid with info, targets, and graph");
        }
        if (command == "rebuild") {
            options.force = true;
            options.cleanFirst = true;
        }
        if (command == "plan") options.dryRun = true;
        if (options.dryRun && command != "build" && command != "plan") {
            throw std::runtime_error("--dry-run is valid only with build or plan");
        }
        const auto compiler = compilerPath(project, executablePath(argv[0]));
        zbuild::BuildEngine engine(std::move(project), compiler);
        if (command == "build" || command == "rebuild" || command == "plan") {
            engine.build(options);
            return 0;
        }
        if (command == "check") return engine.check(options);
        if (command == "run") return engine.run(options, programArguments);
        if (command == "test") return engine.test(options);
        engine.package(options);
        return 0;
    } catch (const std::exception& error) {
        if (diagnosticsJson) {
            std::cerr << "{\"severity\":\"error\",\"code\":\"ZPB0001\",\"message\":\""
                      << escapeJson(error.what()) << "\"}\n";
        } else {
            std::cerr << "error[ZPB0001]: " << error.what() << '\n';
        }
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wideArguments) {
    std::vector<std::string> converted;
    std::vector<char*> arguments;
    converted.reserve(static_cast<std::size_t>(argc));
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[i], -1, nullptr, 0,
            nullptr, nullptr);
        if (length <= 0) {
            std::cerr << "error[ZPB0001]: command-line argument is not valid Unicode\n";
            return 1;
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[i], -1,
                value.data(), length, nullptr, nullptr) <= 0) {
            std::cerr << "error[ZPB0001]: failed converting a command-line argument\n";
            return 1;
        }
        value.resize(static_cast<std::size_t>(length - 1));
        converted.push_back(std::move(value));
    }
    for (std::string& value : converted) arguments.push_back(value.data());
    return runCli(argc, arguments.data());
}
#else
int main(int argc, char** argv) {
    return runCli(argc, argv);
}
#endif
