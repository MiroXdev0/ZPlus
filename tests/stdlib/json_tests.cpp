#include "Json.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireInvalid(const std::string& value, const char* message) {
    try {
        (void)zplus::json::parse(value);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        const std::string source =
            R"({"user":{"name":"Mira \uD83D\uDE80","active":true},"items":[3,{"nested":[null,"é"]}]})";
        const std::string compact = zplus::json::compact(source);
        require(zplus::json::valid(source), "valid nested JSON was rejected");
        require(zplus::json::getString(source, "/user/name") == "Mira \xf0\x9f\x9a\x80",
            "Unicode surrogate pair decoding failed");
        require(zplus::json::getBoolean(source, "/user/active"),
            "nested boolean access failed");
        require(zplus::json::arrayLength(source, "/items") == 2,
            "nested array length failed");
        require(zplus::json::getRaw(source, "/items/1/nested/0") == "null",
            "JSON pointer array/object traversal failed");
        require(zplus::json::arrayGet(source, "/items", 1) ==
            R"({"nested":[null,"é"]})", "raw JSON array access failed");
        require(zplus::json::valueKind(source, "/items/1") == "object",
            "JSON value kind failed");
        require(zplus::json::compact(compact) == compact,
            "JSON compact serialization was not stable");
        require(zplus::json::quote("quote\" slash\\ newline\n") ==
            R"("quote\" slash\\ newline\n")", "JSON string escaping failed");
        require(!zplus::json::valid(R"({"bad":01})"),
            "JSON leading zero was accepted");
        requireInvalid(R"("\uD800")", "unpaired high surrogate was accepted");
        requireInvalid(R"([1,])", "trailing array comma was accepted");
        requireInvalid(R"({"x":1,})", "trailing object comma was accepted");
        std::cout << "JSON standard-library tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
