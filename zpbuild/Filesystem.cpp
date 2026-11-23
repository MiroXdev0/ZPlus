#include "Filesystem.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zbuild {
namespace {

std::filesystem::path temporaryPathFor(const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{0};
    auto temporary = destination;
    temporary += ".zbuild-tmp-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    return temporary;
}

} // namespace

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path pathFromUtf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(encoded);
}

void writeFileAtomically(
    const std::filesystem::path& path,
    std::string_view contents
) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = temporaryPathFor(path);
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error(
                    "cannot create temporary file '" + pathToUtf8(temporary) + "'");
            }
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error(
                    "failed writing temporary file '" + pathToUtf8(temporary) + "'");
            }
        }
#ifdef _WIN32
        const BOOL replaced = ReplaceFileW(
            path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr);
        const DWORD replaceError = replaced ? ERROR_SUCCESS : GetLastError();
        const BOOL moved = replaced || MoveFileExW(
            temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!moved) {
            const DWORD moveError = GetLastError();
            throw std::runtime_error(
                "cannot publish '" + pathToUtf8(path) + "' from '" +
                pathToUtf8(temporary) + "' (ReplaceFile error " +
                std::to_string(replaceError) + ", MoveFileEx error " +
                std::to_string(moveError) + ")");
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) {
            throw std::runtime_error(
                "cannot publish '" + pathToUtf8(path) + "': " + error.message());
        }
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void replaceFileAtomically(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
) {
#ifdef _WIN32
    const BOOL replaced = ReplaceFileW(
        destination.c_str(), source.c_str(), nullptr, 0, nullptr, nullptr);
    const DWORD replaceError = replaced ? ERROR_SUCCESS : GetLastError();
    const BOOL moved = replaced || MoveFileExW(
        source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!moved) {
        const DWORD moveError = GetLastError();
        throw std::runtime_error(
            "cannot publish '" + pathToUtf8(destination) + "' from '" +
            pathToUtf8(source) + "' (ReplaceFile error " +
            std::to_string(replaceError) + ", MoveFileEx error " +
            std::to_string(moveError) + ")");
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw std::runtime_error(
            "cannot publish '" + pathToUtf8(destination) + "': " + error.message());
    }
#endif
}

void writeFileIfChangedAtomically(
    const std::filesystem::path& path,
    std::string_view contents
) {
    std::ifstream existing(path, std::ios::binary);
    if (existing) {
        std::string current(
            (std::istreambuf_iterator<char>(existing)),
            std::istreambuf_iterator<char>());
        if (existing.bad()) {
            throw std::runtime_error(
                "failed reading existing file '" + pathToUtf8(path) + "'");
        }
        if (current == contents) return;
        existing.close();
    } else if (std::filesystem::exists(path)) {
        throw std::runtime_error(
            "cannot read existing file '" + pathToUtf8(path) + "'");
    }
    writeFileAtomically(path, contents);
}

} // namespace zbuild
