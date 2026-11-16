//
// PointRoutineAction.cpp - 分岐のルーチン表のアプリサービスのうち、要素・条件・所属駅の編集
//
#include "PointRoutineAction.h"

#include <algorithm>
#include <utility>

#include "PointRoutineChange.h"

namespace rs::app {

using namespace point_routine_change;
using namespace routine_list_edit;
using rs::world::PointCondition;
using rs::world::PointRoutineElement;

namespace {

template <typename Change>
void ChangeElements(rs::editor::Editor& editor, const rs::world::Guid& nodeId, const PointLayerRef& layer,
                    bool createMissing, Change&& change) {
    ChangeLayerItems<RoutineSlots>(editor, nodeId, layer, createMissing, std::forward<Change>(change));
}

template <typename Change>
void ChangeConditions(rs::editor::Editor& editor, const rs::world::Guid& nodeId, const PointLayerRef& layer,
                      bool createMissing, Change&& change) {
    ChangeLayerItems<ConditionSlots>(editor, nodeId, layer, createMissing, std::forward<Change>(change));
}

// 範囲内のときだけ function(items[index]) を呼ぶ。
template <typename Item, typename Function>
void WithItem(std::vector<Item>& items, std::size_t index, Function&& function) {
    if (index < items.size()) function(items[index]);
}

PointCondition MakeDefaultCondition(PointCondition::Kind kind) {
    PointCondition condition;
    condition.kind = kind;
    if (kind == PointCondition::Kind::TimeRange) {
        condition.startTime = {7, 0, 0};
        condition.endTime = {9, 0, 0};
    }
    return condition;
}

} // namespace

PointRoutineAction::PointRoutineAction(rs::editor::Editor& editor, rs::world::Guid nodeId)
    : editor_(editor), nodeId_(nodeId) {}

void PointRoutineAction::AddElement(const PointLayerRef& layer) {
    ChangeElements(editor_, nodeId_, layer, true, [](std::vector<PointRoutineElement>& routine) { routine.push_back({}); });
}

void PointRoutineAction::RemoveElement(const PointLayerRef& layer, std::size_t elementIndex) {
    ChangeElements(editor_, nodeId_, layer, false,
                   [elementIndex](std::vector<PointRoutineElement>& routine) { EraseAt(routine, elementIndex); });
}

void PointRoutineAction::SetElementEnabled(const PointLayerRef& layer, std::size_t elementIndex, bool enabled) {
    ChangeElements(editor_, nodeId_, layer, false, [elementIndex, enabled](std::vector<PointRoutineElement>& routine) {
        WithItem(routine, elementIndex, [enabled](PointRoutineElement& element) { element.enabled = enabled; });
    });
}

void PointRoutineAction::MoveElementTo(const PointLayerRef& layer, std::size_t fromIndex, std::size_t toIndex) {
    ChangeElements(editor_, nodeId_, layer, false, [fromIndex, toIndex](std::vector<PointRoutineElement>& routine) {
        MoveWithin(routine, fromIndex, toIndex);
    });
}

void PointRoutineAction::SetElementHand(const PointLayerRef& layer, std::size_t elementIndex, int hand, bool random) {
    ChangeElements(editor_, nodeId_, layer, false, [elementIndex, hand, random](std::vector<PointRoutineElement>& routine) {
        WithItem(routine, elementIndex, [hand, random](PointRoutineElement& element) {
            element.hand = hand;
            element.random = random;
        });
    });
}

void PointRoutineAction::AddCondition(const PointLayerRef& layer, PointCondition::Kind kind) {
    ChangeConditions(editor_, nodeId_, layer, true, [kind](std::vector<PointCondition>& conditions) {
        conditions.push_back(MakeDefaultCondition(kind));
    });
}

void PointRoutineAction::RemoveCondition(const PointLayerRef& layer, std::size_t conditionIndex) {
    ChangeConditions(editor_, nodeId_, layer, false,
                     [conditionIndex](std::vector<PointCondition>& conditions) { EraseAt(conditions, conditionIndex); });
}

void PointRoutineAction::SetConditionEnabled(const PointLayerRef& layer, std::size_t conditionIndex, bool enabled) {
    ChangeConditions(editor_, nodeId_, layer, false, [conditionIndex, enabled](std::vector<PointCondition>& conditions) {
        WithItem(conditions, conditionIndex, [enabled](PointCondition& condition) { condition.enabled = enabled; });
    });
}

void PointRoutineAction::SetConditionTime(const PointLayerRef& layer, std::size_t conditionIndex, bool isStart,
                                          const rs::world::DiaTimeOfDay& time) {
    ChangeConditions(editor_, nodeId_, layer, false, [conditionIndex, isStart, time](std::vector<PointCondition>& conditions) {
        WithItem(conditions, conditionIndex, [isStart, &time](PointCondition& condition) {
            (isStart ? condition.startTime : condition.endTime) = time;
        });
    });
}

void PointRoutineAction::SetConditionBlock(const PointLayerRef& layer, std::size_t conditionIndex,
                                           const rs::world::Guid& block, bool selected) {
    ChangeConditions(editor_, nodeId_, layer, false, [conditionIndex, block, selected](std::vector<PointCondition>& conditions) {
        WithItem(conditions, conditionIndex, [&block, selected](PointCondition& condition) {
            std::vector<rs::world::Guid>& blocks = condition.blocks;
            const auto found = std::find(blocks.begin(), blocks.end(), block);
            if (selected && found == blocks.end()) {
                blocks.push_back(block);
            } else if (!selected && found != blocks.end()) {
                blocks.erase(found);
            }
        });
    });
}

void PointRoutineAction::SetConditionRequireAll(const PointLayerRef& layer, std::size_t conditionIndex, bool requireAll) {
    ChangeConditions(editor_, nodeId_, layer, false, [conditionIndex, requireAll](std::vector<PointCondition>& conditions) {
        WithItem(conditions, conditionIndex, [requireAll](PointCondition& condition) { condition.requireAllOccupied = requireAll; });
    });
}

void PointRoutineAction::SetConditionHand(const PointLayerRef& layer, std::size_t conditionIndex, int hand) {
    ChangeConditions(editor_, nodeId_, layer, false, [conditionIndex, hand](std::vector<PointCondition>& conditions) {
        WithItem(conditions, conditionIndex, [hand](PointCondition& condition) { condition.hand = hand; });
    });
}

void PointRoutineAction::SetName(std::string name) {
    if (name.empty() || FindPoint(editor_, nodeId_) == nullptr) return;
    rs::editor::PointDiaDelta delta;
    delta.name = std::move(name);
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
}

void PointRoutineAction::SetStation(const rs::world::Guid& stationId) {
    if (FindPoint(editor_, nodeId_) == nullptr) return;
    rs::editor::PointDiaDelta delta;
    delta.stationId = stationId;
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
}

} // namespace rs::app
