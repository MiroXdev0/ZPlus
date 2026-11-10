#pragma once

#include <cstdint>
#include <string>

namespace zplus::http {

std::int64_t createServer(std::string address, std::int64_t port);
bool startServer(std::int64_t handle);
bool serverRunning(std::int64_t handle);
std::int64_t serverPort(std::int64_t handle);
std::string nextRequest(std::int64_t handle, std::int64_t timeoutMilliseconds);
bool respond(
    std::int64_t handle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    std::string body
);
void stopServer(std::int64_t handle);
void shutdownServers() noexcept;
void configureServer(
    std::int64_t handle,
    std::int64_t maximumBodyBytes,
    std::int64_t timeoutMilliseconds
);
void configureCors(
    std::int64_t handle,
    std::string origin,
    std::string methods,
    std::string headers
);
std::string routeParams(const std::string& path, const std::string& pattern);
std::string queryValue(const std::string& query, const std::string& name);
std::string requestHeader(const std::string& requestJson, const std::string& name);
std::string clientRequest(
    std::string method,
    std::string url,
    const std::string& headersJson,
    std::string body,
    std::int64_t timeoutMilliseconds
);

} // namespace zplus::http
