//
// EdgeMergeCommand.cpp - MergeEdgeCommand の実装(宣言は EdgeSplitMergeCommand.h)
//
#include "EdgeSplitMergeCommand.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/AutoCant.h"
#include "../../world/alignment/BlockRegistry.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::ManualPlacementOverlay;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

namespace {

// forward=true は結合、false は結合のUndo。反転側へ移った個体は向きと横オフセットも反転する。
void RemapIndividualsForMerge(std::vector<rs::world::PlacedIndividual>& individuals,
                              const rs::world::EdgeMergeRemap& remap, bool forward) {
    for (rs::world::PlacedIndividual& individual : individuals) {
        bool reversed = false;
        const bool remapped = forward
                                  ? rs::world::RemapMergedAnchor(remap, individual.edge, individual.s,
                                                                  &reversed)
                                  : rs::world::UnremapMergedAnchor(remap, individual.edge, individual.s,
                                                                    &reversed);
        if (remapped && reversed) {
            individual.direction = -individual.direction;
            individual.lateralOffsetM = -individual.lateralOffsetM;
        }
    }
}

}  // namespace

MergeEdgeCommand::MergeEdgeCommand(Guid sharedNodeGuid, ManualPlacementOverlay& overlay,
                                    const rs::world::WarpLinkTable* warpLinks,
                                    bool allowLayerMismatch)
    : sharedNodeGuid_(sharedNodeGuid),
      overlay_(overlay),
      warpLinks_(warpLinks),
      allowLayerMismatch_(allowLayerMismatch) {}

void MergeEdgeCommand::Apply(TrackGraph& graph) {
    applied_ = false;
    savedReferences_.reset();

    if (rs::world::CheckEdgeMerge(graph, sharedNodeGuid_, warpLinks_, allowLayerMismatch_) !=
        rs::world::EdgeMergeRefusal::None)
        return;

    std::optional<rs::world::EdgeMergeResult> result =
        rs::world::BuildEdgeMerge(graph, sharedNodeGuid_, allowLayerMismatch_);
    if (!result) return;

    const TrackNode sharedNodeSnapshot = graph.nodes.at(sharedNodeGuid_);
    const TrackNode::Link linkFirst = sharedNodeSnapshot.links[0];
    const TrackNode::Link linkSecond = sharedNodeSnapshot.links[1];
    const Guid edgeFirstGuid = linkFirst.edge;
    const Guid edgeSecondGuid = linkSecond.edge;
    // ComputeMergePlan(EdgeMerge.cpp)と同じ判定。前半役/後半役は独立に反転しうる。
    const bool frontReversed = (linkFirst.end == 0);
    const bool backReversed = (linkSecond.end == 1);

    const auto itFirst = graph.edges.find(edgeFirstGuid);
    const auto itSecond = graph.edges.find(edgeSecondGuid);
    if (itFirst == graph.edges.end() || itSecond == graph.edges.end()) return;

    // Revert用スナップショットを保存。
    savedEdgeFirst_ = itFirst->second;
    savedEdgeSecond_ = itSecond->second;
    savedSharedNode_ = sharedNodeSnapshot;

    const Guid frontFarNodeGuid = result->merged.nodeA;
    const Guid backFarNodeGuid = result->merged.nodeB;
    const auto frontFarIt = graph.nodes.find(frontFarNodeGuid);
    hadFrontFarNode_ = frontFarIt != graph.nodes.end();
    if (hadFrontFarNode_) savedFrontFarNode_ = frontFarIt->second;
    const auto backFarIt = graph.nodes.find(backFarNodeGuid);
    hadBackFarNode_ = backFarIt != graph.nodes.end();
    if (hadBackFarNode_) savedBackFarNode_ = backFarIt->second;


    remap_.keptEdge = edgeFirstGuid;
    remap_.removedEdge = result->removedEdgeGuid;
    remap_.lenKept = result->lenFirst;
    remap_.lenRemoved = result->lenSecond;
    remap_.keptReversed = frontReversed;
    remap_.removedReversed = backReversed;

    RewireMergedGraph(graph, *result, edgeFirstGuid, edgeSecondGuid);
    if (!RebuildMergedGeometry(graph, edgeFirstGuid, result->lenFirst + result->lenSecond)) {
        spdlog::warn("[edge] 結合後の形を作れないため結合を取り消し");
        RestoreMergedGraph(graph);
        return;
    }

    // overlay remap。前半役(edgeFirstGuid)・後半役(edgeSecondGuid)の
    // どちらも独立に反転することがあるため、s変換はremapの約束事(RemapMergedAnchor)へ一本化する。
    RemapOverlayForMerge();
    keptEdgeGuid_ = edgeFirstGuid;
    removedEdgeGuid_ = result->removedEdgeGuid;
    removedNodeGuid_ = result->removedNodeGuid;
    hasRemap_ = true;
    applied_ = true;
}

void MergeEdgeCommand::RewireMergedGraph(TrackGraph& graph,
                                         const rs::world::EdgeMergeResult& result,
                                         Guid edgeFirstGuid, Guid edgeSecondGuid) {
    graph.edges[edgeFirstGuid] = result.merged;
    graph.edges.erase(result.removedEdgeGuid);
    graph.nodes.erase(result.removedNodeGuid);
    rs::world::TrackGraphReferenceChange change;
    change.kind = rs::world::TrackGraphReferenceChange::Kind::Merge;
    change.firstEdge = edgeFirstGuid;
    change.secondEdge = edgeSecondGuid;
    change.merge = remap_;
    change.winnerEdge = result.firstWins ? edgeFirstGuid : edgeSecondGuid;
    savedReferences_ = rs::world::RemapTrackGraphReferences(graph, change);
}

bool MergeEdgeCommand::RebuildMergedGeometry(TrackGraph& graph, Guid edgeFirstGuid,
                                             double assumedLen) {
    TrackEdge& mergedRef = graph.edges.at(edgeFirstGuid);
    // 遠端2ノードの姿勢から形を作り直し、実長でRescaleEdgeSections(順序が重要)。
    if (!rs::world::RebuildEdgeSpline(graph, mergedRef)) return false;
    rs::world::RescaleEdgeSections(mergedRef, assumedLen,
                                   static_cast<double>(mergedRef.spline.back().s));
    //自動カントの結合後edgeと遠端の隣は、つなぎ目の値を決め直す。
    rs::world::RebuildAutoCantAround(graph, {edgeFirstGuid});
    return true;
}

void MergeEdgeCommand::RemapOverlayForMerge() {
    // 実体化済み個体(PlacedIndividual)も同じ規約でremapする。lines(WireSpan)は
    // 個体のGuidを参照するだけなので、個体側のedge/sを直せば架線も追従する。
    RemapIndividualsForMerge(overlay_.poles, remap_, true);
    RemapIndividualsForMerge(overlay_.piers, remap_, true);
    // 実体化済みfolderの記録をkeptEdge側へ寄せる。keptEdge側に同じfolderの
    // 記録が既にあれば、結合後は1本のedgeなので重複させずremovedEdge側を消す
    // (droppedSections_へ保存してRevertで復元する)。無ければedgeを付け替える
    // (remappedSectionFolders_へfolderを記録し、RevertでremovedEdgeへ戻す)。
    droppedSections_.clear();
    remappedSectionFolders_.clear();
    for (rs::world::RunAnchor& section : overlay_.runAnchors) {
        if (section.edge != remap_.removedEdge) continue;
        if (rs::world::FindRunAnchor(overlay_.runAnchors, remap_.keptEdge,
                                     section.folderUtf8) != nullptr) {
            droppedSections_.push_back(section);
        } else {
            section.edge = remap_.keptEdge;
            remappedSectionFolders_.push_back(section.folderUtf8);
        }
    }
    overlay_.runAnchors.erase(
        std::remove_if(overlay_.runAnchors.begin(), overlay_.runAnchors.end(),
                       [this](const rs::world::RunAnchor& section) {
                           return section.edge == remap_.removedEdge;
                       }),
        overlay_.runAnchors.end());
}

void MergeEdgeCommand::RestoreMergedGraph(TrackGraph& graph) {
    if (savedReferences_) rs::world::RestoreTrackGraphReferences(graph, *savedReferences_);

    graph.edges[savedEdgeFirst_.guid] = savedEdgeFirst_;
    graph.edges[savedEdgeSecond_.guid] = savedEdgeSecond_;
    graph.nodes[savedSharedNode_.guid] = savedSharedNode_;

    if (hadFrontFarNode_) graph.nodes[savedFrontFarNode_.guid] = savedFrontFarNode_;
    if (hadBackFarNode_) graph.nodes[savedBackFarNode_.guid] = savedBackFarNode_;
    rs::world::RebuildAutoCantAround(graph, {savedEdgeFirst_.guid, savedEdgeSecond_.guid});
}

void MergeEdgeCommand::Revert(TrackGraph& graph) {
    if (!applied_) return;

    RestoreMergedGraph(graph);

    // SplitEdgeCommand::Revert と同じ理由で、overlayは丸ごと復元せず逆remapのみ行う。
    RemapIndividualsForMerge(overlay_.poles, remap_, false);
    RemapIndividualsForMerge(overlay_.piers, remap_, false);
    // runAnchorsはApplyで(a)removedEdge側を消してdroppedSections_へ保存、または
    // (b)keptEdgeへ付け替えてremappedSectionFolders_へfolderを記録、のどちらかを
    // 行っている。(a)は保存した記録をそのまま戻し、(b)はkeptEdge上の該当folderを
    // removedEdgeへ戻す。
    overlay_.runAnchors.insert(overlay_.runAnchors.end(), droppedSections_.begin(),
                             droppedSections_.end());
    for (const std::string& folderUtf8 : remappedSectionFolders_) {
        for (rs::world::RunAnchor& section : overlay_.runAnchors) {
            if (section.edge == remap_.keptEdge && section.folderUtf8 == folderUtf8) {
                section.edge = remap_.removedEdge;
                break;
            }
        }
    }
    droppedSections_.clear();
    remappedSectionFolders_.clear();
    applied_ = false;
}

}  // namespace rs::editor
