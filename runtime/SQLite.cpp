#include "SQLite.hpp"

#include "Json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define SQLITE_CALL __cdecl
using SqliteLibrary = HMODULE;
#else
#include <dlfcn.h>
#define SQLITE_CALL
using SqliteLibrary = void*;
#endif

namespace zplus::sqlite {
namespace {

struct sqlite3;
struct sqlite3_stmt;

constexpr int sqliteOk = 0;
constexpr int sqliteInteger = 1;
constexpr int sqliteFloat = 2;
constexpr int sqliteText = 3;
constexpr int sqliteBlob = 4;
constexpr int sqliteNull = 5;
constexpr int sqliteRow = 100;
constexpr int sqliteDone = 101;
constexpr int sqliteOpenReadWrite = 0x00000002;
constexpr int sqliteOpenCreate = 0x00000004;
constexpr int sqliteOpenFullMutex = 0x00010000;
constexpr std::size_t maximumSqlBytes = 1024 * 1024;
constexpr std::size_t maximumQueryRows = 100000;
constexpr std::size_t maximumQueryJsonBytes = 16 * 1024 * 1024;

using OpenV2 = int (SQLITE_CALL*)(const char*, sqlite3**, int, const char*);
using CloseV2 = int (SQLITE_CALL*)(sqlite3*);
using PrepareV2 = int (SQLITE_CALL*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
using Step = int (SQLITE_CALL*)(sqlite3_stmt*);
using Finalize = int (SQLITE_CALL*)(sqlite3_stmt*);
using BindParameterCount = int (SQLITE_CALL*)(sqlite3_stmt*);
using BindNull = int (SQLITE_CALL*)(sqlite3_stmt*, int);
using BindInt64 = int (SQLITE_CALL*)(sqlite3_stmt*, int, std::int64_t);
using BindDouble = int (SQLITE_CALL*)(sqlite3_stmt*, int, double);
using BindText = int (SQLITE_CALL*)(sqlite3_stmt*, int, const char*, int, void (*)(void*));
using ColumnCount = int (SQLITE_CALL*)(sqlite3_stmt*);
using ColumnName = const char* (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnType = int (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnInt64 = std::int64_t (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnDouble = double (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnText = const unsigned char* (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnBlob = const void* (SQLITE_CALL*)(sqlite3_stmt*, int);
using ColumnBytes = int (SQLITE_CALL*)(sqlite3_stmt*, int);
using Changes = int (SQLITE_CALL*)(sqlite3*);
using ErrorMessage = const char* (SQLITE_CALL*)(sqlite3*);
using BusyTimeout = int (SQLITE_CALL*)(sqlite3*, int);

class SqliteApi {
public:
    SqliteApi() {
#ifdef _WIN32
        for (const char* name : {"sqlite3.dll", "libsqlite3-0.dll"}) {
            library_ = LoadLibraryA(name);
            if (library_) break;
        }
        if (library_) {
            char path[MAX_PATH];
            if (GetModuleFileNameA(library_, path, MAX_PATH) > 0) {
                libraryPath_ = path;
            }
        }
#else
        for (const char* name : {"libsqlite3.so.0", "libsqlite3.so"}) {
            library_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (library_) break;
        }
#endif
        if (!library_) {
            throw std::runtime_error(
                "SQLite runtime was not found; install sqlite3.dll or libsqlite3");
        }
        openV2 = load<OpenV2>("sqlite3_open_v2");
        closeV2 = load<CloseV2>("sqlite3_close_v2");
        prepareV2 = load<PrepareV2>("sqlite3_prepare_v2");
        step = load<Step>("sqlite3_step");
        finalize = load<Finalize>("sqlite3_finalize");
        bindParameterCount = load<BindParameterCount>("sqlite3_bind_parameter_count");
        bindNull = load<BindNull>("sqlite3_bind_null");
        bindInt64 = load<BindInt64>("sqlite3_bind_int64");
        bindDouble = load<BindDouble>("sqlite3_bind_double");
        bindText = load<BindText>("sqlite3_bind_text");
        columnCount = load<ColumnCount>("sqlite3_column_count");
        columnName = load<ColumnName>("sqlite3_column_name");
        columnType = load<ColumnType>("sqlite3_column_type");
        columnInt64 = load<ColumnInt64>("sqlite3_column_int64");
        columnDouble = load<ColumnDouble>("sqlite3_column_double");
        columnText = load<ColumnText>("sqlite3_column_text");
        columnBlob = load<ColumnBlob>("sqlite3_column_blob");
        columnBytes = load<ColumnBytes>("sqlite3_column_bytes");
        changes = load<Changes>("sqlite3_changes");
        errorMessage = load<ErrorMessage>("sqlite3_errmsg");
        busyTimeout = load<BusyTimeout>("sqlite3_busy_timeout");
#ifndef _WIN32
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(openV2), &info) != 0 && info.dli_fname) {
            libraryPath_ = info.dli_fname;
        }
#endif
    }

    template<typename Function>
    Function load(const char* name) {
#ifdef _WIN32
        const auto address = reinterpret_cast<void*>(GetProcAddress(library_, name));
#else
        const auto address = dlsym(library_, name);
#endif
        if (!address) {
            std::string detail = std::string("SQLite runtime is missing ") + name;
            if (!libraryPath_.empty()) {
                detail += " (loaded from " + libraryPath_ + ")";
            }
            throw std::runtime_error(detail);
        }
        return reinterpret_cast<Function>(address);
    }

    OpenV2 openV2 = nullptr;
    CloseV2 closeV2 = nullptr;
    PrepareV2 prepareV2 = nullptr;
    Step step = nullptr;
    Finalize finalize = nullptr;
    BindParameterCount bindParameterCount = nullptr;
    BindNull bindNull = nullptr;
    BindInt64 bindInt64 = nullptr;
    BindDouble bindDouble = nullptr;
    BindText bindText = nullptr;
    ColumnCount columnCount = nullptr;
    ColumnName columnName = nullptr;
    ColumnType columnType = nullptr;
    ColumnInt64 columnInt64 = nullptr;
    ColumnDouble columnDouble = nullptr;
    ColumnText columnText = nullptr;
    ColumnBlob columnBlob = nullptr;
    ColumnBytes columnBytes = nullptr;
    Changes changes = nullptr;
    ErrorMessage errorMessage = nullptr;
    BusyTimeout busyTimeout = nullptr;
    std::string libraryPath_;

private:
    SqliteLibrary library_{};
};

SqliteApi& api() {
    static SqliteApi instance;
    return instance;
}

std::string errorMessage(sqlite3* database, int code, const char* operation) {
    const char* detail = database ? api().errorMessage(database) : nullptr;
    return std::string("SQLite ") + operation + " failed (" +
        std::to_string(code) + "): " + (detail ? detail : "unknown error");
}

struct Database {
    explicit Database(sqlite3* connection) : connection(connection) {}
    ~Database() {
        if (connection) api().closeV2(connection);
    }
    sqlite3* connection = nullptr;
    std::mutex mutex;
};

std::mutex databasesMutex;
std::unordered_map<std::int64_t, std::shared_ptr<Database>> databases;
std::atomic<std::int64_t> nextDatabaseHandle{1};

std::shared_ptr<Database> getDatabase(std::int64_t handle) {
    std::lock_guard lock(databasesMutex);
    const auto found = databases.find(handle);
    if (found == databases.end()) {
        throw std::invalid_argument("invalid SQLite database handle");
    }
    return found->second;
}

std::string base64(const unsigned char* bytes, std::size_t length) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((length + 2) / 3) * 4);
    for (std::size_t i = 0; i < length; i += 3) {
        const std::uint32_t first = bytes[i];
        const std::uint32_t second = i + 1 < length ? bytes[i + 1] : 0;
        const std::uint32_t third = i + 2 < length ? bytes[i + 2] : 0;
        const std::uint32_t value = (first << 16) | (second << 8) | third;
        output.push_back(alphabet[(value >> 18) & 0x3f]);
        output.push_back(alphabet[(value >> 12) & 0x3f]);
        output.push_back(i + 1 < length ? alphabet[(value >> 6) & 0x3f] : '=');
        output.push_back(i + 2 < length ? alphabet[value & 0x3f] : '=');
    }
    return output;
}

class Statement {
public:
    Statement(sqlite3* database, const std::string& sql) : database_(database) {
        if (sql.empty() || sql.size() > maximumSqlBytes ||
            sql.find('\0') != std::string::npos) {
            throw std::invalid_argument("SQL must be non-empty, NUL-free, and at most 1 MiB");
        }
        const char* tail = nullptr;
        const int result = api().prepareV2(database_, sql.data(),
            static_cast<int>(sql.size()), &statement_, &tail);
        if (result != sqliteOk) throw std::runtime_error(errorMessage(database_, result, "prepare"));
        if (!statement_) throw std::invalid_argument("SQL did not contain a statement");
        for (const char* cursor = tail; cursor && *cursor; ++cursor) {
            if (!std::isspace(static_cast<unsigned char>(*cursor))) {
                api().finalize(statement_);
                statement_ = nullptr;
                throw std::invalid_argument("only one SQL statement is allowed per call");
            }
        }
    }

    ~Statement() {
        if (statement_) api().finalize(statement_);
    }

    sqlite3_stmt* get() const noexcept { return statement_; }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_ = nullptr;
};

void bindParameters(
    sqlite3* database,
    sqlite3_stmt* statement,
    const std::string& parametersJson
) {
    const json::Value parsed = json::parse(parametersJson);
    const auto* parameters = std::get_if<json::Value::Array>(&parsed.data);
    if (!parameters) {
        throw std::invalid_argument("SQLite parameters must be a JSON array");
    }
    const int count = api().bindParameterCount(statement);
    if (parameters->size() != static_cast<std::size_t>(count)) {
        throw std::invalid_argument(
            "SQLite parameter count does not match the SQL placeholders");
    }
    for (std::size_t i = 0; i < parameters->size(); ++i) {
        const int index = static_cast<int>(i + 1);
        const auto& parameter = (*parameters)[i].data;
        int result = sqliteOk;
        if (std::holds_alternative<std::nullptr_t>(parameter)) {
            result = api().bindNull(statement, index);
        } else if (const auto* boolean = std::get_if<bool>(&parameter)) {
            result = api().bindInt64(statement, index, *boolean ? 1 : 0);
        } else if (const auto* number = std::get_if<double>(&parameter)) {
            if (!std::isfinite(*number)) {
                throw std::invalid_argument("SQLite numeric parameters must be finite");
            }
            result = api().bindDouble(statement, index, *number);
        } else if (const auto* text = std::get_if<std::string>(&parameter)) {
            if (text->size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                throw std::length_error("SQLite text parameter is too large");
            }
            result = api().bindText(statement, index, text->data(),
                static_cast<int>(text->size()), reinterpret_cast<void (*)(void*)>(-1));
        } else {
            throw std::invalid_argument(
                "SQLite parameters may only be strings, numbers, booleans, or null");
        }
        if (result != sqliteOk) {
            throw std::runtime_error(errorMessage(database, result, "bind"));
        }
    }
}

json::Value columnValue(sqlite3_stmt* statement, int column) {
    switch (api().columnType(statement, column)) {
        case sqliteNull: return json::Value{nullptr};
        case sqliteInteger:
            return json::Value{static_cast<double>(api().columnInt64(statement, column))};
        case sqliteFloat: return json::Value{api().columnDouble(statement, column)};
        case sqliteText: {
            const auto* bytes = api().columnText(statement, column);
            const int length = api().columnBytes(statement, column);
            return json::Value{std::string(
                reinterpret_cast<const char*>(bytes ? bytes :
                    reinterpret_cast<const unsigned char*>("")),
                static_cast<std::size_t>(length))};
        }
        case sqliteBlob: {
            const auto* bytes = static_cast<const unsigned char*>(
                api().columnBlob(statement, column));
            const int length = api().columnBytes(statement, column);
            json::Value::Object blob;
            blob.emplace_back("$blob", json::Value{
                base64(bytes, static_cast<std::size_t>(length))});
            return json::Value{std::move(blob)};
        }
        default: throw std::runtime_error("SQLite returned an unknown column type");
    }
}

std::string uniqueColumnName(
    const std::string& name,
    const std::unordered_map<std::string, std::size_t>& previous
) {
    const auto found = previous.find(name);
    return found == previous.end()
        ? name : name + "_" + std::to_string(found->second + 1);
}

} // namespace

std::string runtimeLibraryPath() {
    return api().libraryPath_;
}

std::int64_t openDatabase(const std::string& path) {
    if (path.empty()) throw std::invalid_argument("SQLite database path must not be empty");
    if (path.find('\0') != std::string::npos) {
        throw std::invalid_argument("SQLite database path must not contain NUL bytes");
    }
    sqlite3* connection = nullptr;
    const int result = api().openV2(path.c_str(), &connection,
        sqliteOpenReadWrite | sqliteOpenCreate | sqliteOpenFullMutex, nullptr);
    if (result != sqliteOk) {
        const std::string message = errorMessage(connection, result, "open");
        if (connection) api().closeV2(connection);
        throw std::runtime_error(message);
    }
    api().busyTimeout(connection, 5000);
    const std::int64_t handle = nextDatabaseHandle.fetch_add(1);
    if (handle <= 0) {
        api().closeV2(connection);
        throw std::overflow_error("SQLite handle space exhausted");
    }
    auto database = std::make_shared<Database>(connection);
    std::lock_guard lock(databasesMutex);
    databases.emplace(handle, std::move(database));
    return handle;
}

void closeDatabase(std::int64_t handle) {
    std::shared_ptr<Database> database;
    {
        std::lock_guard lock(databasesMutex);
        const auto found = databases.find(handle);
        if (found == databases.end()) {
            throw std::invalid_argument("invalid SQLite database handle");
        }
        database = std::move(found->second);
        databases.erase(found);
    }
    std::lock_guard lock(database->mutex);
    const int result = api().closeV2(database->connection);
    if (result != sqliteOk) throw std::runtime_error(
        errorMessage(database->connection, result, "close"));
    database->connection = nullptr;
}

std::int64_t execute(
    std::int64_t handle,
    const std::string& sql,
    const std::string& parametersJson
) {
    const auto database = getDatabase(handle);
    std::lock_guard lock(database->mutex);
    Statement statement(database->connection, sql);
    bindParameters(database->connection, statement.get(), parametersJson);
    int result = sqliteOk;
    while ((result = api().step(statement.get())) == sqliteRow) {}
    if (result != sqliteDone) {
        throw std::runtime_error(errorMessage(database->connection, result, "execute"));
    }
    return api().changes(database->connection);
}

std::string query(
    std::int64_t handle,
    const std::string& sql,
    const std::string& parametersJson
) {
    const auto database = getDatabase(handle);
    std::lock_guard lock(database->mutex);
    Statement statement(database->connection, sql);
    bindParameters(database->connection, statement.get(), parametersJson);
    json::Value::Array rows;
    const int columns = api().columnCount(statement.get());
    int result = sqliteOk;
    while ((result = api().step(statement.get())) == sqliteRow) {
        if (rows.size() >= maximumQueryRows) {
            throw std::length_error("SQLite query exceeded the row limit");
        }
        json::Value::Object row;
        std::unordered_map<std::string, std::size_t> names;
        for (int column = 0; column < columns; ++column) {
            const char* rawName = api().columnName(statement.get(), column);
            const std::string name = rawName ? rawName : "";
            const std::string uniqueName = uniqueColumnName(name, names);
            ++names[name];
            row.emplace_back(uniqueName, columnValue(statement.get(), column));
        }
        rows.emplace_back(std::move(row));
    }
    if (result != sqliteDone) {
        throw std::runtime_error(errorMessage(database->connection, result, "query"));
    }
    std::string output = json::serialize(json::Value{std::move(rows)});
    if (output.size() > maximumQueryJsonBytes) {
        throw std::length_error("SQLite query JSON result exceeded the 16 MiB limit");
    }
    return output;
}

std::int64_t changes(std::int64_t handle) {
    const auto database = getDatabase(handle);
    std::lock_guard lock(database->mutex);
    return api().changes(database->connection);
}

void shutdownDatabases() noexcept {
    for (;;) {
        std::shared_ptr<Database> database;
        {
            std::lock_guard lock(databasesMutex);
            if (databases.empty()) return;
            auto found = databases.begin();
            database = std::move(found->second);
            databases.erase(found);
        }
        std::lock_guard lock(database->mutex);
        if (database->connection) {
            api().closeV2(database->connection);
            database->connection = nullptr;
        }
    }
}

} // namespace zplus::sqlite
