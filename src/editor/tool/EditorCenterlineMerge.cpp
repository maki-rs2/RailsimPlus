//
// EditorCenterlineMerge.cpp - EditorCenterlineMerge.h の実装
//
#include "EditorCenterlineMerge.h"

#include <memory>
#include <vector>

#include "Command.h"
#include "Editor.h"
#include "EditorCenterlineSplit.h"
#include "EdgeSplitMergeCommand.h"

namespace rs::editor {

using rs::world::CheckEdgeMerge;
using rs::world::EdgeMergeRefusal;
using rs::world::EdgeMergeRemap;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;
using rs::world::WarpLinkTable;

namespace {

// edgeAとedgeBの端点集合{nodeA,nodeB}の共通nodeを返す。共通がちょうど1つの場合のみ
// その値を返し、0個(隣接していない)または2個(両端が一致=自己ループ等、想定外の位相)
// の場合はNilGuidを返す。
Guid FindSingleCommonNode(const TrackEdge& edgeA, const TrackEdge& edgeB) {
    const bool nodeAMatches = (edgeA.nodeA == edgeB.nodeA) || (edgeA.nodeA == edgeB.nodeB);
    const bool nodeBMatches = (edgeA.nodeB == edgeB.nodeA) || (edgeA.nodeB == edgeB.nodeB);
    if (nodeAMatches && nodeBMatches) return NilGuid();
    if (nodeAMatches) return edgeA.nodeA;
    if (nodeBMatches) return edgeA.nodeB;
    return NilGuid();
}

}  // namespace

bool IsCenterlineGroupMergeNode(const TrackGraph& graph, const Guid& sharedNodeGuid) {
    const auto nodeIt = graph.nodes.find(sharedNodeGuid);
    if (nodeIt == graph.nodes.end()) return false;
    const TrackNode& sharedNode = nodeIt->second;
    if (sharedNode.links.size() != 2) return false;
    for (const TrackNode::Link& link : sharedNode.links) {
        if (FindOwningCenterlineGroup(graph, link.edge) != NilGuid()) return true;
    }
    return false;
}

std::optional<EdgeMergeRemap> SyncMergeCenterlineGroupAt(Editor& ed, const Guid& sharedNodeGuid,
                                                          const WarpLinkTable* warpLinks,
                                                          bool allowLayerMismatch) {
    TrackGraph& graph = ed.graph;
    const auto nodeIt = graph.nodes.find(sharedNodeGuid);
    if (nodeIt == graph.nodes.end()) return std::nullopt;
    const TrackNode& sharedNode = nodeIt->second;
    if (sharedNode.links.size() != 2) return std::nullopt;

    const Guid edgeAGuid = sharedNode.links[0].edge;
    const Guid edgeBGuid = sharedNode.links[1].edge;
    const auto edgeAIt = graph.edges.find(edgeAGuid);
    const auto edgeBIt = graph.edges.find(edgeBGuid);
    if (edgeAIt == graph.edges.end() || edgeBIt == graph.edges.end()) return std::nullopt;

    const Guid groupA = FindOwningCenterlineGroup(graph, edgeAGuid);
    const Guid groupB = FindOwningCenterlineGroup(graph, edgeBGuid);
    if (groupA == NilGuid() || groupB == NilGuid()) return std::nullopt;

    const bool aIsCenterline = edgeAIt->second.centerlineOnly;
    const bool bIsCenterline = edgeBIt->second.centerlineOnly;

    Guid cl1Guid = NilGuid();
    Guid cl2Guid = NilGuid();
    Guid centerNode = NilGuid();

    if (aIsCenterline && bIsCenterline) {
        // 中心線node直接: sharedNodeGuid自身が2本の中心線edgeの結節点(groupA==edgeAGuid、
        // groupB==edgeBGuidのはず、FindOwningCenterlineGroupの定義より)。
        cl1Guid = edgeAGuid;
        cl2Guid = edgeBGuid;
        centerNode = sharedNodeGuid;
    } else if (!aIsCenterline && !bIsCenterline) {
        // memberノード: sharedNodeGuidは1組のmember同士の結節点。対応する中心線edge
        // 2本(groupA/groupB)が実際に隣接しているnodeを別途探す。
        if (groupA == groupB) return std::nullopt;
        cl1Guid = groupA;
        cl2Guid = groupB;
        const auto cl1It = graph.edges.find(cl1Guid);
        const auto cl2It = graph.edges.find(cl2Guid);
        if (cl1It == graph.edges.end() || cl2It == graph.edges.end()) return std::nullopt;
        centerNode = FindSingleCommonNode(cl1It->second, cl2It->second);
        if (centerNode == NilGuid()) return std::nullopt;
    } else {
        // centerlineOnlyとmemberの混在(想定外の位相)。
        return std::nullopt;
    }

    const auto cl1It = graph.edges.find(cl1Guid);
    const auto cl2It = graph.edges.find(cl2Guid);
    if (cl1It == graph.edges.end() || cl2It == graph.edges.end()) return std::nullopt;
    const TrackEdge& cl1 = cl1It->second;
    const TrackEdge& cl2 = cl2It->second;

    if (cl1.trackEdges.empty() || cl2.trackEdges.empty()) return std::nullopt;
    if (cl1.trackEdges.size() != cl2.trackEdges.size()) return std::nullopt;
    if (cl1.trackCount != cl2.trackCount) return std::nullopt;

    // 各member m1(cl1側)に対応するm2(cl2側)を、共通nodeがちょうど1つの相手として探す。
    // 見つからない/複数見つかる場合は全体を拒否する(部分適用を避ける)。
    std::vector<Guid> memberNodes;
    memberNodes.reserve(cl1.trackEdges.size());
    for (const Guid& m1Guid : cl1.trackEdges) {
        const auto m1It = graph.edges.find(m1Guid);
        if (m1It == graph.edges.end()) return std::nullopt;
        Guid foundNode = NilGuid();
        int matchCount = 0;
        for (const Guid& m2Guid : cl2.trackEdges) {
            const auto m2It = graph.edges.find(m2Guid);
            if (m2It == graph.edges.end()) return std::nullopt;
            const Guid common = FindSingleCommonNode(m1It->second, m2It->second);
            if (common != NilGuid()) {
                foundNode = common;
                ++matchCount;
            }
        }
        if (matchCount != 1) return std::nullopt;
        memberNodes.push_back(foundNode);
    }

    // 収集したnode集合(centerNode + 全memberNodes)に重複があれば位相異常として拒否する。
    std::vector<Guid> allNodes = memberNodes;
    allNodes.push_back(centerNode);
    for (std::size_t i = 0; i < allNodes.size(); ++i) {
        for (std::size_t j = i + 1; j < allNodes.size(); ++j) {
            if (allNodes[i] == allNodes[j]) return std::nullopt;
        }
    }

    // 全nodeを事前検証してから実行する(1つでも結合不能なら全体を拒否、部分適用を避ける。
    // ③aのSplitCenterlineGroupAtと同じ方針)。
    for (const Guid& node : allNodes) {
        if (CheckEdgeMerge(graph, node, warpLinks, allowLayerMismatch) != EdgeMergeRefusal::None)
            return std::nullopt;
    }

    // 各nodeについて、結合実行前に残る側(links[0].edge)のguidを記録する
    // (MergeEdgeCommandは結合後links[0]側のguidを再利用する約束事)。
    const Guid keptCenterGuid = graph.nodes.at(centerNode).links[0].edge;
    std::vector<Guid> keptMemberGuids;
    keptMemberGuids.reserve(memberNodes.size());
    for (const Guid& node : memberNodes)
        keptMemberGuids.push_back(graph.nodes.at(node).links[0].edge);

    // 全件検証済み: 中心線+全memberを1つのCompositeCommandへまとめて実行する。
    std::unique_ptr<CompositeCommand> composite = std::make_unique<CompositeCommand>();

    std::unique_ptr<MergeEdgeCommand> centerMergeCmd = std::make_unique<MergeEdgeCommand>(
        centerNode, ed.manualOverlay, warpLinks, allowLayerMismatch);
    MergeEdgeCommand* centerMergeCmdPtr = centerMergeCmd.get();
    composite->Add(std::move(centerMergeCmd));

    for (const Guid& node : memberNodes) {
        std::unique_ptr<MergeEdgeCommand> memberMergeCmd = std::make_unique<MergeEdgeCommand>(
            node, ed.manualOverlay, warpLinks, allowLayerMismatch);
        composite->Add(std::move(memberMergeCmd));
    }

    // 結合後(kept側)の中心線のtrackEdgesは結合前のどちらか一方の中心線からそのまま
    // コピーされ、結合で消えた側のmember guidを指したままになる。生存後の各member guid
    // へ明示的に差し替える(③aの中心線後半への差し替えと対になる操作)。
    EdgePropertyDelta centerDelta;
    centerDelta.trackEdges = keptMemberGuids;
    composite->Add(std::make_unique<SetEdgePropertyCommand>(keptCenterGuid, centerDelta));

    const std::vector<Guid> selectionBefore = ed.SelectedEdges();
    if (!ed.ExecuteCommand(std::move(composite))) return std::nullopt;

    const EdgeMergeRemap remap = *centerMergeCmdPtr->MergeRemap();

    // 選択中のedgeが結合対象だった場合、結合後に残るedgeへ選択を寄せる
    // (Editor::MergeEdgeAtの単一結合と同じ後処理)。
    ed.SelectEdges(selectionBefore);
    ed.ReplaceSelectedEdge(remap.removedEdge, remap.keptEdge);
    ed.PruneSelectedEdges();
    if (ed.selectedEdge == remap.keptEdge) ed.ResetSelectedSectionIndices();

    return remap;
}

}  // namespace rs::editor
