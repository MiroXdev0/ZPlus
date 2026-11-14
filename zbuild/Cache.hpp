#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace zbuild {

class ProjectBuildLock {
public:
    explicit ProjectBuildLock(const std::filesystem::path& path);
    ProjectBuildLock(const ProjectBuildLock&) = delete;
    ProjectBuildLock& operator=(const ProjectBuildLock&) = delete;
    ~ProjectBuildLock();

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};

struct CacheRecord {
    std::string signature;
    std::string outputHash;
    std::filesystem::path output;
};

struct CacheStatistics {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t rebuilds = 0;
};

CacheRecord loadCacheRecord(const std::filesystem::path& path);
void saveCacheRecord(
    const std::filesystem::path& path,
    const std::string& signature,
    const std::filesystem::path& output
);
CacheStatistics loadCacheStatistics(const std::filesystem::path& path);
void updateCacheStatistics(
    const std::filesystem::path& directory,
    const CacheStatistics& delta
);
bool isCacheRecordValid(
    const std::filesystem::path& recordPath,
    const std::filesystem::path& projectRoot
);

} // namespace zbuild
