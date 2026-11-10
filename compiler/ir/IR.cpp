#include "IR.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace zplus::ir {

// ============================================================
// IRBuilder helpers
// ============================================================
//
// The actual AST -> IR lowering lives in IRBuilder.cpp.
// These helpers manage temporary registers, labels, and
// basic-block creation.
// ============================================================

// ============================================================
// Create basic block
// ============================================================

BasicBlock& IRBuilder::createBlock(
    Function& function,
    const std::string& prefix
) const {

    const std::string label =
        nextLabel(prefix);

    return function.createBlock(label);
}


// ============================================================
// Temporary values
// ============================================================

std::string IRBuilder::nextTemporary() const {

    std::ostringstream stream;

    stream
        << "%"
        << temporaryCounter_++;

    return stream.str();
}


// ============================================================
// Labels
// ============================================================

std::string IRBuilder::nextLabel(
    const std::string& prefix
) const {

    std::ostringstream stream;

    stream
        << prefix
        << "."
        << labelCounter_++;

    return stream.str();
}

} // namespace zplus::ir