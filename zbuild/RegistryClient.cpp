#include "RegistryClient.hpp"

#include "Filesystem.hpp"
#include "Hash.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef ZPM_VERSION
#define ZPM_VERSION "development"
#endif

namespace zbuild {
namespace {

std::string environmentVariable(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct UrlParts {
    std::string origin;
    std::string scheme;
    std::string host;
    std::string resource;
};

UrlParts parseUrl(const std::string& url) {
    const auto separator = url.find("://");
    if (separator == std::string::npos || url.find('#') != std::string::npos) {
        throw std::runtime_error("invalid registry URL");
    }
    const std::string scheme = lowercase(url.substr(0, separator));
    if (scheme != "https" && scheme != "http") {
        throw std::runtime_error("registry communication requires HTTPS");
    }
    const auto authorityStart = separator + 3;
    const auto resourceStart = url.find_first_of("/?", authorityStart);
    const std::string authority = lowercase(url.substr(
        authorityStart,
        resourceStart == std::string::npos
            ? std::string::npos : resourceStart - authorityStart));
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find_first_of(" \t\r\n") != std::string::npos) {
        throw std::runtime_error("invalid registry URL authority");
    }
    std::string host = authority;
    if (host.front() == '[') {
        const auto close = host.find(']');
        if (close == std::string::npos) {
            throw std::runtime_error("invalid registry URL host");
        }
        host = host.substr(1, close - 1);
    } else {
        const auto colon = host.find(':');
        if (colon != std::string::npos) host.resize(colon);
    }
    const bool loopback = host == "localhost" || host == "127.0.0.1" ||
        host == "::1";
    if (scheme != "https" && !loopback) {
        throw std::runtime_error("registry communication requires HTTPS");
    }
    std::string origin = scheme + "://" + authority;
    std::string resource = resourceStart == std::string::npos
        ? "/" : url.substr(resourceStart);
    if (!resource.empty() && resource.front() == '?') {
        resource.insert(resource.begin(), '/');
    }
    return {origin, scheme, host, std::move(resource)};
}

std::string joinUrl(const std::string& base, const std::string& path) {
    if (path.empty() || path.front() != '/' || path.find('#') != std::string::npos) {
        throw std::runtime_error("invalid registry request path");
    }
    std::string result = base;
    while (!result.empty() && result.back() == '/') result.pop_back();
    return result + path;
}

std::string resolveRedirect(const std::string& current, const std::string& location) {
    if (location.empty()) throw std::runtime_error("registry returned an empty redirect");
    if (location.find("://") != std::string::npos) {
        (void)parseUrl(location);
        return location;
    }
    const UrlParts parts = parseUrl(current);
    std::string redirected;
    if (location.front() == '/') {
        redirected = parts.origin + location;
    } else {
        std::string directory = parts.resource;
        const auto query = directory.find('?');
        if (query != std::string::npos) directory.resize(query);
        const auto slash = directory.find_last_of('/');
        if (slash != std::string::npos) directory.resize(slash + 1);
        redirected = parts.origin + directory + location;
    }
    (void)parseUrl(redirected);
    return redirected;
}

#ifdef _WIN32

std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("invalid UTF-8 in registry request");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length) != length) {
        throw std::runtime_error("cannot encode registry request");
    }
    return result;
}

std::string fromWide(const wchar_t* value, std::size_t length) {
    if (length == 0) return {};
    const int bytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("invalid response header encoding");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
            static_cast<int>(length), result.data(), bytes, nullptr, nullptr) != bytes) {
        throw std::runtime_error("cannot decode response header");
    }
    return result;
}

struct WinHandle {
    HINTERNET value = nullptr;
    ~WinHandle() {
        if (value) WinHttpCloseHandle(value);
    }
};

RegistryResponse platformRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    std::size_t maximumResponseBytes
) {
    const std::wstring wideUrl = toWide(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(),
            static_cast<DWORD>(wideUrl.size()), 0, &components)) {
        throw std::runtime_error("cannot parse registry URL");
    }
    const std::wstring host(
        components.lpszHostName, components.dwHostNameLength);
    std::wstring resource(
        components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) {
        resource.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    const std::wstring userAgent = std::wstring(L"ZPM/") + toWide(ZPM_VERSION);
    WinHandle session{WinHttpOpen(
        userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error("cannot initialize HTTPS transport");
    if (!WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 120000)) {
        throw std::runtime_error("cannot configure registry request timeouts");
    }
    WinHandle connection{WinHttpConnect(
        session.value, host.c_str(), components.nPort, 0)};
    if (!connection.value) throw std::runtime_error("cannot connect to package registry");
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS
        ? WINHTTP_FLAG_SECURE : 0;
    const std::wstring wideMethod = toWide(method);
    WinHandle request{WinHttpOpenRequest(
        connection.value, wideMethod.c_str(), resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request.value) throw std::runtime_error("cannot create registry request");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
            &redirectPolicy, sizeof(redirectPolicy))) {
        throw std::runtime_error("cannot disable automatic registry redirects");
    }
    std::wstring requestHeaders = L"Accept: application/json\r\n";
    for (const auto& [name, value] : headers) {
        if (name.find_first_of("\r\n:") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos) {
            throw std::runtime_error("invalid registry request header");
        }
        requestHeaders += toWide(name + ": " + value + "\r\n");
    }
    if (!WinHttpAddRequestHeaders(request.value, requestHeaders.c_str(),
            static_cast<DWORD>(requestHeaders.size()), WINHTTP_ADDREQ_FLAG_ADD)) {
        throw std::runtime_error("cannot configure registry request headers");
    }
    if (body.size() > std::numeric_limits<DWORD>::max()) {
        throw std::length_error("registry request body exceeds the transport limit");
    }
    const DWORD bodySize = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body.empty() ? WINHTTP_NO_REQUEST_DATA
                         : const_cast<char*>(body.data()),
            bodySize, bodySize, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("registry HTTPS request failed");
    }
    RegistryResponse response;
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("registry returned no HTTP status");
    }
    response.status = static_cast<long>(status);
    DWORD locationSize = 0;
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION,
        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &locationSize,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locationSize >= sizeof(wchar_t)) {
        std::vector<wchar_t> location(locationSize / sizeof(wchar_t));
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &locationSize,
                WINHTTP_NO_HEADER_INDEX)) {
            const auto length = std::wcslen(location.data());
            response.headers["location"] = fromWide(location.data(), length);
        }
    }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            throw std::runtime_error("failed reading registry response");
        }
        if (available == 0) break;
        if (response.body.size() > maximumResponseBytes ||
            available > maximumResponseBytes - response.body.size()) {
            throw std::length_error("registry response exceeds the size limit");
        }
        const auto start = response.body.size();
        response.body.resize(start + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.value, response.body.data() + start,
                available, &received)) {
            throw std::runtime_error("failed reading registry response");
        }
        if (received == 0) {
            throw std::runtime_error("registry response ended unexpectedly");
        }
        response.body.resize(start + received);
    }
    return response;
}

#else

struct CurlResponse {
    RegistryResponse response;
    std::size_t maximumBytes = 0;
    bool tooLarge = false;
};

size_t receiveBody(char* data, size_t size, size_t count, void* context) {
    auto& output = *static_cast<CurlResponse*>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        output.tooLarge = true;
        return 0;
    }
    const std::size_t bytes = size * count;
    if (output.response.body.size() > output.maximumBytes ||
        bytes > output.maximumBytes - output.response.body.size()) {
        output.tooLarge = true;
        return 0;
    }
    output.response.body.append(data, bytes);
    return bytes;
}

size_t receiveHeader(char* data, size_t size, size_t count, void* context) {
    auto& output = *static_cast<CurlResponse*>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return 0;
    }
    const std::size_t bytes = size * count;
    const std::string_view line(data, bytes);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
        std::string name(line.substr(0, colon));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::size_t valueStart = colon + 1;
        while (valueStart < line.size() &&
               (line[valueStart] == ' ' || line[valueStart] == '\t')) {
            ++valueStart;
        }
        std::size_t valueEnd = line.find_first_of("\r\n", valueStart);
        if (valueEnd == std::string_view::npos) valueEnd = line.size();
        output.response.headers[name] =
            std::string(line.substr(valueStart, valueEnd - valueStart));
    }
    return bytes;
}

RegistryResponse platformRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    std::size_t maximumResponseBytes
) {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("cannot initialize HTTPS transport");
        }
    });
    CURL* handle = curl_easy_init();
    if (!handle) throw std::runtime_error("cannot initialize HTTPS request");
    CurlResponse output;
    output.maximumBytes = maximumResponseBytes;
    curl_slist* requestHeaders = nullptr;
    auto cleanup = [&] {
        if (requestHeaders) curl_slist_free_all(requestHeaders);
        curl_easy_cleanup(handle);
    };
    for (const auto& [name, value] : headers) {
        if (name.find_first_of("\r\n:") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos) {
            cleanup();
            throw std::runtime_error("invalid registry request header");
        }
        curl_slist* updatedHeaders = curl_slist_append(
            requestHeaders, (name + ": " + value).c_str());
        if (!updatedHeaders) {
            cleanup();
            throw std::runtime_error("cannot allocate registry request headers");
        }
        requestHeaders = updatedHeaders;
    }
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, requestHeaders);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receiveBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, receiveHeader);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &output);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 120000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    const std::string userAgent = std::string("ZPM/") + ZPM_VERSION;
    curl_easy_setopt(handle, CURLOPT_USERAGENT, userAgent.c_str());
    if (method == "POST" || method == "PUT") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
            static_cast<curl_off_t>(body.size()));
    }
    const CURLcode result = curl_easy_perform(handle);
    if (output.tooLarge) {
        cleanup();
        throw std::length_error("registry response exceeds the size limit");
    }
    if (result != CURLE_OK) {
        const std::string message = curl_easy_strerror(result);
        cleanup();
        throw std::runtime_error("registry HTTPS request failed: " + message);
    }
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &output.response.status);
    cleanup();
    return output.response;
}

#endif

std::filesystem::path credentialFile() {
#ifdef _WIN32
    std::string directory = environmentVariable("LOCALAPPDATA");
    if (directory.empty()) directory = environmentVariable("APPDATA");
    if (directory.empty()) throw std::runtime_error(
        "cannot determine the current user's application-data directory");
    const auto root = pathFromUtf8(directory) / "ZPlus" / "zpm";
#else
    std::string directory = environmentVariable("XDG_CONFIG_HOME");
    if (directory.empty()) directory = environmentVariable("HOME");
    if (directory.empty()) throw std::runtime_error(
        "cannot determine the current user's configuration directory");
    auto root = pathFromUtf8(directory);
    if (environmentVariable("XDG_CONFIG_HOME").empty()) root /= ".config";
    root /= "zpm";
#endif
    std::string base;
    const std::string configured = environmentVariable("ZPM_REGISTRY_URL");
    if (configured.empty()) base = "https://zpluse.vercel.app/api/registry";
    else base = configured;
    return root / pathFromUtf8("credentials-" + sha256(base) + ".dat");
}

#ifndef _WIN32

void writePrivateFile(
    const std::filesystem::path& path,
    std::string_view contents
) {
    const auto directory = path.parent_path();
    std::filesystem::create_directories(directory);
    if (::chmod(directory.string().c_str(), S_IRWXU) != 0) {
        throw std::runtime_error("cannot secure ZPM credential directory");
    }
    const auto temporary = path.string() + ".tmp-" + std::to_string(::getpid());
    const int descriptor = ::open(
        temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (descriptor < 0) throw std::runtime_error("cannot create ZPM credential file");
    std::size_t written = 0;
    try {
        while (written < contents.size()) {
            const ssize_t count = ::write(descriptor,
                contents.data() + written, contents.size() - written);
            if (count <= 0) throw std::runtime_error("cannot write ZPM credential file");
            written += static_cast<std::size_t>(count);
        }
        if (::fsync(descriptor) != 0) {
            throw std::runtime_error("cannot flush ZPM credential file");
        }
        if (::close(descriptor) != 0) {
            throw std::runtime_error("cannot close ZPM credential file");
        }
        if (::rename(temporary.c_str(), path.string().c_str()) != 0) {
            throw std::runtime_error("cannot install ZPM credential file");
        }
    } catch (...) {
        ::close(descriptor);
        ::unlink(temporary.c_str());
        throw;
    }
}

#endif

std::string protectToken(std::string_view token) {
#ifdef _WIN32
    DATA_BLOB input{
        static_cast<DWORD>(token.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))
    };
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"ZPM registry session", nullptr, nullptr,
            nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        throw std::runtime_error("Windows could not protect the ZPM session token");
    }
    std::string protectedValue(
        reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return protectedValue;
#else
    return std::string(token);
#endif
}

std::string unprotectToken(std::string_view value) {
#ifdef _WIN32
    DATA_BLOB input{
        static_cast<DWORD>(value.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))
    };
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        throw std::runtime_error(
            "saved ZPM session token cannot be decrypted; run 'zpm login'");
    }
    std::string token(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return token;
#else
    return std::string(value);
#endif
}

} // namespace

std::string registryApiBaseUrl() {
    std::string base = environmentVariable("ZPM_REGISTRY_URL");
    if (base.empty()) base = "https://zpluse.vercel.app/api/registry";
    while (!base.empty() && base.back() == '/') base.pop_back();
    const UrlParts parsed = parseUrl(base);
    if (parsed.resource != "/api/registry") {
        throw std::runtime_error(
            "ZPM_REGISTRY_URL must point to the /api/registry endpoint");
    }
    return base;
}

RegistryResponse registryRequest(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    std::size_t maximumResponseBytes
) {
    const std::string initialUrl = joinUrl(registryApiBaseUrl(), path);
    (void)parseUrl(initialUrl);
    std::string url = initialUrl;
    auto requestHeaders = headers;
    UrlParts initialParts = parseUrl(initialUrl);
    for (unsigned redirects = 0; redirects <= 5; ++redirects) {
        RegistryResponse response = platformRequest(
            method, url, body, requestHeaders, maximumResponseBytes);
        const auto location = response.headers.find("location");
        const bool redirect = response.status == 301 || response.status == 302 ||
            response.status == 303 || response.status == 307 ||
            response.status == 308;
        if (!redirect) return response;
        if (method != "GET" || redirects == 5 || location == response.headers.end()) {
            throw std::runtime_error("registry returned an unsupported redirect");
        }
        const std::string next = resolveRedirect(url, location->second);
        const UrlParts nextParts = parseUrl(next);
        if (nextParts.origin != initialParts.origin) {
            requestHeaders.erase("Authorization");
            requestHeaders.erase("authorization");
        }
        url = next;
    }
    throw std::runtime_error("registry exceeded the redirect limit");
}

std::string loadRegistryToken() {
    const auto path = credentialFile();
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) throw std::filesystem::filesystem_error(
            "cannot inspect ZPM credential file", path, error);
        return {};
    }
#ifndef _WIN32
    const auto permissions = std::filesystem::status(path, error).permissions();
    if (error) throw std::filesystem::filesystem_error(
        "cannot inspect ZPM credential permissions", path, error);
    constexpr auto publicPermissions =
        std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    if ((permissions & publicPermissions) != std::filesystem::perms::none) {
        throw std::runtime_error(
            "ZPM credential file permissions are too broad; run 'zpm logout' and login again");
    }
#endif
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read the saved ZPM session token");
    std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (bytes.size() > 4096) throw std::runtime_error("saved ZPM session token is invalid");
    const std::string token = unprotectToken(bytes);
    if (!std::all_of(token.begin(), token.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        })) {
        throw std::runtime_error("saved ZPM session token is invalid");
    }
    return token;
}

void storeRegistryToken(const std::string& token) {
    if (token.size() < 32 || token.size() > 128 ||
        !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        })) {
        throw std::runtime_error("registry returned an invalid session token");
    }
    const auto path = credentialFile();
    const std::string protectedToken = protectToken(token);
#ifdef _WIN32
    std::filesystem::create_directories(path.parent_path());
    writeFileAtomically(path, protectedToken);
#else
    writePrivateFile(path, protectedToken);
#endif
}

void removeRegistryToken() {
    const auto path = credentialFile();
    std::error_code error;
    if (!std::filesystem::remove(path, error) && error) {
        throw std::filesystem::filesystem_error(
            "cannot remove saved ZPM session token", path, error);
    }
}

} // namespace zbuild
