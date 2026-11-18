#include "PlatformCommand.h"
#include "CommandLookup.h"

#include <algorithm>
#include <utility>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {
AddStationCommand::AddStationCommand(rs::world::StationInfo station) : station_(std::move(station)) {}

// 駅を末尾へ追加する。IDが未採番なら新規発行する。
void AddStationCommand::Apply(rs::world::TrackGraph& graph) {
    if (station_.id == rs::world::NilGuid()) {
        station_.id = rs::world::GenerateGuid();
    }
    index_ = graph.stations.size();
    graph.stations.push_back(station_);
}

// 追加した駅をidで検索し削除する(末尾追加なので添字復元は不要)。
void AddStationCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.stations, &rs::world::StationInfo::id, station_.id);
    if (index < graph.stations.size()) {
        graph.stations.erase(graph.stations.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

RemoveStationCommand::RemoveStationCommand(rs::world::Guid stationId) : stationId_(stationId) {}

// 駅を削除する。その駅に紐づく番線は削除せず、stationIdをNilGuidへ落として孤立させる
// (番線自体はRevertで復元できるよう保持する)。
void RemoveStationCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.stations, &rs::world::StationInfo::id, stationId_);
    if (index == graph.stations.size()) {
        removed_ = false;
        return;
    }
    station_ = graph.stations[index];
    index_ = index;
    removed_ = true;
    detachedPlatformIds_.clear();
    detachedPointIds_.clear();
    for (rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationId == stationId_) {
            detachedPlatformIds_.push_back(platform.id);
            platform.stationId = rs::world::NilGuid();
        }
    }
    // 分岐も番線と同じく所属だけ外す。
    for (auto& [nodeGuid, node] : graph.nodes) {
        if (node.pointStationId == stationId_) {
            detachedPointIds_.push_back(nodeGuid);
            node.pointStationId = rs::world::NilGuid();
        }
    }
    graph.stations.erase(graph.stations.begin() + static_cast<std::ptrdiff_t>(index));
}

// 削除した駅を元の添字位置へ復元し、Apply時にNilGuidへ落とした番線のstationIdを
// このstationIdへ戻す(番線自体は消していないので、ここでの復元は所属付け替えのみ)。
void RemoveStationCommand::Revert(rs::world::TrackGraph& graph) {
    if (!removed_) {
        return;
    }
    graph.stations.insert(graph.stations.begin() + static_cast<std::ptrdiff_t>(std::min(index_, graph.stations.size())), station_);
    for (const rs::world::Guid& id : detachedPlatformIds_) {
        const size_t index = FindIndexByMember(graph.platforms, &rs::world::PlatformStopInfo::id, id);
        if (index < graph.platforms.size()) {
            graph.platforms[index].stationId = stationId_;
        }
    }
    for (const rs::world::Guid& id : detachedPointIds_) {
        const auto found = graph.nodes.find(id);
        if (found != graph.nodes.end()) found->second.pointStationId = stationId_;
    }
}

AddPlatformCommand::AddPlatformCommand(rs::world::PlatformStopInfo platform) : platform_(std::move(platform)) {}

// 番線を末尾へ登録する。IDが未採番なら新規発行する。
void AddPlatformCommand::Apply(rs::world::TrackGraph& graph) {
    if (platform_.id == rs::world::NilGuid()) {
        platform_.id = rs::world::GenerateGuid();
    }
    index_ = graph.platforms.size();
    rs::world::RegisterPlatform(graph, platform_);
}

// 追加した番線をidで検索し登録解除する。
void AddPlatformCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.platforms, &rs::world::PlatformStopInfo::id, platform_.id);
    if (index < graph.platforms.size()) {
        rs::world::UnregisterPlatform(graph, index);
    }
}

RemovePlatformCommand::RemovePlatformCommand(rs::world::Guid platformId) : platformId_(platformId) {}

// 番線を登録解除する。元の添字index_と内容platform_を保持し、Revertで復元できるようにする。
void RemovePlatformCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.platforms, &rs::world::PlatformStopInfo::id, platformId_);
    if (index == graph.platforms.size()) {
        removed_ = false;
        return;
    }
    index_ = index;
    platform_ = rs::world::UnregisterPlatform(graph, index);
    removed_ = true;
}

// 削除した番線を元の添字index_へ戻す。末尾へappendすると、同一edgeを共有する
// 他の番線が既にあった場合にedgeToPlatformの勝者(索引の対応関係)が変わってしまうため、
// 必ずRegisterPlatformAtで元の位置へ復元する。
void RemovePlatformCommand::Revert(rs::world::TrackGraph& graph) {
    if (removed_) {
        rs::world::RegisterPlatformAt(graph, index_, platform_);
    }
}

SetStationPropertyCommand::SetStationPropertyCommand(rs::world::Guid stationId, StationPropertyDelta delta)
    : stationId_(stationId), delta_(std::move(delta)) {}

// deltaに設定されている項目だけを適用し、変更前の値をoldValues_へ保存する。
void SetStationPropertyCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.stations, &rs::world::StationInfo::id, stationId_);
    if (index == graph.stations.size()) {
        hadOld_ = false;
        return;
    }
    oldValues_ = {};
    ApplyOptionalField(delta_.name, graph.stations[index].name, oldValues_.name);
    hadOld_ = true;
}

// Apply時に保存したoldValues_を書き戻す。
void SetStationPropertyCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.stations, &rs::world::StationInfo::id, stationId_);
    if (!hadOld_ || index == graph.stations.size()) {
        return;
    }
    RestoreOptionalField(oldValues_.name, graph.stations[index].name);
}

SetPlatformPropertyCommand::SetPlatformPropertyCommand(rs::world::Guid platformId, PlatformPropertyDelta delta)
    : platformId_(platformId), delta_(std::move(delta)) {}

// deltaに設定されている項目だけを適用し、変更前の値をoldValues_へ保存する。
void SetPlatformPropertyCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.platforms, &rs::world::PlatformStopInfo::id, platformId_);
    if (index == graph.platforms.size()) {
        hadOld_ = false;
        return;
    }
    rs::world::PlatformStopInfo& platform = graph.platforms[index];
    oldValues_ = {};
    ApplyOptionalField(delta_.name, platform.name, oldValues_.name);
    ApplyOptionalField(delta_.stationId, platform.stationId, oldValues_.stationId);
    ApplyOptionalField(delta_.stoppable, platform.stoppable, oldValues_.stoppable);
    ApplyOptionalField(delta_.stopReference, platform.stopReference, oldValues_.stopReference);
    ApplyOptionalField(delta_.stopOffsetM, platform.stopOffsetM, oldValues_.stopOffsetM);
    ApplyOptionalField(delta_.routine, platform.routine, oldValues_.routine);
    ApplyOptionalField(delta_.trainRoutines, platform.trainRoutines, oldValues_.trainRoutines);
    ApplyOptionalField(delta_.tagRoutines, platform.tagRoutines, oldValues_.tagRoutines);
    ApplyOptionalField(delta_.formationRoutines, platform.formationRoutines, oldValues_.formationRoutines);
    // openDoorはC配列なので、std::arrayへ写して書く。
    std::array<bool, 2> openDoor{platform.openDoor[0], platform.openDoor[1]};
    ApplyOptionalField(delta_.openDoor, openDoor, oldValues_.openDoor);
    platform.openDoor[0] = openDoor[0];
    platform.openDoor[1] = openDoor[1];
    ApplyOptionalField(delta_.railEdges, platform.railEdges, oldValues_.railEdges);
    hadOld_ = true;
}

// 復元後の索引はスタックの共通後処理で作り直される。
void SetPlatformPropertyCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.platforms, &rs::world::PlatformStopInfo::id, platformId_);
    if (!hadOld_ || index == graph.platforms.size()) {
        return;
    }
    rs::world::PlatformStopInfo& platform = graph.platforms[index];
    RestoreOptionalField(oldValues_.name, platform.name);
    RestoreOptionalField(oldValues_.stationId, platform.stationId);
    RestoreOptionalField(oldValues_.stoppable, platform.stoppable);
    RestoreOptionalField(oldValues_.stopReference, platform.stopReference);
    RestoreOptionalField(oldValues_.stopOffsetM, platform.stopOffsetM);
    RestoreOptionalField(oldValues_.routine, platform.routine);
    RestoreOptionalField(oldValues_.trainRoutines, platform.trainRoutines);
    RestoreOptionalField(oldValues_.tagRoutines, platform.tagRoutines);
    RestoreOptionalField(oldValues_.formationRoutines, platform.formationRoutines);
    if (oldValues_.openDoor.has_value()) {
        platform.openDoor[0] = (*oldValues_.openDoor)[0];
        platform.openDoor[1] = (*oldValues_.openDoor)[1];
    }
    RestoreOptionalField(oldValues_.railEdges, platform.railEdges);
}

}  // namespace rs::editor
