#include "DiaPanelAction.h"

#include <algorithm>

#include "DiaPanelActionResolve.h"
#include "NotImplementedNotice.h"
#include "PointRoutineAction.h"
#include "StationDiaAction.h"
#include "ToastQueue.h"

namespace rs::app::dia_panel_action {
namespace {

template <typename T>
const T* At(const std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? &values[static_cast<std::size_t>(index)] : nullptr;
}

// 行を返す。アクティブでないシーンの行は操作しないので返さない。
const workspace::DiaPanelListRow* ActiveRow(const Refs& refs, int index, const char* kind) {
    const workspace::DiaPanelListRow* row = At(refs.panel.rows_, index);
    if (row == nullptr || row->kind_ != kind || row->sceneIndex_ != refs.panel.activeScene_) return nullptr;
    return row;
}

bool StationExists(const rs::world::TrackGraph& graph, const rs::world::Guid& id) {
    return std::any_of(graph.stations.begin(), graph.stations.end(),
                       [&id](const rs::world::StationInfo& station) { return station.id == id; });
}

std::string Trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

} // namespace

std::optional<ResolvedTarget> ResolveActiveTarget(const Refs& refs, const Request& request) {
    const workspace::DiaPanelTarget* target = At(refs.panel.targets_, request.targetIndex_);
    if (target == nullptr || target->branch_ || target->sceneIndex_ != refs.panel.activeScene_) return std::nullopt;
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(refs.editor.graph, target->platformId_);
    if (platform == nullptr) return std::nullopt;
    return ResolvedTarget{target, platform};
}

std::optional<RoutineLayerRef> ResolveLayerRef(const rs::world::PlatformStopInfo& platform,
                                               const workspace::DiaPanelLayer& layer) {
    if (layer.routineKey_ == rs::world::kCommonRoutineLayerKey) return RoutineLayerRef{RoutineLayerRef::Kind::Common, 0};
    if (layer.routineKey_ == workspace::kDiaFormationLayerKey) {
        for (std::size_t index = 0; index < platform.formationRoutines.size(); ++index) {
            if (platform.formationRoutines[index].formationId == layer.formationId_) {
                return RoutineLayerRef{RoutineLayerRef::Kind::Formation, index};
            }
        }
        return std::nullopt;
    }
    for (std::size_t index = 0; index < platform.tagRoutines.size(); ++index) {
        if (rs::world::TagRoutineLayerKey(platform.tagRoutines[index].tagId) == layer.routineKey_) {
            return RoutineLayerRef{RoutineLayerRef::Kind::Tag, index};
        }
    }
    return std::nullopt;
}

const std::vector<rs::world::PlatformRoutineElement>* LiveRoutine(const rs::world::PlatformStopInfo& platform,
                                                                  const RoutineLayerRef& ref,
                                                                  const workspace::DiaPanelLayer& layer) {
    const std::vector<rs::world::PlatformRoutineElement>* routine = &platform.routine;
    if (ref.kind == RoutineLayerRef::Kind::Formation) routine = &platform.formationRoutines[ref.entryIndex].routine;
    else if (ref.kind == RoutineLayerRef::Kind::Tag) routine = &platform.tagRoutines[ref.entryIndex].routine;
    return routine->size() == layer.elements_.size() ? routine : nullptr;
}

void FlyToStation(const Refs& refs, const Request& request) {
    const workspace::DiaPanelListRow* row = At(refs.panel.rows_, request.rowIndex_);
    if (row == nullptr || row->kind_ != "station") return;
    if (!row->hasFlyTarget_) {
        refs.queue.Post(ToastSeverity::Info, "この駅には番線のレールがないので、カメラを動かせません");
        return;
    }
    DiaPanelNavigation& navigation = refs.navigation;
    navigation = {};
    navigation.pending_ = true;
    navigation.sceneIndex_ = row->sceneIndex_;
    navigation.fly_ = true;
    navigation.flyX_ = row->flyX_;
    navigation.flyY_ = row->flyY_;
    navigation.flyZ_ = row->flyZ_;
}

void RenameStation(const Refs& refs, const Request& request) {
    const workspace::DiaPanelListRow* row = ActiveRow(refs, request.rowIndex_, "station");
    const std::string name = Trimmed(request.value_);
    if (row == nullptr || name.empty()) return;
    const auto station = std::find_if(refs.editor.graph.stations.begin(), refs.editor.graph.stations.end(),
                                      [row](const rs::world::StationInfo& item) { return item.id == row->id_; });
    if (station == refs.editor.graph.stations.end() || station->name == name) return;
    StationDiaAction(refs.editor).RenameStation(row->id_, name);
}

void DeleteStation(const Refs& refs, const Request& request) {
    const workspace::DiaPanelListRow* row = ActiveRow(refs, request.rowIndex_, "station");
    if (row == nullptr || !StationExists(refs.editor.graph, row->id_)) return;
    StationDiaAction(refs.editor).RemoveStation(row->id_);
}

// 行へ落としたときの所属駅。駅の行・その駅の番線や分岐の行へ落とせばその駅、
// シーンの行・未所属の番線や分岐の行へ落とせば未所属(Nil)。
rs::world::Guid DroppedStation(const Refs& refs, const workspace::DiaPanelListRow& destination) {
    rs::world::Guid stationId = rs::world::NilGuid();
    if (destination.kind_ == "station") {
        stationId = destination.id_;
    } else if (destination.kind_ == "target" && destination.point_) {
        const auto node = refs.editor.graph.nodes.find(destination.id_);
        if (node != refs.editor.graph.nodes.end()) stationId = node->second.pointStationId;
    } else if (destination.kind_ == "target") {
        const rs::world::PlatformStopInfo* other = rs::world::FindPlatformById(refs.editor.graph, destination.id_);
        if (other != nullptr) stationId = other->stationId;
    }
    return StationExists(refs.editor.graph, stationId) ? stationId : rs::world::NilGuid();
}

void MovePlatform(const Refs& refs, const Request& request) {
    const workspace::DiaPanelListRow* source = ActiveRow(refs, request.sourceIndex_, "target");
    const workspace::DiaPanelListRow* destination = At(refs.panel.rows_, request.rowIndex_);
    if (source == nullptr || source->point_ || destination == nullptr || destination->sceneIndex_ != source->sceneIndex_) return;
    const rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(refs.editor.graph, source->id_);
    if (platform == nullptr) return;
    const rs::world::Guid stationId = DroppedStation(refs, *destination);
    if (stationId == platform->stationId) return;
    StationDiaAction(refs.editor).SetPlatformStation(platform->id, stationId);
}

void MovePoint(const Refs& refs, const Request& request) {
    const workspace::DiaPanelListRow* source = ActiveRow(refs, request.sourceIndex_, "target");
    const workspace::DiaPanelListRow* destination = At(refs.panel.rows_, request.rowIndex_);
    if (source == nullptr || !source->point_ || destination == nullptr || destination->sceneIndex_ != source->sceneIndex_) return;
    const auto node = refs.editor.graph.nodes.find(source->id_);
    if (node == refs.editor.graph.nodes.end() || !node->second.IsPoint()) return;
    const rs::world::Guid stationId = DroppedStation(refs, *destination);
    if (stationId == node->second.pointStationId) return;
    PointRoutineAction(refs.editor, source->id_).SetStation(stationId);
}

void ToggleDoor(const Refs& refs, const Request& request) {
    const std::optional<ResolvedTarget> resolved = ResolveActiveTarget(refs, request);
    if (!resolved || (request.field_ != "left" && request.field_ != "right")) return;
    const int side = request.field_ == "left" ? 0 : 1;
    StationDiaAction(refs.editor).SetPlatformDoor(resolved->platform->id, side, !resolved->platform->openDoor[side]);
}

void ChangeConnectionWait(const Refs& refs, const Request&) { NotifyNotImplemented(refs.queue, "接続待ちの変更"); }

} // namespace rs::app::dia_panel_action
