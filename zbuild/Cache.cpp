#include "Cache.hpp"

#include "Filesystem.hpp"
#include "Hash.hpp"

#include "../runtime/Json.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace zbuild {
namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read '" + pathToUtf8(path) + "'");
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("failed reading '" + pathToUtf8(path) + "'");
    }
    return content.str();
}

std::vector<std::string> parseStringArray(const std::string& contents) {
    const zplus::json::Value root = zplus::json::parse(contents);
    const auto* values = std::get_if<zplus::json::Value::Array>(&root.data);
    if (values == nullptr) throw std::runtime_error("cache record must be a JSON array");
    std::vector<std::string> result;
    result.reserve(values->size());
    for (const auto& value : *values) {
        const auto* string = std::get_if<std::string>(&value.data);
        if (string == nullptr) {
            throw std::runtime_error("cache record fields must be JSON strings");
        }
        result.push_back(*string);
    }
    return result;
}

} // namespace

ProjectBuildLock::ProjectBuildLock(const std::filesystem::path& path) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            throw std::runtime_error(
                "another ZPBuild process is using this project's build outputs");
        }
        throw std::runtime_error(
            "cannot acquire build lock '" + pathToUtf8(path) +
            "' (Windows error " + std::to_string(error) + ")");
    }
    handle_ = handle;
#else
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (descriptor_ < 0) {
        throw std::runtime_error("cannot open build lock '" + pathToUtf8(path) + "'");
    }
    if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        ::close(descriptor_);
        descriptor_ = -1;
        if (error != EWOULDBLOCK && error != EAGAIN) {
            throw std::runtime_error(
                "cannot acquire build lock '" + pathToUtf8(path) +
                "' (system error " + std::to_string(error) + ")");
        }
        throw std::runtime_error(
            "another ZPBuild process is using this project's build outputs");
    }
#endif
}

ProjectBuildLock::~ProjectBuildLock() {
#ifdef _WIN32
    const HANDLE handle = static_cast<HANDLE>(handle_);
    if (handle != nullptr) CloseHandle(handle);
#else
    if (descriptor_ >= 0) {
        flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
#endif
}

CacheRecord loadCacheRecord(const std::filesystem::path& path) {
    const auto fields = parseStringArray(readText(path));
    if (fields.size() != 4) {
        throw std::runtime_error(
            "cache record has " + std::to_string(fields.size()) + " fields, expected 4");
    }
    if (fields[0] != "ZPBCACHE1") {
        throw std::runtime_error(
            "cache record has an unknown format marker '" + fields[0] + "'");
    }
    return {fields[1], fields[2], pathFromUtf8(fields[3])};
}

void saveCacheRecord(
    const std::filesystem::path& path,
    const std::string& signature,
    const std::filesystem::path& output
) {
    const std::string contents = "[" +
        zplus::json::quote("ZPBCACHE1") + "," +
        zplus::json::quote(signature) + "," +
        zplus::json::quote(sha256File(output)) + "," +
        zplus::json::quote(pathToUtf8(output)) + "]\n";
    writeFileAtomically(path, contents);
}

CacheStatistics loadCacheStatistics(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return {};
    std::istringstream input(readText(path));
    CacheStatistics result;
    std::string key;
    if (!(input >> key >> result.hits) || key != "hits" ||
        !(input >> key >> result.misses) || key != "misses" ||
        !(input >> key >> result.rebuilds) || key != "rebuilds") {
        throw std::runtime_error(
            "invalid cache statistics file: '" + pathToUtf8(path) + "'");
    }
    std::string trailing;
    if (input >> trailing) {
        throw std::runtime_error(
            "trailing data in cache statistics file: '" + pathToUtf8(path) + "'");
    }
    return result;
}

void updateCacheStatistics(
    const std::filesystem::path& directory,
    const CacheStatistics& delta
) {
    std::filesystem::create_directories(directory);
    const auto path = directory / "stats.txt";
    CacheStatistics total = loadCacheStatistics(path);
    total.hits += delta.hits;
    total.misses += delta.misses;
    total.rebuilds += delta.rebuilds;
    std::ostringstream contents;
    contents << "hits " << total.hits << '\n'
             << "misses " << total.misses << '\n'
             << "rebuilds " << total.rebuilds << '\n';
    writeFileIfChangedAtomically(path, contents.str());
}

bool isCacheRecordValid(
    const std::filesystem::path& recordPath,
    const std::filesystem::path& projectRoot
) {
    const std::string contents = readText(recordPath);
    CacheRecord record;
    try {
        const auto fields = parseStringArray(contents);
        if (fields.size() != 4 || fields[0] != "ZPBCACHE1") return false;
        record = {fields[1], fields[2], pathFromUtf8(fields[3])};
    } catch (const std::exception&) {
        return false;
    }
    if (!record.output.is_absolute()) return false;
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(projectRoot, error);
    if (error) {
        throw std::runtime_error(
            "cannot resolve project root while verifying cache: " + error.message());
    }
    const auto output = std::filesystem::weakly_canonical(record.output, error);
    if (error == std::errc::no_such_file_or_directory) return false;
    if (error) {
        throw std::runtime_error(
            "cannot resolve cached output '" + pathToUtf8(record.output) +
            "': " + error.message());
    }
    const auto relative = output.lexically_relative(canonicalRoot);
    if (relative.empty() || *relative.begin() == ".." ||
        !std::filesystem::is_regular_file(output)) {
        return false;
    }
    return record.outputHash == sha256File(output);
}

} // namespace zbuild
