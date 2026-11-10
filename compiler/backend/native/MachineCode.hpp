#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mpp::native {

// ============================================================
// MachineCode
//
// Simple x86-64 machine-code byte buffer.
//
// This class does NOT know about instructions.
// It only knows how to append raw bytes and integers.
//
// Example:
//
//     MachineCode code;
//
//     code.emit8(0x90);          // NOP
//     code.emit32(0x12345678);
//     code.emit64(0x1122334455667788ULL);
// ============================================================

class MachineCode {
public:

    // --------------------------------------------------------
    // Basic byte emission
    // --------------------------------------------------------

    void emit8(std::uint8_t value);

    void emit16(std::uint16_t value);

    void emit32(std::uint32_t value);

    void emit64(std::uint64_t value);


    // --------------------------------------------------------
    // Signed values
    // --------------------------------------------------------

    void emitS8(std::int8_t value);

    void emitS32(std::int32_t value);

    void emitS64(std::int64_t value);


    // --------------------------------------------------------
    // Raw data
    // --------------------------------------------------------

    void emitBytes(
        const std::vector<std::uint8_t>& bytes
    );

    void emitBytes(
        const std::uint8_t* data,
        std::size_t size
    );


    // --------------------------------------------------------
    // Access
    // --------------------------------------------------------

    const std::vector<std::uint8_t>& bytes() const;

    std::vector<std::uint8_t>& bytes();

    std::size_t size() const;

    bool empty() const;


    // --------------------------------------------------------
    // Modification
    // --------------------------------------------------------

    void clear();

    void reserve(std::size_t capacity);


    // --------------------------------------------------------
    // Patch existing bytes
    //
    // Used later for jumps:
    //
    //     JMP ?????
    //
    // We don't know the destination yet, so we reserve
    // space and patch it after generating the code.
    // --------------------------------------------------------

    void patch8(
        std::size_t offset,
        std::uint8_t value
    );

    void patch32(
        std::size_t offset,
        std::uint32_t value
    );

    void patch64(
        std::size_t offset,
        std::uint64_t value
    );


    // --------------------------------------------------------
    // Pointer to generated code
    // --------------------------------------------------------

    const std::uint8_t* data() const;

    std::uint8_t* data();


    // --------------------------------------------------------
    // Debug
    // --------------------------------------------------------

    std::string hex() const;


private:

    std::vector<std::uint8_t> bytes_;
};

} // namespace mpp::native