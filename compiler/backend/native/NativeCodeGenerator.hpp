#pragma once

#include "../../ir/IR.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zplus::backend::native {

struct NativeImage {
    struct RelativeFixup {
        std::size_t displacementOffset = 0;
        std::size_t targetDataOffset = 0;
    };

    struct ImportFixup {
        std::size_t displacementOffset = 0;
        std::string name;
        std::string library = "KERNEL32.dll";
    };

    std::vector<std::uint8_t> code;
    std::vector<std::uint8_t> data;
    std::size_t entryOffset = 0;
    bool writableData = false;
    std::vector<RelativeFixup> dataFixups;
    std::vector<ImportFixup> importFixups;
};

class NativeCodeGenerator {
public:
    std::vector<std::uint8_t> generate(
        const zplus::ir::Module& module
    ) const;

    void writeExecutable(
        const zplus::ir::Module& module,
        const std::filesystem::path& output,
        bool windowsGui = false
    ) const;
};

} // namespace zplus::backend::native
