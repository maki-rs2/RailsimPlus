//
// EditorPlacementInput.cpp - 敷設ツールのスクリーン座標入力（OnClickScreen）
//
// スクリーン座標→ワールド操作の変換は Editor の責務。変換後のクリック状態機械
// (HandleClickWorldImplArc)は TrackLayingSession(TrackLayingSession.cpp)へ委譲する。
//
#include "Editor.h"

#include "../../world/alignment/TrackTraversal.h"

namespace rs::editor {

using rs::core::Mat4;
using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;

namespace {

// Alt+クリックの分割が拒否されたときの表示文(CheckEdgeSplitの拒否理由に対応)。
// Noneは「判定は通ったが中心線グループの同期分割などで失敗した」場合の汎用文。
const char* SplitRefusalReasonJa(rs::world::EdgeSplitRefusal refusal) {
    switch (refusal) {
    case rs::world::EdgeSplitRefusal::OutOfRange: return "分割できません: この位置はedgeの端に近すぎます";
    case rs::world::EdgeSplitRefusal::MultiTrackInvolved: return "分割できません: 複線の中心線に関わるedgeです";
    case rs::world::EdgeSplitRefusal::ShapeUnsolvable: return "分割できません: 分割後の形を両端の姿勢から作れません";
    case rs::world::EdgeSplitRefusal::HasSeam: return "分割できません: 駅の番線につながる継ぎ目を持つedgeです";
    case rs::world::EdgeSplitRefusal::None: break;
    }
    return "分割できません: この位置では分割できませんでした";
}

}  // namespace

void Editor::OnClickScreen(float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                           uint32_t height) {
    if (mode != ToolMode::PlaceArc) return;

    Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return;
    PlacementSnap snap;
    // 始点を置く前だけ、edge上(カーブ開始点/スプライン上)へのスナップで分岐を始められる。
    if (ResolvePlacementSnap(ray, screenX, screenY, viewProj, width, height,
                             /*allowEdgeSnap=*/!IsPlacingArc(), snap) &&
        (snap.kind == SnapKind::CurveStart || snap.kind == SnapKind::OnSpline)) {
        if (StartBranchFromEdge(snap.edge, snap.edgeS)) return;
        // 分岐を始められないedge(複線の中心線等)は通常のクリックとして扱う。
    }
    Guid snapNode;
    Vec3 hitPos;
    if (!ResolvePlacementHit(ray, screenX, screenY, viewProj, width, height, hitPos, snapNode)) return;
    HandleClickWorldImplArc(hitPos, snapNode);
}

bool Editor::ResolvePlacementHit(const Ray& ray, float screenX, float screenY, const Mat4& viewProj,
                                 uint32_t width, uint32_t height, Vec3& hitPos,
                                 Guid& snapNode) const {
    PlacementSnap snap;
    if (!ResolvePlacementSnap(ray, screenX, screenY, viewProj, width, height,
                              /*allowEdgeSnap=*/false, snap)) return false;
    hitPos = snap.pos;
    snapNode = snap.kind == SnapKind::Node ? snap.node : NilGuid();
    return true;
}

bool Editor::ResolvePlacementSnap(const Ray& ray, float screenX, float screenY, const Mat4& viewProj,
                                  uint32_t width, uint32_t height, bool allowEdgeSnap,
                                  PlacementSnap& out) const {
    std::vector<PlacementSnap> candidates;
    // スナップ解除中は、ノード・並走位置・edge上の候補をすべて集めず、敷設面の点だけにする。
    const bool snapOff = layingSettings.snapDisabledHeld;
    const Guid nodeGuid =
        snapOff ? NilGuid() : PickNode(ray, screenX, screenY, viewProj, width, height);
    if (nodeGuid != NilGuid()) {
        const std::unordered_map<Guid, rs::world::TrackNode, rs::world::GuidHash>::const_iterator it =
            graph.nodes.find(nodeGuid);
        if (it == graph.nodes.end()) return false;
        PlacementSnap nodeSnap;
        nodeSnap.kind = SnapKind::Node;
        nodeSnap.pos = it->second.pos;
        nodeSnap.node = nodeGuid;
        candidates.push_back(nodeSnap);
    }

    Vec3 groundHit;
    const bool haveGround = layingSettings.RayHitPlacementPoint(ray, groundHit);
    bool edgeHit = false;
    if (allowEdgeSnap && !snapOff) {
        double s = 0.0;
        const Guid edgeGuid = PickEdgeS(ray, screenX, screenY, viewProj, width, height, s,
                                        kEdgeSnapRadiusPx, /*skipCenterlineOnly=*/true);
        const std::unordered_map<Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
            graph.edges.find(edgeGuid);
        if (edgeIt != graph.edges.end()) {
            PlacementSnap onSpline;
            onSpline.kind = SnapKind::OnSpline;
            onSpline.edge = edgeGuid;
            onSpline.edgeS = s;
            double boundaryS = 0.0;
            if (SnapToCurveBoundary(edgeIt->second, s, kCurveStartSnapRadiusM, boundaryS)) {
                onSpline.kind = SnapKind::CurveStart;
                onSpline.edgeS = boundaryS;
            }
            onSpline.pos = rs::world::EvaluateAt(edgeIt->second, onSpline.edgeS, +1).pos;
            candidates.push_back(onSpline);
            edgeHit = true;
        }
    }
    // edge上の判定に当たっているときは、そちらを優先する(並走位置は、edgeから離れた所でだけ使う)。
    if (haveGround && !snapOff && !edgeHit) {
        PlacementSnap parallel;
        if (FindParallelSnap(graph, groundHit, layingSettings.trackSpacing, kParallelSnapToleranceM,
                             parallel)) {
            candidates.push_back(parallel);
        }
    }

    out = ChooseSnap(candidates);
    // 既存のノード・edge上の点はすでにある位置なのでそのまま。地面との交点から決めた点
    // (並走位置・なにもない所)だけをSurfaceの範囲内へ押し戻す(RailSim II と同じ、ピンの座標が対象)。
    if (out.kind != SnapKind::None) {
        if (out.kind == SnapKind::Parallel) ClipToSurface(out.pos);
        return true;
    }
    if (!haveGround) return false;
    out.pos = groundHit;
    ClipToSurface(out.pos);
    return true;
}

namespace {

// edge上の点の位置(レール頭の高さ)。マーカーの描画位置に使う。
rs::core::Vec3 EdgeMarkerPos(const rs::world::TrackEdge& edge, double s) {
    const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge, s, +1);
    return frame.pos + frame.up * edge.railTopLift;
}

}  // namespace

Editor::SnapCursor Editor::FindSnapCursor(float screenX, float screenY, const Mat4& viewProj,
                                          uint32_t width, uint32_t height) const {
    SnapCursor cursor;
    if (mode != ToolMode::PlaceArc) return cursor;
    Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return cursor;
    PlacementSnap snap;
    if (!ResolvePlacementSnap(ray, screenX, screenY, viewProj, width, height,
                              /*allowEdgeSnap=*/!IsPlacingArc(), snap) ||
        snap.kind == SnapKind::CurveStart || snap.kind == SnapKind::OnSpline) {
        return cursor;
    }
    cursor.visible = true;
    cursor.kind = snap.kind;
    cursor.pos = snap.pos;
    const std::unordered_map<Guid, rs::world::TrackNode, rs::world::GuidHash>::const_iterator nodeIt =
        graph.nodes.find(snap.node);
    if (snap.kind == SnapKind::Node && nodeIt != graph.nodes.end()) {
        cursor.pos = NodeRailTopPos(nodeIt->second);
    } else {
        cursor.pos.y += layingSettings.placeRailTopLift;
    }
    return cursor;
}

bool Editor::ResolveSplitPoint(float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                               uint32_t height, Guid& edgeGuid, double& s) const {
    Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return false;
    s = 0.0;
    edgeGuid = PickEdgeS(ray, screenX, screenY, viewProj, width, height, s,
                         kEdgeSnapRadiusPx, /*skipCenterlineOnly=*/true);
    const std::unordered_map<Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(edgeGuid);
    if (edgeIt == graph.edges.end()) return false;
    // カーブの開始点・終了点の近くなら、その位置へ寄せる(寄せた位置で分割できなければ元の位置)。
    double boundaryS = 0.0;
    if (SnapToCurveBoundary(edgeIt->second, s, kCurveStartSnapRadiusM, boundaryS) &&
        rs::world::CheckEdgeSplit(graph, edgeIt->second, boundaryS) == rs::world::EdgeSplitRefusal::None) {
        s = boundaryS;
    }
    return true;
}

bool Editor::SplitEdgeAtScreen(float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                               uint32_t height) {
    Guid edgeGuid;
    double s = 0.0;
    if (!ResolveSplitPoint(screenX, screenY, viewProj, width, height, edgeGuid, s)) return false;
    if (SplitEdgeAt(edgeGuid, s)) {
        layingSession_.ClearOperationRejected();
        return true;
    }
    layingSession_.NoteOperationRejected(
        SplitRefusalReasonJa(rs::world::CheckEdgeSplit(graph, graph.edges.at(edgeGuid), s)));
    return false;
}

Editor::EdgeMarker Editor::FindSplitMarker(float screenX, float screenY, const Mat4& viewProj,
                                           uint32_t width, uint32_t height) const {
    EdgeMarker marker;
    Guid edgeGuid;
    double s = 0.0;
    if (!ResolveSplitPoint(screenX, screenY, viewProj, width, height, edgeGuid, s)) return marker;
    const rs::world::TrackEdge& edge = graph.edges.at(edgeGuid);
    marker.visible = true;
    marker.pos = EdgeMarkerPos(edge, s);
    marker.allowed = rs::world::CheckEdgeSplit(graph, edge, s) == rs::world::EdgeSplitRefusal::None;
    return marker;
}

Editor::EdgeMarker Editor::FindBranchMarker(float screenX, float screenY, const Mat4& viewProj,
                                            uint32_t width, uint32_t height) const {
    EdgeMarker marker;
    if (mode != ToolMode::PlaceArc || IsPlacingArc()) return marker;
    Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return marker;
    PlacementSnap snap;
    if (!ResolvePlacementSnap(ray, screenX, screenY, viewProj, width, height,
                              /*allowEdgeSnap=*/true, snap) ||
        (snap.kind != SnapKind::CurveStart && snap.kind != SnapKind::OnSpline)) {
        return marker;
    }
    const rs::world::TrackEdge& edge = graph.edges.at(snap.edge);
    marker.visible = true;
    marker.pos = EdgeMarkerPos(edge, snap.edgeS);
    marker.allowed = TrackLayingSession::IsBranchableEdge(graph, snap.edge) &&
                     rs::world::CheckEdgeSplit(graph, edge, snap.edgeS) ==
                         rs::world::EdgeSplitRefusal::None;
    return marker;
}

} // namespace rs::editor
