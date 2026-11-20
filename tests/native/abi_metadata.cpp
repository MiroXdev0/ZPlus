#include "compiler/backend/native/ELFWriter.hpp"
#include "compiler/backend/native/PEWriter.hpp"
#include "compiler/backend/native/X64CodeGenerator.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using zplus::backend::native::NativeImage;

std::uint16_t read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes.at(offset)) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8);
}

std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
           (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}

void appendCall(
    zplus::ir::BasicBlock& block,
    const std::string& callee,
    std::vector<zplus::ir::Value> arguments = {}
) {
    zplus::ir::Instruction call{"call"};
    call.operands.emplace_back(callee, zplus::ir::Value::Kind::Identifier);
    call.operands.insert(
        call.operands.end(),
        std::make_move_iterator(arguments.begin()),
        std::make_move_iterator(arguments.end()));
    block.emit(std::move(call));
}

NativeImage generateUnwindTestImage() {
    using zplus::ir::Instruction;
    using zplus::ir::Module;
    using zplus::ir::Parameter;
    using zplus::ir::Value;
    using zplus::semantic::Type;

    Module module;
    auto& main = module.createFunction("main");
    auto& mainBlock = main.createBlock("entry");
    appendCall(mainBlock, "middle");
    mainBlock.emit(Instruction{"ret"});

    auto& middle = module.createFunction("middle");
    auto& middleBlock = middle.createBlock("entry");
    std::vector<Value> wideArguments;
    wideArguments.reserve(32);
    for (std::size_t i = 0; i < 20; ++i) {
        Value value{"1", Value::Kind::Number};
        value.type = Type::number();
        wideArguments.push_back(std::move(value));
    }
    for (std::size_t i = 0; i < 12; ++i) {
        Value value{"true", Value::Kind::Boolean};
        value.type = Type::boolean();
        wideArguments.push_back(std::move(value));
    }
    appendCall(middleBlock, "wide", std::move(wideArguments));
    middleBlock.emit(Instruction{"ret"});

    auto& wide = module.createFunction("wide");
    for (std::size_t i = 0; i < 20; ++i) {
        wide.parameters.emplace_back("number" + std::to_string(i), Type::number());
    }
    for (std::size_t i = 0; i < 12; ++i) {
        wide.parameters.emplace_back("flag" + std::to_string(i), Type::boolean());
    }
    wide.createBlock("entry").emit(Instruction{"ret"});

    return zplus::backend::native::generateX64(module, true);
}

struct PeUnwindDirectory {
    std::size_t optionalHeader = 0;
    std::size_t sectionTable = 0;
    std::uint16_t sectionCount = 0;
    std::uint32_t exceptionRva = 0;
    std::uint32_t exceptionSize = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t pdataRva = 0;
    std::uint32_t pdataVirtualSize = 0;
    std::uint32_t pdataRawOffset = 0;
    std::uint32_t pdataRawSize = 0;
    std::uint32_t xdataRva = 0;
    std::uint32_t xdataVirtualSize = 0;
    std::uint32_t xdataRawOffset = 0;
    std::uint32_t xdataRawSize = 0;
};

bool readPeUnwindDirectory(
    const std::vector<std::uint8_t>& pe,
    PeUnwindDirectory& directory
) {
    if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') return false;
    const std::size_t peOffset = read32(pe, 0x3C);
    const std::size_t coff = peOffset + 4;
    directory.sectionCount = read16(pe, coff + 2);
    directory.optionalHeader = coff + 20;
    directory.sectionTable = directory.optionalHeader + read16(pe, coff + 16);
    directory.exceptionRva =
        read32(pe, directory.optionalHeader + 112 + 3 * 8);
    directory.exceptionSize =
        read32(pe, directory.optionalHeader + 112 + 3 * 8 + 4);
    directory.imageSize = read32(pe, directory.optionalHeader + 56);

    for (std::size_t index = 0; index < directory.sectionCount; ++index) {
        const std::size_t section =
            directory.sectionTable + index * 40;
        if (std::memcmp(pe.data() + section, ".pdata", 6) == 0) {
            directory.pdataVirtualSize = read32(pe, section + 8);
            directory.pdataRva = read32(pe, section + 12);
            directory.pdataRawSize = read32(pe, section + 16);
            directory.pdataRawOffset = read32(pe, section + 20);
        } else if (std::memcmp(pe.data() + section, ".xdata", 6) == 0) {
            directory.xdataVirtualSize = read32(pe, section + 8);
            directory.xdataRva = read32(pe, section + 12);
            directory.xdataRawSize = read32(pe, section + 16);
            directory.xdataRawOffset = read32(pe, section + 20);
        }
    }
    return directory.exceptionRva != 0 &&
           directory.exceptionSize != 0 &&
           directory.pdataRva != 0 &&
           directory.xdataRva != 0 &&
           directory.pdataRawSize != 0 &&
           directory.xdataRawSize != 0 &&
           directory.pdataRawOffset <= pe.size() &&
           directory.pdataRawSize <= pe.size() - directory.pdataRawOffset &&
           directory.xdataRawOffset <= pe.size() &&
           directory.xdataRawSize <= pe.size() - directory.xdataRawOffset;
}

bool testPeUnwindDirectory(
    const NativeImage& image,
    const std::vector<std::uint8_t>& pe,
    PeUnwindDirectory& directory
) {
    if (!readPeUnwindDirectory(pe, directory) ||
    read16(pe, read32(pe, 0x3C) + 4 + 2) != 4 ||
        directory.exceptionSize != image.unwindFunctions.size() * 12 ||
    directory.pdataVirtualSize != directory.exceptionSize ||
    directory.pdataVirtualSize % 12 != 0 ||
        directory.exceptionRva < directory.pdataRva ||
        directory.exceptionRva + directory.exceptionSize >
            directory.pdataRva + directory.pdataVirtualSize) {
        std::cerr << "PE writer did not emit a valid Exception Directory and .pdata section\n";
        return false;
    }

    const std::size_t tableOffset = directory.pdataRawOffset +
        (directory.exceptionRva - directory.pdataRva);
    std::uint32_t previousBegin = 0;
    for (std::size_t index = 0; index < image.unwindFunctions.size(); ++index) {
        const std::size_t entry = tableOffset + index * 12;
        const std::uint32_t begin = read32(pe, entry);
        const std::uint32_t end = read32(pe, entry + 4);
        const std::uint32_t unwindRva = read32(pe, entry + 8);
        if (begin >= end || (index != 0 && begin < previousBegin) ||
            unwindRva < directory.xdataRva ||
            unwindRva >= directory.xdataRva + directory.xdataVirtualSize) {
            std::cerr << "PE writer emitted an invalid or unsorted RUNTIME_FUNCTION entry\n";
            return false;
        }
        previousBegin = begin;
        const std::size_t infoOffset = directory.xdataRawOffset +
            (unwindRva - directory.xdataRva);
        if (pe.at(infoOffset) != 1) {
            std::cerr << "PE writer emitted an unsupported UNWIND_INFO version\n";
            return false;
        }
        if (index < image.unwindFunctions.size() - 2) {
            const std::uint8_t codeCount = pe.at(infoOffset + 2);
            const std::uint8_t frameRegister = pe.at(infoOffset + 3) & 0x0F;
            if (codeCount < 3 || frameRegister != 5) {
                std::cerr << "generated function unwind data omits its saved RBP frame\n";
                return false;
            }
        }
    }
    return true;
}

bool testSystemVImportedStackArguments() {
    using zplus::ir::Instruction;
    using zplus::ir::Module;
    using zplus::ir::Value;
    using zplus::semantic::Type;

    Module module;
    auto& main = module.createFunction("main");
    auto& block = main.createBlock("entry");

    std::vector<Type> parameterTypes;
    std::vector<Value> arguments;
    parameterTypes.reserve(16);
    arguments.reserve(16);
    for (std::size_t i = 0; i < 7; ++i) {
        parameterTypes.push_back(Type::boolean());
        Value boolean{"true", Value::Kind::Boolean};
        boolean.type = Type::boolean();
        arguments.push_back(std::move(boolean));

        parameterTypes.push_back(Type::number());
        Value number{"0.5", Value::Kind::Number};
        number.type = Type::number();
        arguments.push_back(std::move(number));
    }
    for (std::size_t i = 0; i < 2; ++i) {
        parameterTypes.push_back(Type::number());
        Value number{"0.5", Value::Kind::Number};
        number.type = Type::number();
        arguments.push_back(std::move(number));
    }
    auto& generatedTarget = module.createFunction("acceptStackArgs");
    for (std::size_t i = 0; i < parameterTypes.size(); ++i) {
        generatedTarget.parameters.emplace_back(
            "argument" + std::to_string(i), parameterTypes[i]);
    }
    generatedTarget.createBlock("entry").emit(Instruction{"ret"});
    module.builtinRegistry.add(
        "__zphysics_abiProbe",
        Type::function(parameterTypes, Type::voidType()));
    appendCall(block, "acceptStackArgs", arguments);
    appendCall(block, "__zphysics_abiProbe", std::move(arguments));
    block.emit(Instruction{"ret"});

    const NativeImage image =
        zplus::backend::native::generateX64(module, false);
    const auto import = std::find_if(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) {
            return fixup.name == "zpphysics_abiProbe" &&
                   fixup.library == "libzphysics_runtime.so";
        });
    const auto hasStackStore = [&image](std::uint32_t displacement) {
        for (std::size_t offset = 0; offset + 8 <= image.code.size(); ++offset) {
            if (image.code[offset] == 0x48 &&
                image.code[offset + 1] == 0x89 &&
                image.code[offset + 2] == 0x84 &&
                image.code[offset + 3] == 0x24 &&
                read32(image.code, offset + 4) == displacement) {
                return true;
            }
        }
        return false;
    };
    const auto hasFrameLoad = [&image](std::uint32_t displacement) {
        for (std::size_t offset = 0; offset + 7 <= image.code.size(); ++offset) {
            if (image.code[offset] == 0x48 &&
                image.code[offset + 1] == 0x8B &&
                image.code[offset + 2] == 0x85 &&
                read32(image.code, offset + 3) == displacement) {
                return true;
            }
        }
        return false;
    };
    if (import == image.importFixups.end() ||
        !hasStackStore(0) || !hasStackStore(8) ||
        !hasFrameLoad(16) || !hasFrameLoad(24) ||
        image.code.size() < 11 ||
        read32(image.code, 7) % 16 != 0) {
        std::cerr << "System V imported call did not classify and reserve mixed stack arguments\n";
        return false;
    }
#if defined(__linux__)
    const auto elf = zplus::backend::native::writeELF(image);
    const std::string runtimeName = "libzphysics_runtime.so";
    const auto runtimeNameBytes = std::search(
        elf.begin(), elf.end(), runtimeName.begin(), runtimeName.end());
    if (elf.size() < 64 || elf[0] != 0x7F || elf[1] != 'E' ||
        elf[2] != 'L' || elf[3] != 'F' || elf[4] != 2 || elf[5] != 1 ||
        read16(elf, 16) != 2 || read16(elf, 18) != 62 ||
        runtimeNameBytes == elf.end()) {
        std::cerr << "ELF writer did not preserve the System V runtime import\n";
        return false;
    }
#endif
    return true;
}

bool testXmmRegisterAllocationSpills() {
    using zplus::ir::Instruction;
    using zplus::ir::Module;
    using zplus::ir::Value;
    using zplus::semantic::Type;

    Module module;
    auto& main = module.createFunction("main");
    main.returnType = Type::number();
    auto& mainBlock = main.createBlock("entry");

    auto& pressure = module.createFunction("pressure");
    pressure.returnType = Type::number();
    pressure.parameters.emplace_back("input", Type::number());
    auto& pressureBlock = pressure.createBlock("entry");

    std::vector<std::string> values;
    values.reserve(8);
    for (std::size_t i = 0; i < 8; ++i) {
        const std::string result = "%value" + std::to_string(i);
        Instruction add{"add", result};
        add.resultType = Type::number();
        Value input{"input", Value::Kind::Identifier};
        input.type = Type::number();
        add.operands.push_back(std::move(input));
        Value constant{std::to_string(i + 1), Value::Kind::Number};
        constant.type = Type::number();
        add.operands.push_back(std::move(constant));
        pressureBlock.emit(std::move(add));
        values.push_back(result);
    }

    std::string sum = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) {
        const std::string result = "%sum" + std::to_string(i);
        Instruction add{"add", result};
        add.resultType = Type::number();
        add.operands.emplace_back(sum, Value::Kind::Identifier);
        add.operands.emplace_back(values[i], Value::Kind::Identifier);
        pressureBlock.emit(std::move(add));
        sum = result;
    }
    Instruction returnSum{"ret"};
    Value sumValue{sum, Value::Kind::Identifier};
    sumValue.type = Type::number();
    returnSum.operands.push_back(std::move(sumValue));
    pressureBlock.emit(std::move(returnSum));

    Instruction call{"call", "%mainResult"};
    call.resultType = Type::number();
    call.operands.emplace_back("pressure", Value::Kind::Identifier);
    Value argument{"3", Value::Kind::Number};
    argument.type = Type::number();
    call.operands.push_back(std::move(argument));
    mainBlock.emit(std::move(call));
    Instruction returnMain{"ret"};
    Value mainResult{"%mainResult", Value::Kind::Identifier};
    mainResult.type = Type::number();
    returnMain.operands.push_back(std::move(mainResult));
    mainBlock.emit(std::move(returnMain));

    const NativeImage image =
        zplus::backend::native::generateX64(module, true);
    const std::vector<std::uint8_t> xmmStackStore{
        0xF2, 0x0F, 0x11, 0x85};
    std::size_t stackStores = 0;
    std::size_t registerMoves = 0;
    for (auto current = image.code.begin();
         current != image.code.end();) {
        current = std::search(
            current, image.code.end(),
            xmmStackStore.begin(), xmmStackStore.end());
        if (current == image.code.end()) {
            break;
        }
        ++stackStores;
        ++current;
    }
    for (std::size_t offset = 0; offset + 4 <= image.code.size(); ++offset) {
        if (image.code[offset] == 0xF2 &&
            image.code[offset + 1] == 0x0F &&
            image.code[offset + 2] == 0x10 &&
            (image.code[offset + 3] & 0xC0) == 0xC0) {
            ++registerMoves;
        }
    }
    if (stackStores < 5 || registerMoves == 0) {
        std::cerr << "XMM allocation did not emit both register moves and spill stores\n";
        return false;
    }
    return true;
}

bool testLargeStackUnwindEncoding() {
    NativeImage image;
    image.code = {
        0x55, 0x48, 0x89, 0xE5,
        0x48, 0x81, 0xEC, 0xC0, 0x27, 0x09, 0x00,
        0xC9, 0xC3,
        0xFF, 0x15, 0, 0, 0, 0,
        0x0F, 0x0B
    };
    image.entryOffset = 0;
    image.importFixups.push_back({15, "ExitProcess"});
    image.unwindFunctions.push_back({0, image.code.size(), 600000, 11, true});

    const auto pe = zplus::backend::native::writePE(image);
    PeUnwindDirectory directory;
    if (!readPeUnwindDirectory(pe, directory)) {
        std::cerr << "PE writer omitted unwind sections for a large stack frame\n";
        return false;
    }
    const std::size_t infoOffset = directory.xdataRawOffset;
    if (pe.at(infoOffset) != 1 || pe.at(infoOffset + 2) != 5 ||
        pe.at(infoOffset + 3) != 5 || pe.at(infoOffset + 5) != 0x11 ||
        read32(pe, infoOffset + 6) != 600000) {
        std::cerr << "PE writer incorrectly encoded a large UWOP_ALLOC_LARGE frame\n";
        return false;
    }
    return true;
}

#if defined(_WIN32)
#if defined(_MSC_VER)
DWORD catchGeneratedException(void (WINAPI* generatedEntry)()) {
    __try {
        generatedEntry();
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
#endif

bool testWindowsRuntimeUnwind(
    const NativeImage& image,
    const std::vector<std::uint8_t>& pe,
    const PeUnwindDirectory& directory
) {
    using AddFunctionTable = BOOLEAN (WINAPI*)(
        PRUNTIME_FUNCTION, DWORD, DWORD64);
    using DeleteFunctionTable = BOOLEAN (WINAPI*)(PRUNTIME_FUNCTION);
    using LookupFunctionEntry = PRUNTIME_FUNCTION (NTAPI*)(
        DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);
    using VirtualUnwind = PVOID (NTAPI*)(
        DWORD, DWORD64, DWORD64, PRUNTIME_FUNCTION, PCONTEXT,
        PVOID*, PDWORD64, PKNONVOLATILE_CONTEXT_POINTERS);

    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto addTable = reinterpret_cast<AddFunctionTable>(
        GetProcAddress(kernel, "RtlAddFunctionTable"));
    const auto deleteTable = reinterpret_cast<DeleteFunctionTable>(
        GetProcAddress(kernel, "RtlDeleteFunctionTable"));
    const auto lookup = reinterpret_cast<LookupFunctionEntry>(
        GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
    const auto virtualUnwind = reinterpret_cast<VirtualUnwind>(
        GetProcAddress(ntdll, "RtlVirtualUnwind"));
    if (!addTable || !deleteTable || !lookup || !virtualUnwind) {
        std::cerr << "Windows runtime unwind APIs are unavailable\n";
        return false;
    }

    auto* mappedImage = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, directory.imageSize, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE));
    if (!mappedImage) {
        std::cerr << "failed to allocate a mapped image for Windows unwind validation\n";
        return false;
    }
    for (std::size_t index = 0; index < directory.sectionCount; ++index) {
        const std::size_t section = directory.sectionTable + index * 40;
        const std::uint32_t rawSize = read32(pe, section + 16);
        const std::uint32_t rawOffset = read32(pe, section + 20);
        const std::uint32_t virtualAddress = read32(pe, section + 12);
        if (rawOffset > pe.size() || rawSize > pe.size() - rawOffset ||
            virtualAddress > directory.imageSize ||
            rawSize > directory.imageSize - virtualAddress) {
            VirtualFree(mappedImage, 0, MEM_RELEASE);
            std::cerr << "PE section is outside the mapped image during unwind validation\n";
            return false;
        }
        std::copy_n(
            pe.begin() + rawOffset, rawSize, mappedImage + virtualAddress);
    }

    auto* functions = reinterpret_cast<PRUNTIME_FUNCTION>(
        mappedImage + directory.exceptionRva);
    const DWORD functionCount = directory.exceptionSize / sizeof(RUNTIME_FUNCTION);
    const DWORD64 imageBase = reinterpret_cast<DWORD64>(mappedImage);
    if (!addTable(functions, functionCount, imageBase)) {
        VirtualFree(mappedImage, 0, MEM_RELEASE);
        std::cerr << "RtlAddFunctionTable rejected generated unwind records\n";
        return false;
    }

    auto* generatedMain = reinterpret_cast<void (WINAPI*)()>(
        mappedImage + 0x1000 + image.unwindFunctions.front().beginOffset);
    FlushInstructionCache(GetCurrentProcess(), mappedImage, directory.imageSize);
    generatedMain();

    const auto wideFunction = std::find_if(
        image.unwindFunctions.begin(), image.unwindFunctions.end(),
        [](const auto& function) {
            return function.framePointer &&
                   function.stackAllocationSize >= 320;
        });
    if (wideFunction == image.unwindFunctions.end()) {
        deleteTable(functions);
        VirtualFree(mappedImage, 0, MEM_RELEASE);
        std::cerr << "test image has no wide generated frame for unwind validation\n";
        return false;
    }
#if defined(_MSC_VER)
    bool exceptionCaught = false;
    const auto raiseException = reinterpret_cast<std::uintptr_t>(
        GetProcAddress(kernel, "RaiseException"));
    if (wideFunction != image.unwindFunctions.end() && raiseException != 0) {
        constexpr std::uint32_t exceptionCode = 0xE0424242;
        std::vector<std::uint8_t> raiseSequence{
            0xB9,
            static_cast<std::uint8_t>(exceptionCode),
            static_cast<std::uint8_t>(exceptionCode >> 8),
            static_cast<std::uint8_t>(exceptionCode >> 16),
            static_cast<std::uint8_t>(exceptionCode >> 24),
            0x31, 0xD2,
            0x45, 0x31, 0xC0,
            0x45, 0x31, 0xC9,
            0x48, 0xB8
        };
        for (unsigned shift = 0; shift < 64; shift += 8) {
            raiseSequence.push_back(
                static_cast<std::uint8_t>(raiseException >> shift));
        }
        raiseSequence.insert(raiseSequence.end(), {0xFF, 0xD0, 0xCC});
        const std::size_t patchOffset =
            0x1000 + wideFunction->beginOffset + wideFunction->prologueSize;
        if (raiseSequence.size() <=
            wideFunction->endOffset - wideFunction->beginOffset -
                wideFunction->prologueSize) {
            std::copy(
                raiseSequence.begin(), raiseSequence.end(),
                mappedImage + patchOffset);
            FlushInstructionCache(
                GetCurrentProcess(), mappedImage + patchOffset,
                raiseSequence.size());
            exceptionCaught =
                catchGeneratedException(generatedMain) == exceptionCode;
        }
    }
    if (!exceptionCaught) {
        deleteTable(functions);
        VirtualFree(mappedImage, 0, MEM_RELEASE);
        std::cerr << "Windows exception did not unwind through generated frames\n";
        return false;
    }
#endif

    const auto unwindSingleFrame = [&](const auto& function) {
        auto* stack = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!stack) return false;
        const DWORD64 controlPc = imageBase + 0x1000 +
            function.beginOffset + function.prologueSize + 1;
        DWORD64 functionImageBase = 0;
        PRUNTIME_FUNCTION functionEntry =
            lookup(controlPc, &functionImageBase, nullptr);
        bool result = functionEntry && functionImageBase == imageBase;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        context.Rip = controlPc;
        constexpr DWORD64 oldRbp = 0x1234432112344321ULL;
        constexpr DWORD64 returnAddress = 0x1234567887654321ULL;
        DWORD64 entryRsp = 0;
        if (function.framePointer) {
            const DWORD64 rbp =
                reinterpret_cast<DWORD64>(stack + 0x800);
            std::memcpy(reinterpret_cast<void*>(rbp), &oldRbp, sizeof(oldRbp));
            std::memcpy(
                reinterpret_cast<void*>(rbp + 8),
                &returnAddress, sizeof(returnAddress));
            context.Rbp = rbp;
            context.Rsp = rbp - function.stackAllocationSize;
        } else {
            entryRsp = reinterpret_cast<DWORD64>(stack + 0x800);
            std::memcpy(
                reinterpret_cast<void*>(entryRsp),
                &returnAddress, sizeof(returnAddress));
            context.Rbp = oldRbp;
            context.Rsp = entryRsp - function.stackAllocationSize;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        if (result) {
            virtualUnwind(
                UNW_FLAG_NHANDLER, imageBase, context.Rip, functionEntry,
                &context, &handlerData, &establisherFrame, nullptr);
            result = context.Rip == returnAddress &&
                (function.framePointer
                    ? context.Rsp == reinterpret_cast<DWORD64>(stack + 0x810) &&
                          context.Rbp == oldRbp
                    : context.Rsp == entryRsp + 8 &&
                          context.Rbp == oldRbp);
        }
        VirtualFree(stack, 0, MEM_RELEASE);
        return result;
    };
    const auto& entryStub =
        image.unwindFunctions[image.unwindFunctions.size() - 2];
    const auto& writeHelper = image.unwindFunctions.back();
    if (!unwindSingleFrame(entryStub) || !unwindSingleFrame(writeHelper)) {
        deleteTable(functions);
        VirtualFree(mappedImage, 0, MEM_RELEASE);
        std::cerr << "Windows RtlVirtualUnwind rejected entry/helper frame metadata\n";
        return false;
    }

    const auto middleFunction = std::find_if(
        image.unwindFunctions.begin(), image.unwindFunctions.end(),
        [](const auto& function) {
            return function.framePointer &&
                   function.stackAllocationSize > 128 &&
                   function.stackAllocationSize < 320;
        });
    const auto mainFunction = std::find_if(
        image.unwindFunctions.begin(), image.unwindFunctions.end(),
        [](const auto& function) {
            return function.framePointer &&
                   function.stackAllocationSize <= 128;
        });
    bool success = wideFunction != image.unwindFunctions.end() &&
                   middleFunction != image.unwindFunctions.end() &&
                   mainFunction != image.unwindFunctions.end();
    if (!success) {
        std::cerr << "test image does not contain both wide and small generated frames\n";
    } else {
        const auto controlPc = [imageBase](const auto& function) {
            return imageBase + 0x1000 + function.beginOffset +
                   function.prologueSize + 1;
        };
        const DWORD64 widePc = controlPc(*wideFunction);
        const DWORD64 middlePc = controlPc(*middleFunction);
        const DWORD64 mainPc = controlPc(*mainFunction);
        DWORD64 lookupImageBase = 0;
        PRUNTIME_FUNCTION wideEntry = lookup(widePc, &lookupImageBase, nullptr);
        success = wideEntry && lookupImageBase == imageBase;

        auto* stack = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!stack) {
            success = false;
            std::cerr << "failed to allocate synthetic Windows unwind stack memory\n";
        } else if (success) {
            const DWORD64 wideRbp =
                reinterpret_cast<DWORD64>(stack + 0x400);
            const DWORD64 middleRbp =
                reinterpret_cast<DWORD64>(stack + 0x900);
            const DWORD64 mainRbp =
                reinterpret_cast<DWORD64>(stack + 0x1400);
            constexpr DWORD64 hostRbp = 0x1122334455667788ULL;
            constexpr DWORD64 hostReturn = 0x8877665544332211ULL;
            std::memcpy(reinterpret_cast<void*>(wideRbp), &middleRbp, sizeof(middleRbp));
            std::memcpy(reinterpret_cast<void*>(wideRbp + 8), &middlePc, sizeof(middlePc));
            std::memcpy(reinterpret_cast<void*>(middleRbp), &mainRbp, sizeof(mainRbp));
            std::memcpy(reinterpret_cast<void*>(middleRbp + 8), &mainPc, sizeof(mainPc));
            std::memcpy(reinterpret_cast<void*>(mainRbp), &hostRbp, sizeof(hostRbp));
            std::memcpy(reinterpret_cast<void*>(mainRbp + 8), &hostReturn, sizeof(hostReturn));

            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            context.Rip = widePc;
            context.Rsp = wideRbp - wideFunction->stackAllocationSize;
            context.Rbp = wideRbp;
            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            virtualUnwind(
                UNW_FLAG_NHANDLER, imageBase, context.Rip, wideEntry, &context,
                &handlerData, &establisherFrame, nullptr);
            success = context.Rip == middlePc &&
                      context.Rsp == wideRbp + 16 &&
                      context.Rbp == middleRbp;

            lookupImageBase = 0;
            PRUNTIME_FUNCTION middleEntry =
                lookup(context.Rip, &lookupImageBase, nullptr);
            success = success && middleEntry && lookupImageBase == imageBase;
            if (success) {
                virtualUnwind(
                    UNW_FLAG_NHANDLER, imageBase, context.Rip, middleEntry,
                    &context, &handlerData, &establisherFrame, nullptr);
                success = context.Rip == mainPc &&
                          context.Rsp == middleRbp + 16 &&
                          context.Rbp == mainRbp;
            }
            lookupImageBase = 0;
            PRUNTIME_FUNCTION mainEntry =
                success ? lookup(context.Rip, &lookupImageBase, nullptr) : nullptr;
            success = success && mainEntry && lookupImageBase == imageBase;
            if (success) {
                virtualUnwind(
                    UNW_FLAG_NHANDLER, imageBase, context.Rip, mainEntry,
                    &context, &handlerData, &establisherFrame, nullptr);
                success = context.Rip == hostReturn &&
                          context.Rsp == mainRbp + 16 &&
                          context.Rbp == hostRbp;
            }
            VirtualFree(stack, 0, MEM_RELEASE);
        }
    }

    const bool removed = deleteTable(functions) != FALSE;
    VirtualFree(mappedImage, 0, MEM_RELEASE);
    if (!success || !removed) {
        std::cerr << "Windows RtlVirtualUnwind failed to walk nested generated frames\n";
        return false;
    }
    return true;
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: zp_native_abi_metadata_tests [pe-output]\n";
        return 2;
    }
    try {
        if (!testSystemVImportedStackArguments()) return 1;
        if (!testXmmRegisterAllocationSpills()) return 1;
        if (!testLargeStackUnwindEncoding()) return 1;
        const NativeImage image = generateUnwindTestImage();
        const auto pe = zplus::backend::native::writePE(image);
        PeUnwindDirectory directory;
        if (!testPeUnwindDirectory(image, pe, directory)) return 1;
#if defined(_WIN32)
        if (!testWindowsRuntimeUnwind(image, pe, directory)) return 1;
#endif
        if (argc == 2) {
            std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
            if (!output ||
                !output.write(
                    reinterpret_cast<const char*>(pe.data()),
                    static_cast<std::streamsize>(pe.size()))) {
                std::cerr << "failed to write generated PE image for inspection\n";
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "native ABI metadata test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
