#include "IRAnalysis.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace zplus::ir {
namespace {

bool isReference(const Value& value) {
    if (value.name.empty()) return false;
    switch (value.kind) {
        case Value::Kind::Identifier:
        case Value::Kind::Unknown:
            return value.kind == Value::Kind::Identifier ||
                value.name.front() == '%';
        case Value::Kind::Number:
        case Value::Kind::String:
        case Value::Kind::Character:
        case Value::Kind::Boolean:
        case Value::Kind::Null:
            return false;
    }
    return false;
}

void addUse(const Value& value, LiveValueSet& uses) {
    if (isReference(value)) uses.insert(value.name);
}

bool isCallLike(const Instruction& instruction) {
    const std::string& opcode = instruction.opcode;
    return opcode == "call" || opcode == "scope_pop" ||
        opcode == "mod" || opcode == "mod_assign" ||
        opcode == "array" || opcode == "index" ||
        opcode == "index_store" || opcode == "member" ||
        opcode == "member_store" || opcode == "member_ptr" ||
        opcode == "struct_new" || opcode == "is" ||
        opcode == "throw";
}

bool mayThrow(const Instruction& instruction) {
    const std::string& opcode = instruction.opcode;
    return isCallLike(instruction) || opcode == "div" ||
        opcode == "div_assign" || opcode == "load" ||
        opcode == "add" || opcode.starts_with("cmp_");
}

InstructionLiveness instructionLiveness(const Instruction& instruction) {
    InstructionLiveness result;
    result.mayCall = isCallLike(instruction);
    result.mayThrow = mayThrow(instruction);
    if (instruction.hasResult()) {
        result.definitions.insert(instruction.result);
    }

    const auto use = [&](std::size_t index) {
        if (index < instruction.operands.size()) {
            addUse(instruction.operands[index], result.uses);
        }
    };
    const auto useAll = [&](std::size_t first = 0) {
        for (std::size_t index = first;
             index < instruction.operands.size();
             ++index) {
            use(index);
        }
    };

    const std::string& opcode = instruction.opcode;
    if (opcode == "store") {
        if (!instruction.operands.empty() &&
            isReference(instruction.operands.front())) {
            result.definitions.insert(instruction.operands.front().name);
        }
        use(1);
    } else if (opcode.ends_with("_assign")) {
        use(0);
        use(1);
        if (!instruction.operands.empty() &&
            isReference(instruction.operands.front())) {
            result.definitions.insert(instruction.operands.front().name);
        }
    } else if (opcode == "pre_inc" || opcode == "post_inc" ||
               opcode == "pre_dec" || opcode == "post_dec" ||
               opcode == "dec") {
        use(0);
        if (!instruction.operands.empty() &&
            isReference(instruction.operands.front())) {
            result.definitions.insert(instruction.operands.front().name);
        }
    } else if (opcode == "alloc" || opcode == "freeze" ||
               opcode == "param" || opcode == "scope_push" ||
               opcode == "scope_pop" || opcode == "try_push" ||
               opcode == "try_pop" || opcode == "br") {
        // These operands are storage names, labels, or runtime metadata.
    } else if (opcode == "condbr") {
        use(0);
    } else if (opcode == "switch") {
        use(0);
        for (std::size_t index = 1;
             index + 1 < instruction.operands.size();
             index += 2) {
            use(index);
        }
    } else if (opcode == "call" || opcode == "defer_push") {
        useAll(1);
    } else if (opcode == "struct_new") {
        useAll(1);
    } else if (opcode == "member" || opcode == "member_ptr" ||
               opcode == "is") {
        use(0);
    } else if (opcode == "member_store") {
        use(0);
        use(2);
    } else {
        useAll();
    }

    return result;
}

void unionInto(LiveValueSet& destination, const LiveValueSet& source) {
    destination.insert(source.begin(), source.end());
}

} // namespace

FunctionLiveness analyzeLiveness(const Function& function) {
    FunctionLiveness analysis;
    analysis.blocks.resize(function.blocks.size());

    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        analysis.blockIndices.emplace(function.blocks[index].label, index);
        analysis.blocks[index].label = function.blocks[index].label;
        analysis.blocks[index].instructions.reserve(
            function.blocks[index].instructions.size());
    }

    std::unordered_map<std::string, std::string> handlerBindings;
    std::vector<std::size_t> handlerBlocks;
    for (const BasicBlock& block : function.blocks) {
        for (const Instruction& instruction : block.instructions) {
            if (instruction.opcode != "try_push" ||
                instruction.operands.size() != 2) {
                continue;
            }
            const auto target =
                analysis.blockIndices.find(instruction.operands[0].name);
            if (target == analysis.blockIndices.end()) continue;
            handlerBindings.insert_or_assign(
                instruction.operands[0].name,
                instruction.operands[1].name);
            handlerBlocks.push_back(target->second);
        }
    }
    std::sort(handlerBlocks.begin(), handlerBlocks.end());
    handlerBlocks.erase(
        std::unique(handlerBlocks.begin(), handlerBlocks.end()),
        handlerBlocks.end());

    for (std::size_t blockIndex = 0;
         blockIndex < function.blocks.size();
         ++blockIndex) {
        const BasicBlock& block = function.blocks[blockIndex];
        BasicBlockLiveness& blockInfo = analysis.blocks[blockIndex];
        const Instruction* last = block.lastInstruction();
        const auto addSuccessor = [&](const Value& target) {
            const auto found = analysis.blockIndices.find(target.name);
            if (found != analysis.blockIndices.end()) {
                blockInfo.successors.push_back(found->second);
            }
        };
        if (last && last->opcode == "br" && last->operands.size() == 1) {
            addSuccessor(last->operands.front());
        } else if (last && last->opcode == "condbr" &&
                   last->operands.size() == 3) {
            addSuccessor(last->operands[1]);
            addSuccessor(last->operands[2]);
        } else if (last && last->opcode == "switch" &&
                   last->operands.size() >= 2 &&
                   last->operands.size() % 2 == 0) {
            for (std::size_t index = 2; index < last->operands.size(); index += 2) {
                addSuccessor(last->operands[index]);
            }
            addSuccessor(last->operands.back());
        } else if (blockIndex + 1 < function.blocks.size() &&
                   (!last || (last->opcode != "ret" &&
                              last->opcode != "throw"))) {
            blockInfo.successors.push_back(blockIndex + 1);
        }
        std::sort(blockInfo.successors.begin(), blockInfo.successors.end());
        blockInfo.successors.erase(
            std::unique(blockInfo.successors.begin(),
                        blockInfo.successors.end()),
            blockInfo.successors.end());

        for (const Instruction& instruction : block.instructions) {
            InstructionLiveness instructionInfo =
                instructionLiveness(instruction);
            for (const std::string& use : instructionInfo.uses) {
                if (!blockInfo.definitions.contains(use)) {
                    blockInfo.uses.insert(use);
                }
            }
            unionInto(blockInfo.definitions, instructionInfo.definitions);
            blockInfo.instructions.push_back(std::move(instructionInfo));
        }
        if (blockIndex == 0) {
            for (const Parameter& parameter : function.parameters) {
                blockInfo.definitions.insert(parameter.name);
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t blockIndex = analysis.blocks.size();
             blockIndex-- > 0;) {
            BasicBlockLiveness& block = analysis.blocks[blockIndex];
            LiveValueSet live = block.liveOut;
            for (std::size_t index = block.instructions.size();
                 index-- > 0;) {
                const InstructionLiveness& instruction =
                    block.instructions[index];
                if (instruction.mayThrow) {
                    for (const std::size_t handlerIndex : handlerBlocks) {
                        LiveValueSet handlerLive =
                            analysis.blocks[handlerIndex].liveIn;
                        if (const auto binding = handlerBindings.find(
                                analysis.blocks[handlerIndex].label);
                            binding != handlerBindings.end()) {
                            handlerLive.erase(binding->second);
                        }
                        unionInto(live, handlerLive);
                    }
                }
                for (const std::string& definition : instruction.definitions) {
                    live.erase(definition);
                }
                unionInto(live, instruction.uses);
            }
            if (blockIndex == 0) {
                for (const Parameter& parameter : function.parameters) {
                    live.erase(parameter.name);
                }
            }

            LiveValueSet liveOut;
            for (const std::size_t successor : block.successors) {
                unionInto(liveOut, analysis.blocks[successor].liveIn);
            }

            if (live != block.liveIn || liveOut != block.liveOut) {
                block.liveIn = std::move(live);
                block.liveOut = std::move(liveOut);
                changed = true;
            }
        }
    }

    for (std::size_t blockIndex = 0;
         blockIndex < analysis.blocks.size();
         ++blockIndex) {
        BasicBlockLiveness& block = analysis.blocks[blockIndex];
        LiveValueSet live = block.liveOut;
        for (std::size_t index = block.instructions.size();
             index-- > 0;) {
            InstructionLiveness& instruction = block.instructions[index];
            instruction.liveAfter = live;
            if (instruction.mayThrow) {
                for (const std::size_t handlerIndex : handlerBlocks) {
                    LiveValueSet handlerLive =
                        analysis.blocks[handlerIndex].liveIn;
                    if (const auto binding = handlerBindings.find(
                            analysis.blocks[handlerIndex].label);
                        binding != handlerBindings.end()) {
                        handlerLive.erase(binding->second);
                    }
                    unionInto(live, handlerLive);
                }
            }
            for (const std::string& definition : instruction.definitions) {
                live.erase(definition);
            }
            unionInto(live, instruction.uses);
            instruction.liveBefore = live;
        }
    }

    return analysis;
}

} // namespace zplus::ir
