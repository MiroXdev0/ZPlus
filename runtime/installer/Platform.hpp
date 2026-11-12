#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zplus::installer {

bool copyFile(const std::string& source, const std::string& destination, bool overwrite);
bool moveFile(const std::string& source, const std::string& destination, bool overwrite);
bool removeFile(const std::string& path);
bool scheduleFileRemovalOnReboot(const std::string& path);
bool removeEmptyDirectory(const std::string& path);
std::int64_t availableSpace(const std::string& path);
std::string sha256File(const std::string& path);
std::string runProcess(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    const std::string& workingDirectory,
    std::int64_t timeoutMilliseconds
);
std::string platformName();
std::string architecture();
std::string operatingSystemVersion();
std::string executablePath();
std::string selectFolder(const std::string& title, const std::string& initialPath);
std::int64_t messageBox(
    const std::string& title,
    const std::string& message,
    const std::string& buttons,
    const std::string& icon
);
std::string registryRead(const std::string& keyPath, const std::string& valueName);
std::string registryValueKind(const std::string& keyPath, const std::string& valueName);
bool registryWrite(
    const std::string& keyPath,
    const std::string& valueName,
    const std::string& value,
    const std::string& kind
);
bool registryDeleteValue(const std::string& keyPath, const std::string& valueName);
bool createShortcut(
    const std::string& path,
    const std::string& target,
    const std::string& arguments,
    const std::string& workingDirectory
);
std::string shortcutTarget(const std::string& path);
std::string shortcutArguments(const std::string& path);
bool removeShortcutIfMatches(
    const std::string& path,
    const std::string& target,
    const std::string& arguments
);

} // namespace zplus::installer
