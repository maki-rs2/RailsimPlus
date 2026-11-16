//
// DiaPanelBranchAction.cpp - ダイヤの左タブの操作のうち、分岐を対象にする分
//
#include "DiaPanelBranchAction.h"

#include <optional>

#include "PointOpeningAction.h"
#include "PointRoutineAction.h"
#include "PointRoutineChange.h"
#include "../workspace/DiaPanelFormat.h"
#include "../workspace/DiaPanelPresentation.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/PointHandLinks.h"

namespace rs::app::dia_panel_action {
namespace {

using namespace point_routine_change;
using rs::world::PointCondition;
using rs::world::PointRoutineElement;

template <typename T>
const T* At(const std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? &values[static_cast<std::size_t>(index)] : nullptr;
}

struct ResolvedBranch {
    const workspace::DiaPanelTarget* target = nullptr;
    const rs::world::TrackNode* node = nullptr;
};

std::optional<ResolvedBranch> ResolveActiveBranch(const Refs& refs, const Request& request) {
    const workspace::DiaPanelTarget* target = At(refs.panel.targets_, request.targetIndex_);
    if (target == nullptr || !target->branch_ || target->sceneIndex_ != refs.panel.activeScene_) return std::nullopt;
    const rs::world::TrackNode* node = FindPoint(refs.editor, target->nodeId_);
    if (node == nullptr) return std::nullopt;
    return ResolvedBranch{target, node};
}

// 表示値の層を、層のキーから分岐の層の参照へ変える(番線の ResolveLayerRef と同じキーの決め方)。
PointLayerRef LayerRefOf(const workspace::DiaPanelLayer& layer) {
    PointLayerRef ref;
    if (layer.routineKey_ == rs::world::kCommonRoutineLayerKey) {
        ref.kind = PointLayerRef::Kind::Common;
    } else if (layer.routineKey_ == workspace::kDiaFormationLayerKey) {
        ref.kind = PointLayerRef::Kind::Formation;
        ref.formationId = layer.formationId_;
    } else {
        ref.kind = PointLayerRef::Kind::Tag;
        ref.tagId = -(layer.routineKey_ + 2);
    }
    return ref;
}

// 実際の項目列。層が無いときは、画面も空のときだけ空の列を返す。画面の数と合わなければ nullptr(画面が古い)。
template <typename Slots>
const std::vector<typename Slots::Item>* LiveItems(const rs::world::TrackNode& node, const PointLayerRef& ref,
                                                   std::size_t expectedSize) {
    static const std::vector<typename Slots::Item> kEmpty;
    const std::vector<typename Slots::Item>* items = &kEmpty;
    if (ref.kind == PointLayerRef::Kind::Common) {
        items = &Slots::Common(node);
    } else if (ref.kind == PointLayerRef::Kind::Formation) {
        const typename Slots::FormationEntry* entry =
            routine_list_edit::FindByKey(Slots::Formations(node), &Slots::FormationEntry::formationId, ref.formationId);
        if (entry != nullptr) items = &Slots::Items(*entry);
    } else {
        const typename Slots::TagEntry* entry =
            routine_list_edit::FindByKey(Slots::Tags(node), &Slots::TagEntry::tagId, ref.tagId);
        if (entry != nullptr) items = &Slots::Items(*entry);
    }
    return items->size() == expectedSize ? items : nullptr;
}

struct ResolvedLayer {
    ResolvedBranch branch;
    const workspace::DiaPanelLayer* layer = nullptr;
    PointLayerRef ref;
};

std::optional<ResolvedLayer> ResolveLayer(const Refs& refs, const Request& request, int layerIndex) {
    const std::optional<ResolvedBranch> branch = ResolveActiveBranch(refs, request);
    const workspace::DiaPanelLayer* layer = branch ? At(branch->target->layers_, layerIndex) : nullptr;
    if (layer == nullptr) return std::nullopt;
    return ResolvedLayer{*branch, layer, LayerRefOf(*layer)};
}

// 要素 index の実際の値を引いて f(action, ref, 要素, index) を呼ぶ。
template <typename Function>
void WithElement(const Refs& refs, const Request& request, int index, Function&& function) {
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.layerIndex_);
    if (!resolved) return;
    const std::vector<PointRoutineElement>* routine = LiveItems<RoutineSlots>(*resolved->branch.node, resolved->ref, resolved->layer->elements_.size());
    const PointRoutineElement* element = routine == nullptr ? nullptr : At(*routine, index);
    if (element == nullptr) return;
    PointRoutineAction action(refs.editor, resolved->branch.target->nodeId_);
    function(action, resolved->ref, *element, static_cast<std::size_t>(index));
}

template <typename Function>
void WithCondition(const Refs& refs, const Request& request, int index, Function&& function) {
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.layerIndex_);
    if (!resolved) return;
    const std::vector<PointCondition>* conditions = LiveItems<ConditionSlots>(*resolved->branch.node, resolved->ref, resolved->layer->conditions_.size());
    const PointCondition* condition = conditions == nullptr ? nullptr : At(*conditions, index);
    if (condition == nullptr) return;
    PointRoutineAction action(refs.editor, resolved->branch.target->nodeId_);
    function(action, resolved->ref, *condition, *resolved->layer, static_cast<std::size_t>(index));
}

} // namespace

namespace branch {

bool IsBranchTarget(const Refs& refs, const Request& request) {
    const workspace::DiaPanelTarget* target = At(refs.panel.targets_, request.targetIndex_);
    return target != nullptr && target->branch_;
}

void AddLayer(const Refs& refs, const Request& request) {
    const workspace::DiaPanelChoice* choice = At(refs.panel.layerChoices_, request.rowIndex_);
    const std::optional<ResolvedBranch> resolved = ResolveActiveBranch(refs, request);
    if (choice == nullptr || !resolved) return;
    PointRoutineAction action(refs.editor, resolved->target->nodeId_);
    if (choice->key_ == workspace::kDiaFormationLayerKey) {
        action.AddFormationLayer(choice->id_);
    } else {
        action.AddTagLayer(-(choice->key_ + 2));
    }
}

void DeleteLayer(const Refs& refs, const Request& request) {
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.rowIndex_);
    if (!resolved || !resolved->layer->deletable_) return;
    PointRoutineAction(refs.editor, resolved->branch.target->nodeId_).RemoveLayer(resolved->ref);
}

void ReorderLayer(const Refs& refs, const Request& request) {
    const std::optional<ResolvedLayer> to = ResolveLayer(refs, request, request.rowIndex_);
    const std::optional<ResolvedLayer> from = ResolveLayer(refs, request, request.sourceIndex_);
    if (!to || !from || !from->layer->reorderable_ || !to->layer->reorderable_) return;
    PointRoutineAction(refs.editor, to->branch.target->nodeId_).MoveTagLayerTo(from->ref.tagId, to->ref.tagId);
}

void AddElement(const Refs& refs, const Request& request) {
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.layerIndex_);
    if (!resolved) return;
    PointRoutineAction(refs.editor, resolved->branch.target->nodeId_).AddElement(resolved->ref);
}

void DeleteElement(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [](PointRoutineAction& action, const PointLayerRef& ref, const PointRoutineElement&, std::size_t index) {
                    action.RemoveElement(ref, index);
                });
}

void DisableElement(const Refs& refs, const Request& request) {
    WithElement(refs, request, request.rowIndex_,
                [](PointRoutineAction& action, const PointLayerRef& ref, const PointRoutineElement& element, std::size_t index) {
                    action.SetElementEnabled(ref, index, !element.enabled);
                });
}

void ReorderElement(const Refs& refs, const Request& request) {
    if (request.sourceIndex_ < 0 || request.rowIndex_ < 0) return;
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.layerIndex_);
    if (!resolved) return;
    PointRoutineAction(refs.editor, resolved->branch.target->nodeId_)
        .MoveElementTo(resolved->ref, static_cast<std::size_t>(request.sourceIndex_), static_cast<std::size_t>(request.rowIndex_));
}

} // namespace branch

void ChangeBranchHand(const Refs& refs, const Request& request) {
    int hand = 0;
    bool random = false;
    if (!workspace::ParsePointHand(request.value_, hand, random)) return;
    WithElement(refs, request, request.rowIndex_,
                [hand, random](PointRoutineAction& action, const PointLayerRef& ref, const PointRoutineElement& element,
                               std::size_t index) {
                    if (random ? !element.random : (element.random || element.hand != hand)) {
                        action.SetElementHand(ref, index, hand, random);
                    }
                });
}

void AddCondition(const Refs& refs, const Request& request) {
    if (request.value_ != "time" && request.value_ != "block") return;
    const PointCondition::Kind kind =
        request.value_ == "time" ? PointCondition::Kind::TimeRange : PointCondition::Kind::BlockOccupancy;
    const std::optional<ResolvedLayer> resolved = ResolveLayer(refs, request, request.layerIndex_);
    if (!resolved) return;
    PointRoutineAction(refs.editor, resolved->branch.target->nodeId_).AddCondition(resolved->ref, kind);
}

void DeleteCondition(const Refs& refs, const Request& request) {
    WithCondition(refs, request, request.rowIndex_,
                  [](PointRoutineAction& action, const PointLayerRef& ref, const PointCondition&,
                     const workspace::DiaPanelLayer&, std::size_t index) { action.RemoveCondition(ref, index); });
}

void DisableCondition(const Refs& refs, const Request& request) {
    WithCondition(refs, request, request.rowIndex_,
                  [](PointRoutineAction& action, const PointLayerRef& ref, const PointCondition& condition,
                     const workspace::DiaPanelLayer&, std::size_t index) {
                      action.SetConditionEnabled(ref, index, !condition.enabled);
                  });
}

// field_: start / end(時刻)、block(閉塞のチェック)、match(all / any)、hand(左 / 右)。
// 値が同じ・読めないときは何もしない(呼び出し側が表示を作り直して、入力欄を元の値へ戻す)。
void EditCondition(const Refs& refs, const Request& request) {
    WithCondition(refs, request, request.rowIndex_,
                  [&request](PointRoutineAction& action, const PointLayerRef& ref, const PointCondition& condition,
                             const workspace::DiaPanelLayer& layer, std::size_t index) {
                      const std::string& field = request.field_;
                      if (field == "start" || field == "end") {
                          rs::world::DiaTimeOfDay time;
                          if (condition.kind != PointCondition::Kind::TimeRange || !workspace::ParseDiaTime(request.value_, time)) return;
                          const rs::world::DiaTimeOfDay& current = field == "start" ? condition.startTime : condition.endTime;
                          if (time.hour == current.hour && time.minute == current.minute && time.second == current.second) return;
                          action.SetConditionTime(ref, index, field == "start", time);
                      } else if (field == "block") {
                          const workspace::DiaPanelCondition* shown = At(layer.conditions_, static_cast<int>(index));
                          const workspace::DiaPanelChoice* choice = shown == nullptr ? nullptr : At(shown->blocks_, request.itemIndex_);
                          if (choice != nullptr) action.SetConditionBlock(ref, index, choice->id_, !choice->selected_);
                      } else if (field == "match") {
                          if (request.value_ != "all" && request.value_ != "any") return;
                          const bool all = request.value_ == "all";
                          if (condition.requireAllOccupied != all) action.SetConditionRequireAll(ref, index, all);
                      } else if (field == "hand") {
                          int hand = 0;
                          bool random = false;
                          if (workspace::ParsePointHand(request.value_, hand, random) && !random && condition.hand != hand) {
                              action.SetConditionHand(ref, index, hand);
                          }
                      }
                  });
}

// request.rowIndex_ は開通先の行の番号、value_ は 左 / 右。
// 在線中の分岐は切り替えない(判定は ChangePointOpening)。
void ChangeOpening(const Refs& refs, const Request& request) {
    const std::optional<ResolvedBranch> resolved = ResolveActiveBranch(refs, request);
    if (!resolved || (request.value_ != "左" && request.value_ != "右")) return;
    const std::vector<rs::world::PointSideLinks> sides = rs::world::BuildPointSideLinks(refs.editor.graph, *resolved->node);
    const rs::world::PointSideLinks* side = At(sides, request.rowIndex_);
    // 画面の行の数と合わなければ画面が古い。
    if (side == nullptr || sides.size() != resolved->target->openings_.size()) return;
    ChangePointOpening(refs.queue, refs.editor, resolved->target->nodeId_,
                       request.value_ == "左" ? side->leftLinkIndex : side->rightLinkIndex, refs.OccupiedBlocks());
}

} // namespace rs::app::dia_panel_action
