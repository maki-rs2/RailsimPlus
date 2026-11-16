//
// SharedStateAxes.cpp
//
#include "SharedStateAxes.h"

#include <algorithm>

namespace rs::app {

namespace {

const rs::legacy_pi::SystemSwitchAxis& AxisOf(const SharedStateAxisEntry& entry) {
    return rs::legacy_pi::SystemSwitchAxisOf(entry.id);
}

bool InMask(uint32_t mask, size_t axisIndex) {
    return (mask & (1u << axisIndex)) != 0;
}

int32_t RadixOf(const rs::legacy_pi::SystemSwitchAxis& axis) {
    return axis.domainMax - axis.domainMin + 1;
}

} // namespace

uint32_t SharedStateAxisMask(const std::map<std::string, int32_t>& systemSwitches) {
    uint32_t mask = 0;
    for (size_t i = 0; i < kSharedStateAxes.size(); ++i) {
        if (systemSwitches.find(AxisOf(kSharedStateAxes[i]).name) != systemSwitches.end())
            mask |= 1u << i;
    }
    return mask;
}

size_t SharedStateSlotCount(uint32_t mask) {
    size_t count = 1;
    for (size_t i = 0; i < kSharedStateAxes.size(); ++i) {
        if (InMask(mask, i)) count *= static_cast<size_t>(RadixOf(AxisOf(kSharedStateAxes[i])));
    }
    return count;
}

size_t SharedStateSlotIndex(uint32_t mask, const rs::world::SceneSharedSwitchValues& values) {
    size_t index = 0;
    for (size_t i = 0; i < kSharedStateAxes.size(); ++i) {
        if (!InMask(mask, i)) continue;
        const rs::legacy_pi::SystemSwitchAxis& axis = AxisOf(kSharedStateAxes[i]);
        const int32_t value =
            std::clamp(values.*(kSharedStateAxes[i].member), axis.domainMin, axis.domainMax);
        index = index * static_cast<size_t>(RadixOf(axis)) +
                static_cast<size_t>(value - axis.domainMin);
    }
    return index;
}

rs::world::SceneSharedSwitchValues SharedStateSlotValues(uint32_t mask, size_t slot) {
    rs::world::SceneSharedSwitchValues values;
    size_t rest = slot;
    // 桁は SharedStateSlotIndex と逆順(最後の軸が最下位)に取り出す。
    for (size_t n = kSharedStateAxes.size(); n > 0; --n) {
        const size_t i = n - 1;
        const rs::legacy_pi::SystemSwitchAxis& axis = AxisOf(kSharedStateAxes[i]);
        if (!InMask(mask, i)) {
            values.*(kSharedStateAxes[i].member) = axis.domainMin;
            continue;
        }
        const size_t radix = static_cast<size_t>(RadixOf(axis));
        values.*(kSharedStateAxes[i].member) = axis.domainMin + static_cast<int32_t>(rest % radix);
        rest /= radix;
    }
    return values;
}

void WriteSharedStateSwitches(std::map<std::string, int32_t>& systemSwitches,
                              const rs::world::SceneSharedSwitchValues& values) {
    for (const SharedStateAxisEntry& entry : kSharedStateAxes) {
        const std::map<std::string, int32_t>::iterator it = systemSwitches.find(AxisOf(entry).name);
        if (it != systemSwitches.end()) it->second = values.*(entry.member);
    }
}

} // namespace rs::app
