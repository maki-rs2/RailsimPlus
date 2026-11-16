//
// PlatformRoutineChange.h - PlatformRoutineAction の実装ファイル間で共有する、番線の層を書き換える補助関数
//
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "RoutineListEdit.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/PlatformCommand.h"

namespace rs::app::routine_change {

inline void ExecuteRoutineChange(rs::editor::Editor& editor, const rs::world::Guid& platformId, std::vector<rs::world::PlatformRoutineElement> routine) {
    rs::editor::PlatformPropertyDelta delta;
    delta.routine = std::move(routine);
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

inline void ExecuteTrainRoutinesChange(rs::editor::Editor& editor, const rs::world::Guid& platformId, std::vector<rs::world::PlatformTrainRoutine> trainRoutines) {
    rs::editor::PlatformPropertyDelta delta;
    delta.trainRoutines = std::move(trainRoutines);
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

inline void ExecuteTagRoutinesChange(rs::editor::Editor& editor, const rs::world::Guid& platformId, std::vector<rs::world::PlatformTagRoutine> tagRoutines) {
    rs::editor::PlatformPropertyDelta delta;
    delta.tagRoutines = std::move(tagRoutines);
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

inline void ExecuteFormationRoutinesChange(rs::editor::Editor& editor, const rs::world::Guid& platformId, std::vector<rs::world::PlatformFormationRoutine> formationRoutines) {
    rs::editor::PlatformPropertyDelta delta;
    delta.formationRoutines = std::move(formationRoutines);
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

inline std::size_t TrainOriginalIndex(const rs::world::PlatformStopInfo& platform, std::size_t sortedIndex) {
    std::vector<std::size_t> indices(platform.trainRoutines.size());
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = index;
    }
    std::sort(indices.begin(), indices.end(), [&platform](std::size_t left, std::size_t right) {
        return platform.trainRoutines[left].trainId < platform.trainRoutines[right].trainId;
    });
    return sortedIndex < indices.size() ? indices[sortedIndex] : indices.size();
}

} // namespace rs::app::routine_change
