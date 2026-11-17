#include "PlatformRoutine.h"

#include <algorithm>

#include "../scene/TrainRunInfoRegistry.h"

namespace rs::world {
namespace {
bool HasEnabledElement(const std::vector<PlatformRoutineElement>& routine) {
    return std::any_of(routine.begin(), routine.end(),
                       [](const PlatformRoutineElement& element) { return element.enabled; });
}

int NormalizeCursor(int cursor, int count) {
    return ((cursor % count) + count) % count;
}

}  // namespace

const char* DiaActionKey(DiaAction action) {
    switch (action) {
    case DiaAction::Stop: return "stop";
    case DiaAction::Reverse: return "reverse";
    case DiaAction::Pass: return "pass";
    }
    return "stop";
}

std::optional<DiaAction> DiaActionFromKey(std::string_view key) {
    for (const DiaAction action : {DiaAction::Stop, DiaAction::Reverse, DiaAction::Pass}) {
        if (key == DiaActionKey(action)) return action;
    }
    return std::nullopt;
}

ResolvedPlatformRoutine ResolveTrainRoutine(
    const std::vector<PlatformTrainRoutine>& trainRoutines,
    const std::vector<PlatformTagRoutine>& tagRoutines,
    const std::vector<PlatformRoutineElement>& commonRoutine, TrainId trainId,
    const std::vector<int>& trainTagIds) {
    static const std::vector<PlatformFormationRoutine> kNoFormationRoutines;
    return ResolveTrainRoutine(trainRoutines, kNoFormationRoutines, tagRoutines,
                               commonRoutine, trainId, NilGuid(), trainTagIds);
}

ResolvedPlatformRoutine ResolveTrainRoutine(
    const std::vector<PlatformTrainRoutine>& trainRoutines,
    const std::vector<PlatformFormationRoutine>& formationRoutines,
    const std::vector<PlatformTagRoutine>& tagRoutines,
    const std::vector<PlatformRoutineElement>& commonRoutine, TrainId trainId,
    const Guid& formationId, const std::vector<int>& trainTagIds) {
    if (!GuidIsNil(trainId)) {
        for (const PlatformTrainRoutine& trainRoutine : trainRoutines) {
            if (trainRoutine.trainId == trainId && HasEnabledElement(trainRoutine.routine)) {
                return {&trainRoutine.routine, kCommonRoutineLayerKey, trainId};
            }
        }
    }
    if (!GuidIsNil(formationId)) {
        for (const PlatformFormationRoutine& formationRoutine : formationRoutines) {
            if (formationRoutine.formationId == formationId &&
                HasEnabledElement(formationRoutine.routine)) {
                return {&formationRoutine.routine, kCommonRoutineLayerKey, trainId};
            }
        }
    }
    for (const PlatformTagRoutine& tagRoutine : tagRoutines) {
        const bool hasTag = std::find(trainTagIds.begin(), trainTagIds.end(), tagRoutine.tagId) != trainTagIds.end();
        if (hasTag && HasEnabledElement(tagRoutine.routine)) {
            return {&tagRoutine.routine, TagRoutineLayerKey(tagRoutine.tagId)};
        }
    }
    return {&commonRoutine, kCommonRoutineLayerKey};
}

int ResolveNextRoutineIndex(const std::vector<PlatformRoutineElement>& routine, int cursor) {
    if (routine.empty()) return -1;
    const int count = static_cast<int>(routine.size());
    const int start = NormalizeCursor(cursor, count);
    for (int offset = 0; offset < count; ++offset) {
        const int index = (start + offset) % count;
        if (routine[static_cast<size_t>(index)].enabled) return index;
    }
    return -1;
}

const PlatformRoutineElement* PeekRoutineElement(
    const std::vector<PlatformRoutineElement>& routine, int cursor) {
    if (routine.empty()) return nullptr;
    const int index = ResolveNextRoutineIndex(routine, cursor);
    return index < 0 ? nullptr : &routine[static_cast<size_t>(index)];
}

const PlatformRoutineElement* DequeueRoutineElement(
    const std::vector<PlatformRoutineElement>& routine, int& cursor) {
    if (routine.empty()) return nullptr;
    const int index = ResolveNextRoutineIndex(routine, cursor);
    if (index < 0) return nullptr;
    cursor = (index + 1) % static_cast<int>(routine.size());
    return &routine[static_cast<size_t>(index)];
}

TrainRunInfo ApplyRunInfoChanges(const TrainRunInfo& base,
                                   const std::vector<RunInfoChange>& changes) {
    TrainRunInfo result = base;
    for (const RunInfoChange& change : changes) {
        switch (change.field) {
        case RunInfoChange::Field::Type:
            result.type = change.value;
            break;
        case RunInfoChange::Field::Destination:
            result.destination = change.value;
            break;
        case RunInfoChange::Field::LineName:
            result.lineName = change.value;
            break;
        case RunInfoChange::Field::TrainNumber:
            result.trainNumber = change.value;
            break;
        case RunInfoChange::Field::DutyNumber:
            result.dutyNumber = change.value;
            break;
        }
    }
    return result;
}
}  // namespace rs::world
