//
// NodeDragPlan.cpp - NodeDragPlan.h の実装
//
#include "NodeDragPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../../core/Epsilon.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "../../world/geometry/EditorGeometryTuning.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "NodeDragCenterline.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

bool IsCenterlineMember(const TrackGraph& graph, const Guid& edgeGuid) {
    for (const auto& kv : graph.edges) {
        if (!kv.second.centerlineOnly) continue;
        for (const Guid& m : kv.second.trackEdges)
            if (m == edgeGuid) return true;
    }
    return false;
}

bool CanMovePlainNode(const TrackGraph& graph, const Guid& nodeGuid) {
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return false;
    if (nit->second.links.empty()) return false;
    return !NodeHasCenterlineEdge(graph, nodeGuid);
}

namespace {

using NodeMap = std::unordered_map<Guid, TrackNode, rs::world::GuidHash>;

// 計画の途中状態: 変更後の姿勢(変更順)と、その姿勢で作った edge の形。
struct PlanState {
    const TrackGraph* graph = nullptr;
    std::vector<NodePoseChange> changes;
    NodeMap poses;
    std::unordered_set<Guid, rs::world::GuidHash> keepEdgeSignsNodes;
    rs::world::EdgeShapesUnderNodePoses shapes;

    void Add(const NodePoseChange& change) {
        changes.push_back(change);
        TrackNode node = graph->nodes.at(change.node);
        node.pos = change.pos;
        node.tangent = change.tangent;
        node.up = change.up;
        poses[change.node] = node;
        if (change.keepEdgeSigns) keepEdgeSignsNodes.insert(change.node);
    }
    // 現在の姿勢で全edgeの形を作れるか。
    bool Verify() {
        shapes = rs::world::BuildEdgeShapesUnderNodePoses(*graph, poses, keepEdgeSignsNodes);
        return shapes.ok;
    }
};


// 次数1のノードが2端のどちらか(nodeIsA)と、その端・相手端の符号。
struct TerminusEdge {
    const TrackEdge* edge = nullptr;
    const TrackNode* far = nullptr;
    bool nodeIsA = false;
    float signSelf = 1.0f;  // このノード側の端の符号
    float signFar = 1.0f;
};

bool FindTerminusEdge(const TrackGraph& graph, const Guid& nodeGuid, const TrackNode& node,
                      TerminusEdge& out) {
    if (node.links.size() != 1) return false;
    const auto eit = graph.edges.find(node.links.front().edge);
    if (eit == graph.edges.end()) return false;
    const TrackEdge& e = eit->second;
    if (e.nodeA == e.nodeB) return false;
    out.edge = &e;
    out.nodeIsA = (e.nodeA == nodeGuid);
    const auto farIt = graph.nodes.find(out.nodeIsA ? e.nodeB : e.nodeA);
    if (farIt == graph.nodes.end()) return false;
    out.far = &farIt->second;
    out.signSelf = out.nodeIsA ? e.nodeATangentSign : e.nodeBTangentSign;
    out.signFar = out.nodeIsA ? e.nodeBTangentSign : e.nodeATangentSign;
    return true;
}

// 終端ノードを newPos へ動かしたときの、その位置での edge 進行方向(水平単位ベクトル)と、
// その単一円弧の半径(直線なら無限大)。
// 相手端の進行方向に接して newPos を通る単一円弧の、このノード側の端の向き(弦を軸に
// 相手端の進行方向を折り返したもの)。ノードの旧接線や旧位置に依存しないので、位置を動かすと
// 向きも滑らかに変わる。作れなければ false。
bool TerminusTravelDirection(const TerminusEdge& te, const Vec3& newPos, Vec3& out, float& radius) {
    Vec3 farH;
    if (!HorizontalUnit(te.far->tangent * te.signFar, farH)) return false;
    const Vec3 farPos = te.far->pos;
    // 弦は進行方向(A→B)にそろえる。
    const Vec3 chordRaw = te.nodeIsA ? Vec3(farPos.x - newPos.x, 0.0f, farPos.z - newPos.z)
                                     : Vec3(newPos.x - farPos.x, 0.0f, newPos.z - farPos.z);
    Vec3 chord;
    if (!HorizontalUnit(chordRaw, chord)) return false;
    // 弦長 L と、弦と相手端の進行方向の角 θ から、半径 R = L / (2 sinθ)。
    const float sinTheta = std::fabs(farH.x * chord.z - farH.z * chord.x);
    radius = sinTheta > rs::core::kDirectionParallelEps ? chordRaw.Length() / (2.0f * sinTheta)
                              : std::numeric_limits<float>::infinity();
    return HorizontalUnit(chord * (2.0f * Vec3::Dot(farH, chord)) - farH, out);
}

// 終端ノードを newPos へ動かしたときの新しい接線。水平方向は相手端に接する単一円弧、ピッチは新しい弦勾配
// (水平弧長は単一円弧の長さ、取れなければ弦の水平長)。edge 端の符号は変えない
// (接線 = 進行方向 × 符号)。作れなければ旧接線のまま。
// 単一円弧の半径が最小半径を下回る(緩和曲線なしのedge)ときは false(動かせない)。
bool TerminusTangent(const TrackGraph& graph, const Guid& nodeGuid, const TrackNode& node,
                     const Vec3& newPos, Vec3& tangent) {
    tangent = node.tangent;
    TerminusEdge te;
    if (!FindTerminusEdge(graph, nodeGuid, node, te)) return true;
    Vec3 direction;
    float arcRadius = 0.0f;
    if (!TerminusTravelDirection(te, newPos, direction, arcRadius)) return true;
    const bool hasTransition = te.edge->refit.available && te.edge->refit.transitionLength > 0.0f;
    if (!hasTransition && arcRadius < rs::world::EditorGeometryTuning::Embedded().minArcRadiusMeters)
        return false;

    // 進行方向の始端(A)と終端(B)の位置・水平向きから、水平弧長を求める。
    Vec3 farH;
    if (!HorizontalUnit(te.far->tangent * te.signFar, farH)) return true;
    const Vec3 posA = te.nodeIsA ? newPos : te.far->pos;
    const Vec3 posB = te.nodeIsA ? te.far->pos : newPos;
    const Vec3 dirA = te.nodeIsA ? direction : farH;
    const Vec3 dirB = te.nodeIsA ? farH : direction;
    float horizontalLength = std::hypot(posB.x - posA.x, posB.z - posA.z);
    rs::world::ThreePointArcSolveResult sol;
    Vec3 startTangent;
    if (rs::world::TryRefitThreePointArc(Vec3(posA.x, 0.0f, posA.z), dirA, 1.0f,
                                         Vec3(posB.x, 0.0f, posB.z), dirB, rs::world::TransitionSpec{},
                                         sol, startTangent)) {
        const float arcLength = rs::world::AlignmentTotalLength(sol.curve);
        if (arcLength > rs::core::kGeometryLengthEpsM) horizontalLength = arcLength;
    }
    const float pitch = rs::world::ChordPitchRad(posB.y - posA.y, horizontalLength);
    tangent = rs::world::PitchedTangent(direction, pitch) * te.signSelf;
    return true;
}

// 計画の仕上げ: 操作したノードにつながる edge の形と、切り離すメンバーを集める。
NodeDragPlan Finish(const PlanState& state, const Guid& primary, bool detachMembers) {
    NodeDragPlan plan;
    plan.ok = true;
    plan.nodes = state.changes;
    const std::vector<Guid> direct = rs::world::CollectEdgesForNode(*state.graph, primary);
    for (const std::pair<Guid, rs::world::EdgeShapeBuild>& entry : state.shapes.edges) {
        if (std::find(direct.begin(), direct.end(), entry.first) != direct.end())
            plan.edges.push_back({entry.first, entry.second.spline});
        if (!detachMembers) continue;
        const TrackEdge& edge = state.graph->edges.at(entry.first);
        if (!edge.detachedFromCenterline && IsCenterlineMember(*state.graph, entry.first))
            plan.detachEdges.push_back(entry.first);
    }
    return plan;
}

NodeDragPlan Fail(const std::string& message) {
    NodeDragPlan plan;
    plan.error = message;
    return plan;
}

// 操作するノードの共通の前提確認と、中心線ノードならメンバーの追従計画。
// 通常ノード/中心線ノードのどちらでもなければ error を返す。
bool BeginPlan(const TrackGraph& graph, const NodePoseChange& primary,
               PlanState& state, bool& isCenterline, std::string& error) {
    const Guid& nodeGuid = primary.node;
    isCenterline = NodeHasCenterlineEdge(graph, nodeGuid);
    if (isCenterline ? !IsCenterlineNodeMovable(graph, nodeGuid)
                     : !CanMovePlainNode(graph, nodeGuid)) {
        error = "このノードは動かせない";
        return false;
    }
    state.graph = &graph;
    state.Add(primary);
    if (isCenterline) {
        std::vector<NodePoseChange> followers;
        if (!PlanCenterlineMemberFollow(graph, nodeGuid, primary, followers)) {
            error = "中心線のメンバーを追従させられない";
            return false;
        }
        for (const NodePoseChange& follower : followers) state.Add(follower);
    }
    if (!state.Verify()) {
        error = state.shapes.error;
        return false;
    }
    return true;
}

} // namespace

NodeDragPlan PlanNodeMove(const TrackGraph& graph, const Guid& nodeGuid, const Vec3& requestedPos) {
    // 高さは許容範囲へ収める。
    Vec3 newPos = requestedPos;
    newPos.y = std::clamp(newPos.y, kMinNodeElevationM, kMaxNodeElevationM);
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return Fail("ノードが無い");
    const TrackNode& node = nit->second;
    Vec3 tangent = node.tangent;
    if (node.links.size() == 1 && !TerminusTangent(graph, nodeGuid, node, newPos, tangent))
        return Fail("曲線が急すぎる(半径が最小半径未満)");

    PlanState state;
    bool isCenterline = false;
    std::string error;
    // 終端は、接線が edge 端の符号込みで進行方向を表すよう決めてあるので、符号を反転させない。
    const bool keepSigns = node.links.size() == 1;
    if (!BeginPlan(graph, {nodeGuid, newPos, tangent, node.up, keepSigns}, state, isCenterline, error))
        return Fail(error);
    return Finish(state, nodeGuid, !isCenterline);
}

NodeDragPlan PlanNodeElevation(const TrackGraph& graph, const Guid& nodeGuid, float elevation) {
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return Fail("ノードが無い");
    const TrackNode& node = nit->second;
    Vec3 newPos = node.pos;
    newPos.y = std::clamp(elevation, kMinNodeElevationM, kMaxNodeElevationM);
    // 標高だけを変える。接線・上方向はそのまま。
    const NodePoseChange primary{nodeGuid, newPos, node.tangent, node.up};

    PlanState state;
    // 孤立ノードにはedgeの形の確認が要らない。位置だけを変える。
    if (node.links.empty()) {
        state.graph = &graph;
        state.Add(primary);
        return Finish(state, nodeGuid, false);
    }
    bool isCenterline = false;
    std::string error;
    if (!BeginPlan(graph, primary, state, isCenterline, error)) return Fail(error);
    // 複線のメンバーを切り離すのは、メンバーのノードを個別に動かす操作(ドラッグ)だけ。
    return Finish(state, nodeGuid, false);
}

NodeDragPlan PlanNodeTangent(const TrackGraph& graph, const Guid& nodeGuid,
                               const Vec3& newTangent) {
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return Fail("ノードが無い");
    const TrackNode& node = nit->second;
    if (node.links.empty() || node.links.size() > 2) return Fail("孤立・分岐ノードの接線は変えられない");

    PlanState state;
    bool isCenterline = false;
    std::string error;
    if (!BeginPlan(graph, {nodeGuid, node.pos, newTangent, node.up}, state, isCenterline, error))
        return Fail(error);
    return Finish(state, nodeGuid, !isCenterline);
}

} // namespace rs::editor
