#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Platform.hpp"

#include "../Json.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <wrl/client.h>
#include <winreg.h>
#endif

namespace zplus::installer {
namespace {

std::filesystem::path checkedPath(const std::string& value, const char* operation) {
    if (value.empty() || value.find('\0') != std::string::npos) {
        throw std::invalid_argument(std::string(operation) + " path must be non-empty UTF-8");
    }
#ifdef _WIN32
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
#else
    return std::filesystem::path(value);
#endif
}

#ifdef _WIN32
std::wstring wide(const std::string& value, const char* argument) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error(std::string(argument) + " is too long");
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count == 0) {
        throw std::invalid_argument(std::string(argument) + " is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), count) != count) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert UTF-8 to UTF-16");
    }
    return result;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Windows returned an excessively long string");
    }
    const int count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count == 0) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert UTF-16 to UTF-8");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), count,
            nullptr, nullptr) != count) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "cannot convert UTF-16 to UTF-8");
    }
    return result;
}

std::string windowsError(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = size && buffer ? std::wstring(buffer, size) : L"Windows error";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return utf8(message);
}

[[noreturn]] void failWindows(const char* operation, DWORD error = GetLastError()) {
    throw std::system_error(
        static_cast<int>(error), std::system_category(),
        std::string(operation) + ": " + windowsError(error));
}

std::wstring quoteArgument(const std::wstring& argument) {
    if (!argument.empty() &&
        argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'"');
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

void readPipe(HANDLE pipe, std::string& output) {
    std::array<char, 8192> buffer{};
    DWORD read = 0;
    while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read != 0) {
        if (output.size() < 16 * 1024 * 1024) {
            const std::size_t remaining = 16 * 1024 * 1024 - output.size();
            const std::size_t appendCount = std::min<std::size_t>(remaining, read);
            output.append(buffer.data(), appendCount);
            if (appendCount < read && !output.ends_with("[output truncated]")) {
                output.append("\n[output truncated]");
            }
        }
    }
}

class ComScope {
public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE) {
            throw std::system_error(
                static_cast<int>(result_), std::system_category(),
                "cannot initialize Windows COM");
        }
        initialized_ = SUCCEEDED(result_);
    }
    ~ComScope() { if (initialized_) CoUninitialize(); }
private:
    HRESULT result_;
    bool initialized_ = false;
};

bool isAllowedRegistryPath(const std::string& path) {
    constexpr std::string_view root = "Software\\";
    return (path == "Environment" || path.rfind(root, 0) == 0) &&
        path.find("..") == std::string::npos &&
        path.find('/') == std::string::npos &&
        path.find('\0') == std::string::npos;
}

HKEY checkedRegistryKey(const std::string& path) {
    constexpr std::string_view root = "Software\\";
    if (!isAllowedRegistryPath(path) ||
        (path != "Environment" && path.size() == root.size())) {
        throw std::invalid_argument(
            "registry access is restricted to current-user Environment and Software keys");
    }
    HKEY key = nullptr;
    const std::wstring subkey = wide(path, "registry key");
    const LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return nullptr;
    if (status != ERROR_SUCCESS) failWindows("cannot open current-user registry key", status);
    return key;
}

std::pair<std::wstring, std::wstring> readShortcutParts(const std::string& path) {
    ComScope com;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(link.GetAddressOf()));
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot load Windows shortcut");
    }
    Microsoft::WRL::ComPtr<IPersistFile> persisted;
    result = link.As(&persisted);
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot read Windows shortcut");
    }
    const std::wstring shortcutPath = wide(path, "shortcut path");
    result = persisted->Load(shortcutPath.c_str(), STGM_READ);
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot open Windows shortcut");
    }
    std::array<wchar_t, 32768> target{};
    std::array<wchar_t, 32768> arguments{};
    WIN32_FIND_DATAW data{};
    result = link->GetPath(target.data(), static_cast<int>(target.size()), &data,
        SLGP_RAWPATH);
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot read shortcut target");
    }
    result = link->GetArguments(arguments.data(), static_cast<int>(arguments.size()));
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot read shortcut arguments");
    }
    return {target.data(), arguments.data()};
}
#endif

} // namespace

bool copyFile(const std::string& source, const std::string& destination, bool overwrite) {
    std::error_code error;
    const auto options = overwrite
        ? std::filesystem::copy_options::overwrite_existing
        : std::filesystem::copy_options::none;
    const bool copied = std::filesystem::copy_file(
        checkedPath(source, "copy source"), checkedPath(destination, "copy destination"),
        options, error);
    if (error) throw std::filesystem::filesystem_error("copy file failed", source, destination, error);
    return copied;
}

bool moveFile(const std::string& source, const std::string& destination, bool overwrite) {
    const auto from = checkedPath(source, "move source");
    const auto to = checkedPath(destination, "move destination");
    std::error_code error;
    if (overwrite && std::filesystem::exists(to, error)) {
        if (error) throw std::filesystem::filesystem_error("inspect move destination failed", to, error);
        std::filesystem::remove(to, error);
        if (error) throw std::filesystem::filesystem_error("replace move destination failed", to, error);
    }
    std::filesystem::rename(from, to, error);
    if (error) throw std::filesystem::filesystem_error("move file failed", from, to, error);
    return true;
}

bool removeFile(const std::string& path) {
    std::error_code error;
    const auto checked = checkedPath(path, "delete");
    if (std::filesystem::is_directory(checked, error)) {
        if (error) throw std::filesystem::filesystem_error("inspect delete target failed", checked, error);
        throw std::invalid_argument("deleteFile refuses to remove directories");
    }
    const bool removed = std::filesystem::remove(checked, error);
    if (error) throw std::filesystem::filesystem_error("delete file failed", checked, error);
    return removed;
}

bool scheduleFileRemovalOnReboot(const std::string& path) {
#ifdef _WIN32
    const auto checked = checkedPath(path, "schedule delete");
    if (!MoveFileExW(checked.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        failWindows("cannot schedule file removal for the next restart", GetLastError());
    }
    return true;
#else
    (void)path;
    throw std::runtime_error("delayed file removal is supported only on Windows");
#endif
}

bool removeEmptyDirectory(const std::string& path) {
    std::error_code error;
    const auto checked = checkedPath(path, "remove directory");
    if (!std::filesystem::is_directory(checked, error)) {
        if (error) throw std::filesystem::filesystem_error("inspect directory failed", checked, error);
        return false;
    }
    const bool removed = std::filesystem::remove(checked, error);
    if (error) throw std::filesystem::filesystem_error("remove empty directory failed", checked, error);
    return removed;
}

std::int64_t availableSpace(const std::string& path) {
    std::error_code error;
    auto target = checkedPath(path, "space");
    while (!std::filesystem::exists(target, error)) {
        if (error) {
            throw std::filesystem::filesystem_error(
                "inspect disk-space target failed", target, error);
        }
        const auto parent = target.parent_path();
        if (parent.empty() || parent == target) {
            throw std::invalid_argument(
                "disk-space target has no existing parent directory");
        }
        target = parent;
    }
    if (error) {
        throw std::filesystem::filesystem_error(
            "inspect disk-space target failed", target, error);
    }
    const auto info = std::filesystem::space(target, error);
    if (error) throw std::filesystem::filesystem_error("query available space failed", path, error);
    if (info.available > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(info.available);
}

std::string sha256File(const std::string& path) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) throw std::runtime_error("cannot initialize SHA-256 provider");
    std::vector<UCHAR> hashObject;
    std::array<UCHAR, 32> digest{};
    DWORD objectSize = 0, returned = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("cannot query SHA-256 provider");
    }
    hashObject.resize(objectSize);
    status = BCryptCreateHash(algorithm, &hash, hashObject.data(), objectSize,
        nullptr, 0, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("cannot create SHA-256 hash");
    }
    try {
        std::ifstream input(checkedPath(path, "SHA-256"), std::ios::binary);
        if (!input) throw std::system_error(errno, std::generic_category(),
            "cannot open file for SHA-256: " + path);
        std::array<char, 128 * 1024> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0) {
                status = BCryptHashData(hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count), 0);
                if (status < 0) throw std::runtime_error("SHA-256 update failed");
            }
        }
        if (!input.eof()) throw std::runtime_error("error reading file for SHA-256: " + path);
        status = BCryptFinishHash(hash, digest.data(),
            static_cast<ULONG>(digest.size()), 0);
        if (status < 0) throw std::runtime_error("SHA-256 finalization failed");
    } catch (...) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(64);
    for (UCHAR byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
#else
    (void)path;
    throw std::runtime_error("SHA-256 file hashing is not available on this platform");
#endif
}

std::string runProcess(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    const std::string& workingDirectory,
    std::int64_t timeoutMilliseconds
) {
#ifdef _WIN32
    if (timeoutMilliseconds < 1 || timeoutMilliseconds > 24LL * 60 * 60 * 1000) {
        throw std::invalid_argument("process timeout must be from 1 to 86400000 milliseconds");
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr, stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &security, 0) ||
        !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD error = GetLastError();
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        failWindows("cannot create process output pipes", error);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    PROCESS_INFORMATION process{};
    std::wstring command = quoteArgument(wide(executable, "executable"));
    for (const std::string& argument : arguments) {
        command.push_back(L' ');
        command += quoteArgument(wide(argument, "process argument"));
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const std::wstring app = wide(executable, "executable");
    const std::wstring cwd = workingDirectory.empty()
        ? std::wstring()
        : wide(workingDirectory, "working directory");
    const BOOL created = CreateProcessW(
        app.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);
    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        failWindows("cannot start process", error);
    }
    std::string output, errorOutput;
    std::thread stdoutThread(readPipe, stdoutRead, std::ref(output));
    std::thread stderrThread(readPipe, stderrRead, std::ref(errorOutput));
    const DWORD wait = WaitForSingleObject(
        process.hProcess, static_cast<DWORD>(timeoutMilliseconds));
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(process.hProcess, INFINITE);
    } else if (wait == WAIT_FAILED) {
        const DWORD error = GetLastError();
        TerminateProcess(process.hProcess, error);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        stdoutThread.join();
        stderrThread.join();
        failWindows("cannot wait for process", error);
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        stdoutThread.join();
        stderrThread.join();
        failWindows("cannot read process exit code", error);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    stdoutThread.join();
    stderrThread.join();
    if (wait == WAIT_TIMEOUT) {
        errorOutput += "\nProcess timed out and was terminated.";
        exitCode = ERROR_TIMEOUT;
    }
    return "{\"exitCode\":" + std::to_string(exitCode) +
        ",\"stdout\":" + zplus::json::quote(output) +
        ",\"stderr\":" + zplus::json::quote(errorOutput) + "}";
#else
    (void)executable; (void)arguments; (void)workingDirectory; (void)timeoutMilliseconds;
    throw std::runtime_error("process execution API is currently supported only on Windows");
#endif
}

std::string platformName() {
#ifdef _WIN32
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string architecture() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string operatingSystemVersion() {
#ifdef _WIN32
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion = module ? reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(module, "RtlGetVersion")) : nullptr;
    if (!getVersion) failWindows("cannot determine Windows version");
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const LONG status = getVersion(&version);
    if (status < 0) throw std::runtime_error("cannot determine Windows version");
    return std::to_string(version.dwMajorVersion) + "." +
        std::to_string(version.dwMinorVersion) + "." +
        std::to_string(version.dwBuildNumber);
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string executablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) failWindows("cannot determine executable path");
        if (length < buffer.size()) {
            return utf8(std::wstring(buffer.data(), length));
        }
        if (buffer.size() >= 32768) {
            throw std::runtime_error("executable path exceeds Windows limits");
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return std::filesystem::current_path().string();
#endif
}

std::string selectFolder(const std::string& title, const std::string& initialPath) {
#ifdef _WIN32
    ComScope com;
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot open folder selection dialog");
    }
    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    const std::wstring wideTitle = wide(title, "dialog title");
    if (SUCCEEDED(result)) result = dialog->SetTitle(wideTitle.c_str());
    if (SUCCEEDED(result) && !initialPath.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> initial;
        const std::wstring initialWide = wide(initialPath, "initial directory");
        if (SUCCEEDED(SHCreateItemFromParsingName(
                initialWide.c_str(), nullptr, IID_PPV_ARGS(initial.GetAddressOf())))) {
            result = dialog->SetFolder(initial.Get());
        }
    }
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot configure folder selection dialog");
    }
    result = dialog->Show(nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "folder selection dialog failed");
    }
    Microsoft::WRL::ComPtr<IShellItem> selected;
    result = dialog->GetResult(selected.GetAddressOf());
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot get selected folder");
    }
    PWSTR path = nullptr;
    result = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(result)) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot read selected folder path");
    }
    std::string converted = utf8(path);
    CoTaskMemFree(path);
    return converted;
#else
    (void)title; (void)initialPath;
    throw std::runtime_error("folder selection dialog is supported only on Windows");
#endif
}

std::int64_t messageBox(
    const std::string& title, const std::string& message,
    const std::string& buttons, const std::string& icon
) {
#ifdef _WIN32
    UINT flags = MB_SETFOREGROUND | MB_TOPMOST;
    if (buttons == "yesNo") flags |= MB_YESNO | MB_DEFBUTTON2;
    else if (buttons == "okCancel") flags |= MB_OKCANCEL | MB_DEFBUTTON2;
    else if (buttons == "ok") flags |= MB_OK;
    else throw std::invalid_argument("message box buttons must be yesNo, okCancel, or ok");
    if (icon == "error") flags |= MB_ICONERROR;
    else if (icon == "warning") flags |= MB_ICONWARNING;
    else if (icon == "info") flags |= MB_ICONINFORMATION;
    else if (icon != "none") throw std::invalid_argument("message box icon is invalid");
    const int result = MessageBoxW(nullptr, wide(message, "message").c_str(),
        wide(title, "title").c_str(), flags);
    if (result == 0) failWindows("message box failed");
    return result;
#else
    (void)title; (void)message; (void)buttons; (void)icon;
    throw std::runtime_error("message boxes are supported only on Windows");
#endif
}

std::string registryRead(const std::string& keyPath, const std::string& valueName) {
#ifdef _WIN32
    HKEY key = checkedRegistryKey(keyPath);
    if (!key) return {};
    const std::wstring name = wide(valueName, "registry value name");
    DWORD type = 0, size = 0;
    LSTATUS status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) { RegCloseKey(key); return {}; }
    if (status != ERROR_SUCCESS) { RegCloseKey(key); failWindows("cannot query registry value", status); }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(key);
        throw std::invalid_argument("registry value is not a string");
    }
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, name.c_str(), nullptr, &type,
        reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) failWindows("cannot read registry value", status);
    return utf8(std::wstring(value.data()));
#else
    (void)keyPath; (void)valueName;
    throw std::runtime_error("registry access is supported only on Windows");
#endif
}

std::string registryValueKind(const std::string& keyPath, const std::string& valueName) {
#ifdef _WIN32
    HKEY key = checkedRegistryKey(keyPath);
    if (!key) return {};
    const std::wstring name = wide(valueName, "registry value name");
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) return {};
    if (status != ERROR_SUCCESS) failWindows("cannot query registry value kind", status);
    if (type == REG_SZ) return "string";
    if (type == REG_EXPAND_SZ) return "expandString";
    throw std::invalid_argument("registry value is not a string");
#else
    (void)keyPath; (void)valueName;
    throw std::runtime_error("registry access is supported only on Windows");
#endif
}

bool registryWrite(
    const std::string& keyPath, const std::string& valueName,
    const std::string& value, const std::string& kind
) {
#ifdef _WIN32
    const std::wstring subkey = wide(keyPath, "registry key");
    if (!isAllowedRegistryPath(keyPath) ||
        (keyPath != "Environment" && keyPath == "Software\\")) {
        throw std::invalid_argument(
            "registry writes are restricted to current-user Environment and Software keys");
    }
    HKEY key = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) failWindows("cannot create current-user registry key", status);
    const std::wstring name = wide(valueName, "registry value name");
    const std::wstring data = wide(value, "registry value");
    const DWORD type = kind == "expandString" ? REG_EXPAND_SZ :
        kind == "string" ? REG_SZ : 0;
    if (type == 0) {
        RegCloseKey(key);
        throw std::invalid_argument("registry string kind must be string or expandString");
    }
    status = RegSetValueExW(key, name.c_str(), 0, type,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) failWindows("cannot write current-user registry value", status);
    return true;
#else
    (void)keyPath; (void)valueName; (void)value; (void)kind;
    throw std::runtime_error("registry access is supported only on Windows");
#endif
}

bool registryDeleteValue(const std::string& keyPath, const std::string& valueName) {
#ifdef _WIN32
    HKEY key = nullptr;
    const std::wstring subkey = wide(keyPath, "registry key");
    if (!isAllowedRegistryPath(keyPath) ||
        (keyPath != "Environment" && keyPath == "Software\\")) {
        throw std::invalid_argument(
            "registry writes are restricted to current-user Environment and Software keys");
    }
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return false;
    if (status != ERROR_SUCCESS) failWindows("cannot open current-user registry key", status);
    const std::wstring name = wide(valueName, "registry value name");
    status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) return false;
    if (status != ERROR_SUCCESS) failWindows("cannot delete current-user registry value", status);
    return true;
#else
    (void)keyPath; (void)valueName;
    throw std::runtime_error("registry access is supported only on Windows");
#endif
}

bool createShortcut(
    const std::string& path, const std::string& target,
    const std::string& arguments, const std::string& workingDirectory
) {
#ifdef _WIN32
    ComScope com;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()));
    if (FAILED(result)) throw std::system_error(static_cast<int>(result),
        std::system_category(), "cannot create Windows shortcut");
    const std::wstring targetWide = wide(target, "shortcut target");
    const std::wstring argumentsWide = wide(arguments, "shortcut arguments");
    const std::wstring workingWide = wide(workingDirectory, "shortcut working directory");
    if (FAILED(result = link->SetPath(targetWide.c_str())) ||
        FAILED(result = link->SetArguments(argumentsWide.c_str())) ||
        FAILED(result = link->SetWorkingDirectory(workingWide.c_str()))) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot configure Windows shortcut");
    }
    Microsoft::WRL::ComPtr<IPersistFile> persisted;
    if (FAILED(result = link.As(&persisted))) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot save Windows shortcut");
    }
    const std::filesystem::path shortcutPath = checkedPath(path, "shortcut");
    std::error_code error;
    std::filesystem::create_directories(shortcutPath.parent_path(), error);
    if (error) throw std::filesystem::filesystem_error(
        "cannot create shortcut directory", shortcutPath.parent_path(), error);
    if (FAILED(result = persisted->Save(wide(path, "shortcut path").c_str(), TRUE))) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "cannot write Windows shortcut");
    }
    return true;
#else
    (void)path; (void)target; (void)arguments; (void)workingDirectory;
    throw std::runtime_error("shortcuts are supported only on Windows");
#endif
}

std::string shortcutTarget(const std::string& path) {
#ifdef _WIN32
    return utf8(readShortcutParts(path).first);
#else
    (void)path;
    throw std::runtime_error("shortcuts are supported only on Windows");
#endif
}

std::string shortcutArguments(const std::string& path) {
#ifdef _WIN32
    return utf8(readShortcutParts(path).second);
#else
    (void)path;
    throw std::runtime_error("shortcuts are supported only on Windows");
#endif
}

bool removeShortcutIfMatches(
    const std::string& path, const std::string& target, const std::string& arguments
) {
#ifdef _WIN32
    const auto [existingTarget, existingArguments] = readShortcutParts(path);
    const auto normalize = [](const std::wstring& value) {
        std::wstring normalized = value;
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        while (normalized.size() > 3 && normalized.back() == L'\\') normalized.pop_back();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
        return normalized;
    };
    if (normalize(existingTarget) != normalize(wide(target, "expected shortcut target")) ||
        existingArguments != wide(arguments, "expected shortcut arguments")) {
        return false;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(checkedPath(path, "shortcut"), error);
    if (error) throw std::filesystem::filesystem_error("cannot remove shortcut", path, error);
    return removed;
#else
    (void)path; (void)target; (void)arguments;
    throw std::runtime_error("shortcuts are supported only on Windows");
#endif
}

} // namespace zplus::installer
