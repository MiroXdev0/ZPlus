#include "Http.hpp"

#include "Base64.hpp"
#include "Json.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <limits>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket invalidSocket = -1;
#endif

namespace zplus::http {
namespace {

constexpr std::size_t maximumHeaderBytes = 16 * 1024;
constexpr std::size_t maximumHeaderCount = 100;
constexpr std::size_t maximumChunkCount = 65536;
constexpr std::size_t maximumChunkMetadataBytes = 16 * 1024 * 1024;
constexpr std::size_t maximumBodyBytes = 1024 * 1024;
constexpr std::size_t maximumConfigurableBodyBytes = 16 * 1024 * 1024;
constexpr std::size_t maximumServerResponseBytes = 16 * 1024 * 1024;
constexpr std::size_t maximumClientResponseBytes = 4 * 1024 * 1024;
constexpr std::size_t maximumQueuedClients = 64;
constexpr std::int64_t defaultTimeoutMilliseconds = 30000;

void closeSocket(NativeSocket socket) noexcept {
    if (socket == invalidSocket) return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#else
    shutdown(socket, SHUT_RDWR);
    close(socket);
#endif
}

void shutdownSocket(NativeSocket socket) noexcept {
    if (socket == invalidSocket) return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("failed to initialize Windows sockets");
        }
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

SocketRuntime& socketRuntime() {
    static SocketRuntime runtime;
    return runtime;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return std::string(value.substr(first, last - first + 1));
}

bool containsToken(std::string_view values, std::string_view wanted) {
    std::size_t start = 0;
    while (start <= values.size()) {
        const auto separator = values.find(',', start);
        const auto item = trim(values.substr(start,
            separator == std::string_view::npos
                ? values.size() - start : separator - start));
        if (lower(item) == lower(std::string(wanted))) return true;
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return false;
}

bool validCorsOrigin(std::string_view origin) {
    if (origin.empty() || origin == "*") return true;
    if (std::any_of(origin.begin(), origin.end(), [](unsigned char ch) {
            return ch <= 0x20 || ch >= 0x7f || ch == ',' || ch == '\\';
        })) return false;
    const auto separator = origin.find("://");
    if (separator == std::string_view::npos) return false;
    const std::string scheme = lower(std::string(origin.substr(0, separator)));
    if (scheme != "http" && scheme != "https") return false;
    const auto authorityStart = separator + 3;
    return authorityStart < origin.size() &&
        origin.find_first_of("/?#", authorityStart) == std::string_view::npos &&
        origin.find('@', authorityStart) == std::string_view::npos;
}

void addVaryToken(std::map<std::string, std::string>& headers, std::string_view token) {
    auto vary = headers.find("vary");
    if (vary == headers.end()) {
        headers.emplace("vary", std::string(token));
    } else if (!containsToken(vary->second, token)) {
        vary->second += ", " + std::string(token);
    }
}

bool validHeaderName(std::string_view name) {
    if (name.empty()) return false;
    for (const unsigned char ch : name) {
        const bool alphaNumeric = (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!(alphaNumeric || ch == '!' || ch == '#' || ch == '$' ||
              ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
              ch == '-' || ch == '.' || ch == '^' || ch == '_' || ch == '`' ||
              ch == '|' || ch == '~')) {
            return false;
        }
    }
    return true;
}

bool validHeaderValue(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch == '\t' || (ch >= 0x20 && ch != 0x7f);
    });
}

std::string decodeComponent(std::string_view value, bool plusAsSpace) {
    std::string decoded;
    decoded.reserve(value.size());
    const auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (i + 2 >= value.size()) {
                throw std::invalid_argument("malformed percent escape in HTTP value");
            }
            const int high = hexValue(value[i + 1]);
            const int lowValue = hexValue(value[i + 2]);
            if (high < 0 || lowValue < 0) {
                throw std::invalid_argument("malformed percent escape in HTTP value");
            }
            decoded.push_back(static_cast<char>((high << 4) | lowValue));
            i += 2;
        } else if (plusAsSpace && value[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::vector<std::string_view> pathSegments(std::string_view path) {
    std::vector<std::string_view> segments;
    if (path.empty() || path.front() != '/') return segments;
    if (path == "/") return segments;
    std::size_t start = 1;
    while (start <= path.size()) {
        const auto separator = path.find('/', start);
        if (separator == std::string_view::npos) {
            segments.push_back(path.substr(start));
            break;
        }
        segments.push_back(path.substr(start, separator - start));
        start = separator + 1;
    }
    return segments;
}

std::string reasonPhrase(std::int64_t status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 415: return "Unsupported Media Type";
        case 417: return "Expectation Failed";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "Response";
    }
}

struct Request {
    std::int64_t id = 0;
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string asJson() const {
        json::Value::Object headerValues;
        for (const auto& [name, value] : headers) {
            headerValues.emplace_back(name, json::Value{value});
        }
        json::Value::Object fields;
        fields.emplace_back("id", json::Value{static_cast<double>(id)});
        fields.emplace_back("method", json::Value{method});
        fields.emplace_back("target", json::Value{target});
        fields.emplace_back("path", json::Value{path});
        fields.emplace_back("query", json::Value{query});
        fields.emplace_back("version", json::Value{version});
        fields.emplace_back("headers", json::Value{std::move(headerValues)});
        bool textBody = true;
        try {
            (void)json::quote(body);
        } catch (const std::invalid_argument&) {
            textBody = false;
        }
        fields.emplace_back("bodyEncoding", json::Value{textBody ? "utf8" : "base64"});
        if (textBody) {
            fields.emplace_back("body", json::Value{body});
        } else {
            fields.emplace_back("body", json::Value{""});
            fields.emplace_back("bodyBase64", json::Value{encoding::base64Encode(body)});
        }
        return json::serialize(json::Value{std::move(fields)});
    }
};

struct Response {
    std::int64_t status = 200;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct PendingRequest {
    Request request;
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Response> response;
    bool cancelled = false;
};

bool sendAll(NativeSocket socket, std::string_view bytes) {
#ifdef SO_NOSIGPIPE
    int noSignal = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
        reinterpret_cast<const char*>(&noSignal), sizeof(noSignal));
#endif
    while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
        constexpr int sendFlags = MSG_NOSIGNAL;
#else
        constexpr int sendFlags = 0;
#endif
#ifdef _WIN32
        const int sent = send(socket, bytes.data(),
            static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX)), sendFlags);
#else
        const auto sent = send(socket, bytes.data(), bytes.size(), sendFlags);
#endif
        if (sent <= 0) return false;
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

bool receiveMore(NativeSocket socket, std::string& buffer) {
    char chunk[8192];
#ifdef _WIN32
    const int received = recv(socket, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
    const auto received = recv(socket, chunk, sizeof(chunk), 0);
#endif
    if (received <= 0) return false;
    buffer.append(chunk, static_cast<std::size_t>(received));
    return true;
}

bool readRequest(
    NativeSocket socket,
    std::string& buffer,
    Request& request,
    std::int64_t requestId,
    std::size_t bodyLimit,
    std::int64_t& errorStatus
) {
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > maximumHeaderBytes) {
            errorStatus = 431;
            return false;
        }
        if (!receiveMore(socket, buffer)) return false;
    }
    if (headerEnd > maximumHeaderBytes) {
        errorStatus = 431;
        return false;
    }

    const std::string_view headerBlock(buffer.data(), headerEnd);
    const auto firstLineEnd = headerBlock.find("\r\n");
    const std::string_view requestLine = firstLineEnd == std::string_view::npos
        ? headerBlock : headerBlock.substr(0, firstLineEnd);
    const auto firstSpace = requestLine.find(' ');
    const auto secondSpace = firstSpace == std::string_view::npos
        ? std::string_view::npos : requestLine.find(' ', firstSpace + 1);
    if (firstSpace == 0 || firstSpace == std::string_view::npos ||
        secondSpace == std::string_view::npos || secondSpace <= firstSpace + 1 ||
        requestLine.find(' ', secondSpace + 1) != std::string_view::npos) {
        errorStatus = 400;
        return false;
    }
    if (!validHeaderName(requestLine.substr(0, firstSpace))) {
        errorStatus = 400;
        return false;
    }
    request.method = std::string(requestLine.substr(0, firstSpace));
    request.target = std::string(requestLine.substr(
        firstSpace + 1, secondSpace - firstSpace - 1));
    request.version = std::string(requestLine.substr(secondSpace + 1));
    if (request.version != "HTTP/1.1" && request.version != "HTTP/1.0") {
        errorStatus = 400;
        return false;
    }
    if (request.target.empty() || request.target.front() != '/' ||
        request.target.find('#') != std::string::npos ||
        std::any_of(request.target.begin(), request.target.end(), [](unsigned char ch) {
            return ch < 0x21 || ch == 0x7f || ch == '\\';
        })) {
        errorStatus = 400;
        return false;
    }
    const auto queryStart = request.target.find('?');
    request.path = request.target.substr(0, queryStart);
    if (queryStart != std::string::npos) {
        request.query = request.target.substr(queryStart + 1);
    }

    std::optional<std::size_t> contentLength;
    std::size_t headerCount = 0;
    std::size_t lineStart = firstLineEnd == std::string_view::npos
        ? headerBlock.size() : firstLineEnd + 2;
    while (lineStart < headerBlock.size()) {
        if (++headerCount > maximumHeaderCount) {
            errorStatus = 431;
            return false;
        }
        const auto lineEnd = headerBlock.find("\r\n", lineStart);
        const auto line = headerBlock.substr(
            lineStart, lineEnd == std::string_view::npos
                ? headerBlock.size() - lineStart : lineEnd - lineStart);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || !validHeaderName(line.substr(0, colon))) {
            errorStatus = 400;
            return false;
        }
        const std::string name = lower(std::string(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (!validHeaderValue(value)) {
            errorStatus = 400;
            return false;
        }
        if (name == "content-length") {
            if (contentLength) {
                errorStatus = 400;
                return false;
            }
            std::size_t length = 0;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), length);
            if (error != std::errc{} || end != value.data() + value.size()) {
                errorStatus = 400;
                return false;
            }
            contentLength = length;
        } else if (name == "transfer-encoding") {
            errorStatus = 501;
            return false;
        }
        if (request.headers.contains(name)) {
            if (name == "host") {
                errorStatus = 400;
                return false;
            }
            request.headers[name] += ", " + value;
        } else {
            request.headers.emplace(name, value);
        }
        if (lineEnd == std::string_view::npos) break;
        lineStart = lineEnd + 2;
    }
    if (request.version == "HTTP/1.1" &&
        (!request.headers.contains("host") || request.headers.at("host").empty())) {
        errorStatus = 400;
        return false;
    }
    if (request.headers.contains("expect")) {
        errorStatus = 417;
        return false;
    }
    const std::size_t bodyLength = contentLength.value_or(0);
    if (bodyLength > bodyLimit) {
        errorStatus = 413;
        return false;
    }
    const std::size_t consumed = headerEnd + 4;
    while (buffer.size() - consumed < bodyLength) {
        if (!receiveMore(socket, buffer)) {
            errorStatus = 400;
            return false;
        }
    }
    request.body.assign(buffer.data() + consumed, bodyLength);
    buffer.erase(0, consumed + bodyLength);
    request.id = requestId;
    return true;
}

bool sendResponse(
    NativeSocket socket,
    const Response& response,
    bool keepAlive,
    bool headRequest = false
) {
    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
        reasonPhrase(response.status) + "\r\n";
    for (const auto& [name, value] : response.headers) {
        wire += name + ": " + value + "\r\n";
    }
    if (response.status != 204 && response.status != 304) {
        wire += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    }
    wire += keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    if (!headRequest) wire += response.body;
    return sendAll(socket, wire);
}

Response errorResponse(std::int64_t status) {
    Response response;
    response.status = status;
    response.headers.emplace("Content-Type", "text/plain; charset=utf-8");
    response.body = reasonPhrase(status) + "\n";
    return response;
}

std::map<std::string, std::string> parseHeaderObject(
    const std::string& source,
    bool forbidManagedHeaders
) {
    const json::Value value = json::parse(source);
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) throw std::invalid_argument("HTTP response headers must be a JSON object");
    std::map<std::string, std::string> headers;
    for (const auto& [rawName, rawValue] : *object) {
        const auto* headerValue = std::get_if<std::string>(&rawValue.data);
        if (!headerValue || !validHeaderName(rawName) ||
            !validHeaderValue(*headerValue)) {
            throw std::invalid_argument("invalid HTTP response header");
        }
        const std::string name = lower(rawName);
        if (forbidManagedHeaders &&
            (name == "content-length" || name == "connection" ||
             name == "transfer-encoding")) {
            throw std::invalid_argument(
                "content-length and connection headers are managed by zhttp");
        }
        if (!headers.emplace(name, *headerValue).second) {
            throw std::invalid_argument("duplicate HTTP response header");
        }
    }
    return headers;
}

bool waitForSocket(NativeSocket socket, bool write, std::int64_t timeoutMilliseconds) {
    fd_set descriptorSet;
    FD_ZERO(&descriptorSet);
    FD_SET(socket, &descriptorSet);
    timeval timeout{
        static_cast<long>(timeoutMilliseconds / 1000),
        static_cast<long>((timeoutMilliseconds % 1000) * 1000)};
#ifdef _WIN32
    const int ready = select(0, write ? nullptr : &descriptorSet,
        write ? &descriptorSet : nullptr, nullptr, &timeout);
#else
    const int ready = select(socket + 1, write ? nullptr : &descriptorSet,
        write ? &descriptorSet : nullptr, nullptr, &timeout);
#endif
    return ready > 0;
}

bool connectWithTimeout(
    NativeSocket socket,
    const sockaddr* address,
    std::size_t addressLength,
    std::int64_t timeoutMilliseconds
) {
#ifdef _WIN32
    u_long nonBlocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0) return false;
    const int result = connect(socket, address, static_cast<int>(addressLength));
    if (result != 0 && WSAGetLastError() != WSAEWOULDBLOCK &&
        WSAGetLastError() != WSAEINPROGRESS) {
        nonBlocking = 0;
        ioctlsocket(socket, FIONBIO, &nonBlocking);
        return false;
    }
#else
    const int previousFlags = fcntl(socket, F_GETFL, 0);
    if (previousFlags < 0 || fcntl(socket, F_SETFL, previousFlags | O_NONBLOCK) < 0) {
        return false;
    }
    const int result = connect(socket, address, static_cast<socklen_t>(addressLength));
    if (result != 0 && errno != EINPROGRESS) {
        fcntl(socket, F_SETFL, previousFlags);
        return false;
    }
#endif
    const bool ready = waitForSocket(socket, true, timeoutMilliseconds);
    int socketError = 0;
#ifdef _WIN32
    int errorLength = sizeof(socketError);
    const bool connected = ready &&
        getsockopt(socket, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&socketError), &errorLength) == 0 &&
        socketError == 0;
    nonBlocking = 0;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
#else
    socklen_t errorLength = sizeof(socketError);
    const bool connected = ready &&
        getsockopt(socket, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) == 0 &&
        socketError == 0;
    fcntl(socket, F_SETFL, previousFlags);
#endif
    return connected;
}

std::string readLineFromSocket(
    NativeSocket socket,
    std::string& buffer,
    std::size_t maximumLineBytes
) {
    for (;;) {
        const auto lineEnd = buffer.find("\r\n");
        if (lineEnd != std::string::npos) {
            if (lineEnd > maximumLineBytes) {
                throw std::runtime_error("HTTP response line exceeds the configured limit");
            }
            std::string line = buffer.substr(0, lineEnd);
            buffer.erase(0, lineEnd + 2);
            return line;
        }
        if (buffer.size() > maximumLineBytes || !receiveMore(socket, buffer)) {
            throw std::runtime_error("incomplete HTTP response line");
        }
    }
}

std::string decodeChunkedResponse(
    NativeSocket socket,
    std::string& buffer
) {
    std::string body;
    std::size_t trailerBytes = 0;
    std::size_t trailerCount = 0;
    std::size_t chunkCount = 0;
    std::size_t chunkMetadataBytes = 0;
    for (;;) {
        const std::string line = readLineFromSocket(socket, buffer, maximumHeaderBytes);
        chunkMetadataBytes += line.size() + 2;
        if (chunkMetadataBytes > maximumChunkMetadataBytes) {
            throw std::runtime_error("HTTP chunk framing exceeds the configured limit");
        }
        const auto extension = line.find(';');
        const std::string_view sizeText(line.data(),
            extension == std::string::npos ? line.size() : extension);
        if (sizeText.empty() ||
            (extension != std::string::npos &&
                std::any_of(line.begin() + static_cast<std::ptrdiff_t>(extension + 1),
                    line.end(), [](unsigned char ch) {
                        return ch < 0x20 || ch == 0x7f;
                    }))) {
            throw std::runtime_error("malformed chunked HTTP response");
        }
        std::size_t chunkSize = 0;
        const auto [end, error] = std::from_chars(
            sizeText.data(), sizeText.data() + sizeText.size(), chunkSize, 16);
        if (error != std::errc{} || end != sizeText.data() + sizeText.size()) {
            throw std::runtime_error("malformed chunked HTTP response");
        }
        if (chunkSize == 0) {
            for (;;) {
                const std::string trailer =
                    readLineFromSocket(socket, buffer, maximumHeaderBytes);
                trailerBytes += trailer.size() + 2;
                if (trailerBytes > maximumHeaderBytes) {
                    throw std::runtime_error("HTTP response trailers exceed the configured limit");
                }
                if (++chunkCount > maximumChunkCount) {
                    throw std::runtime_error("HTTP response contains too many chunks");
                }
                if (trailer.empty()) return body;
                if (++trailerCount > maximumHeaderCount) {
                    throw std::runtime_error("HTTP response contains too many trailers");
                }
                const auto colon = trailer.find(':');
                const std::string_view trailerValue = colon == std::string::npos
                    ? std::string_view{} : std::string_view(trailer).substr(colon + 1);
                if (colon == std::string::npos ||
                    !validHeaderName(std::string_view(trailer).substr(0, colon)) ||
                    !validHeaderValue(trim(trailerValue))) {
                    throw std::runtime_error("malformed HTTP response trailer");
                }
                const std::string name = lower(trailer.substr(0, colon));
                if (name == "content-length" || name == "transfer-encoding" ||
                    name == "trailer" || name == "host") {
                    throw std::runtime_error("forbidden HTTP response trailer field");
                }
            }
        }
        if (chunkSize > maximumClientResponseBytes - body.size()) {
            throw std::runtime_error("HTTP response exceeds the configured size limit");
        }
        while (buffer.size() < chunkSize + 2) {
            if (!receiveMore(socket, buffer)) {
                throw std::runtime_error("incomplete chunked HTTP response");
            }
        }
        if (buffer.compare(chunkSize, 2, "\r\n") != 0) {
            throw std::runtime_error("malformed chunk terminator in HTTP response");
        }
        chunkMetadataBytes += 2;
        if (chunkMetadataBytes > maximumChunkMetadataBytes) {
            throw std::runtime_error("HTTP chunk framing exceeds the configured limit");
        }
        body.append(buffer.data(), chunkSize);
        buffer.erase(0, chunkSize + 2);
    }
}

std::string parseClientUrl(
    const std::string& url,
    std::string& host,
    std::string& port,
    std::string& target
) {
    constexpr std::string_view scheme = "http://";
    if (url.rfind(scheme, 0) != 0) {
        throw std::invalid_argument("zhttp supports only http:// URLs; HTTPS is not available");
    }
    const std::size_t authorityStart = scheme.size();
    const std::size_t authorityEnd = url.find_first_of("/?#", authorityStart);
    const std::string authority = url.substr(
        authorityStart, authorityEnd == std::string::npos
            ? std::string::npos : authorityEnd - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        throw std::invalid_argument("invalid HTTP URL authority");
    }
    std::string hostPort = authority;
    if (hostPort.front() == '[') {
        const auto closing = hostPort.find(']');
        if (closing == std::string::npos) throw std::invalid_argument("invalid IPv6 URL host");
        host = hostPort.substr(1, closing - 1);
        if (closing + 1 < hostPort.size()) {
            if (hostPort[closing + 1] != ':') {
                throw std::invalid_argument("invalid HTTP URL authority");
            }
            port = hostPort.substr(closing + 2);
        }
    } else {
        const auto colon = hostPort.rfind(':');
        if (colon != std::string::npos) {
            if (hostPort.find(':') != colon) {
                throw std::invalid_argument("IPv6 hosts must be enclosed in brackets");
            }
            host = hostPort.substr(0, colon);
            port = hostPort.substr(colon + 1);
        } else {
            host = hostPort;
        }
    }
    if (host.empty()) throw std::invalid_argument("HTTP URL host must not be empty");
    if (port.empty()) port = "80";
    unsigned int numericPort = 0;
    const auto [portEnd, portError] = std::from_chars(
        port.data(), port.data() + port.size(), numericPort);
    if (portError != std::errc{} || portEnd != port.data() + port.size() ||
        numericPort == 0 || numericPort > 65535) {
        throw std::invalid_argument("HTTP URL port must be between 1 and 65535");
    }
    if (authorityEnd == std::string::npos) {
        target = "/";
    } else if (url[authorityEnd] == '?') {
        target = "/" + url.substr(authorityEnd);
    } else if (url[authorityEnd] == '#') {
        throw std::invalid_argument("HTTP URL fragments are not sent to servers");
    } else {
        target = url.substr(authorityEnd);
        if (target.find('#') != std::string::npos) {
            throw std::invalid_argument("HTTP URL fragments are not sent to servers");
        }
    }
    return authority;
}

std::string performClientRequest(
    std::string method,
    std::string url,
    const std::string& headersJson,
    std::string body,
    std::int64_t timeoutMilliseconds
) {
    if (timeoutMilliseconds <= 0 || timeoutMilliseconds > 300000) {
        throw std::invalid_argument("HTTP timeout must be between 1 and 300000 milliseconds");
    }
    if (method.empty() || !std::all_of(method.begin(), method.end(), [](unsigned char ch) {
            return std::isalpha(ch) != 0;
        })) {
        throw std::invalid_argument("HTTP method must contain only letters");
    }
    method = lower(std::move(method));
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    std::string host, port, target;
    const std::string authority = parseClientUrl(url, host, port, target);
    auto headers = parseHeaderObject(headersJson, false);
    for (const std::string& managed : {"host", "content-length", "connection",
             "transfer-encoding"}) {
        if (headers.contains(managed)) {
            throw std::invalid_argument("zhttp manages the " + managed + " request header");
        }
    }

    (void)socketRuntime();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0) {
        throw std::runtime_error("could not resolve HTTP client host");
    }
    NativeSocket connection = invalidSocket;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        connection = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (connection == invalidSocket) continue;
        if (connectWithTimeout(connection, address->ai_addr,
                static_cast<std::size_t>(address->ai_addrlen), timeoutMilliseconds)) {
            break;
        }
        closeSocket(connection);
        connection = invalidSocket;
    }
    freeaddrinfo(addresses);
    if (connection == invalidSocket) {
        throw std::runtime_error("HTTP connection failed or timed out");
    }
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(timeoutMilliseconds);
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{
        static_cast<long>(timeoutMilliseconds / 1000),
        static_cast<long>((timeoutMilliseconds % 1000) * 1000)};
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    std::string request = method + " " + target + " HTTP/1.1\r\nHost: " +
        authority + "\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n";
    for (const auto& [name, value] : headers) {
        request += name + ": " + value + "\r\n";
    }
    request += "\r\n";
    request += body;
    if (!sendAll(connection, request)) {
        closeSocket(connection);
        throw std::runtime_error("failed to send HTTP request");
    }

    std::string buffer;
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > maximumHeaderBytes || !receiveMore(connection, buffer)) {
            closeSocket(connection);
            throw std::runtime_error("incomplete or oversized HTTP response headers");
        }
    }
    const std::string_view headerBlock(buffer.data(), headerEnd);
    const auto statusLineEnd = headerBlock.find("\r\n");
    const std::string_view statusLine = statusLineEnd == std::string_view::npos
        ? headerBlock : headerBlock.substr(0, statusLineEnd);
    if (statusLine.size() < 12 ||
        (statusLine.substr(0, 8) != "HTTP/1.0" &&
            statusLine.substr(0, 8) != "HTTP/1.1") ||
        statusLine[8] != ' ' ||
        (statusLine.size() > 12 && statusLine[12] != ' ') ||
        std::any_of(statusLine.begin() + 12, statusLine.end(),
            [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; })) {
        closeSocket(connection);
        throw std::runtime_error("malformed HTTP response status line");
    }
    std::int64_t status = 0;
    const auto [statusEnd, statusError] = std::from_chars(
        statusLine.data() + 9, statusLine.data() + 12, status);
    if (statusError != std::errc{} || statusEnd != statusLine.data() + 12 ||
        status < 100 || status > 599) {
        closeSocket(connection);
        throw std::runtime_error("invalid HTTP response status");
    }
    std::map<std::string, std::string> responseHeaders;
    std::optional<std::size_t> contentLength;
    std::size_t lineStart = statusLineEnd == std::string_view::npos
        ? headerBlock.size() : statusLineEnd + 2;
    std::size_t responseHeaderCount = 0;
    while (lineStart < headerBlock.size()) {
        if (++responseHeaderCount > maximumHeaderCount) {
            closeSocket(connection);
            throw std::runtime_error("HTTP response contains too many headers");
        }
        const auto lineEnd = headerBlock.find("\r\n", lineStart);
        const auto line = headerBlock.substr(lineStart,
            lineEnd == std::string_view::npos
                ? headerBlock.size() - lineStart : lineEnd - lineStart);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || !validHeaderName(line.substr(0, colon))) {
            closeSocket(connection);
            throw std::runtime_error("malformed HTTP response header");
        }
        const std::string name = lower(std::string(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (!validHeaderValue(value)) {
            closeSocket(connection);
            throw std::runtime_error("invalid HTTP response header value");
        }
        if (name == "content-length") {
            if (contentLength) {
                closeSocket(connection);
                throw std::runtime_error("duplicate HTTP response Content-Length");
            }
            std::size_t length = 0;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), length);
            if (error != std::errc{} || end != value.data() + value.size()) {
                closeSocket(connection);
                throw std::runtime_error("invalid HTTP response Content-Length");
            }
            contentLength = length;
        }
        if (name == "transfer-encoding" && responseHeaders.contains(name)) {
            closeSocket(connection);
            throw std::runtime_error("duplicate HTTP response Transfer-Encoding");
        }
        responseHeaders[name] = responseHeaders.contains(name)
            ? responseHeaders[name] + ", " + value : value;
        if (lineEnd == std::string_view::npos) break;
        lineStart = lineEnd + 2;
    }
    buffer.erase(0, headerEnd + 4);
    std::string responseBody;
    const auto transferEncoding = responseHeaders.find("transfer-encoding");
    if (transferEncoding != responseHeaders.end() && contentLength) {
        closeSocket(connection);
        throw std::runtime_error(
            "HTTP response contains both Transfer-Encoding and Content-Length");
    }
    if (status == 204 || status == 304 || method == "HEAD") {
        responseBody.clear();
    } else if (transferEncoding != responseHeaders.end() &&
        lower(trim(transferEncoding->second)) == "chunked") {
        responseBody = decodeChunkedResponse(connection, buffer);
    } else if (transferEncoding != responseHeaders.end()) {
        closeSocket(connection);
        throw std::runtime_error("unsupported HTTP response Transfer-Encoding");
    } else if (contentLength) {
        if (*contentLength > maximumClientResponseBytes) {
            closeSocket(connection);
            throw std::runtime_error("HTTP response exceeds the configured size limit");
        }
        while (buffer.size() < *contentLength) {
            if (!receiveMore(connection, buffer)) {
                closeSocket(connection);
                throw std::runtime_error("incomplete HTTP response body");
            }
        }
        responseBody.assign(buffer.data(), *contentLength);
    } else {
        responseBody = std::move(buffer);
        if (responseBody.size() > maximumClientResponseBytes) {
            closeSocket(connection);
            throw std::runtime_error("HTTP response exceeds the configured size limit");
        }
        while (receiveMore(connection, buffer)) {
            if (buffer.size() > maximumClientResponseBytes - responseBody.size()) {
                closeSocket(connection);
                throw std::runtime_error("HTTP response exceeds the configured size limit");
            }
            responseBody += buffer;
            buffer.clear();
        }
    }
    closeSocket(connection);
    json::Value::Object responseHeaderValues;
    for (auto& [name, value] : responseHeaders) {
        responseHeaderValues.emplace_back(name, json::Value{std::move(value)});
    }
    json::Value::Object result;
    result.emplace_back("status", json::Value{static_cast<double>(status)});
    result.emplace_back("headers", json::Value{std::move(responseHeaderValues)});
    bool textBody = true;
    try {
        (void)json::quote(responseBody);
    } catch (const std::invalid_argument&) {
        textBody = false;
    }
    result.emplace_back("bodyEncoding", json::Value{textBody ? "utf8" : "base64"});
    if (textBody) {
        result.emplace_back("body", json::Value{std::move(responseBody)});
    } else {
        result.emplace_back("body", json::Value{""});
        result.emplace_back("bodyBase64", json::Value{encoding::base64Encode(responseBody)});
    }
    return json::serialize(json::Value{std::move(result)});
}

class Server {
public:
    Server(std::string address, std::int64_t port)
        : address_(std::move(address)), requestedPort_(port) {
        if (address_.empty()) throw std::invalid_argument("HTTP server address must not be empty");
        if (port < 0 || port > 65535) {
            throw std::invalid_argument("HTTP server port must be between 0 and 65535");
        }
    }

    ~Server() { stop(); }

    bool start() {
        std::lock_guard lifecycleLock(lifecycleMutex_);
        if (stopped_) return false;
        if (running_) return true;
        (void)socketRuntime();
        NativeSocket listener = bindListener();
        listener_.store(listener);
        running_.store(true);
        const unsigned int detected = std::thread::hardware_concurrency();
        const std::size_t workerCount = std::clamp<std::size_t>(
            detected == 0 ? 2 : detected, 2, 8);
        try {
            workers_.reserve(workerCount);
            activeClients_.reserve(workerCount);
            for (std::size_t i = 0; i < workerCount; ++i) {
                workers_.emplace_back([this] { workerLoop(); });
            }
            acceptThread_ = std::thread([this] { acceptLoop(); });
        } catch (...) {
            running_.store(false);
            closeSocket(listener_.exchange(invalidSocket));
            clientsReady_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable()) worker.join();
            }
            workers_.clear();
            throw;
        }
        return true;
    }

    bool running() const noexcept { return running_.load(); }

    std::int64_t port() const noexcept { return actualPort_.load(); }

    void configure(std::int64_t bodyLimit, std::int64_t timeoutMilliseconds) {
        if (bodyLimit < 1 ||
            static_cast<std::uint64_t>(bodyLimit) > maximumConfigurableBodyBytes) {
            throw std::invalid_argument(
                "HTTP request body limit must be between 1 and 16777216 bytes");
        }
        if (timeoutMilliseconds < 100 || timeoutMilliseconds > 300000) {
            throw std::invalid_argument(
                "HTTP timeout must be between 100 and 300000 milliseconds");
        }
        maximumBodyBytes_.store(static_cast<std::size_t>(bodyLimit));
        timeoutMilliseconds_.store(timeoutMilliseconds);
    }

    void configureCors(
        std::string origin,
        std::string methods,
        std::string headers
    ) {
        for (const std::string* value : {&origin, &methods, &headers}) {
            if (!validHeaderValue(*value)) {
                throw std::invalid_argument("CORS values must not contain line breaks");
            }
        }
        if (!validCorsOrigin(origin)) {
            throw std::invalid_argument("CORS origin must be an HTTP(S) origin or '*'");
        }
        const auto validateList = [](std::string_view values) {
            std::size_t start = 0;
            while (start <= values.size()) {
                const auto separator = values.find(',', start);
                const auto item = trim(values.substr(start,
                    separator == std::string_view::npos
                        ? values.size() - start : separator - start));
                if (!validHeaderName(item)) {
                    throw std::invalid_argument("CORS method or header list is invalid");
                }
                if (separator == std::string_view::npos) break;
                start = separator + 1;
            }
        };
        if (!methods.empty()) validateList(methods);
        if (!headers.empty()) validateList(headers);
        std::lock_guard lock(corsMutex_);
        corsOrigin_ = std::move(origin);
        corsMethods_ = std::move(methods);
        corsHeaders_ = std::move(headers);
    }

    std::string next(std::int64_t timeoutMilliseconds) {
        if (timeoutMilliseconds < 0 || timeoutMilliseconds > 300000) {
            throw std::invalid_argument(
                "HTTP request timeout must be between 0 and 300000 milliseconds");
        }
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMilliseconds);
        std::unique_lock lock(requestsMutex_);
        for (;;) {
            if (requests_.empty()) {
                if (!requestsReady_.wait_until(lock, deadline,
                        [this] { return !requests_.empty() || !running_; }) ||
                    requests_.empty()) {
                    return {};
                }
            }
            auto pending = std::move(requests_.front());
            requests_.pop_front();
            lock.unlock();
            bool cancelled = false;
            {
                std::lock_guard pendingLock(pending->mutex);
                cancelled = pending->cancelled;
            }
            if (!cancelled) return pending->request.asJson();
            lock.lock();
        }
    }

    bool respond(
        std::int64_t requestId,
        std::int64_t status,
        const std::string& headersJson,
        std::string body
    ) {
        if (status < 200 || status > 599) {
            throw std::invalid_argument("HTTP status must be between 200 and 599");
        }
        if ((status == 204 || status == 304) && !body.empty()) {
            throw std::invalid_argument("204 and 304 HTTP responses cannot include a body");
        }
        if (body.size() > maximumServerResponseBytes) {
            throw std::length_error("HTTP response body exceeds the 16 MiB limit");
        }
        auto response = std::make_shared<Response>();
        response->status = status;
        response->headers = parseHeaderObject(headersJson, true);
        response->body = std::move(body);
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard lock(requestsMutex_);
            const auto found = pending_.find(requestId);
            if (found == pending_.end()) return false;
            pending = found->second;
        }
        applyCors(pending->request, *response);
        {
            std::lock_guard lock(pending->mutex);
            if (pending->cancelled || pending->response) return false;
            pending->response = std::move(*response);
        }
        pending->ready.notify_one();
        return true;
    }

    void stop() noexcept {
        std::lock_guard lifecycleLock(lifecycleMutex_);
        stopped_ = true;
        if (!running_.exchange(false)) return;
        closeSocket(listener_.exchange(invalidSocket));
        clientsReady_.notify_all();
        requestsReady_.notify_all();
        std::vector<std::shared_ptr<PendingRequest>> pendingRequests;
        {
            std::lock_guard lock(requestsMutex_);
            for (const auto& [id, request] : pending_) {
                (void)id;
                pendingRequests.push_back(request);
            }
        }
        for (const auto& request : pendingRequests) {
            {
                std::lock_guard lock(request->mutex);
                request->cancelled = true;
            }
            request->ready.notify_all();
        }
        if (acceptThread_.joinable()) acceptThread_.join();
        {
            std::lock_guard lock(clientsMutex_);
            for (NativeSocket client : clients_) closeSocket(client);
            clients_.clear();
            for (NativeSocket client : activeClients_) shutdownSocket(client);
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
    }

private:
    bool originAllowed(std::string_view origin) const {
        return !corsOrigin_.empty() &&
            (corsOrigin_ == "*" || origin == corsOrigin_);
    }

    void applyCors(const Request& request, Response& response) {
        std::lock_guard lock(corsMutex_);
        if (corsOrigin_.empty()) return;
        addVaryToken(response.headers, "Origin");
        const auto origin = request.headers.find("origin");
        if (origin == request.headers.end() || !originAllowed(origin->second)) return;
        response.headers.try_emplace("access-control-allow-origin", corsOrigin_);
        if (!corsMethods_.empty()) {
            response.headers.try_emplace("access-control-allow-methods", corsMethods_);
        }
        if (!corsHeaders_.empty()) {
            response.headers.try_emplace("access-control-allow-headers", corsHeaders_);
        }
    }

    std::optional<Response> preflightResponse(const Request& request) {
        if (request.method != "OPTIONS") return std::nullopt;
        const auto origin = request.headers.find("origin");
        const auto requestedMethod =
            request.headers.find("access-control-request-method");
        if (origin == request.headers.end() ||
            requestedMethod == request.headers.end()) return std::nullopt;

        std::lock_guard lock(corsMutex_);
        if (corsOrigin_.empty()) return std::nullopt;
        Response response;
        const bool allowedOrigin = originAllowed(origin->second);
        const std::string method = trim(requestedMethod->second);
        const bool allowedMethod = method.find(',') == std::string::npos &&
            validHeaderName(method) && containsToken(corsMethods_, method);
        bool allowedHeaders = true;
        const auto requestedHeaders =
            request.headers.find("access-control-request-headers");
        if (requestedHeaders != request.headers.end()) {
            std::size_t start = 0;
            while (start <= requestedHeaders->second.size()) {
                const auto separator = requestedHeaders->second.find(',', start);
                const auto name = trim(std::string_view(requestedHeaders->second).substr(
                    start, separator == std::string::npos
                        ? requestedHeaders->second.size() - start
                        : separator - start));
                if (!validHeaderName(name) || !containsToken(corsHeaders_, name)) {
                    allowedHeaders = false;
                    break;
                }
                if (separator == std::string::npos) break;
                start = separator + 1;
            }
        }
        response.status = allowedOrigin && allowedMethod && allowedHeaders ? 204 : 403;
        addVaryToken(response.headers, "Origin");
        addVaryToken(response.headers, "Access-Control-Request-Method");
        addVaryToken(response.headers, "Access-Control-Request-Headers");
        if (response.status == 204) {
            response.headers.emplace("access-control-allow-origin", corsOrigin_);
            response.headers.emplace("access-control-allow-methods", corsMethods_);
            if (!corsHeaders_.empty()) {
                response.headers.emplace("access-control-allow-headers", corsHeaders_);
            }
        }
        return response;
    }

    NativeSocket bindListener() {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* addresses = nullptr;
        const std::string service = std::to_string(requestedPort_);
        const char* host = address_ == "*" ? nullptr : address_.c_str();
        if (getaddrinfo(host, service.c_str(), &hints, &addresses) != 0) {
            throw std::runtime_error("could not resolve HTTP server bind address");
        }
        NativeSocket listener = invalidSocket;
        for (addrinfo* address = addresses; address; address = address->ai_next) {
            listener = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (listener == invalidSocket) continue;
            int enabled = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            if (bind(listener, address->ai_addr,
                    static_cast<int>(address->ai_addrlen)) == 0 &&
                listen(listener, static_cast<int>(maximumQueuedClients)) == 0) {
                break;
            }
            closeSocket(listener);
            listener = invalidSocket;
        }
        freeaddrinfo(addresses);
        if (listener == invalidSocket) {
            throw std::runtime_error("could not bind HTTP server socket");
        }
        sockaddr_storage localAddress{};
#ifdef _WIN32
        int addressLength = sizeof(localAddress);
#else
        socklen_t addressLength = sizeof(localAddress);
#endif
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&localAddress),
                &addressLength) == 0) {
            const auto* internetAddress = reinterpret_cast<const sockaddr_in*>(&localAddress);
            if (internetAddress->sin_family == AF_INET) {
                actualPort_.store(ntohs(internetAddress->sin_port));
            } else {
                const auto* internet6Address =
                    reinterpret_cast<const sockaddr_in6*>(&localAddress);
                actualPort_.store(ntohs(internet6Address->sin6_port));
            }
        }
        return listener;
    }

    void acceptLoop() noexcept {
        while (running_) {
            const NativeSocket listener = listener_.load();
            if (listener == invalidSocket) break;
            NativeSocket client = accept(listener, nullptr, nullptr);
            if (client == invalidSocket) {
                if (!running_) break;
                continue;
            }
#ifdef _WIN32
            DWORD timeout = static_cast<DWORD>(timeoutMilliseconds_.load());
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
            const auto timeoutMs = timeoutMilliseconds_.load();
            timeval timeout{
                static_cast<long>(timeoutMs / 1000),
                static_cast<long>((timeoutMs % 1000) * 1000)};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
            std::unique_lock lock(clientsMutex_);
            if (clients_.size() >= maximumQueuedClients || !running_) {
                lock.unlock();
                sendResponse(client, errorResponse(503), false);
                closeSocket(client);
                continue;
            }
            clients_.push_back(client);
            lock.unlock();
            clientsReady_.notify_one();
        }
    }

    void workerLoop() noexcept {
        for (;;) {
            NativeSocket client = invalidSocket;
            {
                std::unique_lock lock(clientsMutex_);
                clientsReady_.wait(lock, [this] {
                    return !clients_.empty() || !running_;
                });
                if (clients_.empty() && !running_) return;
                client = clients_.front();
                clients_.pop_front();
                activeClients_.push_back(client);
            }
            handleClient(client);
            {
                std::lock_guard lock(clientsMutex_);
                const auto active = std::find(activeClients_.begin(),
                    activeClients_.end(), client);
                if (active != activeClients_.end()) activeClients_.erase(active);
            }
            closeSocket(client);
        }
    }

    void handleClient(NativeSocket client) noexcept {
        std::string buffer;
        while (running_) {
            Request request;
            std::int64_t errorStatus = 400;
            const std::int64_t id = nextRequestId_.fetch_add(1);
            if (!readRequest(client, buffer, request, id,
                    maximumBodyBytes_.load(), errorStatus)) {
                if (!buffer.empty()) sendResponse(client, errorResponse(errorStatus), false);
                break;
            }
            if (const auto preflight = preflightResponse(request)) {
                if (!sendResponse(client, *preflight, false)) break;
                break;
            }
            const auto method = request.method;
            if (method != "GET" && method != "POST" && method != "PUT" &&
                method != "PATCH" && method != "DELETE" && method != "OPTIONS" &&
                method != "HEAD") {
                sendResponse(client, errorResponse(501), false);
                break;
            }
            const auto connection = request.headers.find("connection");
            const bool keepAlive = request.version == "HTTP/1.1"
                ? connection == request.headers.end() ||
                    !containsToken(connection->second, "close")
                : connection != request.headers.end() &&
                    containsToken(connection->second, "keep-alive") &&
                    !containsToken(connection->second, "close");
            auto pending = std::make_shared<PendingRequest>();
            pending->request = std::move(request);
            {
                std::lock_guard lock(requestsMutex_);
                if (requests_.size() >= maximumQueuedClients) {
                    sendResponse(client, errorResponse(503), false);
                    break;
                }
                requests_.push_back(pending);
                pending_.emplace(id, pending);
            }
            requestsReady_.notify_one();
            std::optional<Response> response;
            {
                std::unique_lock lock(pending->mutex);
                if (pending->ready.wait_for(lock,
                        std::chrono::milliseconds(timeoutMilliseconds_.load()), [&] {
                        return pending->response.has_value() || pending->cancelled;
                    })) {
                    response = std::move(pending->response);
                }
                if (!response) pending->cancelled = true;
            }
            {
                std::lock_guard lock(requestsMutex_);
                pending_.erase(id);
            }
            if (!response) {
                sendResponse(client, errorResponse(running_ ? 504 : 503), false);
                break;
            }
            if (!sendResponse(client, *response, keepAlive, method == "HEAD") ||
                !keepAlive) break;
        }
    }

    std::string address_;
    std::int64_t requestedPort_;
    std::atomic<std::int64_t> actualPort_{0};
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> maximumBodyBytes_{maximumBodyBytes};
    std::atomic<std::int64_t> timeoutMilliseconds_{defaultTimeoutMilliseconds};
    std::atomic<NativeSocket> listener_{invalidSocket};
    std::atomic<std::int64_t> nextRequestId_{1};
    std::mutex lifecycleMutex_;
    bool stopped_ = false;
    std::mutex clientsMutex_;
    std::condition_variable clientsReady_;
    std::deque<NativeSocket> clients_;
    std::vector<NativeSocket> activeClients_;
    std::vector<std::thread> workers_;
    std::thread acceptThread_;
    std::mutex requestsMutex_;
    std::condition_variable requestsReady_;
    std::deque<std::shared_ptr<PendingRequest>> requests_;
    std::unordered_map<std::int64_t, std::shared_ptr<PendingRequest>> pending_;
    std::mutex corsMutex_;
    std::string corsOrigin_;
    std::string corsMethods_;
    std::string corsHeaders_;
};

std::mutex serversMutex;
std::unordered_map<std::int64_t, std::shared_ptr<Server>> servers;
std::atomic<std::int64_t> nextServerHandle{1};

std::shared_ptr<Server> getServer(std::int64_t handle) {
    std::lock_guard lock(serversMutex);
    const auto found = servers.find(handle);
    if (found == servers.end()) {
        throw std::invalid_argument("invalid HTTP server handle");
    }
    return found->second;
}

} // namespace

std::int64_t createServer(std::string address, std::int64_t port) {
    const std::int64_t handle = nextServerHandle.fetch_add(1);
    if (handle <= 0) throw std::overflow_error("HTTP server handle space exhausted");
    auto server = std::make_shared<Server>(std::move(address), port);
    std::lock_guard lock(serversMutex);
    servers.emplace(handle, std::move(server));
    return handle;
}

bool startServer(std::int64_t handle) {
    return getServer(handle)->start();
}

bool serverRunning(std::int64_t handle) {
    return getServer(handle)->running();
}

std::int64_t serverPort(std::int64_t handle) {
    return getServer(handle)->port();
}

std::string nextRequest(std::int64_t handle, std::int64_t timeoutMilliseconds) {
    return getServer(handle)->next(timeoutMilliseconds);
}

bool respond(
    std::int64_t handle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    std::string body
) {
    return getServer(handle)->respond(requestId, status, headersJson, std::move(body));
}

bool respondBinary(
    std::int64_t handle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    const std::string& bodyBase64
) {
    const auto maximumEncodedBytes =
        ((maximumServerResponseBytes + 2) / 3) * 4;
    if (bodyBase64.size() > maximumEncodedBytes) {
        throw std::length_error("HTTP response body exceeds the 16 MiB limit");
    }
    return respond(handle, requestId, status, headersJson,
        encoding::base64Decode(bodyBase64));
}

void stopServer(std::int64_t handle) {
    std::shared_ptr<Server> server;
    {
        std::lock_guard lock(serversMutex);
        const auto found = servers.find(handle);
        if (found == servers.end()) throw std::invalid_argument("invalid HTTP server handle");
        server = std::move(found->second);
        servers.erase(found);
    }
    server->stop();
}

void shutdownServers() noexcept {
    for (;;) {
        std::shared_ptr<Server> server;
        {
            std::lock_guard lock(serversMutex);
            if (servers.empty()) return;
            auto found = servers.begin();
            server = std::move(found->second);
            servers.erase(found);
        }
        server->stop();
    }
}

void configureServer(
    std::int64_t handle,
    std::int64_t maximumRequestBodyBytes,
    std::int64_t timeoutMilliseconds
) {
    getServer(handle)->configure(maximumRequestBodyBytes, timeoutMilliseconds);
}

void configureCors(
    std::int64_t handle,
    std::string origin,
    std::string methods,
    std::string headers
) {
    getServer(handle)->configureCors(
        std::move(origin), std::move(methods), std::move(headers));
}

std::string routeParams(const std::string& path, const std::string& pattern) {
    if (path.empty() || pattern.empty() || path.front() != '/' ||
        pattern.front() != '/') {
        return {};
    }
    const auto pathParts = pathSegments(path);
    const auto patternParts = pathSegments(pattern);
    if (pathParts.size() != patternParts.size()) return {};
    json::Value::Object parameters;
    for (std::size_t i = 0; i < pathParts.size(); ++i) {
        const std::string_view expected = patternParts[i];
        const std::string actual = decodeComponent(pathParts[i], false);
        if (expected.size() >= 3 && expected.front() == '{' && expected.back() == '}') {
            const std::string name(expected.substr(1, expected.size() - 2));
            if (name.empty() ||
                !(std::isalpha(static_cast<unsigned char>(name.front())) ||
                  name.front() == '_') ||
                !std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
                    return std::isalnum(ch) || ch == '_';
                })) {
                throw std::invalid_argument("route parameter names must be identifiers");
            }
            if (std::any_of(parameters.begin(), parameters.end(),
                    [&name](const auto& entry) { return entry.first == name; })) {
                throw std::invalid_argument("route parameter names must be unique");
            }
            parameters.emplace_back(name, json::Value{actual});
        } else if (actual != expected) {
            return {};
        }
    }
    return json::serialize(json::Value{std::move(parameters)});
}

std::string queryValue(const std::string& query, const std::string& name) {
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto separator = query.find('&', start);
        const auto pair = std::string_view(query).substr(
            start, separator == std::string::npos
                ? query.size() - start : separator - start);
        const auto equals = pair.find('=');
        const auto key = decodeComponent(pair.substr(0, equals), true);
        if (key == name) {
            return equals == std::string_view::npos
                ? std::string{} : decodeComponent(pair.substr(equals + 1), true);
        }
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return {};
}

std::string requestHeader(const std::string& requestJson, const std::string& name) {
    if (!validHeaderName(name)) {
        throw std::invalid_argument("invalid HTTP header name");
    }
    const auto request = json::parse(requestJson);
    const auto* fields = std::get_if<json::Value::Object>(&request.data);
    if (!fields) throw std::invalid_argument("HTTP request data must be a JSON object");
    const auto headers = std::find_if(fields->begin(), fields->end(),
        [](const auto& field) { return field.first == "headers"; });
    if (headers == fields->end()) return {};
    const auto* values = std::get_if<json::Value::Object>(&headers->second.data);
    if (!values) throw std::invalid_argument("HTTP request headers must be a JSON object");
    const std::string wanted = lower(name);
    const auto value = std::find_if(values->begin(), values->end(),
        [&wanted](const auto& field) { return field.first == wanted; });
    if (value == values->end()) return {};
    const auto* text = std::get_if<std::string>(&value->second.data);
    if (!text) throw std::invalid_argument("HTTP request header value must be a string");
    return *text;
}

std::string clientRequest(
    std::string method,
    std::string url,
    const std::string& headersJson,
    std::string body,
    std::int64_t timeoutMilliseconds
) {
    return performClientRequest(
        std::move(method), std::move(url), headersJson,
        std::move(body), timeoutMilliseconds);
}

std::string clientRequestBinary(
    std::string method,
    std::string url,
    const std::string& headersJson,
    const std::string& bodyBase64,
    std::int64_t timeoutMilliseconds
) {
    return clientRequest(std::move(method), std::move(url), headersJson,
        encoding::base64Decode(bodyBase64), timeoutMilliseconds);
}

} // namespace zplus::http
