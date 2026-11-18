//
// TrackLayingCommit.cpp - 敷設の姿勢の計画(プレビューと確定が共通で使う)
//
#include "TrackLayingCommit.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "../../world/alignment/Alignment.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;
using rs::world::TrackNode;

namespace {

// 既存ノードの姿勢: graph 上のノード、無ければ pendingNode、それも無ければ pos/tangent から合成。
TrackNode ResolveExistingNode(const rs::world::TrackGraph& graph, const LaidEdgeEnd& end) {
    const std::unordered_map<Guid, TrackNode, rs::world::GuidHash>::const_iterator it =
        graph.nodes.find(end.guid);
    if (it != graph.nodes.end()) return it->second;
    if (end.pendingNode.has_value()) return *end.pendingNode;
    TrackNode node;
    node.guid = end.guid;
    node.pos = end.pos;
    node.tangent = end.tangent.Normalized();
    node.up = Vec3(0.0f, 1.0f, 0.0f);
    return node;
}

// 新規ノードのテンプレート(接線 = 水平接線にピッチを付けたもの、up は世界Y)。
TrackNode MakeNewNode(const LaidEdgeEnd& end, const Guid& guid, float pitchRad) {
    Vec3 horizontal(end.tangent.x, 0.0f, end.tangent.z);
    horizontal = horizontal.Normalized();
    TrackNode node;
    node.guid = guid;
    node.pos = end.pos;
    node.tangent = rs::world::PitchedTangent(horizontal, pitchRad);
    node.up = Vec3(0.0f, 1.0f, 0.0f);
    return node;
}

// 既存ノードの接線を、edge の進行方向(端の向き)にそろえたときの勾配[rad]。
float EdgeDirectionPitchRad(const TrackNode& node, const Vec3& edgeHorizontalTangent) {
    const float sign = rs::world::EdgeTangentSignAgainst(node.tangent, edgeHorizontalTangent);
    return rs::world::TangentPitchRad(node.tangent * sign);
}

}  // namespace

LaidEdgePlan PlanLaidEdgePoses(const rs::world::TrackGraph& graph, const LaidEdgeInput& input) {
    LaidEdgePlan plan;
    plan.nodeAIsNew = input.a.isNew;
    plan.nodeBIsNew = input.b.isNew;
    const Guid guidA = input.a.isNew && input.a.guid == NilGuid() ? rs::world::GenerateGuid()
                                                                   : input.a.guid;
    const Guid guidB = input.b.isNew && input.b.guid == NilGuid() ? rs::world::GenerateGuid()
                                                                   : input.b.guid;

    // 勾配はノード接線のピッチで持つ。弦勾配の高さは既存ノードならその高さ。
    TrackNode existingA, existingB;
    if (!input.a.isNew) existingA = ResolveExistingNode(graph, LaidEdgeEnd{false, guidA, input.a.pos,
                                                                         input.a.tangent,
                                                                         input.a.pendingNode});
    if (!input.b.isNew) existingB = ResolveExistingNode(graph, LaidEdgeEnd{false, guidB, input.b.pos,
                                                                         input.b.tangent,
                                                                         input.b.pendingNode});
    const float aY = input.a.isNew ? input.a.pos.y : existingA.pos.y;
    const float bY = input.b.isNew ? input.b.pos.y : existingB.pos.y;
    const float chordPitch = rs::world::ChordPitchRad(bY - aY, input.horizontalLength);

    // 新しく作る側の勾配は、縦断が1本の円弧になるように決める(両端の勾配の平均 = 弦の勾配)。
    // 片側が既存ノードなら、その勾配を変えずに、反対側の新規ノードを 2×弦勾配 − 既存の勾配 にする
    // (上向きで入って水平に戻すS字にせず、そのまま下向きへ抜ける。谷も同様)。
    // 両端が新規なら弦の勾配、両端が既存ならノードの姿勢そのまま。
    float pitchA = chordPitch;
    float pitchB = chordPitch;
    if (!input.a.isNew && input.b.isNew)
        pitchB = 2.0f * chordPitch - EdgeDirectionPitchRad(existingA, input.a.tangent);
    if (input.a.isNew && !input.b.isNew)
        pitchA = 2.0f * chordPitch - EdgeDirectionPitchRad(existingB, input.b.tangent);

    plan.nodeA = input.a.isNew ? MakeNewNode(input.a, guidA, pitchA) : existingA;
    plan.nodeB = input.b.isNew ? MakeNewNode(input.b, guidB, pitchB) : existingB;

    TrackEdge& edge = plan.edge;
    edge.nodeA = guidA;
    edge.nodeB = guidB;
    edge.nodeATangentSign = rs::world::EdgeTangentSignAgainst(plan.nodeA.tangent, input.a.tangent);
    edge.nodeBTangentSign = rs::world::EdgeTangentSignAgainst(plan.nodeB.tangent, input.b.tangent);
    if (input.refit.available) edge.refit = input.refit;
    plan.valid = true;
    return plan;
}

rs::world::EdgeShapeBuild BuildLaidEdgeShape(const LaidEdgePlan& plan) {
    return rs::world::BuildEdgeShape(
        rs::world::MakeEdgeShapeInput(plan.edge, plan.nodeA, plan.nodeB));
}

LaidTracksPlan PlanLaidTracks(const rs::world::TrackGraph& graph, const LaidTracksInput& input) {
    LaidTracksPlan result;
    const int trackCount = static_cast<int>(input.offsets.size());
    const Vec3 up(0.0f, 1.0f, 0.0f);
    const Vec3 right0 = Vec3::Cross(up, input.startTangent).Normalized();
    const Vec3 right1 = Vec3::Cross(up, input.endTangent).Normalized();

    // 指定オフセットの plan を作る(offset curve が作れなければ valid=false)。
    auto planAtOffset = [&](float offset, const LaidEdgeEnd& a, const LaidEdgeEnd& b) {
        rs::world::OffsetCurveResult offsetCurve;
        float horizontalLength = 0.0f;
        if (offset == 0.0f) {
            horizontalLength = rs::world::AlignmentTotalLength(input.curve);
        } else {
            offsetCurve = rs::world::OffsetCurve(input.curve, offset);
            if (!offsetCurve.valid) return LaidEdgePlan{};
            horizontalLength = rs::world::AlignmentTotalLength(offsetCurve.curve);
        }
        LaidEdgeInput edgeInput;
        edgeInput.a = a;
        edgeInput.b = b;
        edgeInput.a.pos = input.startPos + right0 * offset;
        edgeInput.b.pos = input.endPos + right1 * offset;
        edgeInput.a.tangent = input.startTangent;
        edgeInput.b.tangent = input.endTangent;
        edgeInput.horizontalLength = horizontalLength;
        edgeInput.refit = input.refit;
        return PlanLaidEdgePoses(graph, edgeInput);
    };

    float sumOffset = 0.0f;
    bool hasZeroOffsetTrack = false;
    result.tracks.reserve(static_cast<size_t>(trackCount));
    for (int i = 0; i < trackCount; ++i) {
        const float offset = input.offsets[static_cast<size_t>(i)];
        sumOffset += offset;
        hasZeroOffsetTrack = hasZeroOffsetTrack || offset == 0.0f;

        LaidEdgeEnd a;
        const Guid startNode =
            static_cast<size_t>(i) < input.startNodes.size() ? input.startNodes[static_cast<size_t>(i)]
                                                              : NilGuid();
        a.isNew = (startNode == NilGuid());
        a.guid = startNode;
        if (i == 0) a.pendingNode = input.pendingStartNode;
        LaidEdgeEnd b;
        b.isNew = !(i == 0 && input.endNode0 != NilGuid());
        if (i == 0) b.guid = input.endNode0;
        result.tracks.push_back(planAtOffset(offset, a, b));
    }

    result.meanOffset = trackCount > 0 ? sumOffset / static_cast<float>(trackCount) : 0.0f;
    if (trackCount >= 2) {
        // 中心線の始点は、前回の中心線の終端ノードが graph に残っていれば引き継ぐ。
        LaidEdgeEnd a;
        a.isNew = !(input.centerStartNode != NilGuid() &&
                    graph.nodes.find(input.centerStartNode) != graph.nodes.end());
        if (!a.isNew) a.guid = input.centerStartNode;
        result.center = planAtOffset(result.meanOffset, a, LaidEdgeEnd{});
        result.hasCenter = result.center.valid;
    }
    if (trackCount >= 2 && !hasZeroOffsetTrack) {
        result.guide = planAtOffset(0.0f, LaidEdgeEnd{}, LaidEdgeEnd{});
    }
    return result;
}

void ApplyCenterlineAttributes(TrackEdge& edge, int trackNum, float trackSpacing,
                               const std::vector<std::string>& trackPlugins,
                               const std::vector<Guid>& trackEdges) {
    edge.guid = rs::world::GenerateGuid();
    edge.centerlineOnly = true;
    edge.trackCount = trackNum;
    edge.trackSpacing = trackSpacing;
    rs::world::SetEdgeFolders(edge, trackPlugins);
    edge.trackEdges = trackEdges;
}

std::string LaidTracksRejection(const LaidTracksPlan& plan) {
    const bool multi = plan.tracks.size() > 1;
    for (size_t i = 0; i < plan.tracks.size(); ++i) {
        const LaidEdgePlan& track = plan.tracks[i];
        if (track.valid && BuildLaidEdgeShape(track).ok) continue;
        if (!multi) return "線の形を作れません";
        return std::to_string(i + 1) + "本目の線の形を作れません(半径や線間隔を見直してください)";
    }
    if (plan.hasCenter && !BuildLaidEdgeShape(plan.center).ok)
        return "複線の中心線の形を作れません(半径や線間隔を見直してください)";
    return std::string();
}

}  // namespace rs::editor
