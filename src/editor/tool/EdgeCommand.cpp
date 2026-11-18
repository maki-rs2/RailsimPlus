//
// EdgeCommand.cpp - AddEdgeCommand の実装(edge追加とnodeのlinks張り)
//
#include "Command.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/AutoCant.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "../../world/alignment/PointName.h"
#include "EdgeCommandLinks.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

// node の links から (edge, end) の Link を除去する。
// activeSlot は「その面での出現順」なので、link を1本抜くと同じ面の以降の
// スロット番号がずれる。両面について本数に収まる範囲へ丸める。
void RemoveLinkFromNode(TrackNode& node, const Guid& edgeGuid) {
    node.links.erase(
        std::remove_if(node.links.begin(), node.links.end(),
                        [&](const TrackNode::Link& l) { return l.edge == edgeGuid; }),
        node.links.end());
    for (int side = 0; side < 2; ++side) {
        const int maxSlot = std::max(0, node.LinkCountOnSide(side) - 1);
        int& slot = node.activeSlot[static_cast<size_t>(side)];
        slot = std::clamp(slot, 0, maxSlot);
    }
}

namespace {

// 新しいリンクの面を決める。接続済みの面は幾何から再判定しない。
void AddLinkToNode(TrackNode& node, const Guid& edgeGuid, int end,
                   const rs::core::Vec3& edgeTangent) {
    int side = rs::world::ResolveEdgeEndConnection(node, edgeGuid, end, edgeTangent).side;
    // RailSim II と同じく1面あたり最大2本を保つ(幾何が退化していても3本目を同じ面に
    // 積んで行き止まりを作らないための防御)。
    if (node.LinkCountOnSide(side) >= 2 && node.LinkCountOnSide(1 - side) < 2) side = 1 - side;
    node.links.push_back(TrackNode::Link{edgeGuid, end, side});
}

rs::core::Vec3 EndTangent(const TrackEdge& edge, const TrackNode& node, int end) {
    if (edge.spline.empty()) return node.tangent;
    return (end == 0) ? edge.spline.front().tangent : edge.spline.back().tangent;
}

} // namespace

// ============================== AddEdgeCommand ==============================
AddEdgeCommand::AddEdgeCommand(TrackEdge edge, bool nodeAIsNew, TrackNode nodeATemplate,
                                bool nodeBIsNew, TrackNode nodeBTemplate,
                                const rs::core::Vec3* laidEndTangent)
    : edge_(std::move(edge)),
      nodeAIsNew_(nodeAIsNew),
      nodeATemplate_(std::move(nodeATemplate)),
      nodeBIsNew_(nodeBIsNew),
      nodeBTemplate_(std::move(nodeBTemplate)) {
    if (laidEndTangent != nullptr) {
        hasLaidEndTangent_ = true;
        laidEndTangent_ = *laidEndTangent;
    }
}

// nodeB の符号を決める。敷設時の終端接線があればそれと node.tangent の内積、無ければ
// 渡された spline の末尾接線から決める(どちらも無ければ edge の値のまま)。
void AddEdgeCommand::ResolveNodeBSign(TrackEdge& edge, const TrackNode& nodeB) const {
    if (hasLaidEndTangent_)
        edge.nodeBTangentSign =
            rs::world::ResolveEdgeEndConnection(nodeB, edge.guid, 1, laidEndTangent_).tangentSign;
    else if (!edge.spline.empty())
        edge.nodeBTangentSign =
            rs::world::ResolveEdgeEndConnection(nodeB, edge.guid, 1, edge.spline.back().tangent).tangentSign;
}

bool AddEdgeCommand::CanApply(const TrackGraph& graph) const {
    const TrackNode* nodeA = &nodeATemplate_;
    const TrackNode* nodeB = &nodeBTemplate_;
    if (!nodeAIsNew_) {
        const auto it = graph.nodes.find(edge_.nodeA);
        if (it == graph.nodes.end()) return true;  // 未確認(別コマンドが後から作るノード)
        nodeA = &it->second;
    }
    if (!nodeBIsNew_) {
        const auto it = graph.nodes.find(edge_.nodeB);
        if (it == graph.nodes.end()) return true;
        nodeB = &it->second;
    }
    TrackEdge edge = edge_;
    ResolveNodeBSign(edge, *nodeB);
    const rs::world::EdgeShapeBuild built =
        rs::world::BuildEdgeShape(rs::world::MakeEdgeShapeInput(edge, *nodeA, *nodeB));
    if (!built.ok) spdlog::warn("[edge] 形を作れないため追加を拒否: {}", built.error);
    return built.ok;
}

void AddEdgeCommand::Apply(TrackGraph& graph) {
    applied_ = false;
    namedPointNodes_.clear();
    if (nodeAIsNew_) graph.nodes[edge_.nodeA] = nodeATemplate_;
    if (nodeBIsNew_) graph.nodes[edge_.nodeB] = nodeBTemplate_;
    TrackEdge& added = graph.edges[edge_.guid] = edge_;
    auto itB = graph.nodes.find(edge_.nodeB);
    if (itB != graph.nodes.end()) ResolveNodeBSign(added, itB->second);

    // 形は両端ノードの姿勢から作る(作り方を問わず単一の入口)。解けなければ何も追加しない。
    if (!rs::world::RebuildEdgeSpline(graph, added)) {
        graph.edges.erase(edge_.guid);
        if (nodeBIsNew_) graph.nodes.erase(edge_.nodeB);
        if (nodeAIsNew_) graph.nodes.erase(edge_.nodeA);
        return;
    }
    applied_ = true;

    // 面の判定は再構築後の spline を使う(edge_ は spline を持たないことがある)。
    const TrackEdge& built = graph.edges.at(edge_.guid);
    auto itA = graph.nodes.find(edge_.nodeA);
    if (itA != graph.nodes.end())
        AddLinkToNode(itA->second, edge_.guid, 0, EndTangent(built, itA->second, 0));
    itB = graph.nodes.find(edge_.nodeB);
    if (itB != graph.nodes.end())
        AddLinkToNode(itB->second, edge_.guid, 1, EndTangent(built, itB->second, 1));
    // このリンクで分岐になった(名前が空の)ノードに既定名を振る。Revertで戻せるよう覚える。
    for (const Guid& nodeGuid : {edge_.nodeA, edge_.nodeB}) {
        if (rs::world::AssignDefaultPointName(graph, nodeGuid)) namedPointNodes_.push_back(nodeGuid);
    }
    //つなぎ先の自動カントの edge は、相手が増えたのでつなぎ目を決め直す。
    rs::world::RebuildAutoCantAtNodes(graph, {edge_.nodeA, edge_.nodeB});
}

void AddEdgeCommand::Revert(TrackGraph& graph) {
    if (!applied_) return;
    for (const Guid& nodeGuid : namedPointNodes_) {
        const auto named = graph.nodes.find(nodeGuid);
        if (named != graph.nodes.end()) named->second.pointName.clear();
    }
    auto itA = graph.nodes.find(edge_.nodeA);
    if (itA != graph.nodes.end()) RemoveLinkFromNode(itA->second, edge_.guid);
    auto itB = graph.nodes.find(edge_.nodeB);
    if (itB != graph.nodes.end()) RemoveLinkFromNode(itB->second, edge_.guid);

    graph.edges.erase(edge_.guid);

    if (nodeBIsNew_) graph.nodes.erase(edge_.nodeB);
    if (nodeAIsNew_) graph.nodes.erase(edge_.nodeA);
    rs::world::RebuildAutoCantAtNodes(graph, {edge_.nodeA, edge_.nodeB});
}

} // namespace rs::editor
