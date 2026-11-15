#include "Web.hpp"
#include "Json.hpp"
#include "Base64.hpp"
#include "Http.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <stdexcept>
#include <string>

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

} // namespace

int main() {
    using namespace zplus;

    require(encoding::base64Encode(std::string("\0\xff", 2)) == "AP8=" &&
            encoding::base64Decode("AP8=") == std::string("\0\xff", 2),
        "base64 encoding did not preserve binary bytes");
    requireThrows([] { encoding::base64Decode("AP9="); },
        "non-canonical base64 padding bits were accepted");

    require(web::urlEncode("a b/c") == "a%20b%2Fc",
        "URL component encoding failed");
    require(web::urlDecode("a%20b%2Fc") == "a b/c",
        "URL component decoding failed");
    requireThrows([] { web::urlDecode("%Q0"); },
        "malformed URL escape was accepted");

    const std::string url = web::parseUrl("https://example.test:8443/a?x=1#top");
    require(json::getString(url, "/scheme") == "https" &&
            json::getString(url, "/host") == "example.test" &&
            json::getString(url, "/port") == "8443" &&
            json::getString(url, "/path") == "/a" &&
            json::getString(url, "/query") == "x=1" &&
            json::getString(url, "/fragment") == "top",
        "URL parsing lost a component");
    require(web::buildUrl("https", "example.test", "443", "/a", "x=1", "top") ==
            "https://example.test/a?x=1#top",
        "URL construction failed to omit the default port");

    const std::string form = web::parseForm("name=Jane+Doe&tag=one&tag=two");
    require(json::getString(form, "/name") == "Jane Doe" &&
            json::arrayLength(form, "/tag") == 2 &&
            json::getString(web::parseForm("x=a%26b"), "/x") == "a&b",
        "form decoding or duplicate-value handling failed");

    const std::string cookies = web::parseCookies("session=a%20b; theme=dark");
    require(json::getString(cookies, "/session") == "a b" &&
            json::getString(cookies, "/theme") == "dark",
        "cookie parsing failed");
    const auto setCookie = web::cookieHeader(
        "session", "a b", 3600, "/", "example.test", true, true, "Lax");
    require(setCookie ==
            "session=a%20b; Max-Age=3600; Path=/; Domain=example.test; Secure; HttpOnly; SameSite=Lax",
        "cookie serialization omitted attributes");
    requireThrows([] {
        web::cookieHeader("session", "x", -1, "/", "", false, true, "None");
    }, "insecure SameSite=None cookie was accepted");
    require(web::cookieHeaderExpires("session", "x", 60,
                "Wed, 21 Oct 2015 07:28:00 GMT", "/", "", true, true, "Lax")
                .find("Expires=Wed, 21 Oct 2015 07:28:00 GMT") != std::string::npos,
        "cookie expiration was not serialized");

    const std::string routes = web::routeAdd(
        "[]", "GET", "/users/{id}", "showUser");
    const std::string grouped = web::routeGroup("/v1", routes);
    const std::string matched = web::routeMatch(grouped, "GET", "/v1/users/42");
    require(json::getNumber(matched, "/status") == 200 &&
            json::getString(matched, "/handler") == "showUser" &&
            json::getString(matched, "/params/id") == "42",
        "route matching or group prefix failed");
    require(json::getNumber(web::routeMatch(grouped, "POST", "/v1/users/42"),
                "/status") == 405 &&
            json::getNumber(web::routeMatch(grouped, "GET", "/missing"),
                "/status") == 404,
        "router did not distinguish 404 and 405");
    const auto namedRoutes = web::routeAddConfigured("[]", "GET", "/users/{id}",
        "showUser", "user.show", R"({"authenticated":true})");
    const auto groupedNamedRoutes = web::routeGroup("/v2", namedRoutes);
    const auto namedMatch = web::routeMatch(groupedNamedRoutes, "GET", "/v2/users/42");
    require(json::getString(namedMatch, "/name") == "user.show" &&
            json::getBoolean(namedMatch, "/configuration/authenticated") &&
            web::routeUrl(groupedNamedRoutes, "user.show", R"({"id":"a b"})") ==
                "/v2/users/a%20b",
        "configured or named route handling failed");

    const auto jsonReply = web::jsonResponse(201, "{}", R"({"created":true})");
    require(json::getNumber(jsonReply, "/status") == 201 &&
            json::getString(jsonReply, "/headers/content-type") ==
                "application/json; charset=utf-8" &&
            json::getString(jsonReply, "/body") == R"({"created":true})" &&
            json::getNumber(web::notFoundResponse("missing"), "/status") == 404 &&
            json::getString(web::methodNotAllowedResponse("GET", "wrong method"),
                "/headers/allow") == "GET",
        "response helpers generated invalid status, headers, or body");
    requireThrows([] { web::response(204, "{}", "unexpected"); },
        "204 response helper accepted a body");
    requireThrows([] { web::response(205, "{}", "unexpected"); },
        "205 response helper accepted a body");
    requireThrows([] { web::response(101, "{}", ""); },
        "informational response status was accepted as a final response");
    require(web::parseJsonBody(
                R"({"body":" { \"ready\" : true } ","bodyEncoding":"utf8"})") ==
                R"({"ready":true})",
        "request JSON helper did not compact valid request JSON");
    requireThrows([] {
        web::parseJsonBody(R"({"body":"","bodyEncoding":"base64"})");
    }, "request JSON helper accepted a binary request body");

    const auto validated = web::validateForm(
        R"({"name":"Ada","age":"37","role":["reader","writer"]})",
        R"([{"name":"name","required":true,"minLength":2},{"name":"age","numeric":true},{"name":"role","allowed":["reader","writer"]}])");
    require(json::getBoolean(validated, "/valid") &&
            web::formHas(R"({"age":"37"})", "age") &&
            web::formNumber(R"({"age":"37.5"})", "age") == 37.5,
        "form validation or typed access failed");
    require(!json::getBoolean(web::validateForm(
                R"({"name":"A","age":"bad"})",
                R"([{"name":"name","required":true,"minLength":2},{"name":"age","numeric":true}])"),
                "/valid"),
        "form validation accepted invalid values");
    require(!json::getBoolean(web::validateForm(
                R"({"name":""})", R"([{"name":"name","required":true}])"), "/valid"),
        "required form validation accepted an empty value");
    require(json::getBoolean(web::validateForm(
                R"({"email":"ada@example.test","age":"37"})",
                R"([{"name":"email","email":true},{"name":"age","minimum":18,"maximum":120}])"),
                "/valid"),
        "email or numeric range validation rejected valid input");
    const auto invalidEmailAndRange = web::validateForm(
        R"({"email":"ada@@example.test","age":"17"})",
        R"([{"name":"email","email":true},{"name":"age","minimum":18}])");
    require(!json::getBoolean(invalidEmailAndRange, "/valid") &&
            json::arrayLength(invalidEmailAndRange, "/errors") == 2,
        "email or numeric range validation accepted invalid input");
    requireThrows([&] {
        (void)web::validateForm("{}", R"([{"name":"email","email":"yes"}])");
    }, "malformed validation rules were ignored for a missing field");
    requireThrows([&] {
        (void)web::validateForm(R"({"age":"1"})",
            R"([{"name":"age","minLength":9223372036854775808}])");
    }, "out-of-range form length bound was accepted");

    const auto templateResult = web::renderTemplate(
        "Hello {{user.name}} {{#if active}}active{{/if}} "
        "{{#each users}}<b>{{name}}:{{@index}}</b>{{/each}}",
        R"({"user":{"name":"<Ada>"},"active":true,"users":[{"name":"A"},{"name":"B"}]})");
    require(templateResult ==
            "Hello &lt;Ada&gt; active <b>A:0</b><b>B:1</b>",
        "template substitution, escaping, conditional, or iteration failed");
    requireThrows([] {
        web::renderTemplate("{{#if active}}unclosed", R"({"active":true})");
    }, "unclosed template section was accepted");
    requireThrows([] {
        web::renderTemplate("<a title=\"{{value}}\">link</a>", R"({"value":"x"})");
    }, "attribute-context template interpolation was accepted");
    requireThrows([] {
        web::renderTemplate("<script>const value = '{{value}}';</script>",
            R"({"value":"x"})");
    }, "script-context template interpolation was accepted");

    const auto sessionStore = web::sessionStoreCreate();
    auto sessionId = web::sessionCreate(sessionStore, 60);
    const auto csrfToken = web::sessionCsrfToken(sessionStore, sessionId);
    require(sessionId.size() == 43 &&
            csrfToken.size() == 43 &&
            web::sessionValidateCsrf(sessionStore, sessionId, csrfToken) &&
            !web::sessionValidateCsrf(sessionStore, sessionId, csrfToken + "x") &&
            web::sessionSet(sessionStore, sessionId, "user", "Ada") &&
            web::sessionHas(sessionStore, sessionId, "user") &&
            web::sessionGet(sessionStore, sessionId, "user") == "Ada" &&
            web::sessionCookie("sid", sessionId, 60, true, "Lax").find(
                "HttpOnly; SameSite=Lax") != std::string::npos,
        "in-memory session creation, storage, or cookie generation failed");
    const auto rotatedSessionId = web::sessionRotate(sessionStore, sessionId, 60);
    require(rotatedSessionId.size() == 43 && rotatedSessionId != sessionId &&
            !web::sessionHas(sessionStore, sessionId, "user") &&
            web::sessionGet(sessionStore, rotatedSessionId, "user") == "Ada" &&
            !web::sessionValidateCsrf(sessionStore, rotatedSessionId, csrfToken),
        "session rotation did not invalidate the old ID and CSRF token");
    sessionId = rotatedSessionId;
    const auto rotatedCsrfToken = web::sessionCsrfToken(sessionStore, sessionId);
    require(rotatedCsrfToken.size() == 43 &&
            web::sessionValidateCsrf(sessionStore, sessionId, rotatedCsrfToken),
        "rotated session did not issue a fresh CSRF token");
    require(!web::sessionHas(sessionStore, "not-a-session-id", "user") &&
            !web::sessionInvalidate(sessionStore, "not-a-session-id"),
        "invalid session IDs were not rejected safely");
    const auto expiringSession = web::sessionCreate(sessionStore, 1);
    const auto expiringCsrf = web::sessionCsrfToken(sessionStore, expiringSession);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    require(!web::sessionValidateCsrf(sessionStore, expiringSession, expiringCsrf) &&
            !web::sessionHas(sessionStore, expiringSession, "user"),
        "expired sessions or their CSRF tokens remained valid");
    require(web::sessionInvalidate(sessionStore, sessionId) &&
            !web::sessionHas(sessionStore, sessionId, "user"),
        "session invalidation failed");
    web::sessionStoreClose(sessionStore);

    std::string middleware = web::middlewareUse("[]", "requireHeader",
        R"({"header":"x-api-key","equals":"secret"})");
    middleware = web::middlewareUse(middleware, "requestTiming", "{}");
    middleware = web::middlewareUse(middleware, "securityHeaders", "{}");
    middleware = web::middlewareUse(middleware, "cors",
        R"({"origin":"https://example.test","methods":"GET, POST","exposedHeaders":"X-Total"})");
    middleware = web::middlewareUse(middleware, "requestLog", "{}");
    const auto before = web::middlewareBefore(middleware,
        R"({"method":"GET","path":"/private","headers":{"x-api-key":"secret","origin":"https://example.test"},"body":""})");
    require(json::getBoolean(before, "/proceed"),
        "middleware rejected a request with the configured header");
    const auto after = web::middlewareAfter(middleware,
        json::getRaw(before, "/request"),
        R"({"status":200,"headers":{},"body":"ok"})");
    require(json::getString(after, "/headers/x-frame-options") == "DENY" &&
            json::getString(after, "/headers/access-control-allow-origin") ==
                "https://example.test" &&
            json::getString(after, "/headers/access-control-expose-headers") ==
                "X-Total" &&
            json::getString(after, "/headers/server-timing").find("app;dur=") == 0 &&
            json::arrayLength(after, "/logs") == 1,
        "middleware response processing failed");
    const auto rejected = web::middlewareBefore(middleware,
        R"({"method":"GET","path":"/private","headers":{},"body":""})");
    require(!json::getBoolean(rejected, "/proceed") &&
            json::getNumber(rejected, "/response/status") == 401,
        "authentication-header middleware did not reject an unauthenticated request");
    const auto bodyLimited = web::middlewareUse("[]", "maxBodyBytes",
        R"({"bytes":2})");
    const auto oversized = web::middlewareBefore(bodyLimited,
        R"({"method":"POST","path":"/","headers":{},"body":"too big"})");
    require(json::getNumber(oversized, "/response/status") == 413,
        "body-size middleware did not reject an oversized request");
    const auto jsonRequired = web::middlewareUse("[]", "jsonBody", "{}");
    const auto malformedJson = web::middlewareBefore(jsonRequired,
        R"({"method":"POST","path":"/","headers":{"content-type":"application/json"},"body":"{"})");
    require(json::getNumber(malformedJson, "/response/status") == 400,
        "JSON request middleware accepted malformed JSON");

    std::string multipartBody =
        "--zweb-boundary\r\n"
        "Content-Disposition: form-data; name=\"tag\"\r\n\r\none\r\n"
        "--zweb-boundary\r\n"
        "Content-Disposition: form-data; name=\"tag\"\r\n\r\ntwo\r\n"
        "--zweb-boundary\r\n"
        "Content-Disposition: form-data; name=\"upload\"; filename=\"data.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    multipartBody.push_back('\0');
    multipartBody.push_back(static_cast<char>(0xff));
    multipartBody += "\r\n--zweb-boundary--\r\n";
    json::Value::Object multipartHeaders;
    multipartHeaders.emplace_back("content-type",
        json::Value{"multipart/form-data; boundary=zweb-boundary"});
    json::Value::Object multipartRequest;
    multipartRequest.emplace_back("headers", json::Value{std::move(multipartHeaders)});
    multipartRequest.emplace_back("bodyBase64",
        json::Value{encoding::base64Encode(multipartBody)});
    const auto multipart = web::parseMultipart(
        json::serialize(json::Value{std::move(multipartRequest)}), 1024);
    require(json::arrayLength(multipart, "/fields/tag") == 2 &&
            json::getString(multipart, "/fields/tag/0") == "one" &&
            json::arrayLength(multipart, "/files") == 1 &&
            json::getString(multipart, "/files/0/filename") == "data.bin" &&
            encoding::base64Decode(json::getString(multipart, "/files/0/dataBase64")) ==
                std::string("\0\xff", 2),
        "multipart field repetition or binary upload parsing failed");
    requireThrows([] {
        (void)web::parseMultipart(
            R"({"headers":{"content-type":"multipart/form-data; boundary=a; boundary=b"},"body":""})",
            1024);
    }, "duplicate multipart boundaries were accepted");
    requireThrows([] {
        (void)web::parseMultipart(
            R"({"headers":{"content-type":"multipart/form-data; boundary=a"},"body":"--a\r\nContent-Disposition: form-data; name=\"x\"\r\nContent-Disposition: form-data; name=\"y\"\r\n\r\nvalue\r\n--a--\r\n"})",
            1024);
    }, "duplicate multipart part headers were accepted");

    const std::string headers = web::securityHeaders(R"({"content-type":"text/plain"})");
    require(json::getString(headers, "/x-content-type-options") == "nosniff" &&
            json::getString(headers, "/content-type") == "text/plain",
        "security header middleware did not preserve existing headers");
    require(web::mimeType("site.css") == "text/css; charset=utf-8" &&
            web::mimeType("download.unknown") == "application/octet-stream",
        "MIME type lookup failed");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() /
        ("zplus-web-test-" + std::to_string(unique));
    const auto root = base / "public";
    std::filesystem::create_directories(root);
    try {
        {
            std::ofstream file(root / "index.html", std::ios::binary);
            file << "<h1>ok</h1>";
        }
        std::filesystem::create_directories(root / "uploads");
        std::filesystem::create_directories(root / "templates");
        {
            std::ofstream file(root / "templates" / "welcome.html", std::ios::binary);
            file << "<h1>{{name}}</h1>";
        }
        require(web::staticFile(root.string(), "/index.html", 1024) == "<h1>ok</h1>",
            "static file serving did not read an in-root file");
        require(web::renderTemplate(
                    web::loadTemplate((root / "templates").string(), "welcome.html", 1024),
                    R"({"name":"Z+"})") == "<h1>Z+</h1>",
            "template loader did not read and render an in-root template");
        requireThrows([&] { web::staticFile(root.string(), "../outside", 1024); },
            "static file traversal was accepted");
        requireThrows([&] { web::staticFile(root.string(), "%2e%2e/outside", 1024); },
            "encoded static file traversal was accepted");
        requireThrows([&] { web::staticFile(root.string(), "%2e%2e%5coutside", 1024); },
            "encoded Windows path traversal was accepted");
        requireThrows([&] { web::staticFile(root.string(), "/uploads", 1024); },
            "static directory listing was exposed without an index file");
        requireThrows([&] { web::staticFile(root.string(), "/index.html", 4); },
            "static file size limit was ignored");
        const auto savedUpload = web::saveUpload(
            multipart, 0, (root / "uploads").string(), "sub/data.bin", 16);
        std::ifstream uploaded(savedUpload, std::ios::binary);
        const std::string uploadedContents((std::istreambuf_iterator<char>(uploaded)),
            std::istreambuf_iterator<char>());
        require(uploadedContents == std::string("\0\xff", 2),
            "uploaded file was not stored byte-for-byte");
        requireThrows([&] {
            web::saveUpload(multipart, 0, (root / "uploads").string(), "sub/data.bin", 16);
        }, "upload unexpectedly overwrote an existing destination");
        requireThrows([&] {
            web::saveUpload(multipart, 0, (root / "uploads").string(), "../outside.bin", 16);
        }, "upload path traversal was accepted");

        const auto server = http::createServer("127.0.0.1", 0);
        require(http::startServer(server), "static response test server did not start");
        const auto port = http::serverPort(server);
        std::string staticResponse;
        std::exception_ptr clientError;
        std::thread client([&] {
            try {
                staticResponse = http::clientRequest("GET",
                    "http://127.0.0.1:" + std::to_string(port) + "/",
                    "{}", "", 3000);
            } catch (...) {
                clientError = std::current_exception();
            }
        });
        const auto staticRequest = http::nextRequest(server, 3000);
        const bool received = !staticRequest.empty();
        const bool responded = received && web::respondStatic(server,
            static_cast<std::int64_t>(json::getNumber(staticRequest, "/id")),
            200, "{}", root.string(), "/", "index.html", 1024, "public, max-age=60");
        client.join();
        if (clientError) std::rethrow_exception(clientError);
        require(received, "static response request was not delivered");
        require(responded, "static response was rejected");
        require(json::getString(staticResponse, "/body") == "<h1>ok</h1>" &&
                json::getString(staticResponse, "/headers/content-type") ==
                    "text/html; charset=utf-8" &&
                json::getString(staticResponse, "/headers/cache-control") ==
                    "public, max-age=60",
            "static index response missed its content or cache headers");

        const std::string binary = std::string("\0\xff\x01", 3);
        std::string binaryResponse;
        std::exception_ptr binaryClientError;
        std::thread binaryClient([&] {
            try {
                binaryResponse = http::clientRequestBinary("POST",
                    "http://127.0.0.1:" + std::to_string(port) + "/binary",
                    R"({"content-type":"application/octet-stream"})",
                    encoding::base64Encode(binary), 3000);
            } catch (...) {
                binaryClientError = std::current_exception();
            }
        });
        const auto binaryRequest = http::nextRequest(server, 3000);
        const bool gotBinary = !binaryRequest.empty() &&
            json::getString(binaryRequest, "/bodyEncoding") == "base64" &&
            encoding::base64Decode(json::getString(binaryRequest, "/bodyBase64")) == binary;
        const bool repliedBinary = gotBinary && http::respondBinary(server,
            static_cast<std::int64_t>(json::getNumber(binaryRequest, "/id")),
            200, R"({"content-type":"application/octet-stream"})",
            encoding::base64Encode(binary));
        binaryClient.join();
        if (binaryClientError) std::rethrow_exception(binaryClientError);
        require(gotBinary && repliedBinary &&
                json::getString(binaryResponse, "/bodyEncoding") == "base64" &&
                encoding::base64Decode(
                    json::getString(binaryResponse, "/bodyBase64")) == binary,
            "HTTP binary request or response was not preserved byte-for-byte");
        http::stopServer(server);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    return 0;
}
