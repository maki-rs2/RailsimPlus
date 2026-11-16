#include "SwitchAssignmentResolve.h"

namespace rs::legacy_pi::sidecar {

std::vector<std::pair<size_t, int32_t>> ResolveSwitchAssignmentChanges(
    const std::vector<rs::legacy_pi::SwitchDef>& switchDefs,
    const std::vector<int32_t>& currentSwitchOptions,
    const SwitchAssignment& assignment,
    std::vector<std::string>* outIgnoredKeys) {
    std::vector<std::pair<size_t, int32_t>> changes;
    for (const std::pair<const std::string, int>& entry : assignment) {
        const std::string& key = entry.first;
        if (!key.empty() && key.front() == '_') {
            if (outIgnoredKeys != nullptr) outIgnoredKeys->push_back(key);
            continue;
        }

        size_t switchIndex = 0;
        while (switchIndex < switchDefs.size() && switchDefs[switchIndex].name != key) {
            ++switchIndex;
        }
        if (switchIndex == switchDefs.size()) {
            if (outIgnoredKeys != nullptr) outIgnoredKeys->push_back(key);
            continue;
        }

        const int32_t currentValue = switchIndex < currentSwitchOptions.size()
                                         ? currentSwitchOptions[switchIndex]
                                         : 0;
        const int32_t newValue = static_cast<int32_t>(entry.second);
        if (currentValue != newValue) changes.emplace_back(switchIndex, newValue);
    }
    return changes;
}

} // namespace rs::legacy_pi::sidecar
