#include "Rs2DiaImport.h"

#include <unordered_set>
#include <utility>

namespace rs::legacy_layout {

using rs::world::DiaAction;
using rs::world::DiaTimeOfDay;
using rs::world::DiaTimeType;
using rs::world::PlatformRoutineElement;

PlatformRoutineElement ConvertDiaElement(const Rs2DiaElement& sourceElement) {
    PlatformRoutineElement element;
    element.enabled = true;
    element.action = static_cast<DiaAction>(sourceElement.action);
    if (sourceElement.timeType == 0) {
        element.timeType = DiaTimeType::ElapsedFromArrival;
        element.dwellSeconds = sourceElement.time[0] * 3600.0 +
                               sourceElement.time[1] * 60.0 + sourceElement.time[2];
    } else if (sourceElement.timeType == 1) {
        element.timeType = DiaTimeType::AbsoluteTimeOfDay;
        element.departureTimes.push_back(
            DiaTimeOfDay{sourceElement.time[0], sourceElement.time[1], sourceElement.time[2]});
    }
    // 要素ごとの停止位置は、番線既定より優先する正規化Offsetとして取り込む。
    element.stopOffsetNormalized = sourceElement.offset;
    return element;
}

namespace {

std::vector<PlatformRoutineElement> ConvertRoutineElements(const Rs2DiaRoutine& routine) {
    std::vector<PlatformRoutineElement> result;
    result.reserve(routine.elements.size());
    for (const Rs2DiaElement& sourceElement : routine.elements) {
        result.push_back(ConvertDiaElement(sourceElement));
    }
    return result;
}

} // namespace

ConvertedPlatformRoutines ConvertDiaRoutineToPlatformRoutine(const Rs2DiaInst& diaInst,
                                                              std::vector<std::string>& warnings) {
    ConvertedPlatformRoutines result;
    bool hasDefaultRoutine = false;
    std::unordered_set<Rs2Address> seenTrainGroups;
    for (const Rs2DiaRoutine& routine : diaInst.routines) {
        if (Rs2AddressIsNull(routine.trainGroup)) {
            if (hasDefaultRoutine) {
                warnings.push_back("DiaInst " + diaInst.name + ": 既定DiaListが複数あるため先頭以外を破棄");
                continue;
            }
            result.defaultRoutine = ConvertRoutineElements(routine);
            hasDefaultRoutine = true;
            continue;
        }
        if (!seenTrainGroups.insert(routine.trainGroup).second) {
            warnings.push_back("DiaInst " + diaInst.name +
                               ": 同じTrainGroupのDiaListが複数あるため先頭以外を破棄: " +
                               routine.trainGroup);
            continue;
        }
        // UseDefault=yesのリストは要素があっても使われず、その列車は既定のリストで走る。
        if (routine.useDefault) continue;
        result.trainGroups.push_back({routine.trainGroup, ConvertRoutineElements(routine)});
    }
    return result;
}

} // namespace rs::legacy_layout
