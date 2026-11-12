#include "ELFWriter.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace zplus::backend::native {
namespace {

constexpr std::uint64_t kImageBase = 0x400000;
constexpr std::uint64_t kPageSize = 0x1000;
constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kCodeOffset = kPageSize;

std::string linuxInterpreter() {
    constexpr std::array candidates{
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
        "/usr/lib64/ld-linux-x86-64.so.2",
        "/lib/ld-linux-x86-64.so.2",
        "/lib/ld-musl-x86_64.so.1",
        "/lib/x86_64-linux-musl/libc.so",
    };
    for (const char* candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    throw std::runtime_error(
        "ELF writer: no supported x86-64 Linux dynamic loader was found; "
        "native runtime imports require glibc or musl");
}

void put16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("ELF writer: invalid 16-bit field offset");
    }
    for (unsigned i = 0; i < 2; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("ELF writer: invalid 32-bit field offset");
    }
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void put64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("ELF writer: invalid 64-bit field offset");
    }
    for (unsigned i = 0; i < 8; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::size_t alignTo(std::size_t value, std::size_t alignment) {
    if (value > std::numeric_limits<std::size_t>::max() - alignment + 1) {
        throw std::runtime_error("ELF writer: image is too large");
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

std::uint64_t checkedAddress(std::size_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - kImageBase) {
        throw std::runtime_error("ELF writer: address overflow");
    }
    return kImageBase + static_cast<std::uint64_t>(value);
}

void setLoadSegment(
    std::vector<std::uint8_t>& image,
    std::size_t offset,
    std::uint32_t flags,
    std::uint64_t fileOffset,
    std::uint64_t address,
    std::uint64_t size
) {
    put32(image, offset, 1); // PT_LOAD
    put32(image, offset + 4, flags);
    put64(image, offset + 8, fileOffset);
    put64(image, offset + 16, address);
    put64(image, offset + 24, address);
    put64(image, offset + 32, size);
    put64(image, offset + 40, size);
    put64(image, offset + 48, kPageSize);
}

} // namespace

std::vector<std::uint8_t> writeELF(const NativeImage& image) {
    if (image.code.empty() || image.entryOffset >= image.code.size()) {
        throw std::runtime_error("ELF writer: invalid code or entry point");
    }

    std::set<std::string> imports;
    std::set<std::string> runtimeNames;
    for (const auto& fixup : image.importFixups) {
        if (fixup.displacementOffset > image.code.size() ||
            image.code.size() - fixup.displacementOffset < 4) {
            throw std::runtime_error("ELF writer: invalid import fixup");
        }
        if ((fixup.library != "libzaudio_runtime.so" &&
             fixup.library != "libzphysics_runtime.so") || fixup.name.empty()) {
            throw std::runtime_error(
                "ELF writer: unsupported import '" + fixup.library + "!" +
                fixup.name + "'");
        }
        imports.insert(fixup.name);
        runtimeNames.insert(fixup.library);
    }

    const bool hasImports = !imports.empty();
    const std::size_t programHeaderCount = hasImports ? 5 : 3;
    const std::string interpreter = hasImports ? linuxInterpreter() : std::string{};
    const std::size_t interpreterOffset = alignTo(
        kElfHeaderSize + kProgramHeaderSize * programHeaderCount, 8);
    const std::size_t headerRegion = hasImports
        ? interpreterOffset + interpreter.size() + 1
        : kElfHeaderSize + kProgramHeaderSize * programHeaderCount;
    const std::size_t dataOffset = alignTo(kCodeOffset + image.code.size(), kPageSize);

    std::vector<std::uint8_t> data = image.data;
    std::unordered_map<std::string, std::size_t> importIndexes;
    std::size_t dynamicOffset = 0;
    std::size_t dynamicSize = 0;
    std::size_t gotOffset = 0;
    if (hasImports) {
        if (image.data.size() > std::numeric_limits<std::size_t>::max() - 7) {
            throw std::runtime_error("ELF writer: image is too large");
        }
        gotOffset = alignTo(data.size(), 8);
        const std::size_t symbolCount = imports.size();
        if (symbolCount > (std::numeric_limits<std::size_t>::max() - gotOffset) / 8) {
            throw std::runtime_error("ELF writer: image is too large");
        }
        data.resize(gotOffset + symbolCount * 8, 0);
        std::size_t symbolIndex = 0;
        for (const std::string& name : imports) {
            importIndexes.emplace(name, symbolIndex++);
        }

        const std::size_t relaOffset = alignTo(data.size(), 8);
        const std::size_t relaSize = symbolCount * 24;
        data.resize(relaOffset + relaSize, 0);

        const std::size_t symtabOffset = alignTo(data.size(), 8);
        const std::size_t symtabSize = (symbolCount + 1) * 24;
        data.resize(symtabOffset + symtabSize, 0);

        const std::size_t hashOffset = alignTo(data.size(), 4);
        const std::size_t hashSize = (4 + symbolCount) * 4;
        data.resize(hashOffset + hashSize, 0);

        const std::size_t strtabOffset = data.size();
        std::string strings("\0", 1);
        const auto addString = [&strings](const std::string& value) {
            const std::size_t offset = strings.size();
            strings.append(value);
            strings.push_back('\0');
            return offset;
        };
        std::vector<std::size_t> runtimeNameOffsets;
        for (const auto& runtimeName : runtimeNames) {
            runtimeNameOffsets.push_back(addString(runtimeName));
        }
        const std::size_t runpathOffset = addString("$ORIGIN");
        std::unordered_map<std::string, std::size_t> symbolNameOffsets;
        for (const std::string& name : imports) {
            symbolNameOffsets.emplace(name, addString(name));
        }
        data.insert(data.end(), strings.begin(), strings.end());

        const std::size_t dynamicEntryCount = 10 + runtimeNameOffsets.size();
        dynamicOffset = alignTo(data.size(), 8);
        dynamicSize = dynamicEntryCount * 16;
        data.resize(dynamicOffset + dynamicSize, 0);

        const std::uint64_t dataAddress = checkedAddress(dataOffset);
        const auto address = [dataAddress](std::size_t offset) {
            if (offset > std::numeric_limits<std::uint64_t>::max() - dataAddress) {
                throw std::runtime_error("ELF writer: dynamic address overflow");
            }
            return dataAddress + static_cast<std::uint64_t>(offset);
        };

        for (const auto& [name, index] : importIndexes) {
            const std::size_t symbolOffset = symtabOffset + (index + 1) * 24;
            put32(data, symbolOffset, static_cast<std::uint32_t>(symbolNameOffsets.at(name)));
            data[symbolOffset + 4] = 0x12; // STB_GLOBAL | STT_FUNC
            data[symbolOffset + 5] = 0;
            put16(data, symbolOffset + 6, 0);
            put64(data, symbolOffset + 8, 0);
            put64(data, symbolOffset + 16, 0);

            const std::size_t relocationOffset = relaOffset + index * 24;
            put64(data, relocationOffset, address(gotOffset + index * 8));
            put64(data, relocationOffset + 8,
                  (static_cast<std::uint64_t>(index + 1) << 32) | 6); // R_X86_64_GLOB_DAT
            put64(data, relocationOffset + 16, 0);

            const std::size_t chainOffset = hashOffset + 16 + index * 4;
            put32(data, chainOffset,
                  index + 1 < symbolCount
                      ? static_cast<std::uint32_t>(index + 2)
                      : 0);
        }
        put32(data, hashOffset, 1);
        put32(data, hashOffset + 4, static_cast<std::uint32_t>(symbolCount + 1));
        put32(data, hashOffset + 8, 1);

        std::size_t dynamicIndex = 0;
        auto putDynamic = [&](std::uint64_t tag, std::uint64_t value) {
            const std::size_t offset = dynamicOffset + dynamicIndex++ * 16;
            put64(data, offset, tag);
            put64(data, offset + 8, value);
        };
        for (const auto runtimeNameOffset : runtimeNameOffsets) {
            putDynamic(1, runtimeNameOffset); // DT_NEEDED
        }
        putDynamic(29, runpathOffset); // DT_RUNPATH
        putDynamic(5, address(strtabOffset)); // DT_STRTAB
        putDynamic(10, strings.size()); // DT_STRSZ
        putDynamic(6, address(symtabOffset)); // DT_SYMTAB
        putDynamic(11, 24); // DT_SYMENT
        putDynamic(4, address(hashOffset)); // DT_HASH
        putDynamic(7, address(relaOffset)); // DT_RELA
        putDynamic(8, relaSize); // DT_RELASZ
        putDynamic(9, 24); // DT_RELAENT
        putDynamic(0, 0); // DT_NULL
    }

    if (dataOffset > std::numeric_limits<std::size_t>::max() - data.size()) {
        throw std::runtime_error("ELF writer: image is too large");
    }
    std::vector<std::uint8_t> output(dataOffset + data.size(), 0);

    output[0] = 0x7F;
    output[1] = 'E';
    output[2] = 'L';
    output[3] = 'F';
    output[4] = 2; // ELFCLASS64
    output[5] = 1; // little endian
    output[6] = 1; // current version
    put16(output, 16, 2); // ET_EXEC
    put16(output, 18, 62); // EM_X86_64
    put32(output, 20, 1);
    put64(output, 24, checkedAddress(kCodeOffset + image.entryOffset));
    put64(output, 32, kElfHeaderSize);
    put64(output, 40, 0);
    put32(output, 48, 0);
    put16(output, 52, static_cast<std::uint16_t>(kElfHeaderSize));
    put16(output, 54, static_cast<std::uint16_t>(kProgramHeaderSize));
    put16(output, 58, 0);
    put16(output, 60, 0);
    put16(output, 62, 0);

    put16(output, 56, static_cast<std::uint16_t>(programHeaderCount));
    setLoadSegment(output, kElfHeaderSize, 4, 0, kImageBase, headerRegion);
    setLoadSegment(output, kElfHeaderSize + kProgramHeaderSize, 5, kCodeOffset,
                   checkedAddress(kCodeOffset), image.code.size());
    setLoadSegment(output, kElfHeaderSize + kProgramHeaderSize * 2,
                   image.writableData || hasImports ? 6 : 4, dataOffset,
                   checkedAddress(dataOffset), data.size());
    if (hasImports) {
        const std::size_t interpreterHeader =
            kElfHeaderSize + kProgramHeaderSize * 3;
        put32(output, interpreterHeader, 3); // PT_INTERP
        put32(output, interpreterHeader + 4, 4);
        put64(output, interpreterHeader + 8, interpreterOffset);
        put64(output, interpreterHeader + 16, checkedAddress(interpreterOffset));
        put64(output, interpreterHeader + 24, checkedAddress(interpreterOffset));
        put64(output, interpreterHeader + 32, interpreter.size() + 1);
        put64(output, interpreterHeader + 40, interpreter.size() + 1);
        put64(output, interpreterHeader + 48, 1);

        const std::size_t dynamicHeader =
            kElfHeaderSize + kProgramHeaderSize * 4;
        put32(output, dynamicHeader, 2); // PT_DYNAMIC
        put32(output, dynamicHeader + 4, 6);
        put64(output, dynamicHeader + 8, dataOffset + dynamicOffset);
        put64(output, dynamicHeader + 16, checkedAddress(dataOffset + dynamicOffset));
        put64(output, dynamicHeader + 24, checkedAddress(dataOffset + dynamicOffset));
        put64(output, dynamicHeader + 32, dynamicSize);
        put64(output, dynamicHeader + 40, dynamicSize);
        put64(output, dynamicHeader + 48, 8);
        std::copy(interpreter.begin(), interpreter.end(),
                  output.begin() + interpreterOffset);
    }
    if (image.writableData && !hasImports) {
        put32(output, kElfHeaderSize + kProgramHeaderSize * 2 + 4, 6);
    }

    std::copy(image.code.begin(), image.code.end(), output.begin() + kCodeOffset);
    std::copy(data.begin(), data.end(), output.begin() + dataOffset);

    for (const auto& fixup : image.dataFixups) {
        if (fixup.displacementOffset > image.code.size() ||
            image.code.size() - fixup.displacementOffset < 4 ||
            fixup.targetDataOffset >= image.data.size()) {
            throw std::runtime_error("ELF writer: invalid code-to-data fixup");
        }
        const auto instructionEnd = checkedAddress(kCodeOffset + fixup.displacementOffset + 4);
        const auto target = checkedAddress(dataOffset + fixup.targetDataOffset);
        const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(instructionEnd);
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("ELF writer: code-to-data reference is out of range");
        }
        put32(output, kCodeOffset + fixup.displacementOffset, static_cast<std::uint32_t>(delta));
    }

    for (const auto& fixup : image.importFixups) {
        const std::size_t index = importIndexes.at(fixup.name);
        const std::size_t targetOffset = gotOffset + index * 8;
        const auto instructionEnd = checkedAddress(
            kCodeOffset + fixup.displacementOffset + 4);
        const auto target = checkedAddress(dataOffset + targetOffset);
        const auto delta = static_cast<std::int64_t>(target) -
            static_cast<std::int64_t>(instructionEnd);
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("ELF writer: imported call target is out of range");
        }
        put32(output, kCodeOffset + fixup.displacementOffset,
              static_cast<std::uint32_t>(delta));
    }

    return output;
}

} // namespace zplus::backend::native
