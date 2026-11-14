#include "BuiltinRegistry.hpp"

#include <stdexcept>
#include <utility>

namespace zplus::semantic {
namespace {

NativeAvailability nativeAvailabilityFor(const std::string& name) {
    if (name == "print" || name == "println" || name == "time") {
        return NativeAvailability::InterpreterOnly;
    }
    if (name == "to_string" ||
        name == "length" ||
        name == "push" ||
        name == "pop" ||
        name == "clear" ||
        name == "insert" ||
        name == "remove" ||
        name == "contains") {
        return NativeAvailability::WindowsX64;
    }
    if (name.rfind("__zmemory_", 0) == 0 ||
        name.rfind("__zstdlib_", 0) == 0 ||
        name.rfind("__zjson_", 0) == 0 ||
        name.rfind("__zhttp_", 0) == 0 ||
        name.rfind("__zweb_", 0) == 0 ||
        name.rfind("__zsqlite_", 0) == 0 ||
        name.rfind("__zgraphics_", 0) == 0) {
        return NativeAvailability::WindowsX64;
    }
    if (name.rfind("__zaudio_", 0) == 0 ||
        name.rfind("__zphysics_", 0) == 0 ||
        name == "read" ||
        name == "read_number" ||
        name == "read_integer" ||
        name == "read_boolean") {
        return NativeAvailability::WindowsAndLinuxX64;
    }
    if (name == "say" || name == "printz" || name == "printm") {
        return NativeAvailability::PlatformSpecific;
    }
    return NativeAvailability::BackendDependent;
}

} // namespace

void BuiltinRegistry::clear() {
    entries_.clear();
    indices_.clear();
}

void BuiltinRegistry::add(
    std::string name,
    Type signature,
    bool variadic
) {
    if (name.empty() || signature.kind != TypeKind::Function) {
        throw std::invalid_argument("builtin metadata requires a name and function signature");
    }
    const std::size_t index = entries_.size();
    const std::string key = name;
    const auto [it, inserted] = indices_.emplace(key, index);
    if (!inserted) {
        throw std::invalid_argument("duplicate builtin metadata for '" + name + "'");
    }
    entries_.push_back({
        std::move(name),
        std::move(signature),
        variadic,
        true,
        nativeAvailabilityFor(key)
    });
}

const BuiltinMetadata* BuiltinRegistry::find(
    const std::string& name
) const noexcept {
    const auto found = indices_.find(name);
    return found == indices_.end() ? nullptr : &entries_[found->second];
}

const std::vector<BuiltinMetadata>& BuiltinRegistry::entries() const noexcept {
    return entries_;
}

} // namespace zplus::semantic
