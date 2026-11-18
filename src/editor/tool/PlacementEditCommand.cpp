// PlacementEditCommand.cpp - PlacementEditCommand.h の実装
#include "PlacementEditCommand.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

namespace {

// 接続点が指せるか。導出した柱(poleId付き)は常に指せ、それ以外は所属edgeがgraphにあること。
bool WirePointResolvable(const rs::world::TrackGraph& graph, const WirePointRef& point) {
    return IsStructLinePoint(point) || graph.edges.find(point.edge) != graph.edges.end();
}

}  // namespace

PlacementEditCommand::PlacementEditCommand(
    rs::world::ManualPlacementOverlay& overlay,
    std::vector<rs::world::MaterializeSnapshot> snapshots, Request request)
    : overlay_(overlay), snapshots_(std::move(snapshots)), request_(std::move(request)) {}

bool PlacementEditCommand::WouldChange(const rs::world::TrackGraph& graph) const {
    switch (request_.kind) {
    case Kind::Delete:
        return graph.edges.find(request_.edge) != graph.edges.end();
    case Kind::Add:
        return !request_.folderUtf8.empty() &&
               graph.edges.find(request_.edge) != graph.edges.end();
    case Kind::Move:
        return graph.edges.find(request_.newEdge) != graph.edges.end() &&
               !IsNoOpPlacementMove(request_.edge, request_.s, request_.oldLateralOffsetM,
                                    request_.newEdge, request_.newS, request_.newLateralOffsetM);
    case Kind::ConnectWire:
        return !request_.lineFolderUtf8.empty() &&
               !SameWirePoint(request_.wirePointA, request_.wirePointB) &&
               WirePointResolvable(graph, request_.wirePointA) &&
               WirePointResolvable(graph, request_.wirePointB);
    case Kind::DisconnectWire:
        return WirePointResolvable(graph, request_.wirePointA) &&
               WirePointResolvable(graph, request_.wirePointB);
    case Kind::Dematerialize:
        return graph.edges.find(request_.edge) != graph.edges.end() &&
               rs::world::FindRunAnchor(overlay_.runAnchors, request_.edge,
                                                   request_.folderUtf8) != nullptr;
    case Kind::Reanchor: {
        const auto targeted = [this](const auto& item) { return item.edge == request_.edge; };
        return request_.edge != request_.newEdge && graph.edges.contains(request_.newEdge) &&
               (std::ranges::any_of(overlay_.poles, targeted) ||
                std::ranges::any_of(overlay_.piers, targeted) ||
                std::ranges::any_of(overlay_.runAnchors, targeted));
    }
    }
    return false;
}

void PlacementEditCommand::Apply(rs::world::TrackGraph& graph) {
    savedOverlay_ = overlay_;
    savedEdgePluginSections_.reset();
    if (request_.kind == Kind::Dematerialize) {
        if (graph.edges.contains(request_.edge))
            savedEdgePluginSections_ = graph.edges.at(request_.edge).pluginSections;
    }
    savedSections_.clear();
    savedSections_.resize(snapshots_.size());
    appliedSnapshots_.assign(snapshots_.size(), false);

    for (std::size_t index = 0; index < snapshots_.size(); ++index) {
        const rs::world::MaterializeSnapshot& snapshot = snapshots_[index];
        if (snapshot.Empty()) continue;
        appliedSnapshots_[index] =
            rs::world::ApplyMaterializeSnapshot(graph, overlay_, snapshot, savedSections_[index]);
    }

    switch (request_.kind) {
    case Kind::Delete:
        overlayOps_.DeleteSelectedPlacement(overlay_, request_.edge, request_.s, request_.folderUtf8);
        break;
    case Kind::Add:
        overlayOps_.AddManualPlacement(overlay_, graph, request_.edge, request_.s,
                                       request_.folderUtf8, request_.keepUpright,
                                       request_.verticalOffsetM);
        break;
    case Kind::Move:
        overlayOps_.MovePlacementTo(overlay_, graph, request_.edge, request_.s,
                                    request_.folderUtf8, request_.newEdge, request_.newS,
                                    request_.keepUpright, request_.verticalOffsetM,
                                    request_.oldLateralOffsetM, request_.newLateralOffsetM,
                                    request_.newDir);
        break;
    case Kind::ConnectWire:
        if (!overlayOps_.ConnectWirePoints(overlay_, request_.wirePointA, request_.wirePointB,
                                           request_.lineFolderUtf8)) {
            spdlog::warn("[PlacementEdit] wire connect endpoints were not materialized; ignored");
        }
        break;
    case Kind::DisconnectWire:
        overlayOps_.DisconnectWirePoints(overlay_, request_.wirePointA, request_.wirePointB);
        break;
    case Kind::Dematerialize:
        rs::world::DematerializeEdgeFolder(graph, overlay_, request_.edge, request_.folderUtf8);
        break;
    case Kind::Reanchor:
        overlayOps_.ReanchorPlacementEditsForEdge(overlay_, request_.edge, request_.newEdge);
        break;
    }
}

void PlacementEditCommand::Revert(rs::world::TrackGraph& graph) {
    for (std::size_t index = snapshots_.size(); index > 0; --index) {
        const std::size_t snapshotIndex = index - 1;
        if (!appliedSnapshots_[snapshotIndex]) continue;
        rs::world::RevertMaterializeSnapshot(graph, overlay_, snapshots_[snapshotIndex],
                                             savedSections_[snapshotIndex]);
    }
    overlay_ = savedOverlay_;
    if (savedEdgePluginSections_.has_value()) {
        if (graph.edges.contains(request_.edge))
            graph.edges.at(request_.edge).pluginSections = *savedEdgePluginSections_;
    }
}

} // namespace rs::editor
