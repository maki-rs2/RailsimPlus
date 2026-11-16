//
// PlatformRoutineAction.cpp - 番線ルーチン表のアプリサービス
//
#include "PlatformRoutineAction.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "PlatformRoutineChange.h"
#include "../../editor/tool/PlatformCommand.h"
#include "../../world/alignment/PlatformRegistry.h"

namespace rs::app {

using namespace routine_change;

namespace {

bool TimeOfDayLess(const rs::world::DiaTimeOfDay& left, const rs::world::DiaTimeOfDay& right) {
    if (left.hour != right.hour) return left.hour < right.hour;
    if (left.minute != right.minute) return left.minute < right.minute;
    return left.second < right.second;
}

template <typename Change>
void ChangeRoutine(rs::editor::Editor& editor, const rs::world::Guid& platformId, const RoutineLayerRef& layer, Change change) {
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor.graph, platformId);
    if (platform == nullptr) {
        return;
    }
    if (layer.kind == RoutineLayerRef::Kind::Common) {
        std::vector<rs::world::PlatformRoutineElement> updated = platform->routine;
        change(updated);
        ExecuteRoutineChange(editor, platformId, std::move(updated));
        return;
    }
    if (layer.kind == RoutineLayerRef::Kind::Train) {
        const std::size_t originalIndex = TrainOriginalIndex(*platform, layer.entryIndex);
        if (originalIndex >= platform->trainRoutines.size()) {
            return;
        }
        std::vector<rs::world::PlatformTrainRoutine> updated = platform->trainRoutines;
        change(updated[originalIndex].routine);
        ExecuteTrainRoutinesChange(editor, platformId, std::move(updated));
        return;
    }
    if (layer.kind == RoutineLayerRef::Kind::Formation) {
        if (layer.entryIndex >= platform->formationRoutines.size()) {
            return;
        }
        std::vector<rs::world::PlatformFormationRoutine> updated = platform->formationRoutines;
        change(updated[layer.entryIndex].routine);
        ExecuteFormationRoutinesChange(editor, platformId, std::move(updated));
        return;
    }
    if (layer.entryIndex >= platform->tagRoutines.size()) {
        return;
    }
    std::vector<rs::world::PlatformTagRoutine> updated = platform->tagRoutines;
    change(updated[layer.entryIndex].routine);
    ExecuteTagRoutinesChange(editor, platformId, std::move(updated));
}

} // namespace
PlatformRoutineAction::PlatformRoutineAction(rs::editor::Editor& editor, rs::world::Guid platformId,
                                             PlatformRoutineEnvironment environment)
    : editor_(editor), platformId_(platformId), environment_(std::move(environment)) {}

void PlatformRoutineAction::SetElementEnabled(const RoutineLayerRef& layer, std::size_t elementIndex,
                                              bool enabled) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, enabled](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].enabled = enabled;
                      }
                  });
}

void PlatformRoutineAction::SetElementAction(const RoutineLayerRef& layer, std::size_t elementIndex,
                                             rs::world::DiaAction action) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, action](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].action = action;
                      }
                  });
}

void PlatformRoutineAction::SetElementTimeType(const RoutineLayerRef& layer, std::size_t elementIndex,
                                               rs::world::DiaTimeType timeType) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, timeType](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].timeType = timeType;
                      }
                  });
}

void PlatformRoutineAction::SetElementDwellSeconds(const RoutineLayerRef& layer,
                                                   std::size_t elementIndex, double dwellSeconds) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, dwellSeconds](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].dwellSeconds = dwellSeconds;
                      }
                  });
}

void PlatformRoutineAction::AddDepartureTime(const RoutineLayerRef& layer, std::size_t elementIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].departureTimes.push_back({});
                      }
                  });
}

void PlatformRoutineAction::RemoveDepartureTime(const RoutineLayerRef& layer,
                                                std::size_t elementIndex, std::size_t timeIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, timeIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::DiaTimeOfDay>& times = routine[elementIndex].departureTimes;
                      if (timeIndex >= times.size()) {
                          return;
                      }
                      times.erase(times.begin() + static_cast<std::ptrdiff_t>(timeIndex));
                  });
}

void PlatformRoutineAction::SetDepartureTime(const RoutineLayerRef& layer, std::size_t elementIndex,
                                             std::size_t timeIndex, int hour, int minute,
                                             int second) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, timeIndex, hour, minute,
                   second](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::DiaTimeOfDay>& times = routine[elementIndex].departureTimes;
                      if (timeIndex >= times.size()) {
                          return;
                      }
                      rs::world::DiaTimeOfDay& time = times[timeIndex];
                      time.hour = std::max(0, hour);
                      time.minute = std::clamp(minute, 0, 59);
                      time.second = std::clamp(second, 0, 59);
                  });
}

void PlatformRoutineAction::SetDepartureTimeSorted(const RoutineLayerRef& layer, std::size_t elementIndex,
                                                   std::size_t timeIndex, int hour, int minute, int second) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, timeIndex, hour, minute,
                   second](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::DiaTimeOfDay>& times = routine[elementIndex].departureTimes;
                      if (timeIndex >= times.size()) {
                          return;
                      }
                      times[timeIndex] = {std::max(0, hour), std::clamp(minute, 0, 59), std::clamp(second, 0, 59)};
                      std::stable_sort(times.begin(), times.end(), TimeOfDayLess);
                  });
}

void PlatformRoutineAction::AddDepartureTimeSorted(const RoutineLayerRef& layer, std::size_t elementIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::DiaTimeOfDay>& times = routine[elementIndex].departureTimes;
                      times.push_back({});
                      std::stable_sort(times.begin(), times.end(), TimeOfDayLess);
                  });
}

void PlatformRoutineAction::ResetElementStopOffset(const RoutineLayerRef& layer, std::size_t elementIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].stopOffsetNormalized.reset();
                      }
                  });
}

void PlatformRoutineAction::AddRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex,
                                             rs::world::RunInfoChange::Field field) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, field](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::RunInfoChange>& changes = routine[elementIndex].runInfoChanges;
                      const bool exists = std::any_of(changes.begin(), changes.end(),
                                                      [field](const rs::world::RunInfoChange& change) {
                                                          return change.field == field;
                                                      });
                      if (!exists) {
                          changes.push_back({field, {}});
                      }
                  });
}

void PlatformRoutineAction::SetRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex,
                                             std::size_t changeIndex, std::string value) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, changeIndex, value = std::move(value)](
                      std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size() || changeIndex >= routine[elementIndex].runInfoChanges.size()) {
                          return;
                      }
                      routine[elementIndex].runInfoChanges[changeIndex].value = value;
                  });
}

void PlatformRoutineAction::RemoveRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex,
                                                std::size_t changeIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, changeIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size() || changeIndex >= routine[elementIndex].runInfoChanges.size()) {
                          return;
                      }
                      std::vector<rs::world::RunInfoChange>& changes = routine[elementIndex].runInfoChanges;
                      changes.erase(changes.begin() + static_cast<std::ptrdiff_t>(changeIndex));
                  });
}

void PlatformRoutineAction::AddSwitchChange(const RoutineLayerRef& layer, std::size_t elementIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine[elementIndex].switchChanges.push_back({});
                      }
                  });
}

void PlatformRoutineAction::RemoveSwitchChange(const RoutineLayerRef& layer,
                                               std::size_t elementIndex, std::size_t switchIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, switchIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::SwitchChange>& changes = routine[elementIndex].switchChanges;
                      if (switchIndex >= changes.size()) {
                          return;
                      }
                      changes.erase(changes.begin() + static_cast<std::ptrdiff_t>(switchIndex));
                  });
}

void PlatformRoutineAction::SetSwitchChange(const RoutineLayerRef& layer, std::size_t elementIndex,
                                            std::size_t switchIndex, std::string switchName,
                                            int value) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, switchIndex, switchName = std::move(switchName),
                   value](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex >= routine.size()) {
                          return;
                      }
                      std::vector<rs::world::SwitchChange>& changes = routine[elementIndex].switchChanges;
                      if (switchIndex >= changes.size()) {
                          return;
                      }
                      changes[switchIndex].switchName = switchName;
                      changes[switchIndex].value = value;
                  });
}

void PlatformRoutineAction::AddElement(const RoutineLayerRef& layer) {
    ChangeRoutine(editor_, platformId_, layer,
                  [](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      routine.push_back({});
                  });
}

void PlatformRoutineAction::RemoveElement(const RoutineLayerRef& layer, std::size_t elementIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      if (elementIndex < routine.size()) {
                          routine.erase(routine.begin() + static_cast<std::ptrdiff_t>(elementIndex));
                      }
                  });
}

void PlatformRoutineAction::MoveElement(const RoutineLayerRef& layer, std::size_t elementIndex,
                                        int delta) {
    ChangeRoutine(editor_, platformId_, layer,
                  [elementIndex, delta](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(elementIndex) + delta;
                      if (elementIndex >= routine.size() || target < 0 ||
                          target >= static_cast<std::ptrdiff_t>(routine.size())) {
                          return;
                      }
                      std::swap(routine[elementIndex], routine[static_cast<std::size_t>(target)]);
                  });
}

void PlatformRoutineAction::MoveElementTo(const RoutineLayerRef& layer, std::size_t fromIndex,
                                          std::size_t toIndex) {
    ChangeRoutine(editor_, platformId_, layer,
                  [fromIndex, toIndex](std::vector<rs::world::PlatformRoutineElement>& routine) {
                      MoveWithin(routine, fromIndex, toIndex);
                  });
}

} // namespace rs::app
