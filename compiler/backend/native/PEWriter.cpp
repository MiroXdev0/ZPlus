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
constexpr std::size_t kDosHeaderSize = 0x80;
constexpr std::size_t kOptionalHeaderSize = 0xF0;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::uint32_t kImportDirectoryIndex = 1;
constexpr std::uint32_t kExceptionDirectoryIndex = 3;
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

void appendUnwindCode(
    std::vector<std::uint8_t>& codes,
    std::uint8_t codeOffset,
    std::uint8_t operation,
    std::uint8_t operationInfo
) {
    codes.push_back(codeOffset);
    codes.push_back(static_cast<std::uint8_t>((operationInfo << 4) | operation));
}

std::vector<std::uint8_t> makeUnwindInfo(
    const NativeImage::UnwindFunction& function
) {
    if (function.stackAllocationSize % 8 != 0) {
        throw std::runtime_error(
            "PE writer: unwind stack allocation must be a multiple of eight bytes");
    }
    if (function.framePointer && function.prologueSize < 4) {
        throw std::runtime_error(
            "PE writer: frame-pointer unwind data requires a complete frame-pointer prologue");
    }

    std::vector<std::uint8_t> codes;
    if (function.stackAllocationSize != 0) {
        if (function.stackAllocationSize <= 128) {
            const auto operationInfo = static_cast<std::uint8_t>(
                (function.stackAllocationSize - 8) / 8);
            appendUnwindCode(codes, function.prologueSize, 2, operationInfo);
        } else if (function.stackAllocationSize <= 0x7FFF8) {
            appendUnwindCode(codes, function.prologueSize, 1, 0);
            codes.push_back(static_cast<std::uint8_t>(
                (function.stackAllocationSize / 8) & 0xFF));
            codes.push_back(static_cast<std::uint8_t>(
                (function.stackAllocationSize / 8) >> 8));
        } else {
            appendUnwindCode(codes, function.prologueSize, 1, 1);
            for (unsigned shift : {0U, 16U}) {
                codes.push_back(static_cast<std::uint8_t>(
                    function.stackAllocationSize >> shift));
                codes.push_back(static_cast<std::uint8_t>(
                    function.stackAllocationSize >> (shift + 8)));
            }
        }
    }
    if (function.framePointer) {
        appendUnwindCode(codes, 4, 3, 0); // UWOP_SET_FPREG (RBP)
        appendUnwindCode(codes, 1, 0, 5); // UWOP_PUSH_NONVOL (RBP)
    }

    const std::size_t codeSlots = codes.size() / 2;
    if (codeSlots > std::numeric_limits<std::uint8_t>::max()) {
        throw std::runtime_error("PE writer: too many unwind codes");
    }
    std::vector<std::uint8_t> info{
        1,
        function.prologueSize,
        static_cast<std::uint8_t>(codeSlots),
        static_cast<std::uint8_t>(function.framePointer ? 5 : 0)
    };
    info.insert(info.end(), codes.begin(), codes.end());
    if (codeSlots % 2 != 0) {
        info.push_back(0);
        info.push_back(0);
    }
    while (info.size() % 4 != 0) info.push_back(0);
    return info;
}

struct UnwindSections {
    std::vector<std::uint8_t> functionTable;
    std::vector<std::uint8_t> unwindRecords;
};

UnwindSections makeUnwindSections(
    const NativeImage& image,
    std::uint32_t xdataRva
) {
    std::vector<NativeImage::UnwindFunction> functions = image.unwindFunctions;
    std::sort(functions.begin(), functions.end(),
              [](const auto& left, const auto& right) {
                  return left.beginOffset < right.beginOffset;
              });
    if (functions.size() >
        std::numeric_limits<std::uint32_t>::max() / 12) {
        throw std::runtime_error("PE writer: too many unwind function entries");
    }

    UnwindSections sections;
    sections.functionTable.resize(functions.size() * 12, 0);
    std::size_t previousEnd = 0;
    for (std::size_t index = 0; index < functions.size(); ++index) {
        const auto& function = functions[index];
        if (function.beginOffset >= function.endOffset ||
            function.endOffset > image.code.size() ||
            function.prologueSize > function.endOffset - function.beginOffset ||
            (index != 0 && function.beginOffset < previousEnd)) {
            throw std::runtime_error("PE writer: invalid or overlapping unwind function range");
        }
        previousEnd = function.endOffset;

        const std::size_t beginRva =
            static_cast<std::size_t>(kTextRva) + function.beginOffset;
        const std::size_t endRva =
            static_cast<std::size_t>(kTextRva) + function.endOffset;
        const std::uint32_t unwindRva = checkedRva(
            static_cast<std::size_t>(xdataRva) +
            sections.unwindRecords.size());
        const std::size_t entryOffset = index * 12;
        put32(sections.functionTable, entryOffset, checkedRva(beginRva));
        put32(sections.functionTable, entryOffset + 4, checkedRva(endRva));
        put32(sections.functionTable, entryOffset + 8, unwindRva);

        const auto info = makeUnwindInfo(function);
        sections.unwindRecords.insert(
            sections.unwindRecords.end(), info.begin(), info.end());
    }
    return sections;
}

} // namespace

std::vector<std::uint8_t> writePE(const NativeImage& image, bool windowsGui) {
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
            library != "zaudio_runtime.dll" && library != "zphysics_runtime.dll" &&
            library != "zplus_input_runtime.dll") {
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

    const bool hasUnwindInfo = !image.unwindFunctions.empty();
    const std::uint32_t pdataRva = hasUnwindInfo
        ? checkedRva(alignTo(
              static_cast<std::size_t>(dataRva) + data.size(), kSectionAlignment))
        : 0;
    const std::uint32_t xdataRva = hasUnwindInfo
        ? checkedRva(alignTo(
              static_cast<std::size_t>(pdataRva) +
                  image.unwindFunctions.size() * 12,
              kSectionAlignment))
        : 0;
    const UnwindSections unwindSections = hasUnwindInfo
        ? makeUnwindSections(image, xdataRva)
        : UnwindSections{};
    const std::size_t sectionCount = hasUnwindInfo ? 4 : 2;
    const std::size_t sectionTableOffset =
        kDosHeaderSize + 4 + 20 + kOptionalHeaderSize;
    const std::uint32_t headersSize = checkedRva(alignTo(
        sectionTableOffset + sectionCount * kSectionHeaderSize,
        kFileAlignment));
    const std::uint32_t textRawSize = checkedRva(alignTo(image.code.size(), kFileAlignment));
    const std::uint32_t dataRawSize = checkedRva(alignTo(data.size(), kFileAlignment));
    const std::uint32_t textRawOffset = headersSize;
    const std::uint32_t dataRawOffset =
        checkedRva(static_cast<std::size_t>(textRawOffset) + textRawSize);
    const std::uint32_t pdataRawSize = hasUnwindInfo
        ? checkedRva(alignTo(unwindSections.functionTable.size(), kFileAlignment))
        : 0;
    const std::uint32_t pdataRawOffset = checkedRva(
        static_cast<std::size_t>(dataRawOffset) + dataRawSize);
    const std::uint32_t xdataRawSize = hasUnwindInfo
        ? checkedRva(alignTo(unwindSections.unwindRecords.size(), kFileAlignment))
        : 0;
    const std::uint32_t xdataRawOffset = checkedRva(
        static_cast<std::size_t>(pdataRawOffset) + pdataRawSize);
    const std::size_t imageEnd = hasUnwindInfo
        ? static_cast<std::size_t>(xdataRva) +
              unwindSections.unwindRecords.size()
        : static_cast<std::size_t>(dataRva) + data.size();
    const std::uint32_t imageSize =
        checkedRva(alignTo(imageEnd, kSectionAlignment));
    if (image.entryOffset > std::numeric_limits<std::uint32_t>::max() - kTextRva) {
        throw std::runtime_error("PE writer: entry point is out of range");
    }

    const std::size_t outputSize = hasUnwindInfo
        ? static_cast<std::size_t>(xdataRawOffset) + xdataRawSize
        : static_cast<std::size_t>(dataRawOffset) + dataRawSize;
    std::vector<std::uint8_t> output(outputSize, 0);
    output[0] = 'M';
    output[1] = 'Z';
    put32(output, 0x3C, static_cast<std::uint32_t>(kDosHeaderSize));
    output[kDosHeaderSize] = 'P';
    output[kDosHeaderSize + 1] = 'E';
    const std::size_t coff = kDosHeaderSize + 4;
    put16(output, coff, 0x8664);
    put16(output, coff + 2, static_cast<std::uint16_t>(sectionCount));
    put32(output, coff + 4, 0);
    put32(output, coff + 8, 0);
    put32(output, coff + 12, 0);
    put16(output, coff + 16, static_cast<std::uint16_t>(kOptionalHeaderSize));
    put16(output, coff + 18, 0x0023);

    const std::size_t optional = coff + 20;
    put16(output, optional, 0x20B);
    output[optional + 2] = 1;
    put32(output, optional + 4, textRawSize);
    put32(output, optional + 8, checkedRva(
        static_cast<std::size_t>(dataRawSize) +
        (hasUnwindInfo
             ? static_cast<std::size_t>(pdataRawSize) + xdataRawSize
             : 0)));
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
    put32(output, optional + 60, headersSize);
    put32(output, optional + 64, 0);
    put16(output, optional + 68, windowsGui ? 2 : 3);
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
    if (hasUnwindInfo) {
        put32(output, directories + kExceptionDirectoryIndex * 8, pdataRva);
        put32(output, directories + kExceptionDirectoryIndex * 8 + 4,
              checkedRva(image.unwindFunctions.size() * 12));
    }

    const std::size_t sectionTable = optional + kOptionalHeaderSize;
    setSection(output, sectionTable, ".text", checkedRva(image.code.size()), kTextRva,
               textRawSize, textRawOffset, 0x60000020);
    setSection(output, sectionTable + kSectionHeaderSize, ".data", checkedRva(data.size()),
               dataRva, dataRawSize, dataRawOffset, 0xC0000040);
    if (hasUnwindInfo) {
        setSection(output, sectionTable + 2 * kSectionHeaderSize, ".pdata",
                   checkedRva(unwindSections.functionTable.size()), pdataRva,
                   pdataRawSize, pdataRawOffset, 0x40000040);
        setSection(output, sectionTable + 3 * kSectionHeaderSize, ".xdata",
                   checkedRva(unwindSections.unwindRecords.size()), xdataRva,
                   xdataRawSize, xdataRawOffset, 0x40000040);
    }

    std::copy(image.code.begin(), image.code.end(), output.begin() + textRawOffset);
    std::copy(data.begin(), data.end(), output.begin() + dataRawOffset);
    if (hasUnwindInfo) {
        std::copy(unwindSections.functionTable.begin(),
                  unwindSections.functionTable.end(),
                  output.begin() + pdataRawOffset);
        std::copy(unwindSections.unwindRecords.begin(),
                  unwindSections.unwindRecords.end(),
                  output.begin() + xdataRawOffset);
    }

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
        put32(output, textRawOffset + fixup.displacementOffset,
              static_cast<std::uint32_t>(delta));
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
        put32(output, textRawOffset + fixup.displacementOffset,
              static_cast<std::uint32_t>(delta));
    }

    return output;
}

} // namespace zplus::backend::native