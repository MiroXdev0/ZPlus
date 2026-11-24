#include "../zpbuild/Filesystem.hpp"
#include "../zpbuild/Hash.hpp"
#include "../zpbuild/Packages.hpp"
#include "../zpbuild/Process.hpp"
#include "../zpbuild/RegistryClient.hpp"
#include "../zpbuild/RegistryPackages.hpp"
#include "../runtime/Json.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <variant>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#ifndef ZPM_VERSION
#define ZPM_VERSION "unknown"
#endif
#ifndef ZPM_DEFAULT_REGISTRY_URL
#define ZPM_DEFAULT_REGISTRY_URL ""
#endif

namespace {

using Arguments = std::vector<std::string>;
using JsonValue = zplus::json::Value;
using JsonObject = JsonValue::Object;
using JsonArray = JsonValue::Array;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const JsonValue* jsonField(const JsonValue& value, const std::string& name) {
    const auto* object = std::get_if<JsonObject>(&value.data);
    if (!object) throw std::runtime_error("registry returned an invalid JSON object");
    const JsonValue* result = nullptr;
    for (const auto& [key, item] : *object) {
        if (key == name) {
            if (result) throw std::runtime_error(
                "registry returned duplicate field '" + name + "'");
            result = &item;
        }
    }
    return result;
}

std::string jsonString(
    const JsonValue& value,
    const std::string& name,
    bool required = true
) {
    const JsonValue* item = jsonField(value, name);
    if (!item && !required) return {};
    const auto* text = item ? std::get_if<std::string>(&item->data) : nullptr;
    if (!text) throw std::runtime_error(
        "registry response is missing string field '" + name + "'");
    return *text;
}

JsonValue registryJson(const zbuild::RegistryResponse& response, const char* action) {
    if (response.status < 200 || response.status >= 300) {
        std::string message = std::string(action) + " failed with HTTP " +
            std::to_string(response.status);
        try {
            const JsonValue body = zplus::json::parse(response.body);
            const JsonValue* error = jsonField(body, "error");
            if (error) {
                const std::string serverMessage =
                    jsonString(*error, "message", false);
                if (!serverMessage.empty()) message = serverMessage;
            }
        } catch (const std::exception&) {
        }
        throw std::runtime_error(message);
    }
    return zplus::json::parse(response.body);
}

std::string encodeQuery(std::string_view value) {
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

std::string promptPassword() {
    std::string password;
    std::cout << "Password: " << std::flush;
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD originalMode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &originalMode)) {
        throw std::runtime_error("password input requires an interactive terminal");
    }
    if (!SetConsoleMode(input, originalMode & ~ENABLE_ECHO_INPUT)) {
        throw std::runtime_error("cannot disable password echo");
    }
    std::getline(std::cin, password);
    SetConsoleMode(input, originalMode);
#else
    if (!::isatty(STDIN_FILENO)) {
        throw std::runtime_error("password input requires an interactive terminal");
    }
    termios original{};
    if (::tcgetattr(STDIN_FILENO, &original) != 0) {
        throw std::runtime_error("cannot read terminal settings");
    }
    termios hidden = original;
    hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
        throw std::runtime_error("cannot disable password echo");
    }
    std::getline(std::cin, password);
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#endif
    std::cout << '\n';
    return password;
}

class TemporaryArchive {
public:
    TemporaryArchive() {
        const auto directory = std::filesystem::temp_directory_path();
        const auto time = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        std::random_device random;
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            const std::string seed = std::to_string(time) + ":" +
                std::to_string(random()) + ":" + std::to_string(attempt);
            path_ = directory / zbuild::pathFromUtf8(
                "zpm-publish-" + zbuild::sha256(seed) + ".zpm");
            if (!std::filesystem::exists(path_)) return;
        }
        throw std::runtime_error("cannot allocate a temporary package archive path");
    }

    ~TemporaryArchive() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::pair<std::string, std::string> packageSpec(const std::string& value) {
    const auto separator = value.find('@');
    if (separator == std::string::npos) return {value, "*"};
    if (separator == 0 || separator + 1 == value.size()) {
        throw std::runtime_error("package reference must be NAME[@VERSION_CONSTRAINT]");
    }
    return {value.substr(0, separator), value.substr(separator + 1)};
}

std::filesystem::path findTool(
    const std::string& name,
    const std::filesystem::path& executable
);

void registrySearch(const std::string& query) {
    const auto response = registryJson(zbuild::registryRequest(
        "GET", "/search?q=" + encodeQuery(query)), "package search");
    const JsonValue* packagesValue = jsonField(response, "packages");
    const auto* packages = packagesValue
        ? std::get_if<JsonArray>(&packagesValue->data) : nullptr;
    if (!packages) throw std::runtime_error("registry returned an invalid package list");
    if (packages->empty()) {
        std::cout << "No packages found.\n";
        return;
    }
    for (const auto& package : *packages) {
        std::cout << jsonString(package, "name") << ' '
                  << jsonString(package, "version", false) << " - "
                  << jsonString(package, "description", false) << '\n';
    }
}

void registryInfo(const std::string& name) {
    const auto response = registryJson(zbuild::registryRequest(
        "GET", "/packages/" + encodeQuery(name)), "package information");
    std::cout << jsonString(response, "name") << '\n'
              << jsonString(response, "description", false) << '\n'
              << "Owner: " << jsonString(response, "owner", false) << '\n'
              << "Author: " << jsonString(response, "author", false) << '\n'
              << "License: " << jsonString(response, "license", false) << '\n';
    const JsonValue* versionsValue = jsonField(response, "versions");
    const auto* versions = versionsValue
        ? std::get_if<JsonArray>(&versionsValue->data) : nullptr;
    if (!versions) throw std::runtime_error("registry returned invalid release history");
    for (const auto& version : *versions) {
        std::cout << "  " << jsonString(version, "version")
                  << "  compiler " << jsonString(version, "compiler", false)
                  << "  " << jsonString(version, "integrity") << '\n';
    }
}

void registryLogin() {
    std::string email;
    std::cout << "Email: " << std::flush;
    if (!std::getline(std::cin, email) || email.empty()) {
        throw std::runtime_error("email is required");
    }
    std::string password = promptPassword();
    std::string body = zplus::json::serialize(JsonValue{JsonObject{
        {"email", JsonValue{email}},
        {"password", JsonValue{password}}
    }});
    std::fill(password.begin(), password.end(), '\0');
    const auto loginResponse = zbuild::registryRequest(
        "POST", "/auth/cli-login", body,
        {{"Content-Type", "application/json"}});
    std::fill(body.begin(), body.end(), '\0');
    const JsonValue response = registryJson(loginResponse, "login");
    zbuild::storeRegistryToken(jsonString(response, "token"));
    std::cout << "Logged in to " << zbuild::registryApiBaseUrl() << ".\n";
}

void registryLogout() {
    const std::string token = zbuild::loadRegistryToken();
    if (token.empty()) {
        std::cout << "No saved registry session.\n";
        return;
    }
    zbuild::removeRegistryToken();
    (void)registryJson(zbuild::registryRequest(
        "POST", "/auth/logout", "{}",
        {{"Authorization", "Bearer " + token},
         {"Content-Type", "application/json"}}), "logout");
    std::cout << "Logged out of the Z+ package registry.\n";
}

void registryPublish(
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& executable
) {
    const std::string token = zbuild::loadRegistryToken();
    if (token.empty()) throw std::runtime_error(
        "not logged in; run 'zpm login' before publishing");
    const auto manifest = zbuild::loadPackageManifest(packageDirectory);
    const auto buildTool = findTool("zpbuild", executable);
    const auto build = zbuild::runProcess(
        {zbuild::pathToUtf8(buildTool), "build"}, packageDirectory);
    std::cout << build.output;
    if (build.exitCode != 0) {
        throw std::runtime_error(
            "ZPBuild failed to build the package; it was not published");
    }
    TemporaryArchive temporary;
    zbuild::createPackageArchive(packageDirectory, temporary.path());
    const auto size = std::filesystem::file_size(temporary.path());
    constexpr std::uintmax_t maximumRegistryArchiveBytes = 4ull * 1024 * 1024;
    if (size > maximumRegistryArchiveBytes) {
        throw std::length_error(
            "registry package archives are limited to 4 MiB");
    }
    std::ifstream upload(temporary.path(), std::ios::binary);
    if (!upload) throw std::runtime_error("cannot read the package archive");
    std::string bytes((std::istreambuf_iterator<char>(upload)),
        std::istreambuf_iterator<char>());
    if (bytes.size() != size) throw std::runtime_error("failed reading the package archive");
    std::cout << "[packages] uploading " << manifest.name << ' '
              << manifest.version << '\n';
    const JsonValue response = registryJson(zbuild::registryRequest(
        "POST", "/publish", bytes,
        {{"Authorization", "Bearer " + token},
         {"Content-Type", "application/vnd.zpm.archive"}},
        1024 * 1024), "publish");
    std::fill(bytes.begin(), bytes.end(), '\0');
    std::cout << "Published " << jsonString(response, "name") << ' '
              << jsonString(response, "version") << ".\n";
}

std::filesystem::path findTool(
    const std::string& name,
    const std::filesystem::path& executable
) {
    const auto adjacent = executable.parent_path() / zbuild::pathFromUtf8(name);
    if (std::filesystem::is_regular_file(adjacent)) return adjacent;
#ifdef _WIN32
    const auto adjacentExe = executable.parent_path() /
        zbuild::pathFromUtf8(name + ".exe");
    if (std::filesystem::is_regular_file(adjacentExe)) return adjacentExe;
#endif
    const auto onPath = zbuild::findExecutable(name);
    if (!onPath.empty()) return onPath;
    throw std::runtime_error(
        "could not find '" + name + "' beside zpm or on PATH");
}

std::string compilerVersion(const std::filesystem::path& executable) {
    const auto compiler = findTool("zp", executable);
    const auto result = zbuild::runProcess(
        {zbuild::pathToUtf8(compiler), "version"}, std::filesystem::current_path());
    if (result.exitCode != 0) {
        throw std::runtime_error("could not read Z+ compiler version:\n" + result.output);
    }
    constexpr std::string_view prefix = "Z+ Compiler ";
    const std::string output = result.output;
    const auto lineEnd = output.find_first_of("\r\n");
    const std::string line = output.substr(0, lineEnd);
    if (!line.starts_with(prefix)) {
        throw std::runtime_error("unrecognized Z+ compiler version output: " + line);
    }
    return line.substr(prefix.size());
}

std::filesystem::path projectRoot() {
    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (error) throw std::runtime_error("cannot determine current directory");
    while (!current.empty()) {
        if (std::filesystem::is_regular_file(current / "zpm.json", error) && !error) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return std::filesystem::current_path();
}

void usage() {
    std::cout <<
        "ZPM " << ZPM_VERSION << " - Z+ Package Manager\n\n"
        "Usage:\n"
        "  zpm --version\n"
        "  zpm init [NAME]\n"
        "  zpm add <PACKAGE[@CONSTRAINT]|local-directory|package.zpm>\n"
        "  zpm install [PACKAGE[@CONSTRAINT]|local-directory|package.zpm] [--locked]\n"
        "  zpm remove <NAME>\n"
        "  zpm uninstall <NAME>\n"
        "  zpm update\n"
        "  zpm list\n"
        "  zpm search [QUERY...]\n"
        "  zpm info <NAME>\n"
        "  zpm publish [PACKAGE_DIRECTORY]\n"
        "  zpm login\n"
        "  zpm logout\n"
        "  zpm pack [PACKAGE_DIRECTORY] [ARCHIVE.zpm]\n"
        "  zpm build [ZPBUILD_ARGUMENTS...]\n"
        "  zpm test [ZPBUILD_ARGUMENTS...]\n"
        "  zpm cache info|clean\n"
        "\n"
        "Local .zpm archives use the versioned ZPM1 container format.\n"
        "The registry defaults to " ZPM_DEFAULT_REGISTRY_URL "; "
        "ZPM_REGISTRY_URL can select an HTTPS test registry.\n";
}

void ensureProjectDirectory(
    const std::filesystem::path& root,
    const std::filesystem::path& directory
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);
    if (!error && std::filesystem::is_symlink(status)) {
        throw std::runtime_error(
            "refusing to use a symlinked package directory '" +
            zbuild::pathToUtf8(directory) + "'");
    }
    if (!error && std::filesystem::exists(status) &&
        !std::filesystem::is_directory(status)) {
        throw std::runtime_error(
            "package path is not a directory: '" +
            zbuild::pathToUtf8(directory) + "'");
    }
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        if (!std::filesystem::create_directory(directory, error) && error) {
            throw std::filesystem::filesystem_error(
                "cannot create package directory", directory, error);
        }
    } else if (error) {
        throw std::filesystem::filesystem_error(
            "cannot inspect package directory", directory, error);
    }
    const auto canonicalRoot = std::filesystem::canonical(root);
    const auto canonicalDirectory = std::filesystem::canonical(directory);
    const auto relative = canonicalDirectory.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..") {
        throw std::runtime_error("package directory resolves outside the project root");
    }
}

void addArchive(
    const std::filesystem::path& root,
    const std::filesystem::path& archive
) {
    const auto packagesDirectory = root / "z+_packages";
    ensureProjectDirectory(root, packagesDirectory);
    const auto sourceDirectory = packagesDirectory / ".sources";
    ensureProjectDirectory(root, sourceDirectory);
    const std::string archiveHash = zbuild::sha256File(archive);
    const auto extraction = sourceDirectory / zbuild::pathFromUtf8(archiveHash);
    if (!std::filesystem::is_directory(extraction)) {
        zbuild::installPackageArchive(archive, extraction);
    }
    zbuild::cachePackage(extraction);
    zbuild::addLocalPackage(root, extraction);
}

void installLocalSource(
    const std::filesystem::path& root,
    const std::filesystem::path& source
) {
    if (std::filesystem::is_directory(source)) {
        zbuild::addLocalPackage(root, source);
    } else if (std::filesystem::is_regular_file(source) &&
               lower(zbuild::pathToUtf8(source.extension())) == ".zpm") {
        addArchive(root, source);
    } else {
        throw std::runtime_error(
            "local package source must be a directory or a .zpm archive");
    }
}

void printPackages(const std::filesystem::path& root) {
    const auto packages = zbuild::listPackages(root);
    if (packages.empty()) {
        std::cout << "No packages are installed in z+_packages/.\n";
        return;
    }
    for (const auto& package : packages) {
        std::cout << package.name << ' ' << package.version
                  << " (" << package.integrity << ")\n";
    }
}

void cacheCommand(const Arguments& arguments) {
    if (arguments.size() != 1) {
        throw std::runtime_error("usage: zpm cache info|clean");
    }
    const auto cache = zbuild::packageCacheDirectory();
    if (arguments[0] == "info") {
        std::uintmax_t bytes = 0;
        std::size_t files = 0;
        std::error_code error;
        if (std::filesystem::is_directory(cache, error) && !error) {
            for (std::filesystem::recursive_directory_iterator item(
                     cache, std::filesystem::directory_options::none, error),
                 end;
                 !error && item != end; item.increment(error)) {
                if (item->is_symlink()) {
                    item.disable_recursion_pending();
                    continue;
                }
                if (item->is_regular_file()) {
                    bytes += item->file_size();
                    ++files;
                }
            }
        }
        if (error) throw std::runtime_error(
            "cannot inspect package cache: " + error.message());
        std::cout << "cache: " << zbuild::pathToUtf8(cache)
                  << "\nfiles: " << files << "\nbytes: " << bytes << '\n';
        return;
    }
    if (arguments[0] == "clean") {
        const auto removed = zbuild::cleanPackageCache();
        std::cout << "Removed " << removed << " cached package(s).\n";
        return;
    }
    throw std::runtime_error("usage: zpm cache info|clean");
}

int runZpbuild(
    const Arguments& arguments,
    const std::filesystem::path& executable
) {
    const auto tool = findTool("zpbuild", executable);
    Arguments command{zbuild::pathToUtf8(tool)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    const auto result = zbuild::runProcess(command, std::filesystem::current_path());
    std::cout << result.output;
    return result.exitCode;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const auto executable = std::filesystem::absolute(
            zbuild::pathFromUtf8(argv[0]));
        const std::string command = argv[1];
        if (command == "--version" || command == "version") {
            std::cout << "ZPM " << ZPM_VERSION << '\n';
            return 0;
        }
        if (command == "--help" || command == "help" || command == "-h") {
            usage();
            return 0;
        }

        Arguments arguments;
        for (int index = 2; index < argc; ++index) arguments.emplace_back(argv[index]);
        const auto root = projectRoot();

        if (command == "init") {
            if (arguments.size() > 1) {
                throw std::runtime_error("usage: zpm init [NAME]");
            }
            zbuild::initializePackage(root,
                arguments.empty() ? std::string{} : arguments.front());
            std::cout << "Initialized Z+ package in "
                      << zbuild::pathToUtf8(root) << '\n';
            return 0;
        }
        if (command == "pack") {
            if (arguments.size() > 2) {
                throw std::runtime_error(
                    "usage: zpm pack [PACKAGE_DIRECTORY] [ARCHIVE.zpm]");
            }
            const auto directory = arguments.empty()
                ? root : std::filesystem::absolute(zbuild::pathFromUtf8(arguments[0]));
            const auto manifest = zbuild::loadPackageManifest(directory);
            const auto archive = arguments.size() == 2
                ? std::filesystem::absolute(zbuild::pathFromUtf8(arguments[1]))
                : std::filesystem::current_path() /
                    zbuild::pathFromUtf8(manifest.name + "-" + manifest.version + ".zpm");
            zbuild::createPackageArchive(directory, archive);
            std::cout << "Created " << zbuild::pathToUtf8(archive) << '\n';
            return 0;
        }
        if (command == "install" || command == "add") {
            bool locked = false;
            Arguments sources;
            for (const auto& argument : arguments) {
                if (argument == "--locked") locked = true;
                else sources.push_back(argument);
            }
            if (sources.size() > 1 || (locked && !sources.empty())) {
                throw std::runtime_error(
                    "usage: zpm add|install "
                    "[<PACKAGE[@CONSTRAINT]|LOCAL_DIRECTORY|PACKAGE.zpm>] [--locked]");
            }
            if (!std::filesystem::is_regular_file(root / "zpm.json")) {
                throw std::runtime_error(
                    "no zpm.json found; run 'zpm init' first");
            }
            if (!sources.empty()) {
                const std::string source = sources.front();
                const auto sourcePath = zbuild::pathFromUtf8(source);
                const auto extension = lower(
                    zbuild::pathToUtf8(sourcePath.extension()));
                const bool pathLike = source.starts_with(".") ||
                    source.find('/') != std::string::npos ||
                    source.find('\\') != std::string::npos ||
                    extension == ".zpm";
                if (std::filesystem::exists(sourcePath) || pathLike) {
                    installLocalSource(root, sourcePath);
                } else {
                    const auto [name, constraint] = packageSpec(source);
                    zbuild::addPackageDependency(root, name, constraint);
                }
            }
            const std::string selectedCompiler = compilerVersion(executable);
            zbuild::prepareRegistryPackages(
                root, selectedCompiler, locked, false);
            const auto packages = zbuild::installPackages(
                root, selectedCompiler, locked);
            std::cout << "Installed " << packages.size()
                      << " package(s) into z+_packages/.\n";
            return 0;
        }
        if (command == "remove" || command == "uninstall") {
            if (arguments.size() != 1) {
                throw std::runtime_error("usage: zpm remove <NAME>");
            }
            zbuild::removePackage(root, arguments.front());
            const std::string selectedCompiler = compilerVersion(executable);
            zbuild::prepareRegistryPackages(
                root, selectedCompiler, false, false);
            const auto packages = zbuild::installPackages(
                root, selectedCompiler);
            std::cout << "Removed direct dependency '" << arguments.front()
                      << "'; " << packages.size() << " package(s) remain.\n";
            return 0;
        }
        if (command == "update") {
            if (!arguments.empty()) {
                throw std::runtime_error("usage: zpm update");
            }
            const std::string selectedCompiler = compilerVersion(executable);
            zbuild::prepareRegistryPackages(
                root, selectedCompiler, false, true);
            const auto packages = zbuild::installPackages(
                root, selectedCompiler);
            std::cout << "Resolved " << packages.size()
                      << " package(s) from the registry and local sources.\n";
            return 0;
        }
        if (command == "list") {
            if (!arguments.empty()) throw std::runtime_error("usage: zpm list");
            printPackages(root);
            return 0;
        }
        if (command == "cache") {
            cacheCommand(arguments);
            return 0;
        }
        if (command == "build" || command == "test") {
            Arguments delegated{command == "build" ? "build" : "test"};
            delegated.insert(delegated.end(), arguments.begin(), arguments.end());
            return runZpbuild(delegated, executable);
        }
        if (command == "search") {
            std::string query;
            for (const auto& argument : arguments) {
                if (!query.empty()) query.push_back(' ');
                query += argument;
            }
            registrySearch(query);
            return 0;
        }
        if (command == "info") {
            if (arguments.size() != 1) {
                throw std::runtime_error("usage: zpm info <NAME>");
            }
            registryInfo(arguments.front());
            return 0;
        }
        if (command == "publish") {
            if (arguments.size() > 1) {
                throw std::runtime_error("usage: zpm publish [PACKAGE_DIRECTORY]");
            }
            const auto directory = arguments.empty()
                ? root
                : std::filesystem::absolute(zbuild::pathFromUtf8(arguments.front()));
            registryPublish(directory, executable);
            return 0;
        }
        if (command == "login") {
            if (!arguments.empty()) throw std::runtime_error("usage: zpm login");
            registryLogin();
            return 0;
        }
        if (command == "logout") {
            if (!arguments.empty()) throw std::runtime_error("usage: zpm logout");
            registryLogout();
            return 0;
        }
        throw std::runtime_error("unknown command '" + command + "'");
    } catch (const std::exception& error) {
        std::cerr << "zpm: " << error.what() << '\n';
        return 1;
    }
}
