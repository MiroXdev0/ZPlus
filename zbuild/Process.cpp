#include "Process.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace zbuild {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read process output file: " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) throw std::runtime_error("failed reading process output file: " + path.string());
    return contents.str();
}

#ifdef _WIN32
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("process argument is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size) != size) {
        throw std::runtime_error("failed to convert a process argument to UTF-16");
    }
    return result;
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}
#endif

} // namespace

ProcessResult runProcess(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& outputFile
) {
    if (arguments.empty()) throw std::invalid_argument("process arguments cannot be empty");
    if (!outputFile.parent_path().empty()) {
        std::filesystem::create_directories(outputFile.parent_path());
    }
#ifdef _WIN32
    std::wstring commandLine;
    for (const std::string& argument : arguments) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(widen(argument));
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(
        outputFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot create process output file: " + outputFile.string());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const std::wstring directory = workingDirectory.empty()
        ? std::wstring{}
        : workingDirectory.wstring();
    const BOOL created = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, directory.empty() ? nullptr : directory.c_str(),
        &startup, &process);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(output);
    if (!created) {
        throw std::runtime_error(
            "cannot launch '" + arguments.front() + "' (Windows error " +
            std::to_string(createError) + ")");
    }
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD waitError = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error(
            "failed waiting for '" + arguments.front() + "' (Windows error " +
            std::to_string(waitError) + ")");
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD exitError = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error(
            "cannot read exit code for '" + arguments.front() +
            "' (Windows error " + std::to_string(exitError) + ")");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {static_cast<int>(exitCode), readFile(outputFile)};
#else
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("cannot create build process");
    if (child == 0) {
        const int output = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output < 0 || dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(output);
        if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) _exit(126);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("failed while waiting for build process");
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status)
        : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
    return {exitCode, readFile(outputFile)};
#endif
}

std::filesystem::path findExecutable(const std::string& name) {
    std::string paths;
#ifdef _WIN32
    constexpr char separator = ';';
    const std::vector<std::string> extensions{"", ".exe", ".cmd", ".bat"};
    char* pathValue = nullptr;
    std::size_t pathLength = 0;
    if (_dupenv_s(&pathValue, &pathLength, "PATH") != 0 || !pathValue) return {};
    paths.assign(pathValue);
    std::free(pathValue);
#else
    constexpr char separator = ':';
    const std::vector<std::string> extensions{""};
    const char* pathValue = std::getenv("PATH");
    if (!pathValue) return {};
    paths.assign(pathValue);
#endif
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        const std::size_t end = paths.find(separator, begin);
        const std::string directory = paths.substr(begin, end - begin);
        for (const std::string& extension : extensions) {
            const auto candidate = std::filesystem::path(directory) / (name + extension);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::absolute(candidate);
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

} // namespace zbuild
