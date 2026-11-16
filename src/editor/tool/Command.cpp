#include "Command.h"

#include "../../world/scene/SceneRegistry.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/AutoCant.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "EdgeCommandLinks.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::SceneRegistry;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

// ============================= RemoveEdgeCommand =============================
RemoveEdgeCommand::RemoveEdgeCommand(Guid edgeGuid) : edgeGuid_(edgeGuid) {}

void RemoveEdgeCommand::Apply(TrackGraph& graph) {
    auto it = graph.edges.find(edgeGuid_);
    if (it == graph.edges.end()) {
        hadSavedEdge_ = false;
        return;
    }
    savedEdge_ = it->second;
    hadSavedEdge_ = true;

    // RemoveLinkFromNodeで書き換える前に、両端点nodeの完全な状態を保存する。
    auto itA = graph.nodes.find(savedEdge_.nodeA);
    hadNodeA_ = itA != graph.nodes.end();
    if (hadNodeA_) savedNodeA_ = itA->second;

    // 自己ループedge(nodeA==nodeB)ではB側の保存・RemoveLinkFromNode呼び出しを
    // 重複させない(RemoveLinkFromNodeはedgeGuid_に一致する全リンクを一度に
    // 除去するため、同一nodeへの2回目の呼び出しは不要)。
    const bool selfLoop = savedEdge_.nodeB == savedEdge_.nodeA;
    hadNodeB_ = false;
    if (!selfLoop) {
        auto itB = graph.nodes.find(savedEdge_.nodeB);
        hadNodeB_ = itB != graph.nodes.end();
        if (hadNodeB_) savedNodeB_ = itB->second;
    }



    savedLinkNodes_.clear();
    for (const Guid& linkNode : savedEdge_.linkNodes) {
        if (linkNode == rs::world::NilGuid() || linkNode == savedEdge_.nodeA ||
            linkNode == savedEdge_.nodeB)
            continue;
        const auto itL = graph.nodes.find(linkNode);
        if (itL == graph.nodes.end()) continue;
        if (std::ranges::any_of(savedLinkNodes_, [&](const TrackNode& n) { return n.guid == linkNode; }))
            continue;
        savedLinkNodes_.push_back(itL->second);

    }
    rs::world::TrackGraphReferenceChange change;
    change.firstEdge = edgeGuid_;
    savedReferences_ = rs::world::RemapTrackGraphReferences(graph, change);

    graph.edges.erase(it);

    const auto stillReferenced = [&graph](const Guid& node) {
        return std::ranges::any_of(graph.edges, [&node](const auto& entry) {
            const TrackEdge& edge = entry.second;
            return edge.nodeA == node || edge.nodeB == node ||
                   edge.linkNodes[0] == node || edge.linkNodes[1] == node;
        });
    };

    // links が空になった端点node（孤立node）はグラフから削除する。完全な状態は
    // 既にsavedNodeA_/savedNodeB_へ保存済みなので、ここでは削除するだけでよい。
    if (hadNodeA_) {
        auto itA2 = graph.nodes.find(savedEdge_.nodeA);
        if (itA2 != graph.nodes.end() && itA2->second.links.empty() && !stillReferenced(savedEdge_.nodeA))
            graph.nodes.erase(itA2);
    }
    if (hadNodeB_) {
        auto itB2 = graph.nodes.find(savedEdge_.nodeB);
        if (itB2 != graph.nodes.end() && itB2->second.links.empty() && !stillReferenced(savedEdge_.nodeB))
            graph.nodes.erase(itB2);
    }
    //残る側の自動カントの隣は、つなぎ目の相手が変わるので作り直す。
    rs::world::RebuildAutoCantAtNodes(graph, {savedEdge_.nodeA, savedEdge_.nodeB});
}

void RemoveEdgeCommand::Revert(TrackGraph& graph) {
    if (!hadSavedEdge_) return;
    graph.edges[savedEdge_.guid] = savedEdge_;
    // 削除前の完全なnode状態(links順序・activeSlot含む)をそのまま復元する。
    if (hadNodeA_) graph.nodes[savedNodeA_.guid] = savedNodeA_;
    if (hadNodeB_) graph.nodes[savedNodeB_.guid] = savedNodeB_;
    for (const TrackNode& node : savedLinkNodes_) graph.nodes[node.guid] = node;
    rs::world::RestoreTrackGraphReferences(graph, savedReferences_);
    rs::world::RebuildAutoCantAtNodes(graph, {savedEdge_.nodeA, savedEdge_.nodeB});
}

// ========================== EraseOrphanNodeCommand ===========================
EraseOrphanNodeCommand::EraseOrphanNodeCommand(Guid nodeGuid) : nodeGuid_(nodeGuid) {}

void EraseOrphanNodeCommand::Apply(TrackGraph& graph) {
    auto it = graph.nodes.find(nodeGuid_);
    hadNode_ = it != graph.nodes.end();
    if (!hadNode_) return;
    savedNode_ = it->second;
    graph.nodes.erase(it);
}

void EraseOrphanNodeCommand::Revert(TrackGraph& graph) {
    if (!hadNode_) return;
    graph.nodes[nodeGuid_] = savedNode_;
}

// =========================== SetPointLinksCommand =============================
SetPointLinksCommand::SetPointLinksCommand(Guid nodeGuid, PointLinksDelta delta)
    : nodeGuid_(nodeGuid), delta_(delta) {}

void SetPointLinksCommand::Apply(TrackGraph& graph) {
    auto it = graph.nodes.find(nodeGuid_);
    if (it == graph.nodes.end()) { hadOld_ = false; return; }
    TrackNode& node = it->second;
    oldValues_.activeSlot = node.activeSlot;
    changed_ = node.activeSlot != delta_.activeSlot;
    node.activeSlot = delta_.activeSlot;
    hadOld_ = true;
}

void SetPointLinksCommand::Revert(TrackGraph& graph) {
    if (!hadOld_) return;
    auto it = graph.nodes.find(nodeGuid_);
    if (it == graph.nodes.end()) return;
    it->second.activeSlot = oldValues_.activeSlot;
}

// ============================== MoveNodeCommand ==============================
MoveNodeCommand::MoveNodeCommand(Guid nodeGuid, rs::core::Vec3 newPos,
                                  rs::core::Vec3 newDir, rs::core::Vec3 newUp,
                                  bool keepEdgeSigns)
    : nodeGuid_(nodeGuid), newPos_(newPos), newDir_(newDir), newUp_(newUp),
      keepEdgeSigns_(keepEdgeSigns) {}

void MoveNodeCommand::Apply(TrackGraph& graph) {
    auto it = graph.nodes.find(nodeGuid_);
    if (it == graph.nodes.end()) {
        hadOld_ = false;
        return;
    }
    oldPos_ = it->second.pos;
    oldDir_ = it->second.tangent;
    oldUp_ = it->second.up;
    const auto differs = [](const rs::core::Vec3& a, const rs::core::Vec3& b) {
        return a.x != b.x || a.y != b.y || a.z != b.z;
    };
    changed_ = differs(oldPos_, newPos_) || differs(oldDir_, newDir_) || differs(oldUp_, newUp_);
    // Redoでもう一度Applyされるので毎回取り直す。
    savedSections_ =
        rs::world::CaptureEdgeSections(graph, rs::world::CollectEdgesForNode(graph, nodeGuid_));
    hadOld_ = true;

    savedSigns_ =
        rs::world::CaptureEdgeTangentSigns(graph, rs::world::CollectEdgesForNode(graph, nodeGuid_));
    it->second.pos = newPos_;
    it->second.tangent = newDir_;
    it->second.up = newUp_;
    // 接線が反対向きになったら、つながる edge の進行方向を保つよう符号を反転する。
    if (!keepEdgeSigns_)
        rs::world::KeepEdgeSignsForTangentChange(graph, nodeGuid_, oldDir_, newDir_);

    // ノードの姿勢が変わったので接続 edge の spline を再生成する。
    rs::world::RebuildEdgesForNode(graph, nodeGuid_);
}

void MoveNodeCommand::Revert(TrackGraph& graph) {
    if (!hadOld_) return;
    auto it = graph.nodes.find(nodeGuid_);
    if (it == graph.nodes.end()) return;
    it->second.pos = oldPos_;
    it->second.tangent = oldDir_;
    it->second.up = oldUp_;
    rs::world::RestoreEdgeTangentSigns(graph, savedSigns_);

    rs::world::RebuildEdgesForNode(graph, nodeGuid_);
    // 再構築が内部で走らせるRescaleEdgeSectionsに上書きされないよう、必ず後に戻す。
    rs::world::RestoreEdgeSections(graph, savedSections_);
}

// =========================== SetEdgePropertyCommand ===========================
SetEdgePropertyCommand::SetEdgePropertyCommand(Guid edgeGuid, EdgePropertyDelta delta)
    : edgeGuid_(edgeGuid), delta_(std::move(delta)) {}

void SetEdgePropertyCommand::Apply(TrackGraph& graph) {
    auto it = graph.edges.find(edgeGuid_);
    if (it == graph.edges.end()) {
        hadOld_ = false;
        return;
    }
    TrackEdge& e = it->second;
    oldValues_ = EdgePropertyDelta{};
    savedSections_ = rs::world::CaptureEdgeSections(graph, {edgeGuid_});
    changed_ = false;
    changed_ |= ApplyOptionalField(delta_.speedLimitKmh, e.speedLimitKmh, oldValues_.speedLimitKmh);
    changed_ |= ApplyOptionalField(delta_.cantA, e.cantA, oldValues_.cantA);
    changed_ |= ApplyOptionalField(delta_.cantB, e.cantB, oldValues_.cantB);
    changed_ |= ApplyOptionalField(delta_.cantMid, e.cantMid, oldValues_.cantMid);
    changed_ |= ApplyOptionalField(delta_.cantTransitionRatio, e.cantTransitionRatio,
                       oldValues_.cantTransitionRatio);
    changed_ |= ApplyOptionalField(delta_.cantPoints, e.cantPoints, oldValues_.cantPoints);
    changed_ |= ApplyOptionalField(delta_.cantAuto, e.cantAuto, oldValues_.cantAuto);
    changed_ |= ApplyOptionalField(delta_.cantAutoMax, e.cantAutoMax, oldValues_.cantAutoMax);
    changed_ |= ApplyOptionalField(delta_.pluginSections, e.pluginSections, oldValues_.pluginSections);
    changed_ |= ApplyOptionalField(delta_.refit, e.refit, oldValues_.refit);
    changed_ |= ApplyOptionalField(delta_.trackEdges, e.trackEdges, oldValues_.trackEdges);
    changed_ |= ApplyOptionalField(delta_.detachedFromCenterline, e.detachedFromCenterline,
                       oldValues_.detachedFromCenterline);
    hadOld_ = true;

    // カント/緩和曲線の指定が変わったら spline を再生成する（速度制限と装飾は幾何に影響しない）。
    if (delta_.cantA.has_value() || delta_.cantB.has_value() || delta_.cantMid.has_value() ||
        delta_.cantTransitionRatio.has_value() || delta_.cantPoints.has_value() ||
        delta_.cantAuto.has_value() ||
        delta_.cantAutoMax.has_value() || delta_.refit.has_value()) {
        if (!rs::world::RebuildEdgeSpline(graph, e) && delta_.refit.has_value()) {
            // 緩和曲線の指定で形を作れなければ、指定ごと取り消す。
            spdlog::warn("[edge] 緩和曲線の指定で形を作れないため変更を取り消し");
            Revert(graph);
            hadOld_ = false;
        } else {
            //自動カントの隣のつなぎ目を追従させる。
            rs::world::RebuildAutoCantAround(graph, {edgeGuid_});
        }
    }
}

void SetEdgePropertyCommand::Revert(TrackGraph& graph) {
    if (!hadOld_) return;
    auto it = graph.edges.find(edgeGuid_);
    if (it == graph.edges.end()) return;
    TrackEdge& e = it->second;
    RestoreOptionalField(oldValues_.speedLimitKmh, e.speedLimitKmh);
    RestoreOptionalField(oldValues_.cantA, e.cantA);
    RestoreOptionalField(oldValues_.cantB, e.cantB);
    RestoreOptionalField(oldValues_.cantMid, e.cantMid);
    RestoreOptionalField(oldValues_.cantTransitionRatio, e.cantTransitionRatio);
    RestoreOptionalField(oldValues_.cantPoints, e.cantPoints);
    RestoreOptionalField(oldValues_.cantAuto, e.cantAuto);
    RestoreOptionalField(oldValues_.cantAutoMax, e.cantAutoMax);
    RestoreOptionalField(oldValues_.pluginSections, e.pluginSections);
    RestoreOptionalField(oldValues_.refit, e.refit);
    RestoreOptionalField(oldValues_.trackEdges, e.trackEdges);
    RestoreOptionalField(oldValues_.detachedFromCenterline, e.detachedFromCenterline);

    if (oldValues_.cantA.has_value() || oldValues_.cantB.has_value() ||
        oldValues_.cantMid.has_value() || oldValues_.cantTransitionRatio.has_value() ||
        oldValues_.cantPoints.has_value() ||
        oldValues_.cantAuto.has_value() || oldValues_.cantAutoMax.has_value() ||
        oldValues_.refit.has_value()) {
        rs::world::RebuildEdgeSpline(graph, e);
        rs::world::RebuildAutoCantAround(graph, {edgeGuid_});
    }

    // 区間分割はここで正典化する。上の個別復元(pluginSections等)より後、かつ
    // RebuildEdgeSplineより後でなければならない(再構築が内部でRescaleEdgeSectionsを
    // 走らせるため)。
    rs::world::RestoreEdgeSections(graph, savedSections_);
}

// ============================== CompositeCommand ==============================
void CompositeCommand::Add(std::unique_ptr<ICommand> cmd) {
    cmds_.push_back(std::move(cmd));
}

bool CompositeCommand::Empty() const { return cmds_.empty(); }

void CompositeCommand::Apply(TrackGraph& graph) {
    appliedCount_ = 0;
    applied_ = false;
    for (const std::unique_ptr<ICommand>& command : cmds_) {
        ++appliedCount_;
        command->Apply(graph);
        if (!command->Applied()) {
            Revert(graph);
            return;
        }
    }
    applied_ = true;
}

void CompositeCommand::Revert(TrackGraph& graph) {
    while (appliedCount_ > 0) cmds_[--appliedCount_]->Revert(graph);
}

void CompositeCommand::Apply(SceneRegistry& registry) {
    for (const std::unique_ptr<ICommand>& command : cmds_) command->Apply(registry);
}

void CompositeCommand::Revert(SceneRegistry& registry) {
    for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->Revert(registry);
}

bool CompositeCommand::Applied() const {
    return applied_;
}

bool CompositeCommand::Changed() const {
    return std::ranges::any_of(cmds_, [](const std::unique_ptr<ICommand>& c) { return c->Changed(); });
}

} // namespace rs::editor
