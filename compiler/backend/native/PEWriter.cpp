#include "PEWriter.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace zplus::backend::native {
namespace {

constexpr std::uint32_t kTextRva = 0x1000;
constexpr std::uint32_t kFileAlignment = 0x200;
constexpr std::uint32_t kSectionAlignment = 0x1000;
constexpr std::uint64_t kImageBase = 0x140000000ULL;
constexpr std::size_t kHeaderSize = 0x200;
constexpr std::size_t kDosHeaderSize = 0x80;
constexpr std::size_t kOptionalHeaderSize = 0xF0;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::uint32_t kImportDirectoryIndex = 1;
constexpr std::uint32_t kIatDirectoryIndex = 12;

void put16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("PE writer: invalid 16-bit field offset");
    }
    for (unsigned i = 0; i < 2; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("PE writer: invalid 32-bit field offset");
    }
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void put64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("PE writer: invalid 64-bit field offset");
    }
    for (unsigned i = 0; i < 8; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + 2);
    put16(bytes, offset, value);
}

std::size_t alignTo(std::size_t value, std::size_t alignment) {
    if (value > std::numeric_limits<std::size_t>::max() - alignment + 1) {
        throw std::runtime_error("PE writer: image is too large");
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

std::uint32_t checkedRva(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("PE writer: section is too large");
    }
    return static_cast<std::uint32_t>(value);
}

std::uint32_t appendCString(std::vector<std::uint8_t>& bytes, const std::string& value) {
    const auto offset = checkedRva(bytes.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    return offset;
}

std::uint32_t appendImportName(std::vector<std::uint8_t>& bytes, const std::string& value) {
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max() - 2) {
        throw std::runtime_error("PE writer: import table is too large");
    }
    const auto offset = checkedRva(bytes.size());
    append16(bytes, 0);
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    return offset;
}

void setSection(
    std::vector<std::uint8_t>& output,
    std::size_t offset,
    const char* name,
    std::uint32_t virtualSize,
    std::uint32_t virtualAddress,
    std::uint32_t rawSize,
    std::uint32_t rawOffset,
    std::uint32_t characteristics
) {
    for (std::size_t i = 0; i < 8 && name[i] != '\0'; ++i) {
        output[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
    put32(output, offset + 8, virtualSize);
    put32(output, offset + 12, virtualAddress);
    put32(output, offset + 16, rawSize);
    put32(output, offset + 20, rawOffset);
    put32(output, offset + 24, 0);
    put32(output, offset + 28, 0);
    put16(output, offset + 32, 0);
    put16(output, offset + 34, 0);
    put32(output, offset + 36, characteristics);
}

} // namespace

std::vector<std::uint8_t> writePE(const NativeImage& image) {
    if (image.code.empty() || image.entryOffset >= image.code.size()) {
        throw std::runtime_error("PE writer: invalid code or entry point");
    }

    std::map<std::string, std::set<std::string>> imports;
    for (const auto& fixup : image.importFixups) {
        if (fixup.displacementOffset > image.code.size() ||
            image.code.size() - fixup.displacementOffset < 4) {
            throw std::runtime_error("PE writer: invalid import fixup");
        }
        if (fixup.library.empty() || fixup.name.empty()) {
            throw std::runtime_error("PE writer: import library and symbol must not be empty");
        }
        imports[fixup.library].insert(fixup.name);
    }
    if (imports.empty()) {
        throw std::runtime_error("PE writer: native entry point requires ExitProcess");
    }
    const auto kernelImports = imports.find("KERNEL32.dll");
    if (kernelImports == imports.end() ||
        !kernelImports->second.contains("ExitProcess")) {
        throw std::runtime_error("PE writer: native entry point requires KERNEL32.dll!ExitProcess");
    }

    std::size_t thunkCount = 0;
    for (const auto& [library, names] : imports) {
        if (library != "KERNEL32.dll" && library != "zgraphics_runtime.dll" &&
            library != "zaudio_runtime.dll" && library != "zphysics_runtime.dll") {
            throw std::runtime_error("PE writer: unsupported import library '" + library + "'");
        }
        if (library == "KERNEL32.dll") {
            for (const auto& name : names) {
                if (name != "ExitProcess" && name != "GetStdHandle" && name != "WriteFile") {
                    throw std::runtime_error("PE writer: unsupported import '" + name + "'");
                }
            }
        }
        thunkCount += names.size() + 1;
    }

    const std::uint32_t dataRva = checkedRva(alignTo(
        static_cast<std::size_t>(kTextRva) + image.code.size(), kSectionAlignment));
    std::vector<std::uint8_t> data = image.data;
    while (data.size() % 8 != 0) data.push_back(0);
    const std::size_t descriptorOffset = data.size();
    const std::size_t descriptorCount = imports.size() + 1;
    const std::size_t iltOffset = descriptorOffset + descriptorCount * 20;
    const std::size_t iatOffset = iltOffset + thunkCount * sizeof(std::uint64_t);
    const std::size_t namesOffset = iatOffset + thunkCount * sizeof(std::uint64_t);
    data.resize(namesOffset, 0);

    struct LibraryOffsets {
        std::size_t ilt = 0;
        std::size_t iat = 0;
        std::uint32_t nameRva = 0;
    };
    std::map<std::pair<std::string, std::string>, std::size_t> iatSlots;
    std::map<std::pair<std::string, std::string>, std::uint32_t> hintNameOffsets;
    std::size_t libraryIndex = 0;
    std::size_t thunkIndex = 0;
    for (const auto& [library, names] : imports) {
        LibraryOffsets offsets;
        offsets.ilt = iltOffset + thunkIndex * sizeof(std::uint64_t);
        offsets.iat = iatOffset + thunkIndex * sizeof(std::uint64_t);
        offsets.nameRva = checkedRva(
            static_cast<std::size_t>(dataRva) + appendCString(data, library));
        for (const auto& name : names) {
            while (data.size() % 2 != 0) data.push_back(0);
            hintNameOffsets.emplace(std::make_pair(library, name), appendImportName(data, name));
            const std::size_t slotOffset = offsets.iat + (thunkIndex -
                (offsets.ilt - iltOffset) / sizeof(std::uint64_t)) * sizeof(std::uint64_t);
            iatSlots.emplace(std::make_pair(library, name), slotOffset);
            ++thunkIndex;
        }
        ++thunkIndex;
        const std::size_t descriptor = descriptorOffset + libraryIndex * 20;
        put32(data, descriptor, checkedRva(static_cast<std::size_t>(dataRva) + offsets.ilt));
        put32(data, descriptor + 4, 0);
        put32(data, descriptor + 8, 0);
        put32(data, descriptor + 12, offsets.nameRva);
        put32(data, descriptor + 16, checkedRva(static_cast<std::size_t>(dataRva) + offsets.iat));
        ++libraryIndex;
    }

    std::size_t importIndex = 0;
    for (const auto& [library, names] : imports) {
        for (const auto& name : names) {
            const auto hintRva = checkedRva(static_cast<std::size_t>(dataRva) +
                                            hintNameOffsets.at({library, name}));
            put64(data, iltOffset + importIndex * sizeof(std::uint64_t), hintRva);
            put64(data, iatOffset + importIndex * sizeof(std::uint64_t), hintRva);
            ++importIndex;
        }
        ++importIndex;
    }

    const std::uint32_t textRawSize = checkedRva(alignTo(image.code.size(), kFileAlignment));
    const std::uint32_t dataRawSize = checkedRva(alignTo(data.size(), kFileAlignment));
    const std::uint32_t dataRawOffset = checkedRva(kHeaderSize + textRawSize);
    const std::uint32_t imageSize = checkedRva(alignTo(
        static_cast<std::size_t>(dataRva) + data.size(), kSectionAlignment));
    if (image.entryOffset > std::numeric_limits<std::uint32_t>::max() - kTextRva) {
        throw std::runtime_error("PE writer: entry point is out of range");
    }

    std::vector<std::uint8_t> output(static_cast<std::size_t>(dataRawOffset) + dataRawSize, 0);
    output[0] = 'M';
    output[1] = 'Z';
    put32(output, 0x3C, static_cast<std::uint32_t>(kDosHeaderSize));
    output[kDosHeaderSize] = 'P';
    output[kDosHeaderSize + 1] = 'E';
    const std::size_t coff = kDosHeaderSize + 4;
    put16(output, coff, 0x8664);
    put16(output, coff + 2, 2);
    put32(output, coff + 4, 0);
    put32(output, coff + 8, 0);
    put32(output, coff + 12, 0);
    put16(output, coff + 16, static_cast<std::uint16_t>(kOptionalHeaderSize));
    put16(output, coff + 18, 0x0023);

    const std::size_t optional = coff + 20;
    put16(output, optional, 0x20B);
    output[optional + 2] = 1;
    put32(output, optional + 4, textRawSize);
    put32(output, optional + 8, dataRawSize);
    put32(output, optional + 12, 0);
    put32(output, optional + 16, kTextRva + static_cast<std::uint32_t>(image.entryOffset));
    put32(output, optional + 20, kTextRva);
    put64(output, optional + 24, kImageBase);
    put32(output, optional + 32, kSectionAlignment);
    put32(output, optional + 36, kFileAlignment);
    put16(output, optional + 40, 6);
    put16(output, optional + 42, 0);
    put16(output, optional + 44, 1);
    put16(output, optional + 46, 0);
    put16(output, optional + 48, 6);
    put16(output, optional + 50, 0);
    put32(output, optional + 52, 0);
    put32(output, optional + 56, imageSize);
    put32(output, optional + 60, static_cast<std::uint32_t>(kHeaderSize));
    put32(output, optional + 64, 0);
    put16(output, optional + 68, 3);
    put16(output, optional + 70, 0x8100);
    put64(output, optional + 72, 0x100000);
    put64(output, optional + 80, 0x1000);
    put64(output, optional + 88, 0x100000);
    put64(output, optional + 96, 0x1000);
    put32(output, optional + 104, 0);
    put32(output, optional + 108, 16);
    const std::size_t directories = optional + 112;
    const std::uint32_t descriptorRva = checkedRva(
        static_cast<std::size_t>(dataRva) + descriptorOffset);
    const std::uint32_t iatRva = checkedRva(
        static_cast<std::size_t>(dataRva) + iatOffset);
    put32(output, directories + kImportDirectoryIndex * 8, descriptorRva);
    put32(output, directories + kImportDirectoryIndex * 8 + 4,
          checkedRva(descriptorCount * 20));
    put32(output, directories + kIatDirectoryIndex * 8, iatRva);
    put32(output, directories + kIatDirectoryIndex * 8 + 4,
          checkedRva(thunkCount * sizeof(std::uint64_t)));

    const std::size_t sectionTable = optional + kOptionalHeaderSize;
    setSection(output, sectionTable, ".text", checkedRva(image.code.size()), kTextRva,
               textRawSize, static_cast<std::uint32_t>(kHeaderSize), 0x60000020);
    setSection(output, sectionTable + kSectionHeaderSize, ".data", checkedRva(data.size()),
               dataRva, dataRawSize, dataRawOffset, 0xC0000040);

    std::copy(image.code.begin(), image.code.end(), output.begin() + kHeaderSize);
    std::copy(data.begin(), data.end(), output.begin() + dataRawOffset);

    for (const auto& fixup : image.dataFixups) {
        if (fixup.displacementOffset > image.code.size() ||
            image.code.size() - fixup.displacementOffset < 4 ||
            fixup.targetDataOffset >= image.data.size()) {
            throw std::runtime_error("PE writer: invalid code-to-data fixup");
        }
        const std::int64_t target = static_cast<std::int64_t>(dataRva) +
                                    static_cast<std::int64_t>(fixup.targetDataOffset);
        const std::int64_t nextInstruction = static_cast<std::int64_t>(kTextRva) +
                                             static_cast<std::int64_t>(fixup.displacementOffset) + 4;
        const std::int64_t delta = target - nextInstruction;
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("PE writer: code-to-data reference is out of range");
        }
        put32(output, kHeaderSize + fixup.displacementOffset, static_cast<std::uint32_t>(delta));
    }

    for (const auto& fixup : image.importFixups) {
        const auto slot = iatSlots.find({fixup.library, fixup.name});
        if (slot == iatSlots.end()) {
            throw std::runtime_error("PE writer: unresolved import '" + fixup.library +
                                     "!" + fixup.name + "'");
        }
        const std::int64_t target = static_cast<std::int64_t>(dataRva) +
                                    static_cast<std::int64_t>(slot->second);
        const std::int64_t nextInstruction = static_cast<std::int64_t>(kTextRva) +
                                             static_cast<std::int64_t>(fixup.displacementOffset) + 4;
        const std::int64_t delta = target - nextInstruction;
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("PE writer: import reference is out of range");
        }
        put32(output, kHeaderSize + fixup.displacementOffset, static_cast<std::uint32_t>(delta));
    }

    return output;
}

} // namespace zplus::backend::native