#pragma once

#include <cstdint>
#include <string>

namespace zplus::sqlite {

std::int64_t openDatabase(const std::string& path);
void closeDatabase(std::int64_t handle);
void shutdownDatabases() noexcept;
std::int64_t execute(
    std::int64_t handle,
    const std::string& sql,
    const std::string& parametersJson
);
std::string query(
    std::int64_t handle,
    const std::string& sql,
    const std::string& parametersJson
);
std::int64_t changes(std::int64_t handle);

} // namespace zplus::sqlite
