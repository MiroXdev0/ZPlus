#pragma once

#include "compiler/ir/IR.hpp"

#include <string>
#include <vector>

namespace zplus::runtime {

// ============================================================
// Runtime
// ============================================================
//
// Executes Z+ IR produced by IRBuilder.
//
// Supported entry points:
//
//     main()
//     start()
//
// If neither exists, the first function in the module is used.
//
// ============================================================

class Runtime {

public:

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------

    Runtime() = default;

    // --------------------------------------------------------
    // Initialize runtime
    // --------------------------------------------------------

    void initialize();

    // --------------------------------------------------------
    // Execute IR module
    // --------------------------------------------------------

    int execute(
        const zplus::ir::Module& module,
        const std::vector<std::string>& arguments = {}
    );

    // --------------------------------------------------------
    // Execute textual bytecode
    // --------------------------------------------------------

    int executeBytecode(
        const std::string& bytecode
    );
};


// ============================================================
// Convenience functions
// ============================================================
//
// These let existing code use:
//
//     zplus::runtime::execute(module)
//
// without needing to manually create Runtime.
//

int execute(
    const zplus::ir::Module& module
);

int executeBytecode(
    const std::string& bytecode
);

void initialize();

} // namespace zplus::runtime