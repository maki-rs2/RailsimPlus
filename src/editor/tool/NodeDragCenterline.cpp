//
// NodeDragCenterline.cpp - NodeDragCenterline.h の実装
//
#include "NodeDragCenterline.h"
#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

namespace {

Vec3 RightOf(const Vec3& horizontalUnit) {
    return Vec3::Cross(Vec3(0.0f, 1.0f, 0.0f), horizontalUnit).Normalized();
}

} // namespace

bool NodeHasCenterlineEdge(const TrackGraph& graph, const Guid& nodeGuid) {
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return false;
    for (const TrackNode::Link& link : nit->second.links) {
        const auto eit = graph.edges.find(link.edge);
        if (eit != graph.edges.end() && rs::world::IsCenterlineOnlyEdge(eit->second)) return true;
    }
    return false;
}

bool IsCenterlineNodeMovable(const TrackGraph& graph, const Guid& nodeGuid) {
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return false;
    const std::vector<TrackNode::Link>& links = nit->second.links;
    if (links.empty()) return false;
    for (const TrackNode::Link& link : links) {
        const auto eit = graph.edges.find(link.edge);
        if (eit == graph.edges.end()) return false;
        const TrackEdge& e = eit->second;
        if (!e.centerlineOnly) return false;  // 中心線以外のedgeと共有するノードは対象外
        if (e.trackEdges.size() < 2) return false;  // 追従できるメンバーが無い
        const bool nodeIsCenterA = (e.nodeA == nodeGuid);
        for (const Guid& memberGuid : e.trackEdges) {
            const auto mIt = graph.edges.find(memberGuid);
            if (mIt == graph.edges.end()) return false;
            const TrackEdge& member = mIt->second;
            const auto nearIt = graph.nodes.find(nodeIsCenterA ? member.nodeA : member.nodeB);
            if (nearIt == graph.nodes.end()) return false;
            // 切り離し済みメンバーは追従しない。近い側ノードが他のedge(連続敷設のジャンクション)と
            // 共有されていると、切り離し側は動かさず、追従側は動かすという矛盾になるので拒否する。
            if (member.detachedFromCenterline && nearIt->second.links.size() != 1) return false;
        }
    }
    return true;
}

bool PlanCenterlineMemberFollow(const TrackGraph& graph, const Guid& center,
                                const NodePoseChange& newPose, std::vector<NodePoseChange>& out) {
    const auto cit = graph.nodes.find(center);
    if (cit == graph.nodes.end()) return false;
    const TrackNode& c = cit->second;

    // 新しい接線の向きは旧接線と同じ側にそろえてから使う(符号の表現が反転しても、メンバーが
    // 中心線の同じ側に残るように)。
    Vec3 oldH, newH;
    if (!HorizontalUnit(c.tangent, oldH) || !HorizontalUnit(newPose.tangent, newH)) return false;
    if (Vec3::Dot(newH, oldH) < 0.0f) newH = newH * -1.0f;
    const Vec3 rightOld = RightOf(oldH);
    const Vec3 rightNew = RightOf(newH);

    std::vector<Guid> done;
    for (const TrackNode::Link& link : c.links) {
        const auto eit = graph.edges.find(link.edge);
        if (eit == graph.edges.end()) return false;
        const TrackEdge& e = eit->second;
        const bool nearIsA = (e.nodeA == center);
        for (const Guid& memberGuid : e.trackEdges) {
            const auto mIt = graph.edges.find(memberGuid);
            if (mIt == graph.edges.end()) return false;
            const TrackEdge& member = mIt->second;
            if (member.detachedFromCenterline) continue;
            const Guid nearGuid = nearIsA ? member.nodeA : member.nodeB;
            if (std::find(done.begin(), done.end(), nearGuid) != done.end()) continue;
            done.push_back(nearGuid);
            const auto nIt = graph.nodes.find(nearGuid);
            if (nIt == graph.nodes.end()) return false;
            const TrackNode& m = nIt->second;

            const float offset = Vec3::Dot(m.pos - c.pos, rightOld);
            NodePoseChange change;
            change.node = nearGuid;
            change.pos = newPose.pos + rightNew * offset;
            change.pos.y = newPose.pos.y + (m.pos.y - c.pos.y);
            change.tangent = Vec3::Dot(newPose.tangent, m.tangent) < 0.0f ? newPose.tangent * -1.0f
                                                                           : newPose.tangent;
            change.up = m.up;
            out.push_back(change);
        }
    }
    return true;
}

} // namespace rs::editor
