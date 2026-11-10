#pragma once

#include "../ir/IR.hpp"

#include <string>

namespace zplus::backend {

class CodeGenerator {

public:

    std::string generate(
        const zplus::ir::Module& module
    ) const;
};

} // namespace zplus::backend