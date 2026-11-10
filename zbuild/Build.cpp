#include "Build.hpp"

#include "Hash.hpp"
#include "Process.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace zbuild {
namespace {

std::mutex graphicsRuntimeStagingMutex;

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read '" + path.string() + "'");
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) throw std::runtime_error("failed reading '" + path.string() + "'");
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
    std::vector<std::string> patterns = target.sources;
    if (patterns.empty() && project.implicitTarget) {
        patterns = project.sourceDirectories;
    }
    for (const std::string& configured : patterns) {
        const std::filesystem::path path = project.root / configured;
        std::error_code error;
        if (hasGlob(configured)) {
            const auto firstGlob = configured.find_first_of("*?");
            const auto prefix = std::filesystem::path(configured.substr(0, firstGlob));
            const auto base = prefix.has_filename() ? prefix.parent_path() : prefix;
            const auto scanRoot = project.root / base;
            if (!std::filesystem::exists(scanRoot, error) || error) {
                throw std::runtime_error("source glob base does not exist: '" + scanRoot.string() + "'");
            }
            const std::regex matcher = globRegex(
                std::filesystem::path(configured).generic_string());
            for (std::filesystem::recursive_directory_iterator it(scanRoot, error), end;
                 it != end; it.increment(error)) {
                if (error) throw std::runtime_error("failed scanning source glob '" + configured + "'");
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
                if (error) throw std::runtime_error("failed scanning source directory '" + path.string() + "'");
                if (it->is_regular_file() &&
                    (it->path().extension() == ".zp" || it->path().extension() == ".zh")) {
                    unique.insert(std::filesystem::weakly_canonical(it->path()));
                }
            }
        } else if (std::filesystem::is_regular_file(path, error) && !error) {
            if (path.extension() == ".zp" || path.extension() == ".zh") {
                unique.insert(std::filesystem::weakly_canonical(path));
            } else {
                throw std::runtime_error("unsupported source extension: '" + path.string() + "'");
            }
        } else {
            throw std::runtime_error("configured source does not exist: '" + path.string() + "'");
        }
    }
    if (!target.entry.empty()) {
        const auto entry = std::filesystem::weakly_canonical(project.root / target.entry);
        if (!std::filesystem::is_regular_file(entry)) {
            throw std::runtime_error("target '" + target.name + "' entry does not exist: '" + entry.string() + "'");
        }
        unique.insert(entry);
    }
    if (unique.empty()) throw std::runtime_error("target '" + target.name + "' has no Z+ sources");
    return {unique.begin(), unique.end()};
}

std::vector<std::filesystem::path> importedModules(
    const std::filesystem::path& entry
) {
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    const std::regex usePattern(R"(^\s*use\s+(?:\"([^\"]+)\"|([^\s;]+)))");
    std::vector<std::filesystem::path> pending{entry};
    while (!pending.empty()) {
        const auto current = std::filesystem::weakly_canonical(pending.back());
        pending.pop_back();
        if (!seen.insert(current.string()).second) continue;
        const std::string content = readText(current);
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line)) {
            std::smatch match;
            if (!std::regex_search(line, match, usePattern)) continue;
            std::string requested = match[1].matched ? match[1].str() : match[2].str();
            std::replace(requested.begin(), requested.end(), '\\', '/');
            std::filesystem::path importPath(requested);
            if (importPath.is_absolute()) {
                throw std::runtime_error("absolute source imports are not permitted: '" + requested + "'");
            }
            auto resolved = current.parent_path() / importPath;
            const bool bareDottedModule =
                importPath.parent_path().empty() &&
                requested.find('.') != std::string::npos &&
                importPath.extension() != ".zh";
            const bool standardModuleName =
                importPath.parent_path().empty() &&
                (importPath.extension().empty() || bareDottedModule);
            if (!std::filesystem::exists(resolved) && standardModuleName) {
                std::filesystem::path standardRoot;
#ifdef _WIN32
                char* configured = nullptr;
                std::size_t configuredLength = 0;
                if (_dupenv_s(&configured, &configuredLength, "ZPLUS_STDLIB_PATH") == 0 &&
                    configured) {
                    standardRoot = configured;
                    std::free(configured);
                }
#else
                if (const char* configured = std::getenv("ZPLUS_STDLIB_PATH");
                    configured && *configured) {
                    standardRoot = configured;
                }
#endif
#ifdef ZBUILD_STDLIB_DIR
                if (standardRoot.empty()) standardRoot = ZBUILD_STDLIB_DIR;
#endif
                if (!standardRoot.empty()) {
                    if (bareDottedModule) {
                        std::size_t begin = 0;
                        std::size_t separator = requested.find('.');
                        while (separator != std::string::npos) {
                            standardRoot /= requested.substr(begin, separator - begin);
                            begin = separator + 1;
                            separator = requested.find('.', begin);
                        }
                        standardRoot /= requested.substr(begin) + ".zh";
                    } else {
                        standardRoot /= requested;
                        standardRoot /= requested + ".zh";
                    }
                    resolved = std::move(standardRoot);
                }
            }
            std::error_code error;
            if (std::filesystem::is_regular_file(resolved, error) && !error) {
                pending.push_back(std::filesystem::weakly_canonical(resolved));
                result.push_back(std::filesystem::weakly_canonical(resolved));
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::filesystem::path checkedRelativePath(
    const ProjectConfig& project,
    const std::string& configured,
    const char* description
) {
    const std::filesystem::path relative(configured);
    if (relative.empty() || relative.is_absolute()) {
        throw std::runtime_error(std::string(description) + " must be a non-empty project-relative path");
    }
    const auto resolved = (project.root / relative).lexically_normal();
    const auto rel = resolved.lexically_relative(project.root);
    if (rel.empty() || *rel.begin() == "..") {
        throw std::runtime_error(std::string(description) + " must stay inside the project directory");
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

std::map<std::string, std::filesystem::path> collectAssets(const ProjectConfig& project) {
    std::map<std::string, std::filesystem::path> expected;
    for (const std::string& directory : project.assetDirectories) {
        const auto sourceRoot = checkedRelativePath(project, directory, "asset directory");
        if (!std::filesystem::is_directory(sourceRoot)) {
            throw std::runtime_error("asset directory does not exist: '" + sourceRoot.string() + "'");
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
                throw std::runtime_error("asset symlinks are not supported: '" + it->path().string() + "'");
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
                "cannot remove stale asset directory '" + directory.string() +
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
#ifdef _WIN32
    if (!project_.windows) throw std::runtime_error("this project disables Windows builds");
#else
    if (!project_.linux) throw std::runtime_error("this project disables Linux builds");
#endif

    const auto outputRoot = checkedRelativePath(project_, project_.outputDirectory, "build.output_dir");
    const auto profileRoot = outputRoot / mode;
    const auto cacheDirectory = outputRoot / "cache";
    std::filesystem::create_directories(cacheDirectory);

    std::unordered_map<std::string, const TargetConfig*> targets;
    for (const TargetConfig& target : project_.targets) targets.emplace(target.name, &target);
    for (const TargetConfig& target : project_.targets) {
        for (const std::string& dependency : target.dependencies) {
            if (!targets.contains(dependency)) {
                throw std::runtime_error(
                    "target '" + target.name + "' depends on missing target '" + dependency + "'");
            }
        }
    }

    std::vector<std::string> order;
    std::unordered_map<std::string, int> states;
    std::vector<std::string> stack;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (states[name] == 2) return;
        if (states[name] == 1) {
            std::ostringstream chain;
            for (const std::string& item : stack) chain << item << " -> ";
            chain << name;
            throw std::runtime_error("circular target dependency: " + chain.str());
        }
        states[name] = 1;
        stack.push_back(name);
        for (const std::string& dependency : targets.at(name)->dependencies) visit(dependency);
        stack.pop_back();
        states[name] = 2;
        order.push_back(name);
    };

    if (options.target.empty()) {
        for (const auto& target : project_.targets) visit(target.name);
    } else {
        if (!targets.contains(options.target)) {
            throw std::runtime_error("unknown target '" + options.target + "'");
        }
        visit(options.target);
    }

    if (options.cleanFirst) clean(mode);
    const unsigned jobs = options.jobs != 0 ? options.jobs
        : project_.jobs != 0 ? project_.jobs
        : std::max(1u, std::thread::hardware_concurrency());
    std::unordered_map<std::string, std::string> compilerVersion;
    const auto versionOutput = outputRoot / "cache" / "compiler-version.txt";
    const ProcessResult version = runProcess(
        {compiler_.string(), "version"}, project_.root, versionOutput);
    if (version.exitCode != 0) {
        throw std::runtime_error("cannot query compiler version:\n" + version.output);
    }
    compilerVersion.emplace("value", version.output);

    BuildResult result;
    std::unordered_set<std::string> completed;
    std::unordered_map<std::string, std::filesystem::path> outputByTarget;
    std::mutex resultMutex;
    std::mutex consoleMutex;
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
        for (std::size_t begin = 0; begin < ready.size(); begin += jobs) {
            const std::size_t end = std::min<std::size_t>(ready.size(), begin + jobs);
            std::vector<std::future<std::pair<std::string, std::filesystem::path>>> tasks;
            for (std::size_t i = begin; i < end; ++i) {
                const TargetConfig* target = ready[i];
                tasks.push_back(std::async(std::launch::async, [&, target] {
                    const std::string targetBackend =
                        target->backend.empty() ? backend : target->backend;
                    const std::string targetMode =
                        target->mode.empty() ? mode : target->mode;
                    if (targetBackend != "native" && targetBackend != "interpreter") {
                        throw std::runtime_error("target '" + target->name + "' has invalid backend");
                    }
                    if (targetMode != "debug" && targetMode != "release") {
                        throw std::runtime_error("target '" + target->name + "' has invalid mode");
                    }
                    const auto sources = expandSources(project_, *target);
                    std::ostringstream signatureData;
                    signatureData << project_.name << '\n' << project_.version << '\n'
                        << target->name << '\n' << target->type << '\n'
                        << targetBackend << '\n' << targetMode << '\n'
                        << project_.optimization << '\n'
                        << compilerVersion.at("value") << '\n'
                        << sha256File(compiler_) << '\n';
#ifdef _WIN32
                    if (targetBackend == "native") {
                        const auto runtime = graphicsRuntime(compiler_);
                        signatureData << "graphics-runtime:";
                        if (runtime.empty()) signatureData << "missing\n";
                        else signatureData << sha256File(runtime) << '\n';
                    }
#endif
                    for (const std::string& dependency : target->dependencies) {
                        std::filesystem::path dependencyOutput;
                        {
                            std::lock_guard lock(resultMutex);
                            const auto found = outputByTarget.find(dependency);
                            if (found == outputByTarget.end()) {
                                throw std::runtime_error(
                                    "dependency output is unavailable for target '" +
                                    target->name + "'");
                            }
                            dependencyOutput = found->second;
                        }
                        signatureData << "target-dependency:" << dependency << ':'
                                      << sha256File(dependencyOutput) << '\n';
                    }
                    std::set<std::filesystem::path> tracked(sources.begin(), sources.end());
                    if (!target->entry.empty()) {
                        const auto modules = importedModules(project_.root / target->entry);
                        tracked.insert(modules.begin(), modules.end());
                    }
                    const bool usesAudio =
                        targetBackend == "native" && needsAudioRuntime(tracked);
                    const bool usesPhysics =
                        targetBackend == "native" && needsPhysicsRuntime(tracked);
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
                    for (const auto& source : tracked) {
                        signatureData << source.generic_string() << ':'
                                      << sha256File(source) << '\n';
                    }
                    for (const auto& [relative, asset] : collectAssets(project_)) {
                        signatureData << "asset:" << relative << ':'
                                      << sha256File(asset) << '\n';
                    }
                    for (const auto& [dependencyName, dependencyPath] : project_.localDependencies) {
                        const auto manifest = project_.root / dependencyPath / "zproject.toml";
                        if (!std::filesystem::is_regular_file(manifest)) {
                            throw std::runtime_error("local dependency '" + dependencyName +
                                "' has no project manifest: '" + manifest.string() + "'");
                        }
                        signatureData << "local-dependency:" << dependencyName << ':'
                                      << sha256File(manifest) << '\n';
                    }
                    const std::string signature = sha256(signatureData.str());
                    const auto cacheRecord = cacheDirectory /
                        (targetComponent(target->name) + "-" + targetMode + "-" +
                         targetBackend + ".sha256");
#ifdef _WIN32
                    const std::string extension = ".exe";
#else
                    const std::string extension = ".out";
#endif
                    const std::string targetOutputDirectory = target->outputDirectory.empty()
                        ? "bin"
                        : target->outputDirectory;
                    const auto targetProfileRoot = outputRoot / targetMode;
                    const auto targetDirectory = checkedRelativePath(
                        project_, (targetProfileRoot / targetOutputDirectory).lexically_relative(project_.root).string(),
                        "target output directory");
                    std::filesystem::create_directories(targetDirectory);
                    const auto output = targetDirectory / (targetComponent(target->name) +
                        (targetBackend == "interpreter" ? ".checked" : extension));
                    const auto temporaryOutput = output.string() + ".zbuild-tmp";
                    bool cacheHit = false;
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
                    if (!options.force && std::filesystem::is_regular_file(output) &&
                        std::filesystem::is_regular_file(cacheRecord) && runtimeMatches) {
                        std::istringstream record(readText(cacheRecord));
                        std::string cachedSignature;
                        std::string cachedOutputHash;
                        std::string trailing;
                        const bool validRecord =
                            static_cast<bool>(std::getline(record, cachedSignature)) &&
                            static_cast<bool>(std::getline(record, cachedOutputHash)) &&
                            !(record >> trailing);
                        if (!validRecord) {
                            std::lock_guard lock(consoleMutex);
                            std::cout << "[cache] invalid record for " << target->name
                                      << "; rebuilding\n";
                        } else {
                            cacheHit = trim(cachedSignature) == signature &&
                                trim(cachedOutputHash) == sha256File(output);
                        }
                    }
                    if (cacheHit) {
                        std::lock_guard lock(resultMutex);
                        result.reused.push_back(target->name);
                    } else {
                        if (std::filesystem::exists(temporaryOutput)) {
                            std::filesystem::remove(temporaryOutput);
                        }
                        std::vector<std::string> command{
                            compiler_.string(),
                            targetBackend == "interpreter" ? "check" : "build",
                            (project_.root / target->entry).string()
                        };
                        if (targetBackend == "native") {
                            command.push_back("-o");
                            command.push_back(temporaryOutput);
                        }
                        const auto logFile = cacheDirectory / (targetComponent(target->name) + ".log");
                        const ProcessResult built = runProcess(command, project_.root, logFile);
                        if (options.verbose || built.exitCode != 0) {
                            std::lock_guard lock(consoleMutex);
                            std::cout << built.output;
                        }
                        if (built.exitCode != 0) {
                            std::error_code ignored;
                            std::filesystem::remove(temporaryOutput, ignored);
                            std::filesystem::remove(output, ignored);
                            throw std::runtime_error(
                                "compiler failed for target '" + target->name +
                                "' with exit code " + std::to_string(built.exitCode));
                        }
                        if (targetBackend == "native") {
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
                                        compiler_.string() + "'");
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
                                        compiler_.string() + "'");
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
                            std::error_code error;
                            std::filesystem::rename(temporaryOutput, output, error);
                            if (error) {
                                std::filesystem::remove(output, error);
                                error.clear();
                                std::filesystem::rename(temporaryOutput, output, error);
                            }
                            if (error) throw std::runtime_error(
                                "cannot publish target output '" + output.string() + "': " + error.message());
                        } else {
                            std::ofstream marker(output, std::ios::binary | std::ios::trunc);
                            if (!marker) throw std::runtime_error(
                                "cannot write interpreter validation marker: '" + output.string() + "'");
                            marker << signature << '\n';
                        }
                        const auto temporaryRecord = cacheRecord.string() + ".tmp";
                        {
                            std::ofstream record(temporaryRecord, std::ios::trunc);
                            if (!record) throw std::runtime_error("cannot write build cache");
                            record << signature << '\n' << sha256File(output) << '\n';
                            if (!record) throw std::runtime_error("failed writing build cache");
                        }
                        std::error_code recordError;
                        std::filesystem::remove(cacheRecord, recordError);
                        if (recordError) throw std::runtime_error(
                            "cannot replace build cache record: " + recordError.message());
                        std::filesystem::rename(temporaryRecord, cacheRecord, recordError);
                        if (recordError) throw std::runtime_error(
                            "cannot publish build cache record: " + recordError.message());
                        std::lock_guard lock(resultMutex);
                        result.built.push_back(target->name);
                    }
                    {
                        std::lock_guard lock(resultMutex);
                        outputByTarget[target->name] = output;
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

    if (result.selectedOutput.empty() && !outputByTarget.empty()) {
        const std::string selected = options.target.empty()
            ? defaultBuildTargetName(project_)
            : options.target;
        const auto output = outputByTarget.find(selected);
        if (output != outputByTarget.end()) result.selectedOutput = output->second;
    }
    syncAssets(project_, profileRoot / "assets");
    for (const std::string& target : result.built) {
        std::cout << "[build] " << target << " complete\n";
    }
    for (const std::string& target : result.reused) {
        std::cout << "[build] " << target << " up to date (content cache)\n";
    }
    return result;
}

int BuildEngine::run(
    const BuildOptions& options,
    const std::vector<std::string>& arguments
) {
    const TargetConfig& target = selectedTarget(project_, options);
    if (target.type == "test") throw std::runtime_error("cannot run a test target with 'zbuild run'");
    const BuildResult result = build(options);
    const std::string backend = target.backend.empty()
        ? effectiveBackend(project_, options)
        : target.backend;
    std::vector<std::string> command;
    if (backend == "interpreter") {
        command = {compiler_.string(), "run", (project_.root / target.entry).string()};
        if (!arguments.empty()) {
            command.push_back("--");
            command.insert(command.end(), arguments.begin(), arguments.end());
        }
    } else {
        command.push_back(result.selectedOutput.string());
        command.insert(command.end(), arguments.begin(), arguments.end());
    }
    const std::string mode = effectiveMode(project_, options);
    const auto workDirectory = project_.root;
    const auto outputFile = checkedRelativePath(
        project_, (std::filesystem::path(project_.outputDirectory) / mode / "run.log")
            .generic_string(), "run output");
    const ProcessResult process = runProcess(command, workDirectory, outputFile);
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
        const auto log = checkedRelativePath(
            project_, (std::filesystem::path(project_.outputDirectory) /
                effectiveMode(project_, options) /
                "cache" / (targetComponent(name) + ".test.log")).generic_string(),
            "test log");
        std::vector<std::string> command;
        if (backend == "interpreter") {
            command = {compiler_.string(), "run", (project_.root / target->entry).string()};
            if (!project_.testArguments.empty()) command.push_back("--");
        } else {
            const auto outputIt = built.outputs.find(name);
            if (outputIt == built.outputs.end()) {
                throw std::runtime_error("test target output was not generated: '" + name + "'");
            }
            command.push_back(outputIt->second.string());
        }
        command.insert(command.end(), project_.testArguments.begin(), project_.testArguments.end());
        const ProcessResult result = runProcess(command, project_.root, log);
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

void BuildEngine::clean(const std::string& requestedMode) {
    const auto outputRoot = checkedRelativePath(project_, project_.outputDirectory, "build.output_dir");
    if (!std::filesystem::exists(outputRoot)) return;
    if (requestedMode.empty()) {
        std::filesystem::remove_all(outputRoot / "debug");
        std::filesystem::remove_all(outputRoot / "release");
        std::filesystem::remove_all(outputRoot / "cache");
    } else {
        const std::string mode = requestedMode == "debug" || requestedMode == "release"
            ? requestedMode
            : throw std::runtime_error("clean mode must be debug or release");
        std::filesystem::remove_all(outputRoot / mode);
        std::filesystem::remove_all(outputRoot / "cache");
    }
    std::cout << "[clean] removed generated ZBuild outputs\n";
}

void BuildEngine::printTargets(bool json) const {
    if (json) {
        std::cout << "{\"project\":\"" << jsonEscape(project_.name) << "\",\"targets\":[";
        for (std::size_t i = 0; i < project_.targets.size(); ++i) {
            if (i) std::cout << ',';
            const auto& target = project_.targets[i];
            std::cout << "{\"name\":\"" << jsonEscape(target.name)
                      << "\",\"type\":\"" << jsonEscape(target.type)
                      << "\",\"entry\":\"" << jsonEscape(target.entry) << "\"}";
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
    if (dot) std::cout << "digraph zbuild {\n";
    for (const auto& target : project_.targets) {
        if (dot) {
            std::cout << "  \"" << target.name << "\";\n";
            for (const auto& dependency : target.dependencies) {
                std::cout << "  \"" << target.name << "\" -> \"" << dependency << "\";\n";
            }
        } else {
            std::cout << target.name;
            if (target.dependencies.empty()) {
                std::cout << " (no dependencies)\n";
            } else {
                std::cout << " -> ";
                for (std::size_t i = 0; i < target.dependencies.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << target.dependencies[i];
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
                  << "\",\"root\":\"" << jsonEscape(project_.root.string())
                  << "\",\"backend\":\"" << jsonEscape(project_.backend)
                  << "\",\"mode\":\"" << jsonEscape(project_.mode)
                  << "\",\"target_count\":" << project_.targets.size() << "}\n";
        return;
    }
    std::cout << project_.name << " " << project_.version << '\n'
              << "Root: " << project_.root.string() << '\n'
              << "Backend: " << project_.backend << '\n'
              << "Mode: " << project_.mode << '\n'
              << "Targets: " << project_.targets.size() << '\n';
}

void BuildEngine::package(const BuildOptions& options) {
    const TargetConfig& target = selectedTarget(project_, options);
    if (target.type == "test") throw std::runtime_error("cannot package a test target");
    const std::string backend = target.backend.empty()
        ? effectiveBackend(project_, options)
        : target.backend;
    if (backend != "native") throw std::runtime_error("packaging requires the native backend");
    const std::string mode = target.mode.empty()
        ? effectiveMode(project_, options)
        : target.mode;
    const auto result = build(options);
    const auto packageRoot = checkedRelativePath(
        project_, (std::filesystem::path(project_.outputDirectory) / mode / "package")
            .generic_string(), "package directory");
    std::filesystem::create_directories(packageRoot);
    const auto executableDestination = packageRoot / result.selectedOutput.filename();
    std::filesystem::copy_file(
        result.selectedOutput, executableDestination,
        std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
    const auto runtime = result.selectedOutput.parent_path() / "zgraphics_runtime.dll";
    if (std::filesystem::exists(runtime)) {
        std::filesystem::copy_file(
            runtime, packageRoot / runtime.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    const auto sqlite = result.selectedOutput.parent_path() / "sqlite3.dll";
    if (std::filesystem::exists(sqlite)) {
        std::filesystem::copy_file(
            sqlite, packageRoot / sqlite.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
#endif
    const auto audioRuntimeFile =
#ifdef _WIN32
        result.selectedOutput.parent_path() / "zaudio_runtime.dll";
#elif defined(__linux__)
        result.selectedOutput.parent_path() / "libzaudio_runtime.so";
#else
        std::filesystem::path{};
#endif
    if (!audioRuntimeFile.empty() && std::filesystem::exists(audioRuntimeFile)) {
        std::filesystem::copy_file(
            audioRuntimeFile, packageRoot / audioRuntimeFile.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    const auto physicsRuntimeFile =
#ifdef _WIN32
        result.selectedOutput.parent_path() / "zphysics_runtime.dll";
#elif defined(__linux__)
        result.selectedOutput.parent_path() / "libzphysics_runtime.so";
#else
        std::filesystem::path{};
#endif
    if (!physicsRuntimeFile.empty() && std::filesystem::exists(physicsRuntimeFile)) {
        std::filesystem::copy_file(
            physicsRuntimeFile, packageRoot / physicsRuntimeFile.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    const auto stagedAssets = checkedRelativePath(
        project_, (std::filesystem::path(project_.outputDirectory) /
            mode / "assets").generic_string(), "staged asset directory");
    if (!project_.assetDirectories.empty()) {
        const auto destinationRoot = packageRoot / "assets";
        std::filesystem::copy(
            stagedAssets, destinationRoot,
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing);
    }
    std::cout << "[package] " << packageRoot.string() << '\n';
}

} // namespace zbuild
