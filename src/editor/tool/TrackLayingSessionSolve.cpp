//
// TrackLayingSessionSolve.cpp - 敷設フロー(解く/プレビュー系)
//
// クリック状態機械(HandleClickWorldImplArc等)は TrackLayingSession.cpp、
// 確定系(CommitArc/CommitMultiTrackNodes)は TrackLayingSessionCommit.cpp、
// プレビュー構築は TrackLayingSessionPreview.cpp に分離。
// ここには座標解決・ソルバ・姿勢計画の入力の組み立てを残す。
//
#include "../../core/Epsilon.h"
#include "TrackLayingSession.h"

#include <algorithm>
#include <cmath>

#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "Editor.h"
#include "EditorPlacementSolution.h"
#include "TrackLayingCommit.h"

namespace rs::editor {

using rs::core::Mat4;
using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;

bool SolveEndNodeConnection(const Vec3& startPos, const Vec3& startTangent, const Vec3& endPos,
                            const Vec3& endDir, const rs::world::TransitionSpec& spec,
                            PlacementSolution& out) {
    const Vec3 candidates[2] = {endDir, endDir * -1.0f};
    // まず単一円弧(緩和曲線つき)。
    for (const Vec3& t1cand : candidates) {
        Vec3 intersection;
        if (!rs::world::IntersectTangentRays(startPos, startTangent, endPos, t1cand, intersection))
            continue;
        const rs::world::ThreePointArcSolveResult sol =
            rs::world::SolveThreePointArc(startPos, intersection, endPos, spec);
        out.haveSolveInfo = true;
        out.solveInfo = sol;
        if (sol.valid && sol.acceptable) {
            out.curve = sol.curve;
            out.endTangent = t1cand;
            out.usedMid = intersection;
            out.solved = true;
            out.endSnappedToNode = true;
            return true;
        }
    }
    // 単一円弧が不成立: 平行同方向(渡り線)ならbiarc(S字)で救済する。
    for (const Vec3& t1cand : candidates) {
        const rs::world::BiarcSolveResult bsol =
            rs::world::SolveBiarc(startPos, startTangent, endPos, t1cand);
        if (!(bsol.valid && bsol.acceptable)) continue;
        out.curve = bsol.curve;
        out.endTangent = bsol.endTangent;
        out.usedMid = bsol.joint;
        out.isBiarc = true;
        out.biarcJoint = bsol.joint;
        out.solved = true;
        out.endSnappedToNode = true;
        // R/θ表示用に ThreePointArcSolveResult を合成。
        out.haveSolveInfo = true;
        out.solveInfo = rs::world::ThreePointArcSolveResult{};
        out.solveInfo.valid = true;
        out.solveInfo.acceptable = true;
        out.solveInfo.isStraightFallback = false;
        const float sgn = (bsol.theta >= 0.0f) ? 1.0f : -1.0f;
        out.solveInfo.radius = sgn * bsol.radius;
        out.solveInfo.theta = bsol.theta;
        out.solveInfo.endTangent = bsol.endTangent;
        return true;
    }
    return false;
}

rs::world::TrackEdge::Refit MakeRefitFromSolution(const PlacementSolution& sln,
                                                  const TrackLayingSettings& settings) {
    rs::world::TrackEdge::Refit refit;
    // 3点フィレット由来(非biarc・非直線)のみ有効。直線は頂点C復元(接線交点)が
    // できないため後編集対象外とする。
    if (!sln.isBiarc && sln.haveSolveInfo && !sln.solveInfo.isStraightFallback) {
        refit.shape = settings.transShape;
        refit.transitionLength = sln.solveInfo.transLen;
        refit.available = true;
    }
    return refit;
}

bool TrackLayingSession::ProjectMidpointOnTangentRay(const Vec3& rawPos, Vec3& outMid) const {
    Vec3 t0(startTangent_.x, 0.0f, startTangent_.z);
    float t0Len = t0.Length();
    if (t0Len < rs::core::kDegenerateVectorEps) return false;
    t0 = t0 * (1.0f / t0Len);
    Vec3 diff(rawPos.x - placeStartPos_.x, 0.0f, rawPos.z - placeStartPos_.z);
    float s = Vec3::Dot(diff, t0);
    // edge途中からの分岐は、始点の前後どちらへも伸ばせる(後ろ側は s が負)。
    const bool branchFromEdge = pendingSplitEdge_ != NilGuid();
    if (s < kMinMidpointDistanceM && !(branchFromEdge && s <= -kMinMidpointDistanceM)) return false;
    outMid = placeStartPos_ + t0 * s;
    return true;
}

bool TrackLayingSession::ResolveStartTangent(Vec3& out) const {
    if (haveStartTangent_) {
        out = startTangent_;
        return true;
    }
    Vec3 t0raw(midPos_.x - placeStartPos_.x, 0.0f, midPos_.z - placeStartPos_.z);
    float l = t0raw.Length();
    if (l > rs::core::kTrackPositionEpsM) {
        out = t0raw * (1.0f / l);
        return true;
    }
    return false;
}

bool TrackLayingSession::SolvePlacement(const Editor& ed, const Vec3& startPos,
                                        const Vec3& startTangent, const Vec3& midPos,
                                        const Vec3& hitPos, const Guid& snappedNode,
                                        PlacementSolution& out) const {
    out = PlacementSolution{};
    out.usedMid = midPos;
    out.endPos = hitPos;
    Vec3 up(0, 1, 0);

    const bool hasSnap = (snappedNode != NilGuid());
    if (hasSnap) {
        auto it = ed.graph.nodes.find(snappedNode);
        if (it != ed.graph.nodes.end()) out.endPos = it->second.pos;
        bool nodeDirValid = (it != ed.graph.nodes.end() && it->second.tangent.Length() > rs::core::kDegenerateVectorEps);
        if (nodeDirValid) {
            // 終点ノードスナップ: ユーザー指定の中間点は捨て、始点接線レイと
            // 終点接線(±node.tangent)レイの交点を新たな中間点として解く。折れた接続を
            // 絶対に敷かないため、交点/円弧が求まらなければ確定拒否。
            Vec3 ndRaw(it->second.tangent.x, 0.0f, it->second.tangent.z);
            float ndLen = ndRaw.Length();
            if (ndLen > rs::core::kDegenerateVectorEps) {
                Vec3 nd = ndRaw * (1.0f / ndLen);
                SolveEndNodeConnection(startPos, startTangent, out.endPos, nd,
                                       ed.layingSettings.CurrentTransitionSpec(), out);
                if (!out.solved) out.endSnapUnreachable = true;
            }
        }
        return out.solved;
    }

    // 非スナップ: 中間点C確定済み。C->hitPos方向で3点円弧を解く。
    Vec3 mid = midPos;
    if (ed.layingSettings.AngleSnapActive()) {
        // rawのthetaをスナップし、C->hitPos方向をスナップ後の向きへ置き換える。
        // (raw は theta を得るためだけなので緩和曲線なしで解く: 緩和曲線起因の
        //  確定拒否で theta 自体が取れなくなるのを避ける)
        rs::world::ThreePointArcSolveResult raw = rs::world::SolveThreePointArc(startPos, mid, hitPos);
        if (raw.valid) {
            float theta = rs::world::SnapThetaToStep(raw.theta, ed.layingSettings.angleSnapDeg);
            Vec3 t0(startTangent.x, 0.0f, startTangent.z);
            float t0Len = t0.Length();
            if (t0Len > rs::core::kDegenerateVectorEps) {
                t0 = t0 * (1.0f / t0Len);
                Vec3 right0 = Vec3::Cross(up, t0).Normalized();
                Vec3 snapDir = t0 * std::cos(theta) + right0 * std::sin(theta);
                Vec3 mouseDiff(hitPos.x - mid.x, 0.0f, hitPos.z - mid.z);
                float proj = std::max(Vec3::Dot(mouseDiff, snapDir), 0.05f);
                Vec3 p1Snapped = mid + snapDir * proj;
                p1Snapped.y = hitPos.y;  // 水平位置だけスナップし、高さはカーソルの高さにする
                rs::world::ThreePointArcSolveResult sol =
                    rs::world::SolveThreePointArc(startPos, mid, p1Snapped, ed.layingSettings.CurrentTransitionSpec());
                out.haveSolveInfo = true;
                out.solveInfo = sol;
                if (sol.valid && sol.acceptable) {
                    out.curve = sol.curve;
                    out.endTangent = sol.endTangent;
                    out.endPos = p1Snapped;
                    out.solved = true;
                }
            }
        }
    } else {
        rs::world::ThreePointArcSolveResult sol =
            rs::world::SolveThreePointArc(startPos, mid, hitPos, ed.layingSettings.CurrentTransitionSpec());
        out.haveSolveInfo = true;
        out.solveInfo = sol;
        if (sol.valid && sol.acceptable) {
            out.curve = sol.curve;
            out.endTangent = sol.endTangent;
            out.endPos = hitPos;
            out.solved = true;
        }
    }
    return out.solved;
}


LaidTracksInput TrackLayingSession::BuildLaidTracksInput(const Editor& ed,
                                                         const PlacementSolution& sln,
                                                         const Vec3& startTangentUsed,
                                                         const Guid& snappedNode,
                                                         const Guid& splitNodeGuid) const {
    LaidTracksInput in;
    in.curve = sln.curve;
    in.startPos = placeStartPos_;
    in.startTangent = startTangentUsed;
    in.endPos = sln.endPos;
    in.endTangent = sln.endTangent;
    in.endNode0 = snappedNode;
    in.centerStartNode = centerlineStartNode_;

    // 本数・オフセットは activeTrackCount_/parallelOffsets_ を唯一の情報源とする
    // (新規点開始/既存ノード開始のどちらでも敷設開始時に確定済み)。
    const size_t trackCount = static_cast<size_t>(std::max(1, activeTrackCount_));
    in.offsets.assign(trackCount, 0.0f);
    in.startNodes.assign(trackCount, NilGuid());
    for (size_t i = 0; i < trackCount; ++i) {
        if (i < parallelOffsets_.size()) in.offsets[i] = parallelOffsets_[i];
        if (i >= 1 && i - 1 < parallelStartNodes_.size()) in.startNodes[i] = parallelStartNodes_[i - 1];
    }
    in.startNodes[0] = placeStartNode_;
    if (pendingSplitEdge_ != NilGuid()) {
        // 分岐敷設: track0 の始点は確定時に作る分割ノード。graph にはまだ無いので、
        // 分割位置の姿勢(分割の組み立てと同じ関数)を既存ノードの姿勢として渡す。
        in.startNodes[0] = splitNodeGuid;
        const std::unordered_map<Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator it =
            ed.graph.edges.find(pendingSplitEdge_);
        if (it != ed.graph.edges.end())
            in.pendingStartNode = rs::world::MakeSplitMiddleNode(it->second, pendingSplitS_, splitNodeGuid);
    }

    // 緩和曲線の後編集用の指定(edge.refit)。
    in.refit = MakeRefitFromSolution(sln, ed.layingSettings);
    return in;
}

} // namespace rs::editor
