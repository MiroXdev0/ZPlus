#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace zplus::json {

struct Value {
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Storage data = nullptr;
};

Value parse(std::string_view source, std::size_t maximumDepth = 128);
std::string serialize(const Value& value);
std::string quote(std::string_view value);
const Value* atPointer(const Value& root, std::string_view pointer);
std::string kind(const Value& value);
std::string compact(std::string_view source);
bool valid(std::string_view source);
std::string getRaw(std::string_view source, std::string_view pointer);
std::string getString(std::string_view source, std::string_view pointer);
double getNumber(std::string_view source, std::string_view pointer);
bool getBoolean(std::string_view source, std::string_view pointer);
std::size_t arrayLength(std::string_view source, std::string_view pointer);
std::string arrayGet(std::string_view source, std::string_view pointer, std::size_t index);
std::string valueKind(std::string_view source, std::string_view pointer);

} // namespace zplus::json
