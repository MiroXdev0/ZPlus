#include "Http.hpp"
#include "Json.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
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

        std::string firstClientResponse;
        std::exception_ptr firstClientError;
        std::thread firstClient([&] {
            try {
                const TestSocket socket = connectTo(port);
                writeAll(socket,
                    "POST /api/items/7?verbose=1 HTTP/1.1\r\n"
                    "Host: localhost\r\n"
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
                    R"({"content-type":"application/json","x-client":"integration"})",
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
