#pragma once

#include "Type.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace zplus::semantic {

enum class NativeAvailability {
    InterpreterOnly,
    BackendDependent,
    WindowsX64,
    LinuxX64,
    WindowsAndLinuxX64,
    PlatformSpecific
};

struct BuiltinMetadata {
    std::string name;
    Type signature;
    bool variadic = false;
    bool interpreterAvailable = true;
    NativeAvailability nativeAvailability = NativeAvailability::BackendDependent;

    std::size_t minimumArgumentCount() const noexcept {
        return signature.requiredParameterCount;
    }
};

class BuiltinRegistry {
public:
    void clear();
    void add(std::string name, Type signature, bool variadic = false);
    const BuiltinMetadata* find(const std::string& name) const noexcept;
    const std::vector<BuiltinMetadata>& entries() const noexcept;

private:
    std::vector<BuiltinMetadata> entries_;
    std::unordered_map<std::string, std::size_t> indices_;
};

} // namespace zplus::semantic
