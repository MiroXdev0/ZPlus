#include "MachineCode.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mpp::native {

// ============================================================
// Internal helpers
// ============================================================

namespace {

template <typename T>
void emitLittleEndian(
    std::vector<std::uint8_t>& buffer,
    T value
) {
    static_assert(
        std::is_integral_v<T>,
        "emitLittleEndian requires an integral type"
    );

    using UnsignedT = std::make_unsigned_t<T>;

    const UnsignedT unsignedValue =
        static_cast<UnsignedT>(value);

    for (std::size_t i = 0;
         i < sizeof(T);
         ++i) {

        buffer.push_back(
            static_cast<std::uint8_t>(
                (unsignedValue >> (i * 8)) & 0xFF
            )
        );
    }
}

template <typename T>
void patchLittleEndian(
    std::vector<std::uint8_t>& buffer,
    std::size_t offset,
    T value
) {
    static_assert(
        std::is_integral_v<T>,
        "patchLittleEndian requires an integral type"
    );

    if (offset > buffer.size() ||
        sizeof(T) > buffer.size() - offset) {

        throw std::out_of_range(
            "MachineCode patch offset out of range"
        );
    }

    using UnsignedT = std::make_unsigned_t<T>;

    const UnsignedT unsignedValue =
        static_cast<UnsignedT>(value);

    for (std::size_t i = 0;
         i < sizeof(T);
         ++i) {

        buffer[offset + i] =
            static_cast<std::uint8_t>(
                (unsignedValue >> (i * 8)) & 0xFF
            );
    }
}

} // namespace


// ============================================================
// Basic byte emission
// ============================================================

void MachineCode::emit8(
    std::uint8_t value
) {
    bytes_.push_back(value);
}


void MachineCode::emit16(
    std::uint16_t value
) {
    emitLittleEndian(bytes_, value);
}


void MachineCode::emit32(
    std::uint32_t value
) {
    emitLittleEndian(bytes_, value);
}


void MachineCode::emit64(
    std::uint64_t value
) {
    emitLittleEndian(bytes_, value);
}


// ============================================================
// Signed values
// ============================================================

void MachineCode::emitS8(
    std::int8_t value
) {
    emitLittleEndian(bytes_, value);
}


void MachineCode::emitS32(
    std::int32_t value
) {
    emitLittleEndian(bytes_, value);
}


void MachineCode::emitS64(
    std::int64_t value
) {
    emitLittleEndian(bytes_, value);
}


// ============================================================
// Raw bytes
// ============================================================

void MachineCode::emitBytes(
    const std::vector<std::uint8_t>& bytes
) {
    if (bytes.empty()) {
        return;
    }

    bytes_.insert(
        bytes_.end(),
        bytes.begin(),
        bytes.end()
    );
}


void MachineCode::emitBytes(
    const std::uint8_t* data,
    std::size_t size
) {
    if (data == nullptr || size == 0) {
        return;
    }

    bytes_.insert(
        bytes_.end(),
        data,
        data + size
    );
}


// ============================================================
// Access
// ============================================================

const std::vector<std::uint8_t>&
MachineCode::bytes() const {
    return bytes_;
}


std::vector<std::uint8_t>&
MachineCode::bytes() {
    return bytes_;
}


std::size_t MachineCode::size() const {
    return bytes_.size();
}


bool MachineCode::empty() const {
    return bytes_.empty();
}


// ============================================================
// Modification
// ============================================================

void MachineCode::clear() {
    bytes_.clear();
}


void MachineCode::reserve(
    std::size_t capacity
) {
    bytes_.reserve(capacity);
}


// ============================================================
// Patching
// ============================================================

void MachineCode::patch8(
    std::size_t offset,
    std::uint8_t value
) {
    if (offset >= bytes_.size()) {
        throw std::out_of_range(
            "MachineCode::patch8: offset out of range"
        );
    }

    bytes_[offset] = value;
}


void MachineCode::patch32(
    std::size_t offset,
    std::uint32_t value
) {
    patchLittleEndian(
        bytes_,
        offset,
        value
    );
}


void MachineCode::patch64(
    std::size_t offset,
    std::uint64_t value
) {
    patchLittleEndian(
        bytes_,
        offset,
        value
    );
}


// ============================================================
// Data pointer
// ============================================================

const std::uint8_t*
MachineCode::data() const {

    if (bytes_.empty()) {
        return nullptr;
    }

    return bytes_.data();
}


std::uint8_t*
MachineCode::data() {

    if (bytes_.empty()) {
        return nullptr;
    }

    return bytes_.data();
}


// ============================================================
// Debug hex dump
// ============================================================

std::string MachineCode::hex() const {

    std::ostringstream output;

    output
        << std::uppercase
        << std::hex
        << std::setfill('0');

    for (std::size_t i = 0;
         i < bytes_.size();
         ++i) {

        if (i != 0) {
            output << ' ';
        }

        output
            << std::setw(2)
            << static_cast<unsigned int>(
                bytes_[i]
            );
    }

    return output.str();
}

} // namespace mpp::native