#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace zbuild {

struct RegistryResponse {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

std::string registryApiBaseUrl();
RegistryResponse registryRequest(
    const std::string& method,
    const std::string& path,
    const std::string& body = {},
    const std::map<std::string, std::string>& headers = {},
    std::size_t maximumResponseBytes = 1024 * 1024
);
std::string loadRegistryToken();
void storeRegistryToken(const std::string& token);
void removeRegistryToken();

} // namespace zbuild
