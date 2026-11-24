#include "Build.hpp"

#include "BuildGraph.hpp"
#include "Cache.hpp"
#include "Dependencies.hpp"
#include "Filesystem.hpp"
#include "Hash.hpp"
#include "Packages.hpp"
#include "Process.hpp"
#include "RegistryPackages.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef ZBUILD_VERSION
#define ZBUILD_VERSION "unknown"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zbuild {
namespace {

std::mutex graphicsRuntimeStagingMutex;

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path.parent_path();
#else
    return {};
#endif
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string compilerVersionNumber(const std::string& output) {
    constexpr std::string_view prefix = "Z+ Compiler ";
    const std::string version = trim(output);
    if (!version.starts_with(prefix)) {
        throw std::runtime_error("unrecognized Z+ compiler version: " + version);
    }
    return version.substr(prefix.size());
}

void preparePackageDependencies(
    const std::filesystem::path& projectRoot,
    const std::string& compilerVersion
) {
    const bool locked = std::filesystem::is_regular_file(
        projectRoot / "zpm-lock.json");
    prepareRegistryPackages(projectRoot, compilerVersion, locked);
    const auto packages = installPackages(projectRoot, compilerVersion, locked);
    if (!packages.empty()) {
        std::cout << "[packages] resolved " << packages.size()
                  << " package(s) from zpm.json\n";
    }
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read '" + pathToUtf8(path) + "'");
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) throw std::runtime_error("failed reading '" + pathToUtf8(path) + "'");
    return content.str();
}

std::string safeComponent(std::string value) {
    for (char& ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (!std::isalnum(byte) && ch != '_' && ch != '-') ch = '_';
    }
    return value.empty() ? "target" : value;
}

std::string targetComponent(const std::string& value) {
    return safeComponent(value);
}

std::string jsonEscape(const std::string& value) {
    std::string result;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex[ch >> 4];
                    result += hex[ch & 0x0f];
                } else result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

void writeBuildMetadata(
    const std::filesystem::path& destination,
    const ProjectConfig& project,
    const TargetConfig& target,
    const std::filesystem::path& compiler,
    const std::string& compilerVersion,
    const std::string& profile,
    const std::string& backend,
    const std::string& signature,
    const std::filesystem::path& output
) {
    std::ostringstream metadata;
    metadata << "{\n"
             << "  \"format_version\": 1,\n"
             << "  \"build_tool_version\": \"" << ZBUILD_VERSION << "\",\n"
             << "  \"project\": \"" << jsonEscape(project.name) << "\",\n"
             << "  \"target\": \"" << jsonEscape(target.name) << "\",\n"
             << "  \"type\": \"" << jsonEscape(target.type) << "\",\n"
             << "  \"profile\": \"" << jsonEscape(profile) << "\",\n"
             << "  \"backend\": \"" << jsonEscape(backend) << "\",\n"
             << "  \"compiler\": \"" << jsonEscape(pathToUtf8(compiler)) << "\",\n"
             << "  \"compiler_version\": \"" << jsonEscape(compilerVersion) << "\",\n"
             << "  \"signature\": \"" << signature << "\",\n"
             << "  \"output\": ";
    if (output.empty()) metadata << "null\n";
    else metadata << '"' << jsonEscape(pathToUtf8(output)) << "\"\n";
    metadata << "}\n";
    writeFileIfChangedAtomically(destination, metadata.str());
}

std::regex globRegex(const std::string& glob) {
    std::string result = "^";
    for (std::size_t i = 0; i < glob.size(); ++i) {
        const char ch = glob[i];
        if (ch == '*') {
            if (i + 1 < glob.size() && glob[i + 1] == '*') {
                ++i;
                if (i + 1 < glob.size() && glob[i + 1] == '/') {
                    ++i;
                    result += "(?:.*/)?";
                } else result += ".*";
            } else result += "[^/]*";
        } else if (ch == '?') {
            result += "[^/]";
        } else {
            if (std::string(".^$|()[]{}+\\").find(ch) != std::string::npos) {
                result.push_back('\\');
            }
            result.push_back(ch == '\\' ? '/' : ch);
        }
    }
    result += "$";
    return std::regex(result);
}

bool hasGlob(const std::string& path) {
    return path.find_first_of("*?") != std::string::npos;
}

std::vector<std::filesystem::path> expandSources(
    const ProjectConfig& project,
    const TargetConfig& target
) {
    std::vector<std::filesystem::path> result;
    std::set<std::filesystem::path> unique;
    static const std::vector<std::string> noPatterns;
    const std::vector<std::string>& patterns = !target.sources.empty()
        ? target.sources
        : project.implicitTarget ? project.sourceDirectories : noPatterns;
    const auto excluded = [&](const std::filesystem::path& candidate) {
        const auto relative = candidate.lexically_relative(project.root);
        const std::string name = relative.generic_string();
        if (relative.empty() || name.starts_with("../")) return false;
        for (const std::string& configured : project.sourceExcludes) {
            const std::string pattern =
                pathFromUtf8(configured).generic_string();
            if (hasGlob(pattern)) {
                if (std::regex_match(name, globRegex(pattern))) return true;
            } else if (name == pattern || name.starts_with(pattern + "/")) {
                return true;
            }
        }
        return false;
    };
    for (const std::string& configured : patterns) {
        const std::filesystem::path path = project.root / pathFromUtf8(configured);
        std::error_code error;
        if (hasGlob(configured)) {
            const auto firstGlob = configured.find_first_of("*?");
            const auto prefix = pathFromUtf8(configured.substr(0, firstGlob));
            const auto base = prefix.has_filename() ? prefix.parent_path() : prefix;
            const auto scanRoot = project.root / base;
            if (!std::filesystem::exists(scanRoot, error) || error) {
                throw std::runtime_error(
                    "source glob base does not exist: '" + pathToUtf8(scanRoot) + "'");
            }
            const std::regex matcher = globRegex(
                pathFromUtf8(configured).generic_string());
            for (std::filesystem::recursive_directory_iterator it(scanRoot, error), end;
                 it != end; it.increment(error)) {
                if (error) throw std::runtime_error("failed scanning source glob '" + configured + "'");
                if (excluded(it->path())) {
                    if (it->is_directory()) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file()) continue;
                const auto relative = std::filesystem::relative(it->path(), project.root, error);
                if (!error && std::regex_match(relative.generic_string(), matcher) &&
                    (it->path().extension() == ".zp" || it->path().extension() == ".zh")) {
                    unique.insert(std::filesystem::weakly_canonical(it->path()));
                }
            }
        } else if (std::filesystem::is_directory(path, error) && !error) {
            for (std::filesystem::recursive_directory_iterator it(path, error), end;
                 it != end; it.increment(error)) {
                if (error) throw std::runtime_error(
                    "failed scanning source directory '" + pathToUtf8(path) + "'");
                if (excluded(it->path())) {
                    if (it->is_directory()) it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() &&
                    (it->path().extension() == ".zp" || it->path().extension() == ".zh")) {
                    unique.insert(std::filesystem::weakly_canonical(it->path()));
                }
            }
        } else if (std::filesystem::is_regular_file(path, error) && !error) {
            if (excluded(path)) continue;
            if (path.extension() == ".zp" || path.extension() == ".zh") {
                unique.insert(std::filesystem::weakly_canonical(path));
            } else {
                throw std::runtime_error(
                    "unsupported source extension: '" + pathToUtf8(path) + "'");
            }
        } else {
            throw std::runtime_error(
                "configured source does not exist: '" + pathToUtf8(path) + "'");
        }
    }
    if (!target.entry.empty()) {
        const auto entry = std::filesystem::weakly_canonical(
            project.root / pathFromUtf8(target.entry));
        if (!std::filesystem::is_regular_file(entry)) {
            throw std::runtime_error("target '" + target.name +
                "' entry does not exist: '" + pathToUtf8(entry) + "'");
        }
        unique.insert(entry);
    }
    if (unique.empty() && target.type != "custom") {
        throw std::runtime_error("target '" + target.name + "' has no Z+ sources");
    }
    return {unique.begin(), unique.end()};
}

std::filesystem::path checkedRelativePath(
    const ProjectConfig& project,
    const std::string& configured,
    const char* description
) {
    const std::filesystem::path relative = pathFromUtf8(configured);
    if (relative.empty() || relative.is_absolute()) {
        throw std::runtime_error(std::string(description) + " must be a non-empty project-relative path");
    }
    const auto resolved = (project.root / relative).lexically_normal();
    const auto rel = resolved.lexically_relative(project.root);
    if (rel.empty() || *rel.begin() == "..") {
        throw std::runtime_error(std::string(description) + " must stay inside the project directory");
    }
    auto current = project.root;
    for (const auto& component : rel) {
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw std::runtime_error(
                "cannot inspect " + std::string(description) + " path '" +
                pathToUtf8(current) + "': " + error.message());
        }
        if (!error && std::filesystem::is_symlink(status)) {
            throw std::runtime_error(
                std::string(description) + " cannot traverse symlink '" +
                pathToUtf8(current) + "'");
        }
    }
    return resolved;
}

std::string effectiveMode(const ProjectConfig& project, const BuildOptions& options) {
    const std::string mode = options.mode.empty() ? project.mode : options.mode;
    if (mode != "debug" && mode != "release") {
        throw std::runtime_error("build mode must be debug or release");
    }
    return mode;
}

std::string effectiveBackend(const ProjectConfig& project, const BuildOptions& options) {
    const std::string backend = options.backend.empty() ? project.backend : options.backend;
    if (backend != "native" && backend != "interpreter") {
        throw std::runtime_error("backend must be native or interpreter");
    }
    return backend;
}

const TargetConfig& selectedTarget(
    const ProjectConfig& project,
    const BuildOptions& options,
    bool testsOnly = false
) {
    if (!options.target.empty()) {
        const auto found = std::find_if(
            project.targets.begin(), project.targets.end(),
            [&](const TargetConfig& target) { return target.name == options.target; });
        if (found == project.targets.end()) {
            throw std::runtime_error("unknown target '" + options.target + "'");
        }
        return *found;
    }
    if (!testsOnly && !project.defaultTarget.empty()) {
        const auto found = std::find_if(
            project.targets.begin(), project.targets.end(),
            [&](const TargetConfig& target) {
                return target.name == project.defaultTarget &&
                    (target.type == "executable" || target.type == "test");
            });
        if (found != project.targets.end()) return *found;
    }
    const auto found = std::find_if(
        project.targets.begin(), project.targets.end(),
        [&](const TargetConfig& target) {
            return testsOnly ? target.type == "test" : target.type == "executable";
        });
    if (found == project.targets.end()) {
        throw std::runtime_error(testsOnly
            ? "project has no configured test targets"
            : "project has no executable target");
    }
    return *found;
}

std::string defaultBuildTargetName(const ProjectConfig& project) {
    if (!project.defaultTarget.empty()) return project.defaultTarget;
    const auto executable = std::find_if(
        project.targets.begin(), project.targets.end(),
        [](const TargetConfig& target) { return target.type == "executable"; });
    return executable == project.targets.end()
        ? project.targets.front().name
        : executable->name;
}

std::filesystem::path graphicsRuntime(const std::filesystem::path& compiler) {
#ifdef _WIN32
    auto runtime = compiler.parent_path() / "zgraphics_runtime.dll";
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    runtime = compiler.parent_path().parent_path() / "Release" /
        "zgraphics_runtime.dll";
    if (std::filesystem::is_regular_file(runtime)) return runtime;
#else
    (void)compiler;
#endif
    return {};
}

std::filesystem::path sqliteRuntime(const std::filesystem::path& compiler) {
#ifdef _WIN32
    auto runtime = compiler.parent_path() / "sqlite3.dll";
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    runtime = compiler.parent_path().parent_path() / "Release" / "sqlite3.dll";
    if (std::filesystem::is_regular_file(runtime)) return runtime;
#else
    (void)compiler;
#endif
    return {};
}

std::filesystem::path audioRuntime(const std::filesystem::path& compiler) {
#if defined(_WIN32)
    const std::string name = "zaudio_runtime.dll";
#elif defined(__linux__)
    const std::string name = "libzaudio_runtime.so";
#else
    (void)compiler;
    return {};
#endif
#if defined(_WIN32) || defined(__linux__)
    auto runtime = compiler.parent_path() / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    runtime = compiler.parent_path().parent_path() / "Release" / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    return {};
#endif
}

std::filesystem::path physicsRuntime(const std::filesystem::path& compiler) {
#if defined(_WIN32)
    const std::string name = "zphysics_runtime.dll";
#elif defined(__linux__)
    const std::string name = "libzphysics_runtime.so";
#else
    (void)compiler;
    return {};
#endif
#if defined(_WIN32) || defined(__linux__)
    auto runtime = compiler.parent_path() / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    runtime = compiler.parent_path().parent_path() / "Release" / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    return {};
#endif
}

std::filesystem::path terminalInputRuntime(const std::filesystem::path& compiler) {
#if defined(_WIN32)
    const std::string name = "zplus_input_runtime.dll";
#elif defined(__linux__)
    const std::string name = "libzplus_input_runtime.so";
#else
    (void)compiler;
    return {};
#endif
#if defined(_WIN32) || defined(__linux__)
    auto runtime = compiler.parent_path() / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    runtime = compiler.parent_path().parent_path() / "Release" / name;
    if (std::filesystem::is_regular_file(runtime)) return runtime;
    return {};
#endif
}

bool needsAudioRuntime(
    const std::set<std::filesystem::path>& sourceFiles
) {
    for (const auto& source : sourceFiles) {
        if (readText(source).find("__zaudio_") != std::string::npos) return true;
    }
    return false;
}

bool needsPhysicsRuntime(const std::set<std::filesystem::path>& sourceFiles) {
    for (const auto& source : sourceFiles) {
        if (readText(source).find("__zphysics_") != std::string::npos) return true;
    }
    return false;
}

bool needsTerminalInputRuntime(
    const std::set<std::filesystem::path>& sourceFiles
) {
    static const std::regex inputCall(
        R"(\b(read|read_number|read_integer|read_boolean)\s*\()");
    for (const auto& source : sourceFiles) {
        if (std::regex_search(readText(source), inputCall)) return true;
    }
    return false;
}

std::map<std::string, std::filesystem::path> collectAssets(const ProjectConfig& project) {
    std::map<std::string, std::filesystem::path> expected;
    for (const std::string& directory : project.assetDirectories) {
        const auto sourceRoot = checkedRelativePath(project, directory, "asset directory");
        if (!std::filesystem::is_directory(sourceRoot)) {
            throw std::runtime_error(
                "asset directory does not exist: '" + pathToUtf8(sourceRoot) + "'");
        }
        const auto outputRoot =
            (project.root / project.outputDirectory).lexically_normal();
        const auto outputWithinAssets = outputRoot.lexically_relative(sourceRoot);
        if (!outputWithinAssets.empty() && *outputWithinAssets.begin() != "..") {
            throw std::runtime_error(
                "build.output_dir cannot be inside an asset source directory");
        }
        for (std::filesystem::recursive_directory_iterator it(sourceRoot), end;
             it != end; ++it) {
            const auto status = it->symlink_status();
            if (std::filesystem::is_symlink(status)) {
                throw std::runtime_error(
                    "asset symlinks are not supported: '" +
                    pathToUtf8(it->path()) + "'");
            }
            if (!std::filesystem::is_regular_file(status)) continue;
            const auto relative = project.preserveAssetStructure
                ? std::filesystem::relative(it->path(), sourceRoot)
                : it->path().filename();
            const std::string key = relative.generic_string();
            if (!expected.emplace(key, it->path()).second) {
                throw std::runtime_error("asset output path collision: '" + key + "'");
            }
        }
    }
    return expected;
}

void syncAssets(const ProjectConfig& project, const std::filesystem::path& destination) {
    if (project.assetDirectories.empty()) return;
    const auto expected = collectAssets(project);
    std::filesystem::create_directories(destination);
    for (const auto& [relative, source] : expected) {
        const auto output = checkedRelativePath(project, (destination / relative)
            .lexically_relative(project.root).generic_string(), "asset output path");
        std::filesystem::create_directories(output.parent_path());
        const auto outputStatus = std::filesystem::symlink_status(output);
        if (std::filesystem::is_symlink(outputStatus)) {
            std::filesystem::remove(output);
        }
        if (!std::filesystem::is_regular_file(output) ||
            sha256File(source) != sha256File(output)) {
            std::filesystem::copy_file(
                source, output, std::filesystem::copy_options::overwrite_existing);
        }
    }

    std::vector<std::filesystem::path> stale;
    for (std::filesystem::recursive_directory_iterator it(destination), end;
         it != end; ++it) {
        if (!it->is_regular_file() &&
            !std::filesystem::is_symlink(it->symlink_status())) continue;
        const auto relative = std::filesystem::relative(it->path(), destination);
        if (!expected.contains(relative.generic_string())) stale.push_back(it->path());
    }
    for (const auto& file : stale) std::filesystem::remove(file);
    std::vector<std::filesystem::path> directories;
    for (std::filesystem::recursive_directory_iterator it(destination), end;
         it != end; ++it) {
        if (it->is_directory()) directories.push_back(it->path());
    }
    std::sort(directories.rbegin(), directories.rend());
    for (const auto& directory : directories) {
        std::error_code error;
        std::filesystem::remove(directory, error);
        if (error) {
            throw std::runtime_error(
                "cannot remove stale asset directory '" + pathToUtf8(directory) +
                "': " + error.message());
        }
    }
}

} // namespace

BuildEngine::BuildEngine(ProjectConfig project, std::filesystem::path compiler)
    : project_(std::move(project)), compiler_(std::move(compiler)) {}

BuildResult BuildEngine::build(const BuildOptions& options) {
    const std::string mode = effectiveMode(project_, options);
    const std::string backend = effectiveBackend(project_, options);
    if (!project_.windows && !project_.linux) {
        throw std::runtime_error("project disables every supported target platform");
    }
#ifndef _WIN32
    if (project_.windowsGui) {
        throw std::runtime_error("a Windows GUI subsystem executable requires a Windows build");
    }
#endif
    if (project_.windowsGui && backend != "native") {
        throw std::runtime_error("a Windows GUI subsystem executable requires the native backend");
    }
#ifdef _WIN32
    if (!project_.windows) throw std::runtime_error("this project disables Windows builds");
#else
    if (!project_.linux) throw std::runtime_error("this project disables Linux builds");
#endif

    const auto outputRoot = checkedRelativePath(project_, project_.outputDirectory, "build.output_dir");
    const auto profileRoot = outputRoot / mode;
    const auto profile = project_.profiles.find(mode);
    if (profile == project_.profiles.end()) {
        throw std::runtime_error("build profile '" + mode + "' is not configured");
    }

    std::unordered_map<std::string, const TargetConfig*> targets;
    for (const TargetConfig& target : project_.targets) targets.emplace(target.name, &target);
    const std::vector<std::string> order =
        targetBuildOrder(project_.targets, options.target);

    if (options.dryRun && options.cleanFirst) {
        throw std::runtime_error("--dry-run cannot be combined with --clean-first");
    }
    if (compiler_.empty() || !std::filesystem::is_regular_file(compiler_)) {
        throw std::runtime_error("Z+ compiler does not exist: '" + pathToUtf8(compiler_) + "'");
    }
    std::unique_ptr<ProjectBuildLock> buildLock;
    if (!options.dryRun) {
        std::filesystem::create_directories(outputRoot);
        buildLock = std::make_unique<ProjectBuildLock>(outputRoot / ".zpbuild.lock");
    }
    if (options.cleanFirst) {
        const auto profileOutput = checkedRelativePath(
            project_, pathToUtf8((outputRoot / mode).lexically_relative(project_.root)),
            "profile output directory");
        const auto legacyCache = checkedRelativePath(
            project_, pathToUtf8((outputRoot / "cache").lexically_relative(project_.root)),
            "legacy cache directory");
        std::filesystem::remove_all(profileOutput);
        std::filesystem::remove_all(legacyCache);
    }
    const unsigned hardwareJobs = std::min(
        32u, std::max(1u, std::thread::hardware_concurrency()));
    const unsigned jobs = options.jobs != 0 ? options.jobs
        : !options.parallel && project_.jobs != 0 ? project_.jobs
        : hardwareJobs;
    const ProcessResult version = runProcess(
        {pathToUtf8(compiler_), "version"}, project_.root);
    if (version.exitCode != 0) {
        throw std::runtime_error("cannot query compiler version:\n" + version.output);
    }
    const std::string compilerVersion = trim(version.output);
    preparePackageDependencies(project_.root,
        compilerVersionNumber(compilerVersion));

    BuildResult result;
    std::unordered_set<std::string> completed;
    std::unordered_map<std::string, std::filesystem::path> outputByTarget;
    std::unordered_map<std::string, std::string> signatureByTarget;
    std::mutex resultMutex;
    std::mutex consoleMutex;
    CacheStatistics statisticsDelta;
    std::atomic<std::uint64_t> temporarySequence{0};
    std::size_t maximumReadyTargets = 0;
    try {
    while (completed.size() < order.size()) {
        std::vector<const TargetConfig*> ready;
        for (const std::string& name : order) {
            if (completed.contains(name)) continue;
            const TargetConfig* target = targets.at(name);
            if (std::all_of(target->dependencies.begin(), target->dependencies.end(),
                    [&](const std::string& dependency) { return completed.contains(dependency); })) {
                ready.push_back(target);
            }
        }
        if (ready.empty()) throw std::runtime_error("internal build scheduler deadlock");
        const auto customTarget = std::find_if(
            ready.begin(), ready.end(), [](const TargetConfig* target) {
                return target->type == "custom";
            });
        if (customTarget != ready.end()) {
            const TargetConfig* serializedCustom = *customTarget;
            ready.assign(1, serializedCustom);
        }
        maximumReadyTargets = std::max(maximumReadyTargets, ready.size());
        for (std::size_t begin = 0; begin < ready.size(); begin += jobs) {
            const std::size_t end = std::min<std::size_t>(ready.size(), begin + jobs);
            std::vector<std::future<std::pair<std::string, std::filesystem::path>>> tasks;
            for (std::size_t i = begin; i < end; ++i) {
                const TargetConfig* target = ready[i];
                tasks.push_back(std::async(std::launch::async, [&, target] {
                    const std::string targetBackend =
                        target->backend.empty() ? backend : target->backend;
                    if (project_.windowsGui && targetBackend != "native") {
                        throw std::runtime_error(
                            "project build.windows_gui cannot be used with interpreter targets");
                    }
                    const std::string targetMode =
                        target->mode.empty() ? mode : target->mode;
                    if (targetBackend != "native" && targetBackend != "interpreter") {
                        throw std::runtime_error("target '" + target->name + "' has invalid backend");
                    }
                    if (targetMode != "debug" && targetMode != "release") {
                        throw std::runtime_error("target '" + target->name + "' has invalid mode");
                    }
                    const auto targetProfile = project_.profiles.find(targetMode);
                    if (targetProfile == project_.profiles.end()) {
                        throw std::runtime_error(
                            "target '" + target->name + "' selects unconfigured profile '" +
                            targetMode + "'");
                    }
                    const auto invalidateStaleOutput = [&] {
                        if (options.dryRun || target->type == "custom") return;
#ifdef _WIN32
                        const std::string outputExtension = ".exe";
#else
                        const std::string outputExtension = ".out";
#endif
                        const std::string configuredDirectory =
                            target->outputDirectory.empty()
                                ? "artifacts"
                                : target->outputDirectory;
                        const auto profileDirectory = outputRoot / targetMode;
                        const auto outputDirectory = checkedRelativePath(
                            project_, pathToUtf8((profileDirectory /
                                pathFromUtf8(configuredDirectory))
                                .lexically_relative(project_.root)),
                            "target output directory");
                        const auto output = outputDirectory /
                            (targetComponent(target->name) +
                                (targetBackend == "interpreter"
                                    ? ".checked"
                                    : outputExtension));
                        const auto cacheRecord = profileDirectory / "metadata" / "cache" /
                            (targetComponent(target->name) + "-" + targetMode + "-" +
                                targetBackend + ".sha256");
                        std::error_code error;
                        std::filesystem::remove(output, error);
                        if (error) {
                            throw std::runtime_error(
                                "cannot remove stale target output '" +
                                pathToUtf8(output) + "': " + error.message());
                        }
                        std::filesystem::remove(cacheRecord, error);
                        if (error) {
                            throw std::runtime_error(
                                "cannot invalidate target cache record '" +
                                pathToUtf8(cacheRecord) + "': " + error.message());
                        }
                    };
                    std::vector<std::filesystem::path> sources;
                    std::set<std::filesystem::path> tracked;
                    try {
                        sources = expandSources(project_, *target);
                        tracked.insert(sources.begin(), sources.end());
                        if (!target->entry.empty()) {
                            const auto modules = compilerDependencies(
                                compiler_,
                                project_.root / pathFromUtf8(target->entry),
                                project_.root);
                            tracked.insert(modules.begin(), modules.end());
                        }
                    } catch (...) {
                        invalidateStaleOutput();
                        throw;
                    }
                    std::ostringstream signatureData;
                    signatureData << "zbuild:" << ZBUILD_VERSION << '\n'
                        << project_.name << '\n' << project_.version << '\n'
                        << target->name << '\n' << target->type << '\n'
                        << target->entry << '\n' << target->outputDirectory << '\n'
                        << targetBackend << '\n' << targetMode << '\n'
                        << targetProfile->second.optimization << '\n'
                        << targetProfile->second.debugSymbols << '\n'
                        << (project_.windowsGui && targetBackend == "native"
                                ? "windows-gui\n"
                                : "console\n")
                        << compilerVersion << '\n'
                        << pathToUtf8(compiler_) << '\n'
                        << sha256File(compiler_) << '\n';
#ifdef _WIN32
                    signatureData << "platform:windows\n";
#elif defined(__linux__)
                    signatureData << "platform:linux\n";
#else
                    signatureData << "platform:other\n";
#endif
                    for (const std::string& argument : target->command) {
                        signatureData << "command:" << argument << '\n';
                    }
                    std::vector<std::string> targetDependencies = target->dependencies;
                    std::sort(targetDependencies.begin(), targetDependencies.end());
                    targetDependencies.erase(
                        std::unique(targetDependencies.begin(), targetDependencies.end()),
                        targetDependencies.end());
#ifdef _WIN32
                    if (targetBackend == "native") {
                        const auto runtime = graphicsRuntime(compiler_);
                        signatureData << "graphics-runtime:";
                        if (runtime.empty()) signatureData << "missing\n";
                        else signatureData << sha256File(runtime) << '\n';
                    }
#endif
                    for (const std::string& dependency : targetDependencies) {
                        std::filesystem::path dependencyOutput;
                        std::string dependencySignature;
                        {
                            std::lock_guard lock(resultMutex);
                            const auto signature = signatureByTarget.find(dependency);
                            if (signature == signatureByTarget.end()) {
                                throw std::runtime_error(
                                    "dependency result is unavailable for target '" +
                                    target->name + "'");
                            }
                            dependencySignature = signature->second;
                            const auto output = outputByTarget.find(dependency);
                            if (output != outputByTarget.end()) {
                                dependencyOutput = output->second;
                            }
                        }
                        signatureData << "target-dependency:" << dependency << ':'
                                      << dependencySignature;
                        if (!dependencyOutput.empty() &&
                            std::filesystem::is_regular_file(dependencyOutput)) {
                            signatureData << ':' << sha256File(dependencyOutput);
                        }
                        signatureData << '\n';
                    }
                    const bool usesAudio =
                        target->type != "custom" && targetBackend == "native" &&
                        needsAudioRuntime(tracked);
                    const bool usesPhysics =
                        target->type != "custom" && targetBackend == "native" &&
                        needsPhysicsRuntime(tracked);
                    const bool usesTerminalInput =
                        target->type != "custom" && targetBackend == "native" &&
                        needsTerminalInputRuntime(tracked);
                    if (usesAudio) {
                        const auto runtime = audioRuntime(compiler_);
                        signatureData << "audio-runtime:";
                        if (runtime.empty()) signatureData << "missing\n";
                        else signatureData << sha256File(runtime) << '\n';
                    }
                    if (usesPhysics) {
                        const auto runtime = physicsRuntime(compiler_);
                        signatureData << "physics-runtime:";
                        if (runtime.empty()) signatureData << "missing\n";
                        else signatureData << sha256File(runtime) << '\n';
                    }
                    if (usesTerminalInput) {
                        const auto runtime = terminalInputRuntime(compiler_);
                        signatureData << "terminal-input-runtime:";
                        if (runtime.empty()) signatureData << "missing\n";
                        else signatureData << sha256File(runtime) << '\n';
                    }
                    for (const auto& source : tracked) {
                        signatureData << pathToUtf8(source) << ':'
                                      << sha256File(source) << '\n';
                    }
                    const std::string signature = sha256(signatureData.str());
                    const auto targetMetadataRoot = outputRoot / targetMode / "metadata";
                    const auto targetCacheDirectory = targetMetadataRoot / "cache";
                    if (!options.dryRun && !options.noCache) {
                        std::filesystem::create_directories(targetCacheDirectory);
                    }
                    const auto cacheRecord = targetCacheDirectory /
                        (targetComponent(target->name) + "-" + targetMode + "-" +
                         targetBackend + ".sha256");
#ifdef _WIN32
                    const std::string extension = ".exe";
#else
                    const std::string extension = ".out";
#endif
                    const std::string targetOutputDirectory = target->outputDirectory.empty()
                        ? "artifacts"
                        : target->outputDirectory;
                    const auto targetProfileRoot = outputRoot / targetMode;
                    const auto targetDirectory = checkedRelativePath(
                        project_, pathToUtf8((targetProfileRoot /
                            pathFromUtf8(targetOutputDirectory))
                            .lexically_relative(project_.root)),
                        "target output directory");
                    if (!options.dryRun) std::filesystem::create_directories(targetDirectory);
                    if (target->type == "custom") {
                        if (options.dryRun) {
                            std::lock_guard lock(resultMutex);
                            result.plan.push_back({
                                target->name, false,
                                "custom targets have no declared outputs and always run"
                            });
                            signatureByTarget[target->name] = signature;
                            return std::pair{target->name, std::filesystem::path{}};
                        }
                        {
                            std::lock_guard lock(resultMutex);
                            result.plan.push_back({
                                target->name, false,
                                "custom targets have no declared outputs and always run"
                            });
                        }
                        std::vector<std::string> command = target->command;
                        const std::unordered_map<std::string, std::string> substitutions{
                            {"{project_root}", pathToUtf8(project_.root)},
                            {"{output_dir}", pathToUtf8(targetDirectory)},
                            {"{profile}", targetMode},
                            {"{target}", target->name}
                        };
                        for (std::string& argument : command) {
                            for (const auto& [placeholder, value] : substitutions) {
                                std::size_t position = 0;
                                while ((position = argument.find(placeholder, position)) !=
                                       std::string::npos) {
                                    argument.replace(position, placeholder.size(), value);
                                    position += value.size();
                                }
                            }
                        }
                        {
                            std::lock_guard lock(resultMutex);
                            ++statisticsDelta.rebuilds;
                        }
                        const ProcessResult custom = runProcess(command, project_.root);
                        if (options.verbose || custom.exitCode != 0) {
                            std::lock_guard lock(consoleMutex);
                            std::cout << custom.output;
                        }
                        if (custom.exitCode != 0) {
                            throw std::runtime_error(
                                "custom target '" + target->name +
                                "' failed with exit code " +
                                std::to_string(custom.exitCode));
                        }
                        writeBuildMetadata(
                            targetMetadataRoot / (targetComponent(target->name) + ".json"),
                            project_, *target, compiler_, compilerVersion, targetMode,
                            targetBackend, signature, {});
                        {
                            std::lock_guard lock(resultMutex);
                            result.built.push_back(target->name);
                            signatureByTarget[target->name] = signature;
                        }
                        return std::pair{target->name, std::filesystem::path{}};
                    }
                    const auto output = targetDirectory / (targetComponent(target->name) +
                        (targetBackend == "interpreter" ? ".checked" : extension));
                    const auto temporaryOutput = pathToUtf8(output) + ".zbuild-tmp-" +
                        std::to_string(temporarySequence.fetch_add(1));
                    struct TemporaryFileCleanup {
                        std::filesystem::path path;
                        ~TemporaryFileCleanup() {
                            if (!path.empty()) {
                                std::error_code ignored;
                                std::filesystem::remove(path, ignored);
                            }
                        }
                        void release() { path.clear(); }
                    };
                    TemporaryFileCleanup temporaryOutputCleanup{
                        pathFromUtf8(temporaryOutput)};
                    bool cacheHit = false;
                    const bool cacheLookup = !options.noCache && !options.force;
                    std::string rebuildReason;
                    bool runtimeMatches = true;
#ifdef _WIN32
                    if (targetBackend == "native") {
                        std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                        const auto runtime = graphicsRuntime(compiler_);
                        const auto stagedRuntime = targetDirectory / "zgraphics_runtime.dll";
                        runtimeMatches = runtime.empty()
                            ? !std::filesystem::exists(stagedRuntime)
                            : std::filesystem::is_regular_file(stagedRuntime) &&
                                sha256File(runtime) == sha256File(stagedRuntime);
                        const auto sqlite = sqliteRuntime(compiler_);
                        const auto stagedSqlite = targetDirectory / "sqlite3.dll";
                        runtimeMatches = runtimeMatches && (sqlite.empty()
                            ? !std::filesystem::exists(stagedSqlite)
                            : std::filesystem::is_regular_file(stagedSqlite) &&
                                sha256File(sqlite) == sha256File(stagedSqlite));
                    }
#endif
                    if (targetBackend == "native") {
                        std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                        const auto runtime = usesAudio ? audioRuntime(compiler_)
                                                       : std::filesystem::path{};
                        const auto stagedRuntime = targetDirectory /
#ifdef _WIN32
                            "zaudio_runtime.dll";
#else
                            "libzaudio_runtime.so";
#endif
                        runtimeMatches = runtimeMatches && (usesAudio
                            ? !runtime.empty() &&
                                std::filesystem::is_regular_file(stagedRuntime) &&
                                sha256File(runtime) == sha256File(stagedRuntime)
                            : !std::filesystem::exists(stagedRuntime));
                    }
                    if (targetBackend == "native") {
                        std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                        const auto runtime = usesPhysics ? physicsRuntime(compiler_)
                                                        : std::filesystem::path{};
                        const auto stagedRuntime = targetDirectory /
#ifdef _WIN32
                            "zphysics_runtime.dll";
#else
                            "libzphysics_runtime.so";
#endif
                        runtimeMatches = runtimeMatches && (usesPhysics
                            ? !runtime.empty() &&
                                std::filesystem::is_regular_file(stagedRuntime) &&
                                sha256File(runtime) == sha256File(stagedRuntime)
                            : !std::filesystem::exists(stagedRuntime));
                    }
                    if (targetBackend == "native") {
                        std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                        const auto runtime = usesTerminalInput
                            ? terminalInputRuntime(compiler_)
                            : std::filesystem::path{};
#ifdef _WIN32
                        const auto stagedRuntime =
                            targetDirectory / "zplus_input_runtime.dll";
#else
                        const auto stagedRuntime =
                            targetDirectory / "libzplus_input_runtime.so";
#endif
                        runtimeMatches = runtimeMatches && (usesTerminalInput
                            ? !runtime.empty() &&
                                std::filesystem::is_regular_file(stagedRuntime) &&
                                sha256File(runtime) == sha256File(stagedRuntime)
                            : !std::filesystem::exists(stagedRuntime));
                    }
                    const bool outputExists = std::filesystem::is_regular_file(output);
                    if (options.noCache) {
                        rebuildReason = "cache bypassed (--no-cache)";
                    } else if (options.force) {
                        rebuildReason = "forced rebuild";
                    } else if (!outputExists) {
                        rebuildReason = "output is missing";
                    } else if (!std::filesystem::is_regular_file(cacheRecord)) {
                        rebuildReason = "cache record is missing";
                    } else {
                        try {
                            const CacheRecord cached = loadCacheRecord(cacheRecord);
                            if (pathToUtf8(cached.output) != pathToUtf8(output)) {
                                rebuildReason = "cached output path changed";
                            } else if (cached.signature != signature) {
                                rebuildReason = "source or build configuration changed";
                            } else if (cached.outputHash != sha256File(output)) {
                                rebuildReason = "output integrity check failed";
                            } else if (!runtimeMatches) {
                                rebuildReason = "staged runtime is missing or changed";
                            } else {
                                cacheHit = true;
                            }
                        } catch (const std::exception& error) {
                            rebuildReason = "cache record is invalid";
                            std::lock_guard lock(consoleMutex);
                            std::cout << "[cache] invalid record for " << target->name
                                      << ": " << error.what() << '\n';
                        }
                    }
                    if (!runtimeMatches && rebuildReason.empty()) {
                        rebuildReason = "staged runtime is missing or changed";
                    }
                    if (rebuildReason == "source or build configuration changed") {
                        std::lock_guard lock(resultMutex);
                        for (const std::string& dependency : targetDependencies) {
                            const auto plannedDependency = std::find_if(
                                result.plan.begin(), result.plan.end(),
                                [&](const BuildPlanEntry& item) {
                                    return item.target == dependency && !item.upToDate;
                                });
                            if (plannedDependency != result.plan.end()) {
                                rebuildReason = "dependency target '" + dependency +
                                    "' requires rebuilding";
                                break;
                            }
                        }
                    }
                    if (options.dryRun) {
                        std::lock_guard lock(resultMutex);
                        result.plan.push_back({
                            target->name, cacheHit,
                            cacheHit ? "all tracked inputs and outputs match the cache"
                                     : rebuildReason
                        });
                        if (cacheHit) ++result.cacheHits;
                        else if (cacheLookup) ++result.cacheMisses;
                        signatureByTarget[target->name] = signature;
                        outputByTarget[target->name] = output;
                        result.outputs[target->name] = output;
                        return std::pair{target->name, output};
                    }
                    {
                        std::lock_guard lock(resultMutex);
                        result.plan.push_back({
                            target->name, cacheHit,
                            cacheHit ? "all tracked inputs and outputs match the cache"
                                     : rebuildReason
                        });
                    }
                    if (cacheHit) {
                        std::lock_guard lock(resultMutex);
                        result.reused.push_back(target->name);
                        ++statisticsDelta.hits;
                        ++result.cacheHits;
                    } else {
                        {
                            std::lock_guard lock(resultMutex);
                            if (cacheLookup) {
                                ++statisticsDelta.misses;
                                ++result.cacheMisses;
                            }
                            ++statisticsDelta.rebuilds;
                        }
                        if (std::filesystem::exists(temporaryOutput)) {
                            std::filesystem::remove(temporaryOutput);
                        }
                        {
                            std::lock_guard lock(consoleMutex);
                            std::cout << "[build] " << target->name << " building ("
                                      << rebuildReason << ")\n";
                        }
                        std::vector<std::string> command{
                            pathToUtf8(compiler_),
                            targetBackend == "interpreter" ? "check" : "build",
                            pathToUtf8(project_.root / pathFromUtf8(target->entry))
                        };
                        if (targetBackend == "native") {
                            command.push_back("-o");
                            command.push_back(temporaryOutput);
                            if (project_.windowsGui) command.push_back("--windows-gui");
                        }
                        const ProcessResult built = runProcess(command, project_.root);
                        if (options.verbose || built.exitCode != 0) {
                            std::lock_guard lock(consoleMutex);
                            std::cout << built.output;
                        }
                        if (built.exitCode != 0) {
                            std::string cleanupFailure;
                            for (const auto& stale : {
                                     pathFromUtf8(temporaryOutput), output, cacheRecord}) {
                                std::error_code error;
                                std::filesystem::remove(stale, error);
                                if (error) {
                                    if (!cleanupFailure.empty()) cleanupFailure += "; ";
                                    cleanupFailure += "cannot remove '" + pathToUtf8(stale) +
                                        "': " + error.message();
                                }
                            }
                            std::string message =
                                "compiler failed for target '" + target->name +
                                "' with exit code " + std::to_string(built.exitCode);
                            if (!cleanupFailure.empty()) {
                                message += "; cleanup failed: " + cleanupFailure;
                            }
                            throw std::runtime_error(
                                std::move(message));
                        }
                        if (targetBackend == "native") {
                            if (!std::filesystem::is_regular_file(
                                    pathFromUtf8(temporaryOutput)) ||
                                std::filesystem::file_size(pathFromUtf8(temporaryOutput)) == 0) {
                                throw std::runtime_error(
                                    "compiler reported success but produced no valid output for target '" +
                                    target->name + "'");
                            }
#ifdef _WIN32
                            const auto runtime = graphicsRuntime(compiler_);
                            const auto stagedRuntime = targetDirectory / "zgraphics_runtime.dll";
                            const auto sqlite = sqliteRuntime(compiler_);
                            if (!runtime.empty()) {
                                std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                                if (!std::filesystem::is_regular_file(stagedRuntime) ||
                                    sha256File(runtime) != sha256File(stagedRuntime)) {
                                    std::filesystem::copy_file(
                                        runtime, stagedRuntime,
                                        std::filesystem::copy_options::overwrite_existing);
                                }
                                if (!sqlite.empty()) {
                                    const auto stagedSqlite = targetDirectory / "sqlite3.dll";
                                    if (!std::filesystem::is_regular_file(stagedSqlite) ||
                                        sha256File(sqlite) != sha256File(stagedSqlite)) {
                                        std::filesystem::copy_file(
                                            sqlite, stagedSqlite,
                                            std::filesystem::copy_options::overwrite_existing);
                                    }
                                } else {
                                    std::error_code ignored;
                                    std::filesystem::remove(targetDirectory / "sqlite3.dll", ignored);
                                }
                            } else if (std::filesystem::exists(stagedRuntime)) {
                                std::filesystem::remove(stagedRuntime);
                            }
#endif
                            {
                                std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                                const auto audioRuntimeFile = usesAudio
                                    ? audioRuntime(compiler_)
                                    : std::filesystem::path{};
#ifdef _WIN32
                                const auto stagedAudioRuntime =
                                    targetDirectory / "zaudio_runtime.dll";
#else
                                const auto stagedAudioRuntime =
                                    targetDirectory / "libzaudio_runtime.so";
#endif
                                if (usesAudio && audioRuntimeFile.empty()) {
                                    throw std::runtime_error(
                                        "zaudio runtime was not found beside compiler '" +
                                        pathToUtf8(compiler_) + "'");
                                }
                                if (usesAudio &&
                                    (!std::filesystem::is_regular_file(stagedAudioRuntime) ||
                                     sha256File(audioRuntimeFile) != sha256File(stagedAudioRuntime))) {
                                    std::filesystem::copy_file(audioRuntimeFile, stagedAudioRuntime,
                                        std::filesystem::copy_options::overwrite_existing);
                                } else if (!usesAudio &&
                                    std::filesystem::exists(stagedAudioRuntime)) {
                                    std::filesystem::remove(stagedAudioRuntime);
                                }
                            }
                            {
                                std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                                const auto physicsRuntimeFile = usesPhysics
                                    ? physicsRuntime(compiler_)
                                    : std::filesystem::path{};
#ifdef _WIN32
                                const auto stagedPhysicsRuntime =
                                    targetDirectory / "zphysics_runtime.dll";
#else
                                const auto stagedPhysicsRuntime =
                                    targetDirectory / "libzphysics_runtime.so";
#endif
                                if (usesPhysics && physicsRuntimeFile.empty()) {
                                    throw std::runtime_error(
                                        "zphysics runtime was not found beside compiler '" +
                                        pathToUtf8(compiler_) + "'");
                                }
                                if (usesPhysics &&
                                    (!std::filesystem::is_regular_file(stagedPhysicsRuntime) ||
                                     sha256File(physicsRuntimeFile) !=
                                        sha256File(stagedPhysicsRuntime))) {
                                    std::filesystem::copy_file(
                                        physicsRuntimeFile, stagedPhysicsRuntime,
                                        std::filesystem::copy_options::overwrite_existing);
                                } else if (!usesPhysics &&
                                    std::filesystem::exists(stagedPhysicsRuntime)) {
                                    std::filesystem::remove(stagedPhysicsRuntime);
                                }
                            }
                            {
                                std::lock_guard runtimeLock(graphicsRuntimeStagingMutex);
                                const auto inputRuntimeFile = usesTerminalInput
                                    ? terminalInputRuntime(compiler_)
                                    : std::filesystem::path{};
#ifdef _WIN32
                                const auto stagedInputRuntime =
                                    targetDirectory / "zplus_input_runtime.dll";
#else
                                const auto stagedInputRuntime =
                                    targetDirectory / "libzplus_input_runtime.so";
#endif
                                if (usesTerminalInput && inputRuntimeFile.empty()) {
                                    throw std::runtime_error(
                                        "terminal input runtime was not found beside compiler '" +
                                        pathToUtf8(compiler_) + "'");
                                }
                                if (usesTerminalInput &&
                                    (!std::filesystem::is_regular_file(stagedInputRuntime) ||
                                     sha256File(inputRuntimeFile) !=
                                        sha256File(stagedInputRuntime))) {
                                    std::filesystem::copy_file(
                                        inputRuntimeFile, stagedInputRuntime,
                                        std::filesystem::copy_options::overwrite_existing);
                                } else if (!usesTerminalInput &&
                                    std::filesystem::exists(stagedInputRuntime)) {
                                    std::filesystem::remove(stagedInputRuntime);
                                }
                            }
                            replaceFileAtomically(
                                pathFromUtf8(temporaryOutput), output);
                            temporaryOutputCleanup.release();
                        } else {
                            writeFileAtomically(output, signature + '\n');
                        }
                        if (!std::filesystem::is_regular_file(output) ||
                            std::filesystem::file_size(output) == 0) {
                            throw std::runtime_error(
                                "build output validation failed for target '" + target->name + "'");
                        }
                        if (!options.noCache) {
                            saveCacheRecord(cacheRecord, signature, output);
                        }
                        std::lock_guard lock(resultMutex);
                        result.built.push_back(target->name);
                    }
                    writeBuildMetadata(
                        targetMetadataRoot / (targetComponent(target->name) + ".json"),
                        project_, *target, compiler_, compilerVersion, targetMode,
                        targetBackend, signature, output);
                    {
                        std::lock_guard lock(resultMutex);
                        outputByTarget[target->name] = output;
                        signatureByTarget[target->name] = signature;
                        result.outputs[target->name] = output;
                    }
                    return std::pair{target->name, output};
                }));
            }
            std::vector<std::string> batch;
            for (auto& task : tasks) {
                const auto [name, output] = task.get();
                batch.push_back(name);
                const std::string selectedName = options.target.empty()
                    ? defaultBuildTargetName(project_)
                    : options.target;
                if (name == selectedName) {
                    result.selectedOutput = output;
                }
            }
            completed.insert(batch.begin(), batch.end());
        }
    }

    } catch (...) {
        if (!options.dryRun && !options.noCache) {
            updateCacheStatistics(profileRoot / "metadata" / "cache", statisticsDelta);
        }
        throw;
    }

    if (result.selectedOutput.empty() && !outputByTarget.empty()) {
        const std::string selected = options.target.empty()
            ? defaultBuildTargetName(project_)
            : options.target;
        const auto output = outputByTarget.find(selected);
        if (output != outputByTarget.end()) result.selectedOutput = output->second;
    }
    const auto orderIndex = [&](const std::string& name) {
        return std::find(order.begin(), order.end(), name) - order.begin();
    };
    if (options.dryRun) {
        std::sort(result.plan.begin(), result.plan.end(), [&](const auto& left, const auto& right) {
            return orderIndex(left.target) < orderIndex(right.target);
        });
        std::cout << "Build plan: " << project_.name << '\n'
                  << "Profile: " << mode << "\nBackend: " << backend << '\n';
        for (const auto& item : result.plan) {
            std::cout << (item.upToDate ? "[UP-TO-DATE] " : "[REBUILD]    ")
                      << item.target;
            const auto target = targets.find(item.target);
            if (target != targets.end() && !target->second->dependencies.empty()) {
                std::cout << " [depends: ";
                for (std::size_t i = 0; i < target->second->dependencies.size(); ++i) {
                    if (i != 0) std::cout << ", ";
                    std::cout << target->second->dependencies[i];
                }
                std::cout << ']';
            }
            std::cout << '\n';
            if (!item.upToDate) std::cout << "  Reason: " << item.reason << '\n';
        }
        std::cout << "Plan: " << (result.plan.size() - result.cacheHits)
                  << " targets require rebuilding; " << result.cacheHits
                  << " cache hits; " << result.cacheMisses << " cache misses\n"
                  << "Parallelizable ready targets: " << maximumReadyTargets
                  << " (job limit " << jobs << ")\n";
        return result;
    }

    syncAssets(project_, profileRoot / "assets");
    if (!options.noCache) {
        updateCacheStatistics(profileRoot / "metadata" / "cache", statisticsDelta);
    }
    std::sort(result.built.begin(), result.built.end(), [&](const auto& left, const auto& right) {
        return orderIndex(left) < orderIndex(right);
    });
    std::sort(result.reused.begin(), result.reused.end(), [&](const auto& left, const auto& right) {
        return orderIndex(left) < orderIndex(right);
    });
    std::sort(result.plan.begin(), result.plan.end(), [&](const auto& left, const auto& right) {
        return orderIndex(left.target) < orderIndex(right.target);
    });
    for (std::size_t i = 0; i < result.plan.size(); ++i) {
        const auto& item = result.plan[i];
        std::cout << '[' << (i + 1) << '/' << result.plan.size() << "] "
                  << item.target << ' '
                  << (item.upToDate ? "CACHE HIT" : "BUILT") << '\n';
    }
    for (const std::string& target : result.built) {
        std::cout << "[build] " << target << " complete\n";
    }
    for (const std::string& target : result.reused) {
        std::cout << "[build] " << target << " up to date (content cache)\n";
    }
    std::cout << "[build] summary: built=" << result.built.size()
              << ", cache_hits=" << result.cacheHits
              << ", cache_misses=" << result.cacheMisses
              << ", failed=0\n";
    return result;
}

int BuildEngine::check(const BuildOptions& options) {
    const std::vector<std::string> order =
        targetBuildOrder(project_.targets, options.target);
    if (!compiler_.empty() && !std::filesystem::is_regular_file(compiler_)) {
        throw std::runtime_error(
            "configured Z+ compiler does not exist: '" + pathToUtf8(compiler_) + "'");
    }
    const ProcessResult version = runProcess(
        {pathToUtf8(compiler_), "version"}, project_.root);
    if (version.exitCode != 0) {
        throw std::runtime_error("cannot query compiler version:\n" + version.output);
    }
    preparePackageDependencies(project_.root,
        compilerVersionNumber(version.output));

    for (const std::string& name : order) {
        const auto target = std::find_if(
            project_.targets.begin(), project_.targets.end(),
            [&](const TargetConfig& item) { return item.name == name; });
        if (target == project_.targets.end()) {
            throw std::runtime_error("internal error: graph returned unknown target '" + name + "'");
        }
        if (target->type == "custom") {
            if (name == options.target) {
                throw std::runtime_error(
                    "custom target '" + name + "' has no Z+ entry point to check");
            }
            continue;
        }
        const ProcessResult checked = runProcess(
            {pathToUtf8(compiler_), "check",
                pathToUtf8(project_.root / pathFromUtf8(target->entry))},
            project_.root);
        if (checked.exitCode != 0) {
            std::cout << checked.output;
            std::cerr << "[check] target '" << target->name
                      << "' failed with exit code " << checked.exitCode << '\n';
            return checked.exitCode;
        }
        if (options.verbose) std::cout << checked.output;
        std::cout << "[check] " << target->name << " passed\n";
    }
    return 0;
}

int BuildEngine::run(
    const BuildOptions& options,
    const std::vector<std::string>& arguments
) {
    const TargetConfig& target = selectedTarget(project_, options);
    if (target.type != "executable") {
        throw std::runtime_error(
            "cannot run target '" + target.name + "' of type '" + target.type + "'");
    }
    const BuildResult result = build(options);
    const std::string backend = target.backend.empty()
        ? effectiveBackend(project_, options)
        : target.backend;
    std::vector<std::string> command;
    if (backend == "interpreter") {
        command = {pathToUtf8(compiler_), "run",
            pathToUtf8(project_.root / pathFromUtf8(target.entry))};
        if (!arguments.empty()) {
            command.push_back("--");
            command.insert(command.end(), arguments.begin(), arguments.end());
        }
    } else {
        command.push_back(pathToUtf8(result.selectedOutput));
        command.insert(command.end(), arguments.begin(), arguments.end());
    }
    const std::string mode = effectiveMode(project_, options);
    const auto workDirectory = project_.root;
    const ProcessResult process = runProcess(command, workDirectory);
    std::cout << process.output;
    return process.exitCode;
}

int BuildEngine::test(const BuildOptions& options) {
    std::vector<std::string> testTargets = project_.testTargets;
    if (testTargets.empty()) for (const TargetConfig& target : project_.targets) {
        if (target.type == "test") testTargets.push_back(target.name);
    }
    if (!options.target.empty()) {
        const auto found = std::find(testTargets.begin(), testTargets.end(), options.target);
        if (found == testTargets.end()) {
            throw std::runtime_error("target '" + options.target + "' is not a configured test target");
        }
        testTargets = {*found};
    }
    if (testTargets.empty()) throw std::runtime_error("project has no configured test targets");
    std::size_t passed = 0;
    std::size_t failed = 0;
    int firstFailure = 0;
    for (const std::string& name : testTargets) {
        const auto target = std::find_if(project_.targets.begin(), project_.targets.end(),
            [&](const TargetConfig& item) { return item.name == name; });
        if (target == project_.targets.end() || target->type != "test") {
            throw std::runtime_error("configured test target '" + name + "' is not type = \"test\"");
        }
        BuildOptions buildOptions = options;
        buildOptions.target = name;
        const BuildResult built = build(buildOptions);
        const std::string backend = target->backend.empty()
            ? effectiveBackend(project_, options)
            : target->backend;
        std::vector<std::string> command;
        if (backend == "interpreter") {
            command = {pathToUtf8(compiler_), "run",
                pathToUtf8(project_.root / pathFromUtf8(target->entry))};
            if (!project_.testArguments.empty()) command.push_back("--");
        } else {
            const auto outputIt = built.outputs.find(name);
            if (outputIt == built.outputs.end()) {
                throw std::runtime_error("test target output was not generated: '" + name + "'");
            }
            command.push_back(pathToUtf8(outputIt->second));
        }
        command.insert(command.end(), project_.testArguments.begin(), project_.testArguments.end());
        const ProcessResult result = runProcess(command, project_.root);
        if (options.verbose || result.exitCode != 0) std::cout << result.output;
        if (result.exitCode != 0) {
            std::cerr << "[test] " << name << " failed (" << result.exitCode << ")\n";
            ++failed;
            if (firstFailure == 0) firstFailure = result.exitCode;
            continue;
        }
        ++passed;
        std::cout << "[test] " << name << " passed\n";
    }
    std::cout << "[test] " << passed << " passed, " << failed << " failed\n";
    return firstFailure;
}

int BuildEngine::cacheCommand(const std::string& action) {
    if (action != "info" && action != "clean" && action != "verify") {
        throw std::runtime_error(
            "cache command must be info, clean, or verify");
    }
    const auto outputRoot =
        checkedRelativePath(project_, project_.outputDirectory, "build.output_dir");
    if (!std::filesystem::exists(outputRoot)) {
        std::cout << "[cache] entries=0, valid=0, invalid=0, size=0 bytes, "
                     "hits=0, misses=0, rebuilds=0\n";
        return 0;
    }

    auto buildLock = std::make_unique<ProjectBuildLock>(outputRoot / ".zpbuild.lock");
    if (action == "clean") {
        for (const std::string& profile : {"debug", "release"}) {
            const auto directory = checkedRelativePath(
                project_,
                pathToUtf8((outputRoot / profile / "metadata" / "cache")
                    .lexically_relative(project_.root)),
                "cache directory");
            std::filesystem::remove_all(directory);
        }
        std::cout << "[cache] removed ZPBuild cache data\n";
        return 0;
    }

    std::uint64_t entries = 0;
    std::uint64_t valid = 0;
    std::uint64_t bytes = 0;
    CacheStatistics totals;
    for (const std::string& profile : {"debug", "release"}) {
        const auto directory = checkedRelativePath(
            project_,
            pathToUtf8((outputRoot / profile / "metadata" / "cache")
                .lexically_relative(project_.root)),
            "cache directory");
        if (!std::filesystem::is_directory(directory)) continue;
        const CacheStatistics statistics =
            loadCacheStatistics(directory / "stats.txt");
        totals.hits += statistics.hits;
        totals.misses += statistics.misses;
        totals.rebuilds += statistics.rebuilds;
        for (std::filesystem::directory_iterator it(directory), end; it != end; ++it) {
            if (!it->is_regular_file()) continue;
            bytes += it->file_size();
            if (it->path().extension() != ".sha256") continue;
            ++entries;
            if (isCacheRecordValid(it->path(), project_.root)) ++valid;
        }
    }
    const std::uint64_t invalid = entries - valid;
    std::cout << "[cache] entries=" << entries << ", valid=" << valid
              << ", invalid=" << invalid << ", size=" << bytes
              << " bytes, hits=" << totals.hits << ", misses=" << totals.misses
              << ", rebuilds=" << totals.rebuilds << '\n';
    if (action == "verify") {
        std::cout << (invalid == 0 ? "[cache] verification passed\n"
                                  : "[cache] verification found invalid entries\n");
        return invalid == 0 ? 0 : 1;
    }
    return 0;
}

void BuildEngine::clean(const std::string& requestedMode) {
    const auto outputRoot = checkedRelativePath(project_, project_.outputDirectory, "build.output_dir");
    if (!std::filesystem::exists(outputRoot)) return;
    ProjectBuildLock buildLock(outputRoot / ".zpbuild.lock");
    if (requestedMode.empty()) {
        for (const std::string& profile : {"debug", "release", "cache"}) {
            const auto directory = checkedRelativePath(
                project_,
                pathToUtf8((outputRoot / profile).lexically_relative(project_.root)),
                "build output directory");
            std::filesystem::remove_all(directory);
        }
    } else {
        const std::string mode = requestedMode == "debug" || requestedMode == "release"
            ? requestedMode
            : throw std::runtime_error("clean mode must be debug or release");
        const auto profileOutput = checkedRelativePath(
            project_, pathToUtf8((outputRoot / mode).lexically_relative(project_.root)),
            "profile output directory");
        const auto legacyCache = checkedRelativePath(
            project_, pathToUtf8((outputRoot / "cache").lexically_relative(project_.root)),
            "legacy cache directory");
        std::filesystem::remove_all(profileOutput);
        std::filesystem::remove_all(legacyCache);
    }
    std::cout << "[clean] removed generated ZBuild outputs\n";
}

void BuildEngine::printTargets(bool json) const {
    if (json) {
        std::cout << "{\"project\":\"" << jsonEscape(project_.name)
                  << "\",\"default_target\":\""
                  << jsonEscape(project_.defaultTarget) << "\",\"targets\":[";
        for (std::size_t i = 0; i < project_.targets.size(); ++i) {
            if (i) std::cout << ',';
            const auto& target = project_.targets[i];
            std::cout << "{\"name\":\"" << jsonEscape(target.name)
                      << "\",\"type\":\"" << jsonEscape(target.type)
                      << "\",\"entry\":\"" << jsonEscape(target.entry)
                      << "\",\"dependencies\":[";
            for (std::size_t dependency = 0;
                 dependency < target.dependencies.size(); ++dependency) {
                if (dependency) std::cout << ',';
                std::cout << '"' << jsonEscape(target.dependencies[dependency]) << '"';
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
        return;
    }
    for (const auto& target : project_.targets) {
        std::cout << target.name << " (" << target.type << ")";
        if (!target.entry.empty()) std::cout << " - " << target.entry;
        if (!target.dependencies.empty()) {
            std::cout << " [depends: ";
            for (std::size_t i = 0; i < target.dependencies.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << target.dependencies[i];
            }
            std::cout << ']';
        }
        std::cout << '\n';
    }
}

void BuildEngine::printGraph(bool dot) const {
    const auto order = targetBuildOrder(project_.targets);
    if (dot) std::cout << "digraph zbuild {\n";
    for (const std::string& name : order) {
        const auto target = std::find_if(
            project_.targets.begin(), project_.targets.end(),
            [&](const TargetConfig& item) { return item.name == name; });
        if (dot) {
            std::cout << "  \"" << target->name << "\";\n";
            for (const auto& dependency : target->dependencies) {
                std::cout << "  \"" << target->name << "\" -> \"" << dependency << "\";\n";
            }
        } else {
            std::cout << target->name;
            if (target->dependencies.empty()) {
                std::cout << " (no dependencies)\n";
            } else {
                std::cout << " -> ";
                for (std::size_t i = 0; i < target->dependencies.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << target->dependencies[i];
                }
                std::cout << '\n';
            }
        }
    }
    if (dot) std::cout << "}\n";
}

void BuildEngine::printInfo(bool json) const {
    if (json) {
        std::cout << "{\"name\":\"" << jsonEscape(project_.name)
                  << "\",\"version\":\"" << jsonEscape(project_.version)
                  << "\",\"root\":\"" << jsonEscape(pathToUtf8(project_.root))
                  << "\",\"backend\":\"" << jsonEscape(project_.backend)
                  << "\",\"default_profile\":\"" << jsonEscape(project_.mode)
                  << "\",\"default_target\":\"" << jsonEscape(project_.defaultTarget)
                  << "\",\"compiler\":\"" << jsonEscape(pathToUtf8(compiler_))
                  << "\",\"profiles\":[";
        bool first = true;
        for (const auto& [profile, settings] : project_.profiles) {
            (void)settings;
            if (!first) std::cout << ',';
            first = false;
            std::cout << '"' << jsonEscape(profile) << '"';
        }
        std::cout << "],\"target_count\":" << project_.targets.size() << "}\n";
        return;
    }
    std::cout << project_.name << " " << project_.version << '\n'
              << "Root: " << pathToUtf8(project_.root) << '\n'
              << "Compiler: " << pathToUtf8(compiler_) << '\n'
              << "Backend: " << project_.backend << '\n'
              << "Default profile: " << project_.mode << '\n'
              << "Profiles: ";
    bool first = true;
    for (const auto& [profile, settings] : project_.profiles) {
        (void)settings;
        if (!first) std::cout << ", ";
        first = false;
        std::cout << profile;
    }
    std::cout << "\nDefault target: " << project_.defaultTarget << '\n'
              << "Targets: " << project_.targets.size() << '\n';
}

void BuildEngine::package(const BuildOptions& options) {
    const TargetConfig& target = selectedTarget(project_, options);
    if (target.type != "executable") {
        throw std::runtime_error(
            "cannot package target '" + target.name + "' of type '" + target.type + "'");
    }
    const std::string backend = target.backend.empty()
        ? effectiveBackend(project_, options)
        : target.backend;
    if (backend != "native") throw std::runtime_error("packaging requires the native backend");
    const std::string mode = target.mode.empty()
        ? effectiveMode(project_, options)
        : target.mode;
    const auto result = build(options);
    const auto packageRoot = checkedRelativePath(
        project_, (pathFromUtf8(project_.outputDirectory) / mode / "package")
            .generic_string(), "package directory");
    std::filesystem::create_directories(packageRoot);
    const auto executableDestination = packageRoot / result.selectedOutput.filename();
    const auto copyIfDifferent = [](const std::filesystem::path& source,
                                    const std::filesystem::path& destination) {
        if (!std::filesystem::is_regular_file(source)) {
            throw std::runtime_error(
                "package source file is missing: " + pathToUtf8(source));
        }
        if (std::filesystem::is_regular_file(destination) &&
            sha256File(source) == sha256File(destination)) {
            return;
        }
        std::filesystem::copy_file(
            source, destination,
            std::filesystem::copy_options::overwrite_existing);
    };
    const auto syncOptionalFile = [&](const std::filesystem::path& source,
                                      const std::string& destinationName) {
        const auto destination = packageRoot / destinationName;
        if (!source.empty() && std::filesystem::is_regular_file(source)) {
            copyIfDifferent(source, destination);
        } else if (std::filesystem::exists(destination)) {
            std::filesystem::remove(destination);
        }
    };
    copyIfDifferent(result.selectedOutput, executableDestination);
#ifdef _WIN32
    const auto runtime = result.selectedOutput.parent_path() / "zgraphics_runtime.dll";
    syncOptionalFile(runtime, pathToUtf8(runtime.filename()));
    const auto sqlite = result.selectedOutput.parent_path() / "sqlite3.dll";
    syncOptionalFile(sqlite, pathToUtf8(sqlite.filename()));
#endif
    const auto audioRuntimeFile =
#ifdef _WIN32
        result.selectedOutput.parent_path() / "zaudio_runtime.dll";
#elif defined(__linux__)
        result.selectedOutput.parent_path() / "libzaudio_runtime.so";
#else
        std::filesystem::path{};
#endif
    if (!audioRuntimeFile.empty()) {
        syncOptionalFile(audioRuntimeFile, pathToUtf8(audioRuntimeFile.filename()));
    }
    const auto physicsRuntimeFile =
#ifdef _WIN32
        result.selectedOutput.parent_path() / "zphysics_runtime.dll";
#elif defined(__linux__)
        result.selectedOutput.parent_path() / "libzphysics_runtime.so";
#else
        std::filesystem::path{};
#endif
    if (!physicsRuntimeFile.empty()) {
        syncOptionalFile(physicsRuntimeFile, pathToUtf8(physicsRuntimeFile.filename()));
    }
    const auto inputRuntimeFile =
#ifdef _WIN32
        result.selectedOutput.parent_path() / "zplus_input_runtime.dll";
#elif defined(__linux__)
        result.selectedOutput.parent_path() / "libzplus_input_runtime.so";
#else
        std::filesystem::path{};
#endif
    if (!inputRuntimeFile.empty()) {
        syncOptionalFile(inputRuntimeFile, pathToUtf8(inputRuntimeFile.filename()));
    }
    const auto stagedAssets = checkedRelativePath(
        project_, (pathFromUtf8(project_.outputDirectory) /
            mode / "assets").generic_string(), "staged asset directory");
    if (!project_.assetDirectories.empty()) {
        const auto destinationRoot = packageRoot / "assets";
        if (!std::filesystem::is_directory(stagedAssets)) {
            throw std::runtime_error(
                "staged asset directory is missing: " + pathToUtf8(stagedAssets));
        }
        std::filesystem::create_directories(destinationRoot);
        std::unordered_set<std::string> expectedFiles;
        std::unordered_set<std::string> expectedDirectories;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(stagedAssets)) {
            const auto relative = entry.path().lexically_relative(stagedAssets);
            const std::string key = relative.generic_string();
            const auto destination = destinationRoot / relative;
            if (entry.is_directory()) {
                expectedDirectories.insert(key);
                std::filesystem::create_directories(destination);
            } else if (entry.is_regular_file()) {
                expectedFiles.insert(key);
                copyIfDifferent(entry.path(), destination);
            }
        }

        std::vector<std::filesystem::path> staleDirectories;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(destinationRoot)) {
            const auto relative = entry.path().lexically_relative(destinationRoot);
            const std::string key = relative.generic_string();
            if (entry.is_directory()) {
                if (!expectedDirectories.contains(key)) {
                    staleDirectories.push_back(entry.path());
                }
            } else if (entry.is_regular_file() &&
                       !expectedFiles.contains(key)) {
                std::filesystem::remove(entry.path());
            }
        }
        std::sort(staleDirectories.begin(), staleDirectories.end(),
                  [](const auto& left, const auto& right) {
                      return left.native().size() > right.native().size();
                  });
        for (const auto& directory : staleDirectories) {
            std::filesystem::remove(directory);
        }
    } else {
        const auto destinationRoot = packageRoot / "assets";
        if (std::filesystem::exists(destinationRoot)) {
            std::filesystem::remove_all(destinationRoot);
        }
    }
    std::cout << "[package] " << pathToUtf8(packageRoot) << '\n';
}

} // namespace zbuild
