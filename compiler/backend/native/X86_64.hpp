#pragma once

#include "MachineCode.hpp"

#include <cstdint>

namespace mpp::native {

// ============================================================
// x86-64 Registers
// ============================================================

enum class Register : std::uint8_t {

    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,

    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,

    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,

    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15
};


// ============================================================
// X86_64 Encoder
//
// Converts high-level instructions into raw x86-64 bytes.
//
// Example:
//
//     X86_64 cpu(code);
//
//     cpu.mov(Register::RAX, 42);
//     cpu.add(Register::RAX, Register::RBX);
//
// Produces:
//
//     48 B8 2A 00 00 00 00 00 00 00
//     ...
// ============================================================

class X86_64 {
public:

    explicit X86_64(
        MachineCode& code
    );


    // --------------------------------------------------------
    // Data movement
    // --------------------------------------------------------

    void mov(
        Register destination,
        Register source
    );

    void mov(
        Register destination,
        std::uint64_t immediate
    );


    // --------------------------------------------------------
    // Stack
    // --------------------------------------------------------

    void push(
        Register reg
    );

    void pop(
        Register reg
    );


    // --------------------------------------------------------
    // Arithmetic
    // --------------------------------------------------------

    void add(
        Register destination,
        Register source
    );

    void sub(
        Register destination,
        Register source
    );

    void imul(
        Register destination,
        Register source
    );

    void neg(
        Register reg
    );


    // --------------------------------------------------------
    // Division
    // --------------------------------------------------------

    void cqo();

    void idiv(
        Register divisor
    );


    // --------------------------------------------------------
    // Comparison
    // --------------------------------------------------------

    void cmp(
        Register left,
        Register right
    );


    // --------------------------------------------------------
    // Control flow
    // --------------------------------------------------------

    void ret();

    void nop();

    void jmp(
        std::int32_t relativeOffset
    );

    void je(
        std::int32_t relativeOffset
    );

    void jne(
        std::int32_t relativeOffset
    );

    void jl(
        std::int32_t relativeOffset
    );

    void jg(
        std::int32_t relativeOffset
    );

    void jle(
        std::int32_t relativeOffset
    );

    void jge(
        std::int32_t relativeOffset
    );


    // --------------------------------------------------------
    // Conditional set
    // --------------------------------------------------------

    void sete(
        Register destination
    );

    void setne(
        Register destination
    );

    void setl(
        Register destination
    );

    void setg(
        Register destination
    );

    void setle(
        Register destination
    );

    void setge(
        Register destination
    );


private:

    MachineCode& code_;


    // --------------------------------------------------------
    // REX prefix
    // --------------------------------------------------------

    void rex(
        bool w,
        Register reg,
        Register rm
    );


    // --------------------------------------------------------
    // ModRM byte
    // --------------------------------------------------------

    void modrm(
        std::uint8_t mod,
        Register reg,
        Register rm
    );


    // --------------------------------------------------------
    // Register encoding
    // --------------------------------------------------------

    static std::uint8_t low3(
        Register reg
    );

    static bool high(
        Register reg
    );
};

} // namespace mpp::native