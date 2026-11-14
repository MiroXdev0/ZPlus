#include "Process.hpp"
#include "Filesystem.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace zbuild {
namespace {

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
    const std::filesystem::path& workingDirectory
) {
    if (arguments.empty()) throw std::invalid_argument("process arguments cannot be empty");
#ifdef _WIN32
    std::wstring commandLine;
    for (const std::string& argument : arguments) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(widen(argument));
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        throw std::runtime_error(
            "cannot create process output pipe (Windows error " +
            std::to_string(GetLastError()) + ")");
    }
    if (!SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD error = GetLastError();
        CloseHandle(outputRead);
        CloseHandle(outputWrite);
        throw std::runtime_error(
            "cannot configure process output pipe (Windows error " +
            std::to_string(error) + ")");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
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
    CloseHandle(outputWrite);
    if (!created) {
        CloseHandle(outputRead);
        throw std::runtime_error(
            "cannot launch '" + arguments.front() + "' (Windows error " +
            std::to_string(createError) + ")");
    }

    std::string output;
    std::exception_ptr readException;
    std::thread reader;
    try {
        reader = std::thread([&] {
            try {
                std::array<char, 8192> buffer{};
                for (;;) {
                    DWORD bytesRead = 0;
                    if (!ReadFile(
                            outputRead, buffer.data(),
                            static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                        const DWORD error = GetLastError();
                        if (error != ERROR_BROKEN_PIPE) {
                            throw std::runtime_error(
                                "failed reading process output (Windows error " +
                                std::to_string(error) + ")");
                        }
                        break;
                    }
                    if (bytesRead == 0) break;
                    output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
                }
            } catch (...) {
                readException = std::current_exception();
                TerminateProcess(process.hProcess, 1);
            }
            CloseHandle(outputRead);
        });
    } catch (...) {
        CloseHandle(outputRead);
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD waitError = GetLastError();
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        reader.join();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error(
            "failed waiting for '" + arguments.front() + "' (Windows error " +
            std::to_string(waitError) + ")");
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD exitError = GetLastError();
        reader.join();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error(
            "cannot read exit code for '" + arguments.front() +
            "' (Windows error " + std::to_string(exitError) + ")");
    }
    reader.join();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (readException) std::rethrow_exception(readException);
    return {static_cast<int>(exitCode), std::move(output)};
#else
    int outputPipe[2];
    if (pipe(outputPipe) != 0) {
        throw std::runtime_error("cannot create build process output pipe");
    }
    const pid_t child = fork();
    if (child < 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        throw std::runtime_error("cannot create build process");
    }
    if (child == 0) {
        close(outputPipe[0]);
        if (dup2(outputPipe[1], STDOUT_FILENO) < 0 ||
            dup2(outputPipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(outputPipe[1]);
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
    close(outputPipe[1]);
    std::string output;
    std::array<char, 8192> buffer{};
    int readError = 0;
    std::exception_ptr captureException;
    for (;;) {
        const ssize_t bytesRead = read(outputPipe[0], buffer.data(), buffer.size());
        if (bytesRead > 0) {
            try {
                output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
            } catch (...) {
                captureException = std::current_exception();
                break;
            }
        } else if (bytesRead == 0) {
            break;
        } else if (errno != EINTR) {
            readError = errno;
            break;
        }
    }
    close(outputPipe[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("failed while waiting for build process");
    }
    if (captureException) std::rethrow_exception(captureException);
    if (readError != 0) {
        throw std::runtime_error(
            "failed reading process output: " + std::to_string(readError));
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status)
        : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
    return {exitCode, std::move(output)};
#endif
}

std::filesystem::path findExecutable(const std::string& name) {
#ifdef _WIN32
    const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (required == 0) return {};
    std::wstring paths(static_cast<std::size_t>(required), L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"PATH", paths.data(), required);
    if (length == 0 || length >= required) return {};
    paths.resize(length);
    constexpr wchar_t separator = L';';
    const std::vector<std::string> extensions{"", ".exe", ".cmd", ".bat"};
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        const std::size_t end = paths.find(separator, begin);
        const std::filesystem::path directory(paths.substr(
            begin, end == std::wstring::npos ? std::wstring::npos : end - begin));
        for (const std::string& extension : extensions) {
            const auto candidate =
                directory / pathFromUtf8(name + extension);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::absolute(candidate);
            }
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
#else
    std::string paths;
    constexpr char separator = ':';
    const std::vector<std::string> extensions{""};
    const char* pathValue = std::getenv("PATH");
    if (!pathValue) return {};
    paths.assign(pathValue);
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        const std::size_t end = paths.find(separator, begin);
        const std::string directory = paths.substr(begin, end - begin);
        for (const std::string& extension : extensions) {
            const auto candidate = pathFromUtf8(directory) /
                pathFromUtf8(name + extension);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::absolute(candidate);
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
#endif
    return {};
}

} // namespace zbuild
