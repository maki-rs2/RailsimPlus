#include "PointRoutine.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "PointRoutineResolve.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

int NormalizeCursor(int cursor, int count) {
    return ((cursor % count) + count) % count;
}

int FindEnabledRoutineIndex(const std::vector<PointRoutineElement>& routine, int cursor) {
    const int count = static_cast<int>(routine.size());
    const int start = NormalizeCursor(cursor, count);
    for (int offset = 0; offset < count; ++offset) {
        const int index = (start + offset) % count;
        if (routine[static_cast<size_t>(index)].enabled) return index;
    }
    return -1;
}

bool HasEnabledRoutine(const std::vector<PointRoutineElement>& routine) {
    return std::any_of(routine.begin(), routine.end(),
                       [](const PointRoutineElement& element) { return element.enabled; });
}

bool HasEnabledCondition(const std::vector<PointCondition>& conditions) {
    return std::any_of(conditions.begin(), conditions.end(),
                       [](const PointCondition& condition) { return condition.enabled; });
}

// PointConditionのBlockOccupancy判定(requireAllOccupied=all/any-of)。
bool BlockOccupancyConditionMatches(const PointCondition& condition,
                                    const std::unordered_set<Guid, GuidHash>& previousOccupiedBlocks) {
    if (condition.blocks.empty()) return false;
    if (condition.requireAllOccupied) {
        return std::all_of(condition.blocks.begin(), condition.blocks.end(),
                           [&previousOccupiedBlocks](const Guid& block) {
                               return previousOccupiedBlocks.contains(block);
                           });
    }
    return std::any_of(condition.blocks.begin(), condition.blocks.end(),
                       [&previousOccupiedBlocks](const Guid& block) {
                           return previousOccupiedBlocks.contains(block);
                       });
}

}  // namespace

ResolvedPointRoutine ResolvePointTrainRoutine(
    const std::vector<PointTrainRoutine>& trainRoutines,
    const std::vector<PointTagRoutine>& tagRoutines,
    const std::vector<PointRoutineElement>& commonRoutine, TrainId trainId,
    const std::vector<int>& trainTagIds) {
    static const std::vector<PointFormationRoutine> kNoFormationRoutines;
    return ResolvePointTrainRoutine(trainRoutines, kNoFormationRoutines, tagRoutines,
                                    commonRoutine, trainId, NilGuid(), trainTagIds);
}

ResolvedPointRoutine ResolvePointTrainRoutine(
    const std::vector<PointTrainRoutine>& trainRoutines,
    const std::vector<PointFormationRoutine>& formationRoutines,
    const std::vector<PointTagRoutine>& tagRoutines,
    const std::vector<PointRoutineElement>& commonRoutine, TrainId trainId,
    const Guid& formationId, const std::vector<int>& trainTagIds) {
    if (!GuidIsNil(trainId)) {
        for (const PointTrainRoutine& trainRoutine : trainRoutines) {
            if (trainRoutine.trainId == trainId && HasEnabledRoutine(trainRoutine.routine)) {
                return {&trainRoutine.routine, kCommonRoutineLayerKey, trainId};
            }
        }
    }
    if (!GuidIsNil(formationId)) {
        for (const PointFormationRoutine& formationRoutine : formationRoutines) {
            if (formationRoutine.formationId == formationId &&
                HasEnabledRoutine(formationRoutine.routine)) {
                return {&formationRoutine.routine, kCommonRoutineLayerKey, trainId};
            }
        }
    }
    for (const PointTagRoutine& tagRoutine : tagRoutines) {
        const bool hasTag = std::find(trainTagIds.begin(), trainTagIds.end(), tagRoutine.tagId) != trainTagIds.end();
        if (hasTag && HasEnabledRoutine(tagRoutine.routine)) {
            return {&tagRoutine.routine, TagRoutineLayerKey(tagRoutine.tagId)};
        }
    }
    return {&commonRoutine, kCommonRoutineLayerKey};
}

const std::vector<PointCondition>* ResolvePointConditionLayer(
    const std::vector<PointTrainCondition>& trainConditions,
    const std::vector<PointTagCondition>& tagConditions,
    const std::vector<PointCondition>& commonConditions,
    TrainId trainId, const std::vector<int>& trainTagIds) {
    static const std::vector<PointFormationCondition> kNoFormationConditions;
    return ResolvePointConditionLayer(trainConditions, kNoFormationConditions, tagConditions,
                                      commonConditions, trainId, NilGuid(), trainTagIds);
}

const std::vector<PointCondition>* ResolvePointConditionLayer(
    const std::vector<PointTrainCondition>& trainConditions,
    const std::vector<PointFormationCondition>& formationConditions,
    const std::vector<PointTagCondition>& tagConditions,
    const std::vector<PointCondition>& commonConditions,
    TrainId trainId, const Guid& formationId, const std::vector<int>& trainTagIds) {
    for (const PointTrainCondition& item : trainConditions) {
        if (item.trainId == trainId && HasEnabledCondition(item.conditions)) return &item.conditions;
    }
    if (!GuidIsNil(formationId)) {
        for (const PointFormationCondition& item : formationConditions) {
            if (item.formationId == formationId && HasEnabledCondition(item.conditions)) {
                return &item.conditions;
            }
        }
    }
    for (const PointTagCondition& item : tagConditions) {
        if (std::find(trainTagIds.begin(), trainTagIds.end(), item.tagId) != trainTagIds.end()) {
            if (HasEnabledCondition(item.conditions)) return &item.conditions;
        }
    }
    return &commonConditions;
}

bool TimeOfDayInRange(double nowAbsSeconds, const DiaTimeOfDay& start, const DiaTimeOfDay& end) {
    constexpr double kSecondsPerDay = 86400.0;
    double now = std::fmod(nowAbsSeconds, kSecondsPerDay);
    if (now < 0.0) now += kSecondsPerDay;
    const int begin = start.hour * 3600 + start.minute * 60 + start.second;
    const int finish = end.hour * 3600 + end.minute * 60 + end.second;
    if (begin <= finish) return now >= begin && now < finish;
    return now >= begin || now < finish;  // 日跨ぎ
}

std::optional<int> ResolvePointConditionHand(
    const TrackNode& node, TrainId trainId, const std::vector<int>& trainTagIds,
    const std::unordered_set<Guid, GuidHash>& previousOccupiedBlocks, double nowAbsSeconds) {
    return ResolvePointConditionHand(node, trainId, NilGuid(), trainTagIds,
                                     previousOccupiedBlocks, nowAbsSeconds);
}

std::optional<int> ResolvePointConditionHand(
    const TrackNode& node, TrainId trainId, const Guid& formationId,
    const std::vector<int>& trainTagIds,
    const std::unordered_set<Guid, GuidHash>& previousOccupiedBlocks, double nowAbsSeconds) {
    const std::vector<PointCondition>* conditions = ResolvePointConditionLayer(
        node.pointTrainConditions, node.pointFormationConditions, node.pointTagConditions,
        node.pointConditions, trainId, formationId, trainTagIds);
    for (const PointCondition& condition : *conditions) {
        if (!condition.enabled) continue;
        const bool matches = condition.kind == PointCondition::Kind::TimeRange
            ? TimeOfDayInRange(nowAbsSeconds, condition.startTime, condition.endTime)
            : BlockOccupancyConditionMatches(condition, previousOccupiedBlocks);
        if (matches) return condition.hand;
    }
    return std::nullopt;
}

const PointRoutineElement* PeekPointRoutineElement(
    const std::vector<PointRoutineElement>& routine, int cursor) {
    if (routine.empty()) return nullptr;
    const int index = FindEnabledRoutineIndex(routine, cursor);
    return index < 0 ? nullptr : &routine[static_cast<size_t>(index)];
}

const PointRoutineElement* DequeuePointRoutineElement(
    const std::vector<PointRoutineElement>& routine, int& cursor) {
    if (routine.empty()) return nullptr;
    const int index = FindEnabledRoutineIndex(routine, cursor);
    if (index < 0) return nullptr;
    cursor = (index + 1) % static_cast<int>(routine.size());
    return &routine[static_cast<size_t>(index)];
}

}  // namespace rs::world
