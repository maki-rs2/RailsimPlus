//
// PlatformRoutineLayerAction.cpp - 番線ルーチン表のアプリサービスのうち、層(編成・列車・タグ)の追加・削除・並べ替え
//
#include "PlatformRoutineAction.h"

#include <algorithm>
#include <utility>

#include "PlatformRoutineChange.h"
#include "../../world/alignment/PlatformRegistry.h"

namespace rs::app {

using namespace routine_change;

bool PlatformRoutineAction::AddTrainRoutine(TrainId trainId) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return false;
    }
    const bool exists = std::any_of(platform->trainRoutines.begin(), platform->trainRoutines.end(),
                                    [trainId](const rs::world::PlatformTrainRoutine& entry) {
                                        return entry.trainId == trainId;
                                    });
    if (exists) {
        return false;
    }
    std::vector<rs::world::PlatformTrainRoutine> updated = platform->trainRoutines;
    updated.push_back({trainId, {}});
    ExecuteTrainRoutinesChange(editor_, platformId_, std::move(updated));
    return true;
}

void PlatformRoutineAction::RemoveTrainRoutine(std::size_t entryIndex) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return;
    }
    const std::size_t originalIndex = TrainOriginalIndex(*platform, entryIndex);
    if (originalIndex >= platform->trainRoutines.size()) {
        return;
    }
    std::vector<rs::world::PlatformTrainRoutine> updated = platform->trainRoutines;
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(originalIndex));
    ExecuteTrainRoutinesChange(editor_, platformId_, std::move(updated));
}

bool PlatformRoutineAction::AddTagRoutine(int tagId) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return false;
    }
    const bool exists = std::any_of(platform->tagRoutines.begin(), platform->tagRoutines.end(),
                                    [tagId](const rs::world::PlatformTagRoutine& entry) {
                                        return entry.tagId == tagId;
                                    });
    if (exists) {
        return false;
    }
    std::vector<rs::world::PlatformTagRoutine> updated = platform->tagRoutines;
    // 追加した層は、既定値の要素を1つ持った状態で始める(空の層にしない)。
    updated.push_back({tagId, std::vector<rs::world::PlatformRoutineElement>(1)});
    ExecuteTagRoutinesChange(editor_, platformId_, std::move(updated));
    return true;
}

void PlatformRoutineAction::RemoveTagRoutine(std::size_t entryIndex) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr || entryIndex >= platform->tagRoutines.size()) {
        return;
    }
    std::vector<rs::world::PlatformTagRoutine> updated = platform->tagRoutines;
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(entryIndex));
    ExecuteTagRoutinesChange(editor_, platformId_, std::move(updated));
}

void PlatformRoutineAction::MoveTagRoutine(std::size_t entryIndex, int delta) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr || entryIndex >= platform->tagRoutines.size()) {
        return;
    }
    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(entryIndex) + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(platform->tagRoutines.size())) {
        return;
    }
    std::vector<rs::world::PlatformTagRoutine> updated = platform->tagRoutines;
    std::swap(updated[entryIndex], updated[static_cast<std::size_t>(target)]);
    ExecuteTagRoutinesChange(editor_, platformId_, std::move(updated));
}

void PlatformRoutineAction::MoveTagRoutineTo(std::size_t fromIndex, std::size_t toIndex) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return;
    }
    std::vector<rs::world::PlatformTagRoutine> updated = platform->tagRoutines;
    if (MoveWithin(updated, fromIndex, toIndex)) {
        ExecuteTagRoutinesChange(editor_, platformId_, std::move(updated));
    }
}

bool PlatformRoutineAction::AddFormationRoutine(const rs::world::Guid& formationId) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return false;
    }
    const bool exists = std::any_of(platform->formationRoutines.begin(), platform->formationRoutines.end(),
                                    [&formationId](const rs::world::PlatformFormationRoutine& entry) {
                                        return entry.formationId == formationId;
                                    });
    if (exists) {
        return false;
    }
    std::vector<rs::world::PlatformFormationRoutine> updated = platform->formationRoutines;
    updated.push_back({formationId, std::vector<rs::world::PlatformRoutineElement>(1)});
    ExecuteFormationRoutinesChange(editor_, platformId_, std::move(updated));
    return true;
}

void PlatformRoutineAction::RemoveFormationRoutine(std::size_t entryIndex) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr || entryIndex >= platform->formationRoutines.size()) {
        return;
    }
    std::vector<rs::world::PlatformFormationRoutine> updated = platform->formationRoutines;
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(entryIndex));
    ExecuteFormationRoutinesChange(editor_, platformId_, std::move(updated));
}

} // namespace rs::app
