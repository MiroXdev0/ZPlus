#include "X86_64.hpp"

namespace mpp::native {

// ============================================================
// Constructor
// ============================================================

X86_64::X86_64(
    MachineCode& code
)
    : code_(code) {
}


// ============================================================
// Register helpers
// ============================================================

std::uint8_t X86_64::low3(
    Register reg
) {
    return static_cast<std::uint8_t>(reg) & 0x07;
}


bool X86_64::high(
    Register reg
) {
    return static_cast<std::uint8_t>(reg) >= 8;
}


// ============================================================
// REX prefix
// ============================================================

void X86_64::rex(
    bool w,
    Register reg,
    Register rm
) {
    std::uint8_t prefix = 0x40;

    if (w) {
        prefix |= 0x08;
    }

    if (high(reg)) {
        prefix |= 0x04;
    }

    if (high(rm)) {
        prefix |= 0x01;
    }

    code_.emit8(prefix);
}


// ============================================================
// ModRM
// ============================================================

void X86_64::modrm(
    std::uint8_t mod,
    Register reg,
    Register rm
) {
    code_.emit8(
        static_cast<std::uint8_t>(
            (mod << 6) |
            (low3(reg) << 3) |
            low3(rm)
        )
    );
}


// ============================================================
// MOV register, register
//
// 48 89 /r
// ============================================================

void X86_64::mov(
    Register destination,
    Register source
) {
    rex(
        true,
        source,
        destination
    );

    code_.emit8(0x89);

    modrm(
        0x03,
        source,
        destination
    );
}


// ============================================================
// MOV register, immediate
//
// REX.W + B8+rd + imm64
// ============================================================

void X86_64::mov(
    Register destination,
    std::uint64_t immediate
) {
    std::uint8_t rexPrefix = 0x48;

    if (high(destination)) {
        rexPrefix |= 0x01;
    }

    code_.emit8(rexPrefix);

    code_.emit8(
        static_cast<std::uint8_t>(
            0xB8 + low3(destination)
        )
    );

    code_.emit64(immediate);
}


// ============================================================
// PUSH
// ============================================================

void X86_64::push(
    Register reg
) {
    if (high(reg)) {
        code_.emit8(0x41);
    }

    code_.emit8(
        static_cast<std::uint8_t>(
            0x50 + low3(reg)
        )
    );
}


// ============================================================
// POP
// ============================================================

void X86_64::pop(
    Register reg
) {
    if (high(reg)) {
        code_.emit8(0x41);
    }

    code_.emit8(
        static_cast<std::uint8_t>(
            0x58 + low3(reg)
        )
    );
}


// ============================================================
// ADD
//
// 48 01 /r
// ============================================================

void X86_64::add(
    Register destination,
    Register source
) {
    rex(
        true,
        source,
        destination
    );

    code_.emit8(0x01);

    modrm(
        0x03,
        source,
        destination
    );
}


// ============================================================
// SUB
//
// 48 29 /r
// ============================================================

void X86_64::sub(
    Register destination,
    Register source
) {
    rex(
        true,
        source,
        destination
    );

    code_.emit8(0x29);

    modrm(
        0x03,
        source,
        destination
    );
}


// ============================================================
// IMUL
//
// 48 0F AF /r
// ============================================================

void X86_64::imul(
    Register destination,
    Register source
) {
    rex(
        true,
        destination,
        source
    );

    code_.emit8(0x0F);
    code_.emit8(0xAF);

    modrm(
        0x03,
        destination,
        source
    );
}


// ============================================================
// NEG
//
// 48 F7 /3
// ============================================================

void X86_64::neg(
    Register reg
) {
    rex(
        true,
        Register::RAX,
        reg
    );

    code_.emit8(0xF7);

    modrm(
        0x03,
        static_cast<Register>(3),
        reg
    );
}


// ============================================================
// CQO
//
// 48 99
// ============================================================

void X86_64::cqo() {
    code_.emit8(0x48);
    code_.emit8(0x99);
}


// ============================================================
// IDIV
//
// 48 F7 /7
// ============================================================

void X86_64::idiv(
    Register divisor
) {
    rex(
        true,
        Register::RAX,
        divisor
    );

    code_.emit8(0xF7);

    modrm(
        0x03,
        static_cast<Register>(7),
        divisor
    );
}


// ============================================================
// CMP
//
// 48 39 /r
// ============================================================

void X86_64::cmp(
    Register left,
    Register right
) {
    rex(
        true,
        right,
        left
    );

    code_.emit8(0x39);

    modrm(
        0x03,
        right,
        left
    );
}


// ============================================================
// RET
// ============================================================

void X86_64::ret() {
    code_.emit8(0xC3);
}


// ============================================================
// NOP
// ============================================================

void X86_64::nop() {
    code_.emit8(0x90);
}


// ============================================================
// JMP
//
// E9 rel32
// ============================================================

void X86_64::jmp(
    std::int32_t relativeOffset
) {
    code_.emit8(0xE9);
    code_.emitS32(relativeOffset);
}


// ============================================================
// Conditional jumps
// ============================================================

void X86_64::je(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x84);
    code_.emitS32(relativeOffset);
}


void X86_64::jne(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x85);
    code_.emitS32(relativeOffset);
}


void X86_64::jl(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x8C);
    code_.emitS32(relativeOffset);
}


void X86_64::jg(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x8F);
    code_.emitS32(relativeOffset);
}


void X86_64::jle(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x8E);
    code_.emitS32(relativeOffset);
}


void X86_64::jge(
    std::int32_t relativeOffset
) {
    code_.emit8(0x0F);
    code_.emit8(0x8D);
    code_.emitS32(relativeOffset);
}


// ============================================================
// SETcc
//
// These write an 8-bit value.
//
// For now the destination must be one of the low registers.
// ============================================================

void X86_64::sete(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x94);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}


void X86_64::setne(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x95);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}


void X86_64::setl(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x9C);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}


void X86_64::setg(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x9F);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}


void X86_64::setle(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x9E);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}


void X86_64::setge(
    Register destination
) {
    std::uint8_t rexPrefix = 0x40;

    if (high(destination)) {
        rexPrefix |= 0x41;
    }

    code_.emit8(rexPrefix);
    code_.emit8(0x0F);
    code_.emit8(0x9D);

    modrm(
        0x03,
        Register::RAX,
        destination
    );
}

} // namespace mpp::native