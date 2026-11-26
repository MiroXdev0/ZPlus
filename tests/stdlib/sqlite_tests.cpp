#include "Json.hpp"
#include "SQLite.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Exception, typename Operation>
void requireThrows(Operation&& operation, const char* message) {
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        std::cerr << "[sqlite-unit] loading SQLite runtime\n";
        const auto database = zplus::sqlite::openDatabase(":memory:");
        std::cerr << "[sqlite-unit] opened database (runtime: "
            << zplus::sqlite::runtimeLibraryPath() << ")\n";
        require(zplus::sqlite::execute(database,
            "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER, "
            "active BOOLEAN, note TEXT, payload BLOB)", "[]") == 0,
            "SQLite schema creation failed");
        std::cerr << "[sqlite-unit] schema created\n";

        const std::string hostileText = "Alice'); DROP TABLE users; --";
        require(zplus::sqlite::execute(database,
            "INSERT INTO users (name, age, active, note, payload) "
            "VALUES (?, ?, ?, ?, X'00FF10')",
            "[\"Alice'); DROP TABLE users; --\",29,true,null]") == 1,
            "parameterized SQLite insert failed");
        const std::string rows = zplus::sqlite::query(database,
            "SELECT id, name, age, active, note, payload FROM users WHERE name = ?",
            "[\"Alice'); DROP TABLE users; --\"]");
        require(zplus::json::arrayLength(rows, "") == 1,
            "parameterized SQLite query failed");
        require(zplus::json::getString(rows, "/0/name") == hostileText,
            "SQLite text parameter was modified or interpreted as SQL");
        require(zplus::json::getNumber(rows, "/0/age") == 29,
            "SQLite integer value was not returned");
        require(zplus::json::getNumber(rows, "/0/active") == 1,
            "SQLite boolean binding was not preserved");
        require(zplus::json::getRaw(rows, "/0/note") == "null",
            "SQLite NULL was not preserved");
        require(zplus::json::getString(rows, "/0/payload/$blob") == "AP8Q",
            "SQLite BLOB did not use base64 JSON encoding");
        std::cerr << "[sqlite-unit] parameterized insert/query verified\n";

        zplus::sqlite::execute(database, "BEGIN", "[]");
        zplus::sqlite::execute(database,
            "INSERT INTO users (name) VALUES (?)", R"(["rolled back"])");
        zplus::sqlite::execute(database, "ROLLBACK", "[]");
        require(zplus::sqlite::query(database,
            "SELECT id FROM users WHERE name = ?", R"(["rolled back"])") == "[]",
            "SQLite rollback failed");
        std::cerr << "[sqlite-unit] rollback verified\n";

        zplus::sqlite::execute(database, "BEGIN", "[]");
        zplus::sqlite::execute(database,
            "INSERT INTO users (name) VALUES (?)", R"(["committed"])");
        zplus::sqlite::execute(database, "COMMIT", "[]");
        require(zplus::json::getString(
                zplus::sqlite::query(database,
                    "SELECT name FROM users WHERE name = ?", R"(["committed"])"),
                "/0/name") == "committed",
            "SQLite commit failed");

        const std::string duplicateColumns = zplus::sqlite::query(
            database, "SELECT name, name FROM users WHERE id = 1", "[]");
        require(zplus::json::getString(duplicateColumns, "/0/name") == hostileText &&
                zplus::json::getString(duplicateColumns, "/0/name_2") == hostileText,
            "duplicate SQLite result columns were not disambiguated");
        require(zplus::sqlite::changes(database) == 1,
            "SQLite changes count was incorrect");
        requireThrows<std::invalid_argument>([&] {
            (void)zplus::sqlite::execute(database,
                "INSERT INTO users (name) VALUES (?)", "[]");
        }, "SQLite accepted a mismatched parameter count");
        requireThrows<std::invalid_argument>([&] {
            (void)zplus::sqlite::execute(database,
                "DELETE FROM users; DELETE FROM users", "[]");
        }, "SQLite accepted multiple statements");
        requireThrows<std::runtime_error>([&] {
            (void)zplus::sqlite::execute(database, "SELECT * FROM missing_table", "[]");
        }, "SQLite did not report an invalid table");
        std::cerr << "[sqlite-unit] commit and error handling verified\n";

        zplus::sqlite::closeDatabase(database);
        requireThrows<std::invalid_argument>([&] {
            (void)zplus::sqlite::changes(database);
        }, "closed SQLite database handle remained valid");
        std::cout << "SQLite runtime tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
