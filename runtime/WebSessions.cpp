#include "Web.hpp"

#include "Base64.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace zplus::web {
namespace {

constexpr std::int64_t maximumSessionLifetimeSeconds = 31'536'000;
constexpr std::size_t maximumSessionsPerStore = 4096;
constexpr std::size_t maximumSessionValueBytes = 16 * 1024;
constexpr std::size_t maximumSessionDataBytes = 64 * 1024;

struct Session {
    std::chrono::steady_clock::time_point expiresAt;
    std::map<std::string, std::string> values;
    std::string csrfToken;
};

struct Store {
    std::mutex mutex;
    std::unordered_map<std::string, Session> sessions;
};

std::mutex storesMutex;
std::unordered_map<std::int64_t, std::shared_ptr<Store>> stores;
std::int64_t nextStoreHandle = 1;

void fillSecureRandom(std::span<unsigned char> bytes) {
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) throw std::runtime_error("operating system random generator failed");
#elif defined(__linux__)
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("operating system random generator failed");
        offset += static_cast<std::size_t>(count);
    }
#else
    const int file = ::open("/dev/urandom", O_RDONLY);
    if (file < 0) throw std::runtime_error("operating system random generator is unavailable");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::read(file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            ::close(file);
            throw std::runtime_error("operating system random generator failed");
        }
        offset += static_cast<std::size_t>(count);
    }
    ::close(file);
#endif
}

std::shared_ptr<Store> findStore(std::int64_t handle) {
    std::lock_guard lock(storesMutex);
    const auto found = stores.find(handle);
    if (found == stores.end()) throw std::out_of_range("session store handle is invalid");
    return found->second;
}

bool validSessionId(std::string_view id) {
    return id.size() == 43 &&
        std::all_of(id.begin(), id.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        });
}

std::string randomToken() {
    std::array<unsigned char, 32> random{};
    fillSecureRandom(random);
    return encoding::base64UrlEncode(std::string_view(
        reinterpret_cast<const char*>(random.data()), random.size()));
}

bool constantTimeEqual(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return difference == 0;
}

bool validLifetime(std::int64_t seconds) {
    return seconds > 0 && seconds <= maximumSessionLifetimeSeconds;
}

Session* findSession(Store& store, const std::string& id) {
    if (!validSessionId(id)) return nullptr;
    const auto found = store.sessions.find(id);
    if (found == store.sessions.end()) return nullptr;
    if (found->second.expiresAt <= std::chrono::steady_clock::now()) {
        store.sessions.erase(found);
        return nullptr;
    }
    return &found->second;
}

} // namespace

std::int64_t sessionStoreCreate() {
    std::lock_guard lock(storesMutex);
    if (nextStoreHandle == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("session store handle space is exhausted");
    }
    const auto handle = nextStoreHandle++;
    stores.emplace(handle, std::make_shared<Store>());
    return handle;
}

void sessionStoreClose(std::int64_t storeHandle) {
    std::lock_guard lock(storesMutex);
    if (stores.erase(storeHandle) == 0) {
        throw std::out_of_range("session store handle is invalid");
    }
}

std::string sessionCreate(std::int64_t storeHandle, std::int64_t lifetimeSeconds) {
    if (!validLifetime(lifetimeSeconds)) {
        throw std::invalid_argument("session lifetime must be between 1 second and 1 year");
    }
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    const auto now = std::chrono::steady_clock::now();
    for (auto item = store->sessions.begin(); item != store->sessions.end();) {
        if (item->second.expiresAt <= now) item = store->sessions.erase(item);
        else ++item;
    }
    if (store->sessions.size() >= maximumSessionsPerStore) {
        throw std::length_error("session store has reached its 4096-session limit");
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::string id = randomToken();
        if (!store->sessions.contains(id)) {
            store->sessions.emplace(id, Session{
                now + std::chrono::seconds(lifetimeSeconds), {}});
            return id;
        }
    }
    throw std::runtime_error("could not create a unique session identifier");
}

bool sessionSet(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key,
    const std::string& value
) {
    if (key.empty() || key.size() > 1024) {
        throw std::invalid_argument("session key must contain between 1 and 1024 bytes");
    }
    if (value.size() > maximumSessionValueBytes) {
        throw std::length_error("session value exceeds the 16384-byte limit");
    }
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* session = findSession(*store, sessionId);
    if (!session) return false;
    std::size_t currentBytes = 0;
    for (const auto& [storedKey, storedValue] : session->values) {
        if (storedKey == key) continue;
        if (storedKey.size() > maximumSessionDataBytes - currentBytes) {
            throw std::length_error("session data exceeds the 65536-byte limit");
        }
        currentBytes += storedKey.size();
        if (storedValue.size() > maximumSessionDataBytes - currentBytes) {
            throw std::length_error("session data exceeds the 65536-byte limit");
        }
        currentBytes += storedValue.size();
    }
    if (key.size() > maximumSessionDataBytes - currentBytes ||
        value.size() > maximumSessionDataBytes - currentBytes - key.size()) {
        throw std::length_error("session data exceeds the 65536-byte limit");
    }
    session->values.insert_or_assign(key, value);
    return true;
}

std::string sessionGet(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key
) {
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* session = findSession(*store, sessionId);
    if (!session) return {};
    const auto value = session->values.find(key);
    return value == session->values.end() ? std::string{} : value->second;
}

bool sessionHas(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& key
) {
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* session = findSession(*store, sessionId);
    return session && session->values.contains(key);
}

bool sessionInvalidate(std::int64_t storeHandle, const std::string& sessionId) {
    if (!validSessionId(sessionId)) return false;
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    return store->sessions.erase(sessionId) != 0;
}

std::string sessionRotate(
    std::int64_t storeHandle,
    const std::string& sessionId,
    std::int64_t lifetimeSeconds
) {
    if (!validLifetime(lifetimeSeconds)) {
        throw std::invalid_argument("session lifetime must be between 1 second and 1 year");
    }
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* current = findSession(*store, sessionId);
    if (!current) return {};
    Session rotated{
        std::chrono::steady_clock::now() + std::chrono::seconds(lifetimeSeconds),
        current->values,
        {}};
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::string id = randomToken();
        if (store->sessions.contains(id)) continue;
        store->sessions.emplace(id, std::move(rotated));
        store->sessions.erase(sessionId);
        return id;
    }
    throw std::runtime_error("could not rotate the session identifier");
}

std::string sessionCsrfToken(
    std::int64_t storeHandle,
    const std::string& sessionId
) {
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* session = findSession(*store, sessionId);
    if (!session) return {};
    if (session->csrfToken.empty()) session->csrfToken = randomToken();
    return session->csrfToken;
}

bool sessionValidateCsrf(
    std::int64_t storeHandle,
    const std::string& sessionId,
    const std::string& token
) {
    if (token.size() != 43) return false;
    const auto store = findStore(storeHandle);
    std::lock_guard lock(store->mutex);
    Session* session = findSession(*store, sessionId);
    return session && !session->csrfToken.empty() &&
        constantTimeEqual(session->csrfToken, token);
}

std::string sessionCookie(
    const std::string& name,
    const std::string& sessionId,
    std::int64_t lifetimeSeconds,
    bool secure,
    const std::string& sameSite
) {
    if (!validLifetime(lifetimeSeconds)) {
        throw std::invalid_argument("session lifetime must be between 1 second and 1 year");
    }
    if (!validSessionId(sessionId)) {
        throw std::invalid_argument("session cookie requires a valid session identifier");
    }
    return cookieHeader(name, sessionId, lifetimeSeconds, "/", "", secure, true, sameSite);
}

} // namespace zplus::web
