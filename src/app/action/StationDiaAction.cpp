//
// StationDiaAction.cpp - 駅・ダイヤ編集のアプリサービス
//
#include "StationDiaAction.h"
#include "../../editor/tool/Editor.h"

#include <algorithm>
#include <array>
#include <memory>

#include "../../editor/tool/PlatformCommand.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/PointHandLinks.h"

namespace rs::app {
namespace {

rs::world::StationInfo* FindStation(rs::world::TrackGraph& graph, const rs::world::Guid& id) {
    for (rs::world::StationInfo& station : graph.stations) {
        if (station.id == id) {
            return &station;
        }
    }
    return nullptr;
}

} // namespace

StationDiaAction::StationDiaAction(rs::editor::Editor& editor) : editor_(editor) {}

void StationDiaAction::AddStation() {
    rs::world::StationInfo station;
    station.name = "新しい駅";
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::AddStationCommand>(station));
}

void StationDiaAction::AddPlatform() {
    rs::world::PlatformStopInfo platform;
    platform.name = "新しい番線";
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::AddPlatformCommand>(platform));
    if (!editor_.graph.platforms.empty()) {
        editor_.selectedPlatform = editor_.graph.platforms.back().id;
    }
}

void StationDiaAction::SelectStation(rs::world::Guid stationId) { editor_.selectedStation = stationId; }
void StationDiaAction::SelectPlatform(rs::world::Guid platformId) { editor_.selectedPlatform = platformId; }

void StationDiaAction::BeginHoverFrame() {
    // ホバー対象は毎フレーム作り直す。行から外れたフレームで前回値が残るとハイライトが消えない。
    editor_.hoveredStation = rs::world::NilGuid();
    editor_.hoveredPlatform = rs::world::NilGuid();
    editor_.hoveredPointNode = rs::world::NilGuid();
}

void StationDiaAction::SetHoveredStation(rs::world::Guid stationId) { editor_.hoveredStation = stationId; }
void StationDiaAction::SetHoveredPlatform(rs::world::Guid platformId) { editor_.hoveredPlatform = platformId; }
void StationDiaAction::SelectPoint(rs::world::Guid nodeId) { editor_.selectedNode = nodeId; }
void StationDiaAction::SetHoveredPoint(rs::world::Guid nodeId) { editor_.hoveredPointNode = nodeId; }
void StationDiaAction::SetPointActiveLink(rs::world::Guid nodeId, int linkIndex) {
    editor_.TrySetPointActiveLink(nodeId, linkIndex);
}

void StationDiaAction::RenameStation(rs::world::Guid stationId, std::string name) {
    rs::editor::StationPropertyDelta delta;
    delta.name = std::move(name);
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetStationPropertyCommand>(stationId, std::move(delta)));
}

void StationDiaAction::RemoveStation(rs::world::Guid stationId) {
    // 番線自体は消えず所属だけ外れる。
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::RemoveStationCommand>(stationId));
}

void StationDiaAction::RenamePlatform(rs::world::Guid platformId, std::string name) {
    rs::editor::PlatformPropertyDelta delta;
    delta.name = std::move(name);
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

void StationDiaAction::SetPlatformStation(rs::world::Guid platformId, rs::world::Guid stationId) {
    rs::editor::PlatformPropertyDelta delta;
    delta.stationId = stationId;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

void StationDiaAction::SetPlatformDoor(rs::world::Guid platformId, int side, bool open) {
    const rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId);
    if (platform == nullptr || side < 0 || side > 1) return;
    std::array<bool, 2> door{platform->openDoor[0], platform->openDoor[1]};
    door[static_cast<std::size_t>(side)] = open;
    rs::editor::PlatformPropertyDelta delta;
    delta.openDoor = door;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platformId, std::move(delta)));
}

void StationDiaAction::RenameSelectedPlatform(std::string name) {
    RenamePlatform(editor_.selectedPlatform, std::move(name));
}

void StationDiaAction::SetSelectedPlatformStoppable(bool stoppable) {
    rs::editor::PlatformPropertyDelta delta;
    delta.stoppable = stoppable;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(editor_.selectedPlatform, std::move(delta)));
}

void StationDiaAction::SetSelectedPlatformStopReference(rs::world::StopReference reference) {
    rs::editor::PlatformPropertyDelta delta;
    delta.stopReference = reference;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(editor_.selectedPlatform, std::move(delta)));
}

void StationDiaAction::SetSelectedPlatformStopOffset(double offsetM) {
    rs::editor::PlatformPropertyDelta delta;
    delta.stopOffsetM = offsetM;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(editor_.selectedPlatform, std::move(delta)));
}

void StationDiaAction::SetSelectedPlatformStation(rs::world::Guid stationId) {
    SetPlatformStation(editor_.selectedPlatform, stationId);
}

void StationDiaAction::RemoveSelectedPlatform() {
    const rs::world::Guid platformId = editor_.selectedPlatform;
    editor_.ClearSelectedPlatform();
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::RemovePlatformCommand>(platformId));
}

StationDiaViewModel StationDiaAction::ViewModel() const {
    StationDiaViewModel viewModel;
    for (const std::pair<const rs::world::Guid, rs::world::TrackNode>& entry : editor_.graph.nodes) {
        const rs::world::TrackNode& node = entry.second;
        if (!node.IsPoint()) continue;
        StationDiaPointRowViewModel pointRow;
        pointRow.id = entry.first;
        pointRow.x = node.pos.x;
        pointRow.z = node.pos.z;
        pointRow.selected = editor_.selectedNode == entry.first;
        for (const rs::world::PointSideLinks& side : rs::world::BuildPointSideLinks(editor_.graph, node)) {
            pointRow.sides.push_back({side.leftLinkIndex, side.rightLinkIndex, side.leftActive, side.rightActive});
        }
        viewModel.points.push_back(std::move(pointRow));
    }
    std::sort(viewModel.points.begin(), viewModel.points.end(),
              [](const StationDiaPointRowViewModel& a, const StationDiaPointRowViewModel& b) {
                  return a.x != b.x ? a.x < b.x : a.z < b.z;
              });
    for (const rs::world::StationInfo& station : editor_.graph.stations) {
        StationDiaStationRowViewModel stationRow;
        stationRow.id = station.id;
        stationRow.name = station.name;
        stationRow.selected = editor_.selectedStation == station.id;
        for (const rs::world::PlatformStopInfo& platform : editor_.graph.platforms) {
            if (platform.stationId == station.id) {
                stationRow.platforms.push_back({platform.id, platform.name, editor_.selectedPlatform == platform.id});
            }
        }
        viewModel.stations.push_back(std::move(stationRow));
    }
    for (const rs::world::PlatformStopInfo& platform : editor_.graph.platforms) {
        if (platform.stationId == rs::world::NilGuid()) {
            viewModel.unassignedPlatforms.push_back({platform.id, platform.name, editor_.selectedPlatform == platform.id});
        }
    }
    rs::world::StationInfo* selectedStation = FindStation(editor_.graph, editor_.selectedStation);
    if (selectedStation != nullptr) {
        viewModel.selectedStation = {true, selectedStation->id, selectedStation->name};
    }
    rs::world::PlatformStopInfo* selectedPlatform = rs::world::FindPlatformById(editor_.graph, editor_.selectedPlatform);
    if (selectedPlatform != nullptr) {
        StationDiaSelectedPlatformViewModel& selected = viewModel.selectedPlatform;
        selected.visible = true;
        selected.id = selectedPlatform->id;
        selected.name = selectedPlatform->name;
        selected.stoppable = selectedPlatform->stoppable;
        selected.stopReference = selectedPlatform->stopReference;
        selected.stopOffsetM = selectedPlatform->stopOffsetM;
        selected.stationId = selectedPlatform->stationId;
        selected.edgeCount = static_cast<int>(selectedPlatform->railEdges.size());
        rs::world::StationInfo* station = FindStation(editor_.graph, selected.stationId);
        if (station != nullptr) {
            selected.stationName = station->name;
        }
    }
    return viewModel;
}

} // namespace rs::app
