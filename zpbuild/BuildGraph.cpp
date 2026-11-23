#include "BuildGraph.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace zbuild {

std::vector<std::string> targetBuildOrder(
    const std::vector<TargetConfig>& targets,
    const std::string& selectedTarget
) {
    std::map<std::string, const TargetConfig*> byName;
    for (const TargetConfig& target : targets) {
        if (!byName.emplace(target.name, &target).second) {
            throw std::runtime_error("duplicate target name '" + target.name + "'");
        }
    }

    if (!selectedTarget.empty() && !byName.contains(selectedTarget)) {
        throw std::runtime_error("unknown target '" + selectedTarget + "'");
    }

    std::map<std::string, unsigned char> states;
    std::vector<std::string> stack;
    std::vector<std::string> order;
    const auto visit = [&](const auto& self, const std::string& name) -> void {
        const unsigned char state = states[name];
        if (state == 2) return;
        if (state == 1) {
            const auto cycleBegin = std::find(stack.begin(), stack.end(), name);
            std::ostringstream cycle;
            for (auto it = cycleBegin; it != stack.end(); ++it) {
                if (it != cycleBegin) cycle << " -> ";
                cycle << *it;
            }
            cycle << " -> " << name;
            throw std::runtime_error("circular target dependency: " + cycle.str());
        }

        states[name] = 1;
        stack.push_back(name);
        std::vector<std::string> dependencies = byName.at(name)->dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(
            std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        for (const std::string& dependency : dependencies) {
            if (!byName.contains(dependency)) {
                throw std::runtime_error(
                    "target '" + name + "' depends on missing target '" + dependency + "'");
            }
            self(self, dependency);
        }
        stack.pop_back();
        states[name] = 2;
        order.push_back(name);
    };

    if (selectedTarget.empty()) {
        for (const auto& [name, target] : byName) {
            (void)target;
            visit(visit, name);
        }
    } else {
        visit(visit, selectedTarget);
    }
    return order;
}

} // namespace zbuild
