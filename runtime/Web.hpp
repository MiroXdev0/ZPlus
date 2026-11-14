#pragma once

#include <cstdint>
#include <string>

namespace zplus::web {

std::string urlEncode(const std::string& value);
std::string urlDecode(const std::string& value);
std::string parseUrl(const std::string& value);
std::string buildUrl(
    const std::string& scheme,
    const std::string& host,
    const std::string& port,
    const std::string& path,
    const std::string& query,
    const std::string& fragment
);
std::string parseForm(const std::string& value);
std::string parseCookies(const std::string& value);
std::string cookieHeader(
    const std::string& name,
    const std::string& value,
    std::int64_t maxAge,
    const std::string& path,
    const std::string& domain,
    bool secure,
    bool httpOnly,
    const std::string& sameSite
);
std::string cookieHeaderExpires(
    const std::string& name,
    const std::string& value,
    std::int64_t maxAge,
    const std::string& expires,
    const std::string& path,
    const std::string& domain,
    bool secure,
    bool httpOnly,
    const std::string& sameSite
);
std::string securityHeaders(const std::string& headersJson);
std::string routeAdd(
    const std::string& routesJson,
    const std::string& method,
    const std::string& pattern,
    const std::string& handler
);
std::string routeGroup(
    const std::string& prefix,
    const std::string& routesJson
);
std::string routeMatch(
    const std::string& routesJson,
    const std::string& method,
    const std::string& path
);
std::string routeAddConfigured(
    const std::string& routesJson,
    const std::string& method,
    const std::string& pattern,
    const std::string& handler,
    const std::string& name,
    const std::string& configurationJson
);
std::string routeUrl(
    const std::string& routesJson,
    const std::string& name,
    const std::string& parametersJson
);
std::string response(
    std::int64_t status,
    const std::string& headersJson,
    const std::string& body
);
std::string jsonResponse(
    std::int64_t status,
    const std::string& headersJson,
    const std::string& bodyJson
);
std::string parseJsonBody(const std::string& requestJson);
std::string notFoundResponse(const std::string& body);
std::string methodNotAllowedResponse(
    const std::string& allow,
    const std::string& body
);
std::string mimeType(const std::string& path);
std::string staticFile(
    const std::string& root,
    const std::string& path,
    std::int64_t maximumBytes
);
bool respondFile(
    std::int64_t serverHandle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    const std::string& root,
    const std::string& path,
    std::int64_t maximumBytes
);
bool respondStatic(
    std::int64_t serverHandle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    const std::string& root,
    const std::string& path,
    const std::string& indexFile,
    std::int64_t maximumBytes,
    const std::string& cacheControl
);
std::string parseMultipart(const std::string& requestJson, std::int64_t maximumBytes);
std::string saveUpload(
    const std::string& multipartJson,
    std::int64_t index,
    const std::string& root,
    const std::string& destination,
    std::int64_t maximumBytes
);
std::string validateForm(const std::string& formJson, const std::string& rulesJson);
bool formHas(const std::string& formJson, const std::string& name);
double formNumber(const std::string& formJson, const std::string& name);
std::string loadTemplate(
    const std::string& root,
    const std::string& path,
    std::int64_t maximumBytes
);
std::string renderTemplate(const std::string& source, const std::string& contextJson);
std::string middlewareUse(
    const std::string& pipelineJson,
    const std::string& name,
    const std::string& optionsJson
);
std::string middlewareBefore(
    const std::string& pipelineJson,
    const std::string& requestJson
);
std::string middlewareAfter(
    const std::string& pipelineJson,
    const std::string& requestJson,
    const std::string& responseJson
);
std::int64_t sessionStoreCreate();
void sessionStoreClose(std::int64_t storeHandle);
std::string sessionCreate(
    std::int64_t storeHandle,
    std::int64_t lifetimeSeconds
);
bool sessionSet(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key,
    const std::string& value
);
std::string sessionGet(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key
);
bool sessionHas(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key
);
bool sessionInvalidate(
    std::int64_t storeHandle,
    const std::string& sessionId
);
std::string sessionRotate(
    std::int64_t storeHandle,
    const std::string& sessionId,
    std::int64_t lifetimeSeconds
);
std::string sessionCsrfToken(
    std::int64_t storeHandle,
    const std::string& sessionId
);
bool sessionValidateCsrf(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& token
);
std::string sessionCookie(
    const std::string& name,
    const std::string& sessionId,
    std::int64_t lifetimeSeconds,
    bool secure,
    const std::string& sameSite
);

} // namespace zplus::web
