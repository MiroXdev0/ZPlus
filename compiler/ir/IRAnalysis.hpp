#pragma once

#include "IR.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zplus::ir {

using LiveValueSet = std::unordered_set<std::string>;

struct InstructionLiveness {
    LiveValueSet uses;
    LiveValueSet definitions;
    LiveValueSet liveBefore;
    LiveValueSet liveAfter;
    bool mayCall = false;
    bool mayThrow = false;
};

struct BasicBlockLiveness {
    std::string label;
    LiveValueSet uses;
    LiveValueSet definitions;
    LiveValueSet liveIn;
    LiveValueSet liveOut;
    std::vector<std::size_t> successors;
    std::vector<InstructionLiveness> instructions;
};

struct FunctionLiveness {
    std::vector<BasicBlockLiveness> blocks;
    std::unordered_map<std::string, std::size_t> blockIndices;
};

FunctionLiveness analyzeLiveness(const Function& function);

} // namespace zplus::ir
