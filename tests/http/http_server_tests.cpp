#include "Http.hpp"
#include "Json.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using TestSocket = SOCKET;
constexpr TestSocket invalidTestSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestSocket = int;
constexpr TestSocket invalidTestSocket = -1;
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Function>
void requireThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

template<typename Exception, typename Function>
void requireThrowsType(Function&& function, const char* message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void closeSocket(TestSocket socket) {
    if (socket == invalidTestSocket) return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#else
    shutdown(socket, SHUT_RDWR);
    close(socket);
#endif
}

TestSocket connectTo(std::int64_t port) {
    TestSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == invalidTestSocket) throw std::runtime_error("client socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(socket);
        throw std::runtime_error("client could not connect to test server");
    }
    return socket;
}

void writeAll(TestSocket socket, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
#ifdef _WIN32
        const int sent = send(socket, data.data() + offset,
            static_cast<int>(data.size() - offset), 0);
#else
        const auto sent = send(socket, data.data() + offset, data.size() - offset, 0);
#endif
        if (sent <= 0) throw std::runtime_error("client send failed");
        offset += static_cast<std::size_t>(sent);
    }
}

std::string readResponse(TestSocket socket) {
    std::string buffer;
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        char chunk[1024];
#ifdef _WIN32
        const int received = recv(socket, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const auto received = recv(socket, chunk, sizeof(chunk), 0);
#endif
        if (received <= 0) throw std::runtime_error("client response header was incomplete");
        buffer.append(chunk, static_cast<std::size_t>(received));
    }
    const auto lengthStart = buffer.find("Content-Length:");
    if (lengthStart == std::string::npos) {
        const auto statusLineEnd = buffer.find("\r\n");
        const std::string_view statusLine(buffer.data(),
            statusLineEnd == std::string::npos ? buffer.size() : statusLineEnd);
        if (statusLine.starts_with("HTTP/1.1 204 ") ||
            statusLine.starts_with("HTTP/1.1 304 ")) {
            return buffer.substr(0, headerEnd + 4);
        }
        throw std::runtime_error("server omitted Content-Length");
    }
    const auto lengthEnd = buffer.find("\r\n", lengthStart);
    const auto valueStart = lengthStart + std::string("Content-Length:").size();
    const auto lengthText = buffer.substr(valueStart, lengthEnd - valueStart);
    const std::size_t bodyLength = static_cast<std::size_t>(std::stoul(lengthText));
    const std::size_t bodyStart = headerEnd + 4;
    while (buffer.size() - bodyStart < bodyLength) {
        char chunk[1024];
#ifdef _WIN32
        const int received = recv(socket, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const auto received = recv(socket, chunk, sizeof(chunk), 0);
#endif
        if (received <= 0) throw std::runtime_error("server response body was incomplete");
        buffer.append(chunk, static_cast<std::size_t>(received));
    }
    return buffer.substr(0, bodyStart + bodyLength);
}

std::string requestLine(const std::string& path, bool closeConnection) {
    return "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: " +
        (closeConnection ? "close" : "keep-alive") + "\r\n\r\n";
}

std::string methodRequest(
    const std::string& method,
    const std::string& path,
    bool closeConnection = true
) {
    return method + " " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: " +
        (closeConnection ? "close" : "keep-alive") + "\r\nContent-Length: 0\r\n\r\n";
}

std::string runClientAgainstRawServer(
    const std::string& response,
    const std::string& headersJson = "{}",
    bool expectRequest = true
) {
    const TestSocket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalidTestSocket) throw std::runtime_error("raw server socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        closeSocket(listener);
        throw std::runtime_error("raw test server could not listen");
    }
#ifdef _WIN32
    int addressLength = sizeof(address);
#else
    socklen_t addressLength = sizeof(address);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address),
            &addressLength) != 0) {
        closeSocket(listener);
        throw std::runtime_error("raw test server could not determine its port");
    }
    const auto port = ntohs(address.sin_port);
    std::exception_ptr serverError;
    std::thread server([&] {
        TestSocket client = accept(listener, nullptr, nullptr);
        if (client == invalidTestSocket) {
            serverError = std::make_exception_ptr(
                std::runtime_error("raw test server accept failed"));
            closeSocket(listener);
            return;
        }
        try {
            if (expectRequest) {
                std::string request;
                while (request.find("\r\n\r\n") == std::string::npos) {
                    char chunk[1024];
#ifdef _WIN32
                    const int received = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
                    const auto received = recv(client, chunk, sizeof(chunk), 0);
#endif
                    if (received <= 0) {
                        throw std::runtime_error("raw client request was incomplete");
                    }
                    request.append(chunk, static_cast<std::size_t>(received));
                }
                writeAll(client, response);
            } else {
                char firstByte = 0;
#ifdef _WIN32
                const int received = recv(client, &firstByte, 1, 0);
#else
                const auto received = recv(client, &firstByte, 1, 0);
#endif
                if (received > 0) {
                    throw std::runtime_error("oversized request headers were sent");
                }
            }
        } catch (...) {
            serverError = std::current_exception();
        }
        closeSocket(client);
        closeSocket(listener);
    });

    std::string result;
    std::exception_ptr clientError;
    try {
        result = zplus::http::clientRequest("GET",
            "http://127.0.0.1:" + std::to_string(port) + "/",
            headersJson, "", 3000);
    } catch (...) {
        clientError = std::current_exception();
    }
    server.join();
    if (serverError) std::rethrow_exception(serverError);
    if (clientError) std::rethrow_exception(clientError);
    return result;
}

} // namespace

int main() {
    try {
#ifdef _WIN32
        WSADATA winsock{};
        require(WSAStartup(MAKEWORD(2, 2), &winsock) == 0,
            "test Winsock initialization failed");
#endif
        const auto server = zplus::http::createServer("127.0.0.1", 0);
        zplus::http::configureServer(server, 64, 3000);
        zplus::http::configureCors(
            server, "https://example.test", "GET, POST, OPTIONS", "Content-Type");
        require(zplus::http::startServer(server), "server failed to start");
        const auto port = zplus::http::serverPort(server);
        require(port > 0, "server did not report its ephemeral port");
        requireThrows([&] {
            (void)zplus::http::respond(server, -1, 205, "{}", "unexpected");
        }, "HTTP 205 response accepted a body");
        const std::string matched = zplus::http::routeParams(
            "/api/items/7", "/api/items/{id}");
        require(zplus::json::getString(matched, "/id") == "7",
            "route parameter extraction failed");
        require(zplus::http::routeParams("/api/items", "/api/items/{id}").empty(),
            "non-matching route returned parameters");
        require(zplus::http::queryValue("name=Jane+Doe&next=%2Fusers", "name") ==
                "Jane Doe" &&
                zplus::http::queryValue("name=Jane+Doe&next=%2Fusers", "next") ==
                "/users",
            "query parameter decoding failed");
        requireThrowsType<std::length_error>([&] {
            const std::string oversizedHeaders = "{\"x\":\"" +
                std::string(16 * 1024, 'x') + "\"}";
            (void)zplus::http::clientRequest("GET",
                "http://127.0.0.1:1/", oversizedHeaders, "", 100);
        }, "oversized outbound HTTP headers were accepted");
        std::string tooManyOutboundHeaders = "{";
        for (std::size_t i = 0; i <= 100; ++i) {
            if (i != 0) tooManyOutboundHeaders += ",";
            tooManyOutboundHeaders +=
                "\"x-" + std::to_string(i) + "\":\"v\"";
        }
        tooManyOutboundHeaders += "}";
        requireThrowsType<std::length_error>([&] {
            (void)zplus::http::clientRequest("GET", "http://127.0.0.1:1/",
                tooManyOutboundHeaders, "", 100);
        }, "excessive outbound HTTP header count was accepted");
        const std::string oversizedWireHeaders = "{\"x\":\"" +
            std::string(16 * 1024 - 14, 'x') + "\"}";
        require(oversizedWireHeaders.size() <= 16 * 1024,
            "outbound wire-header test input exceeded the JSON limit");
        requireThrowsType<std::length_error>([&] {
            (void)runClientAgainstRawServer("", oversizedWireHeaders, false);
        }, "oversized serialized HTTP request headers were accepted");
        requireThrowsType<std::length_error>([&] {
            (void)zplus::http::clientRequest("POST",
                "http://127.0.0.1:1/", "{}",
                std::string(16 * 1024 * 1024 + 1, 'x'), 100);
        }, "oversized outbound HTTP body was accepted");

        const auto chunkedResponse = runClientAgainstRawServer(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Trailer: X-Checksum\r\n"
            "Connection: close\r\n\r\n"
            "4\r\nWiki\r\n"
            "5;ext=value\r\npedia\r\n"
            "0\r\nX-Checksum: ok\r\n\r\n");
        require(zplus::json::getString(chunkedResponse, "/body") == "Wikipedia",
            "chunked HTTP response decoding failed");
        const auto resetContentResponse = runClientAgainstRawServer(
            "HTTP/1.1 205 Reset Content\r\nContent-Length: 0\r\n\r\n");
        require(zplus::json::getNumber(resetContentResponse, "/status") == 205 &&
                zplus::json::getString(resetContentResponse, "/body").empty(),
            "HTTP 205 response handling was invalid");
        bool rejectedInformationalResponse = false;
        try {
            (void)runClientAgainstRawServer(
                "HTTP/1.1 100 Continue\r\n\r\n"
                "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        } catch (const std::runtime_error& error) {
            rejectedInformationalResponse =
                std::string_view(error.what()).find("informational") !=
                std::string_view::npos;
        }
        require(rejectedInformationalResponse,
            "informational HTTP response was not rejected explicitly");
        std::string tooManyChunks =
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (std::size_t i = 0; i <= 65536; ++i) {
            tooManyChunks += "1\r\nx\r\n";
        }
        tooManyChunks += "0\r\n\r\n";
        requireThrows([&] {
            (void)runClientAgainstRawServer(tooManyChunks);
        }, "chunked HTTP response count limit was not enforced");
        requireThrows([&] {
            (void)runClientAgainstRawServer(
                "HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Content-Length: 0\r\n\r\n0\r\n\r\n");
        }, "ambiguous Transfer-Encoding and Content-Length response was accepted");
        requireThrows([&] {
            (void)runClientAgainstRawServer(
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 0\r\n"
                "Content-Length: 0\r\n\r\n");
        }, "duplicate response Content-Length was accepted");
        requireThrows([&] {
            (void)runClientAgainstRawServer(
                "HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: gzip\r\n\r\n");
        }, "unsupported response Transfer-Encoding was accepted");

        std::string firstClientResponse;
        std::exception_ptr firstClientError;
        std::thread firstClient([&] {
            try {
                const TestSocket socket = connectTo(port);
                writeAll(socket,
                    "POST /api/items/7?verbose=1 HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Origin: https://example.test\r\n"
                    "X-Request: integration\r\n"
                    "Content-Length: 5\r\n"
                    "Connection: close\r\n\r\n"
                    "hello");
                firstClientResponse = readResponse(socket);
                closeSocket(socket);
            } catch (...) {
                firstClientError = std::current_exception();
            }
        });

        const std::string request = zplus::http::nextRequest(server, 3000);
        require(!request.empty(), "server did not deliver the POST request");
        require(zplus::json::getString(request, "/method") == "POST",
            "request method was not parsed");
        require(zplus::json::getString(request, "/path") == "/api/items/7",
            "request path was not parsed");
        require(zplus::json::getString(request, "/query") == "verbose=1",
            "query string was not parsed");
        require(zplus::json::getString(request, "/headers/x-request") == "integration",
            "request header was not parsed");
        require(zplus::http::requestHeader(request, "X-Request") == "integration",
            "request header helper failed");
        require(zplus::json::getString(request, "/body") == "hello",
            "request body was not parsed");
        const auto requestId = static_cast<std::int64_t>(
            zplus::json::getNumber(request, "/id"));
        require(zplus::http::respond(server, requestId, 201,
            R"({"content-type":"text/plain; charset=utf-8","x-test":"yes"})",
            "created"), "server rejected a valid response");
        firstClient.join();
        if (firstClientError) std::rethrow_exception(firstClientError);
        require(firstClientResponse.find("HTTP/1.1 201 Created\r\n") == 0 &&
                firstClientResponse.ends_with("created"),
            "client received an invalid response");
        require(firstClientResponse.find(
                "access-control-allow-origin: https://example.test\r\n") !=
                std::string::npos,
            "configured CORS origin was not included");

        const TestSocket preflightClient = connectTo(port);
        writeAll(preflightClient,
            "OPTIONS /api/items/7 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://example.test\r\n"
            "Access-Control-Request-Method: POST\r\n"
            "Access-Control-Request-Headers: Content-Type\r\n"
            "Connection: close\r\n\r\n");
        const std::string preflightResponse = readResponse(preflightClient);
        closeSocket(preflightClient);
        require(preflightResponse.find("HTTP/1.1 204 No Content\r\n") == 0 &&
                preflightResponse.find(
                    "access-control-allow-origin: https://example.test\r\n") !=
                    std::string::npos &&
                preflightResponse.find(
                    "access-control-allow-methods: GET, POST, OPTIONS\r\n") !=
                    std::string::npos,
            "valid CORS preflight was not answered with its configured policy");

        const TestSocket deniedPreflightClient = connectTo(port);
        writeAll(deniedPreflightClient,
            "OPTIONS /api/items/7 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://attacker.test\r\n"
            "Access-Control-Request-Method: POST\r\n"
            "Connection: close\r\n\r\n");
        const std::string deniedPreflightResponse = readResponse(deniedPreflightClient);
        closeSocket(deniedPreflightClient);
        require(deniedPreflightResponse.find("HTTP/1.1 403 Forbidden\r\n") == 0 &&
                deniedPreflightResponse.find("access-control-allow-origin:") ==
                    std::string::npos,
            "disallowed CORS preflight received an allow-origin header");

        const TestSocket oversizedClient = connectTo(port);
        writeAll(oversizedClient,
            "POST /large HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 100\r\nConnection: close\r\n\r\n");
        const std::string oversizedResponse = readResponse(oversizedClient);
        closeSocket(oversizedClient);
        require(oversizedResponse.find("HTTP/1.1 413 Payload Too Large\r\n") == 0,
            "request body limit was not enforced");

        const TestSocket malformedClient = connectTo(port);
        writeAll(malformedClient,
            "GET /missing-host HTTP/1.1\r\nConnection: close\r\n\r\n");
        const std::string malformedResponse = readResponse(malformedClient);
        closeSocket(malformedClient);
        require(malformedResponse.find("HTTP/1.1 400 Bad Request\r\n") == 0,
            "malformed request was not rejected safely");

        const TestSocket duplicateLengthClient = connectTo(port);
        writeAll(duplicateLengthClient,
            "POST /duplicate-length HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 0\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
        const std::string duplicateLengthResponse = readResponse(duplicateLengthClient);
        closeSocket(duplicateLengthClient);
        require(duplicateLengthResponse.find("HTTP/1.1 400 Bad Request\r\n") == 0,
            "duplicate Content-Length request framing was accepted");

        std::string pipelinedResponses;
        std::exception_ptr pipelinedClientError;
        std::thread pipelinedClient([&] {
            try {
                const TestSocket socket = connectTo(port);
                writeAll(socket,
                    "GET /pipeline-first HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Connection: keep-alive\r\n\r\n"
                    "POST /pipeline-large HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: 100\r\n"
                    "Connection: close\r\n\r\n" + std::string(100, 'x'));
                pipelinedResponses = readResponse(socket);
                closeSocket(socket);
            } catch (...) {
                pipelinedClientError = std::current_exception();
            }
        });
        const std::string pipelineFirst = zplus::http::nextRequest(server, 3000);
        require(zplus::json::getString(pipelineFirst, "/path") == "/pipeline-first",
            "buffered pipelined bytes incorrectly rejected the first request");
        require(zplus::http::respond(server,
            static_cast<std::int64_t>(zplus::json::getNumber(pipelineFirst, "/id")),
            200, "{}", "first"), "server rejected the first pipelined response");
        pipelinedClient.join();
        if (pipelinedClientError) std::rethrow_exception(pipelinedClientError);
        require(pipelinedResponses.find("HTTP/1.1 200 OK\r\n") == 0,
            "buffered pipelined request bytes incorrectly caused a 413 response");

        std::string excessiveHeaders =
            "GET /too-many-headers HTTP/1.1\r\nHost: localhost\r\n";
        for (int i = 0; i < 100; ++i) {
            excessiveHeaders += "X-Test-" + std::to_string(i) + ": x\r\n";
        }
        excessiveHeaders += "Connection: close\r\n\r\n";
        const TestSocket excessiveHeadersClient = connectTo(port);
        writeAll(excessiveHeadersClient, excessiveHeaders);
        const std::string excessiveHeadersResponse = readResponse(excessiveHeadersClient);
        closeSocket(excessiveHeadersClient);
        require(excessiveHeadersResponse.find(
                    "HTTP/1.1 431 Request Header Fields Too Large\r\n") == 0,
            "request header-count limit was not enforced");

        std::string persistentResponse;
        std::exception_ptr persistentClientError;
        std::thread persistentClient([&] {
            try {
                const TestSocket socket = connectTo(port);
                writeAll(socket, requestLine("/first", false));
                const std::string first = readResponse(socket);
                writeAll(socket, requestLine("/second", true));
                const std::string second = readResponse(socket);
                persistentResponse = first + second;
                closeSocket(socket);
            } catch (...) {
                persistentClientError = std::current_exception();
            }
        });
        for (const std::string expectedPath : {"/first", "/second"}) {
            const std::string current = zplus::http::nextRequest(server, 3000);
            require(zplus::json::getString(current, "/path") == expectedPath,
                "persistent connection request order was incorrect");
            require(zplus::http::respond(server,
                static_cast<std::int64_t>(zplus::json::getNumber(current, "/id")),
                200, R"({"content-type":"text/plain"})", expectedPath),
                "persistent response was rejected");
        }
        persistentClient.join();
        if (persistentClientError) std::rethrow_exception(persistentClientError);
        require(persistentResponse.find("/first") != std::string::npos &&
                persistentResponse.find("/second") != std::string::npos,
            "persistent HTTP responses were incomplete");

        for (const std::string method : {"PUT", "PATCH", "DELETE", "OPTIONS"}) {
            std::string response;
            std::exception_ptr error;
            std::thread client([&, method] {
                try {
                    const TestSocket socket = connectTo(port);
                    writeAll(socket, methodRequest(method, "/methods/" + method));
                    response = readResponse(socket);
                    closeSocket(socket);
                } catch (...) {
                    error = std::current_exception();
                }
            });
            const std::string current = zplus::http::nextRequest(server, 3000);
            require(zplus::json::getString(current, "/method") == method,
                "supported HTTP method was not parsed");
            require(zplus::http::respond(server,
                static_cast<std::int64_t>(zplus::json::getNumber(current, "/id")),
                204, "{}", ""), "HTTP method response was rejected");
            client.join();
            if (error) std::rethrow_exception(error);
            require(response.find("HTTP/1.1 204 No Content\r\n") == 0,
                "HTTP method did not receive a valid response");
        }

        std::array<std::thread, 4> clients;
        std::array<std::exception_ptr, 4> clientErrors{};
        for (std::size_t i = 0; i < clients.size(); ++i) {
            clients[i] = std::thread([&, i] {
                try {
                    const TestSocket socket = connectTo(port);
                    writeAll(socket, requestLine("/parallel/" + std::to_string(i), true));
                    const std::string response = readResponse(socket);
                    require(response.ends_with("client-" + std::to_string(i)),
                        "parallel request received another client's response");
                    closeSocket(socket);
                } catch (...) {
                    clientErrors[i] = std::current_exception();
                }
            });
        }
        for (std::size_t i = 0; i < clients.size(); ++i) {
            const std::string current = zplus::http::nextRequest(server, 3000);
            require(!current.empty(), "parallel request was not delivered");
            const std::string path = zplus::json::getString(current, "/path");
            const std::string id = path.substr(path.find_last_of('/') + 1);
            require(zplus::http::respond(server,
                static_cast<std::int64_t>(zplus::json::getNumber(current, "/id")),
                200, R"({"content-type":"text/plain"})", "client-" + id),
                "parallel response was rejected");
        }
        for (std::size_t i = 0; i < clients.size(); ++i) {
            clients[i].join();
            if (clientErrors[i]) std::rethrow_exception(clientErrors[i]);
        }

        std::string clientResult;
        std::exception_ptr outboundClientError;
        std::thread outboundClient([&] {
            try {
                clientResult = zplus::http::clientRequest(
                    "POST",
                    "http://127.0.0.1:" + std::to_string(port) + "/client?mode=json",
                    R"({"content-type":"application/json","x-client":"integration","origin":"https://example.test"})",
                    R"({"active":true})",
                    3000);
            } catch (...) {
                outboundClientError = std::current_exception();
            }
        });
        const std::string outboundRequest = zplus::http::nextRequest(server, 3000);
        require(zplus::json::getString(outboundRequest, "/method") == "POST" &&
                zplus::json::getString(outboundRequest, "/path") == "/client" &&
                zplus::json::getString(outboundRequest, "/query") == "mode=json" &&
                zplus::json::getString(outboundRequest, "/headers/x-client") == "integration" &&
                zplus::json::getString(outboundRequest, "/body") == R"({"active":true})",
            "outbound HTTP client sent an invalid request");
        require(zplus::http::respond(server,
            static_cast<std::int64_t>(zplus::json::getNumber(outboundRequest, "/id")),
            202, R"({"content-type":"application/json","x-server":"integration"})",
            R"({"accepted":true})"), "outbound HTTP client response was rejected");
        outboundClient.join();
        if (outboundClientError) std::rethrow_exception(outboundClientError);
        require(zplus::json::getNumber(clientResult, "/status") == 202 &&
                zplus::json::getString(clientResult, "/body") == R"({"accepted":true})" &&
                zplus::json::getString(clientResult, "/headers/x-server") == "integration" &&
                zplus::json::getString(
                    clientResult, "/headers/access-control-allow-origin") ==
                    "https://example.test",
            "outbound HTTP client parsed an invalid response");

        bool rejectedHttps = false;
        try {
            (void)zplus::http::clientRequest(
                "GET", "https://127.0.0.1/", "{}", "", 100);
        } catch (const std::invalid_argument&) {
            rejectedHttps = true;
        }
        require(rejectedHttps, "HTTP client falsely accepted an HTTPS URL");

        zplus::http::configureServer(server, 64, 100);
        std::string timedOutResponse;
        std::exception_ptr timeoutClientError;
        std::thread timeoutClient([&] {
            try {
                const TestSocket socket = connectTo(port);
                writeAll(socket, requestLine("/timeout", true));
                timedOutResponse = readResponse(socket);
                closeSocket(socket);
            } catch (...) {
                timeoutClientError = std::current_exception();
            }
        });
        require(!zplus::http::nextRequest(server, 3000).empty(),
            "timeout request was not delivered");
        timeoutClient.join();
        if (timeoutClientError) std::rethrow_exception(timeoutClientError);
        require(timedOutResponse.find("HTTP/1.1 504 Gateway Timeout\r\n") == 0,
            "unanswered request did not time out");

        zplus::http::stopServer(server);
#ifdef _WIN32
        WSACleanup();
#endif
        std::cout << "HTTP server tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
