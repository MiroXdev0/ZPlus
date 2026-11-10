#include "compiler/backend/native/X64CodeGenerator.hpp"
#include "compiler/backend/native/ELFWriter.hpp"
#include "compiler/backend/native/PEWriter.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
           (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}

std::uint64_t read64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint64_t>(read32(bytes, offset)) |
           (static_cast<std::uint64_t>(read32(bytes, offset + 4)) << 32);
}

std::uint16_t read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes.at(offset)) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8);
}

bool testExecutableFormats(const char* elfOutputPath) {
    zplus::backend::native::NativeImage image;
    image.code = {0xE8, 0, 0, 0, 0};
    image.data = {0};
    image.dataFixups.push_back({1, 0});
    image.entryOffset = 0;

    const auto elf = zplus::backend::native::writeELF(image);
    if (elf.size() < 0x2001 || elf[0] != 0x7F || elf[1] != 'E' ||
        elf[2] != 'L' || elf[3] != 'F' || elf[4] != 2 || elf[5] != 1 ||
        read64(elf, 24) != 0x401000 || read32(elf, 68) != 4 ||
        read32(elf, 124) != 5 || read32(elf, 180) != 4 ||
        read32(elf, 0x1001) != 0xFFB) {
        std::cerr << "ELF writer produced an invalid x86-64 executable image\n";
        return false;
    }
    if (elfOutputPath) {
        std::ofstream file(elfOutputPath, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(elf.data()),
                                 static_cast<std::streamsize>(elf.size()))) {
            std::cerr << "failed to write ELF format-test image\n";
            return false;
        }
    }

    image.code = {0xFF, 0x15, 0, 0, 0, 0, 0xC3};
    image.dataFixups.clear();
    image.importFixups = {{2, "ExitProcess"}};
    const auto pe = zplus::backend::native::writePE(image);
    const std::uint32_t peOffset = read32(pe, 0x3C);
    const std::size_t optionalHeader = static_cast<std::size_t>(peOffset) + 4 + 20;
    if (pe[0] != 'M' || pe[1] != 'Z' ||
        pe.at(peOffset) != 'P' || pe.at(peOffset + 1) != 'E' ||
        read16(pe, optionalHeader) != 0x20B ||
        read32(pe, optionalHeader + 16) != 0x1000 ||
        read16(pe, peOffset + 6) != 2 ||
        read32(pe, 0x202) == 0) {
        std::cerr << "PE writer produced an invalid PE32+ executable image\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 || !testExecutableFormats(argc == 2 ? argv[1] : nullptr)) {
        return 1;
    }

    zplus::ir::Module module;
    auto& function = module.createFunction("main");
    auto& block = function.createBlock("entry");
    block.emit(zplus::ir::Instruction("unsupported_native_opcode"));

    try {
        (void)zplus::backend::native::generateX64(module, false);
    }
    catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message.find("unsupported_native_opcode") != std::string::npos) {
            return 0;
        }
        std::cerr << "unexpected native backend diagnostic: " << message << '\n';
        return 1;
    }

    std::cerr << "native backend accepted an unsupported IR opcode\n";
    return 1;
}
