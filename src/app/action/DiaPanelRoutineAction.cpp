//
// DiaPanelRoutineAction.cpp - ダイヤの左タブの操作のうち、層・要素・発車時刻・運行情報の書換を扱う
//
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "DiaPanelActionResolve.h"
#include "DiaPanelBranchAction.h"
#include "../workspace/DiaPanelFormat.h"

namespace rs::app::dia_panel_action {
namespace {

using rs::world::PlatformRoutineElement;

template <typename T>
const T* At(const std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? &values[static_cast<std::size_t>(index)] : nullptr;
}

const PlatformRoutineElement* Element(const std::vector<PlatformRoutineElement>& routine, int index) {
    return At(routine, index);
}

// 画面の層の添字(request.layerIndex_ の層)の要素 index を引き直して f に渡す。
template <typename Function>
void WithElement(const Refs& refs, const Request& request, int elementIndex, Function&& function) {
    WithLayer(refs, request, request.layerIndex_,
              [&](PlatformRoutineAction& action, const RoutineLayerRef& ref,
                  const std::vector<PlatformRoutineElement>& routine, const workspace::DiaPanelLayer&) {
                  const PlatformRoutineElement* element = Element(routine, elementIndex);
                  if (element != nullptr) function(action, ref, *element, static_cast<std::size_t>(elementIndex));
              });
}

} // namespace

void AddLayer(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::AddLayer(refs, request);
    const workspace::DiaPanelChoice* choice = At(refs.panel.layerChoices_, request.rowIndex_);
    const std::optional<ResolvedTarget> resolved = ResolveActiveTarget(refs, request);
    if (choice == nullptr || !resolved) return;
    PlatformRoutineAction action(refs.editor, resolved->platform->id, {});
    if (choice->key_ == workspace::kDiaFormationLayerKey) {
        action.AddFormationRoutine(choice->id_);
    } else {
        action.AddTagRoutine(-(choice->key_ + 2));
    }
}

void DeleteLayer(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::DeleteLayer(refs, request);
    const std::optional<ResolvedTarget> resolved = ResolveActiveTarget(refs, request);
    const workspace::DiaPanelLayer* layer = resolved ? At(resolved->target->layers_, request.rowIndex_) : nullptr;
    if (layer == nullptr || !layer->deletable_) return;
    const std::optional<RoutineLayerRef> ref = ResolveLayerRef(*resolved->platform, *layer);
    if (!ref) return;
    PlatformRoutineAction action(refs.editor, resolved->platform->id, {});
    if (ref->kind == RoutineLayerRef::Kind::Formation) action.RemoveFormationRoutine(ref->entryIndex);
    else if (ref->kind == RoutineLayerRef::Kind::Tag) action.RemoveTagRoutine(ref->entryIndex);
}

void ReorderLayer(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::ReorderLayer(refs, request);
    const std::optional<ResolvedTarget> resolved = ResolveActiveTarget(refs, request);
    if (!resolved) return;
    const workspace::DiaPanelLayer* source = At(resolved->target->layers_, request.sourceIndex_);
    const workspace::DiaPanelLayer* destination = At(resolved->target->layers_, request.rowIndex_);
    if (source == nullptr || destination == nullptr || !source->reorderable_ || !destination->reorderable_) return;
    const std::optional<RoutineLayerRef> from = ResolveLayerRef(*resolved->platform, *source);
    const std::optional<RoutineLayerRef> to = ResolveLayerRef(*resolved->platform, *destination);
    if (!from || !to) return;
    PlatformRoutineAction(refs.editor, resolved->platform->id, {}).MoveTagRoutineTo(from->entryIndex, to->entryIndex);
}

void AddElement(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::AddElement(refs, request);
    WithLayer(refs, request, request.layerIndex_,
              [](PlatformRoutineAction& action, const RoutineLayerRef& ref,
                 const std::vector<PlatformRoutineElement>&, const workspace::DiaPanelLayer&) { action.AddElement(ref); });
}

void DeleteElement(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::DeleteElement(refs, request);
    WithElement(refs, request, request.rowIndex_,
                [](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement&, std::size_t index) {
                    action.RemoveElement(ref, index);
                });
}

void DisableElement(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::DisableElement(refs, request);
    WithElement(refs, request, request.rowIndex_,
                [](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    action.SetElementEnabled(ref, index, !element.enabled);
                });
}

void ReorderElement(const Refs& refs, const Request& request) {
    if (branch::IsBranchTarget(refs, request)) return branch::ReorderElement(refs, request);
    if (request.sourceIndex_ < 0 || request.rowIndex_ < 0) return;
    WithLayer(refs, request, request.layerIndex_,
              [&request](PlatformRoutineAction& action, const RoutineLayerRef& ref,
                         const std::vector<PlatformRoutineElement>&, const workspace::DiaPanelLayer&) {
                  action.MoveElementTo(ref, static_cast<std::size_t>(request.sourceIndex_),
                                       static_cast<std::size_t>(request.rowIndex_));
              });
}

void ChangeOperation(const Refs& refs, const Request& request) {
    const std::optional<rs::world::DiaAction> operation = rs::world::DiaActionFromKey(request.value_);
    if (!operation) return;
    WithElement(refs, request, request.rowIndex_,
                [operation](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (element.action != *operation) action.SetElementAction(ref, index, *operation);
                });
}

void ChangeDepartureType(const Refs& refs, const Request& request) {
    if (request.value_ != "seconds" && request.value_ != "time") return;
    const rs::world::DiaTimeType type = request.value_ == "seconds" ? rs::world::DiaTimeType::ElapsedFromArrival
                                                                     : rs::world::DiaTimeType::AbsoluteTimeOfDay;
    WithElement(refs, request, request.rowIndex_,
                [type](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (element.timeType != type) action.SetElementTimeType(ref, index, type);
                });
}

// 停車秒数(field_ が "seconds")か、itemIndex_ 番目の発車時刻を書く。値が同じ・読めないときは何もしない
// (呼び出し側が表示を作り直して、入力欄を元の値へ戻す)。
void ChangeDepartureTime(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [&request](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (request.field_ == "seconds") {
                        double seconds = 0.0;
                        if (workspace::ParseDiaSeconds(request.value_, seconds) && seconds != element.dwellSeconds) {
                            action.SetElementDwellSeconds(ref, index, seconds);
                        }
                        return;
                    }
                    const rs::world::DiaTimeOfDay* current = At(element.departureTimes, request.itemIndex_);
                    rs::world::DiaTimeOfDay time;
                    if (current == nullptr || !workspace::ParseDiaTime(request.value_, time)) return;
                    if (time.hour == current->hour && time.minute == current->minute && time.second == current->second) return;
                    action.SetDepartureTimeSorted(ref, index, static_cast<std::size_t>(request.itemIndex_), time.hour, time.minute, time.second);
                });
}

void AddDepartureTime(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement&, std::size_t index) {
                    action.AddDepartureTimeSorted(ref, index);
                });
}

void DeleteDepartureTime(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [&request](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (At(element.departureTimes, request.itemIndex_) != nullptr) {
                        action.RemoveDepartureTime(ref, index, static_cast<std::size_t>(request.itemIndex_));
                    }
                });
}

void ResetStopPosition(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (element.stopOffsetNormalized.has_value()) action.ResetElementStopOffset(ref, index);
                });
}

void ChangeStopPosition(const Refs& refs, const Request& request) {
    char* end = nullptr;
    const double value = std::strtod(request.value_.c_str(), &end);
    if (end == request.value_.c_str()) return;
    WithElement(refs, request, request.rowIndex_,
                [value](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    // 同じ値への変更は積まない(値の書き戻しでもchangeが届くため)。
                    if (element.stopOffsetNormalized.has_value() && std::abs(*element.stopOffsetNormalized - value) < 1e-6) return;
                    action.SetElementStopOffset(ref, index, value);
                });
}

void ChangeRunInfo(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [&request](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    const rs::world::RunInfoChange* change = At(element.runInfoChanges, request.itemIndex_);
                    if (change != nullptr && change->value != request.value_) {
                        action.SetRunInfoChange(ref, index, static_cast<std::size_t>(request.itemIndex_), request.value_);
                    }
                });
}

void AddRunInfo(const Refs& refs, const Request& request) {
    const workspace::DiaPanelChoice* choice = At(refs.panel.overwriteChoices_, request.itemIndex_);
    if (choice == nullptr) return;
    const rs::world::RunInfoChange::Field field = static_cast<rs::world::RunInfoChange::Field>(choice->key_);
    WithElement(refs, request, request.rowIndex_,
                [field](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement&, std::size_t index) {
                    action.AddRunInfoChange(ref, index, field);
                });
}

void DeleteRunInfo(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [&request](PlatformRoutineAction& action, const RoutineLayerRef& ref, const PlatformRoutineElement& element, std::size_t index) {
                    if (At(element.runInfoChanges, request.itemIndex_) != nullptr) {
                        action.RemoveRunInfoChange(ref, index, static_cast<std::size_t>(request.itemIndex_));
                    }
                });
}

} // namespace rs::app::dia_panel_action
