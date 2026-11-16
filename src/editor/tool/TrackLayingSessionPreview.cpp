//
// TrackLayingSessionPreview.cpp - 敷設プレビューの構築
//
// 確定(CommitArc)と同じ姿勢の計画(PlanLaidTracks)から、同じ生成関数
// (BuildEdgeShape)で作った spline をそのまま折れ線にする。プレビューの形と確定後の
// edge.spline は同じ入力から作られるので一致する。形を作れない edge が1本でもあれば
// 確定も拒否される(確定前の事前確認が同じ関数)。
//
#include "TrackLayingSession.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>

#include "Editor.h"
#include "EditorPlacementSolution.h"
#include "TrackLayingCommit.h"

namespace rs::editor {

using rs::core::Mat4;
using rs::core::Vec3;
using rs::world::AlignmentSegment;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackNodeSample;

namespace {

// 弦(1本)の端点を spline の境界サンプルから作る。
PlacePreview::ArcChord MakeChord(const std::vector<TrackNodeSample>& spline, size_t startIndex,
                                 size_t endIndex, float radius, PlacePreview::ArcChord::Kind kind) {
    PlacePreview::ArcChord chord;
    chord.start = spline[startIndex].pos;
    chord.end = spline[endIndex].pos;
    chord.mid = (chord.start + chord.end) * 0.5f;
    chord.radius = radius;
    chord.kind = kind;
    return chord;
}

// 弧長 s に最も近いサンプルの添字。
size_t NearestSampleByS(const std::vector<TrackNodeSample>& spline, float s) {
    size_t best = 0;
    float bestDiff = std::fabs(spline[0].s - s);
    for (size_t i = 1; i < spline.size(); ++i) {
        const float diff = std::fabs(spline[i].s - s);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

// S字(双円弧): curve の Arc セグメントごとに1本ずつ弦を出す(隣り合う同じ半径の Arc は
// 1本にまとめる)。緩和曲線を含まないので、spline の s は curve の累積長と一致する。
void AppendBiarcChords(const std::vector<AlignmentSegment>& curve,
                       const std::vector<TrackNodeSample>& spline,
                       std::vector<PlacePreview::ArcChord>& chords) {
        float cumulative = 0.0f;
    bool inRun = false;
    float runStartS = 0.0f, runEndS = 0.0f, runRadius = 0.0f;
    auto flush = [&]() {
        if (!inRun) return;
        chords.push_back(MakeChord(spline, NearestSampleByS(spline, runStartS),
                                   NearestSampleByS(spline, runEndS), runRadius,
                                   PlacePreview::ArcChord::Kind::Arc));
        inRun = false;
    };
    for (const AlignmentSegment& seg : curve) {
        if (seg.kind == AlignmentSegment::Kind::Arc) {
            const bool sameRun =
                inRun && std::fabs(seg.radius - runRadius) <= rs::core::kPreviewRadiusRelativeEps * std::fabs(runRadius);
            if (!sameRun) {
                flush();
                inRun = true;
                runStartS = cumulative;
                runRadius = seg.radius;
            }
            runEndS = cumulative + seg.length;
        } else {
            flush();
        }
        cumulative += seg.length;
    }
    flush();
}

// 指定の kind が最初/最後に現れるサンプルの添字(なければ -1)。
void FindKindRange(const std::vector<TrackNodeSample>& spline,
                   bool (*matches)(AlignmentSegment::Kind), int& first, int& last) {
    first = -1;
    last = -1;
    for (size_t i = 0; i < spline.size(); ++i) {
        if (!matches(spline[i].kind)) continue;
        if (first < 0) first = static_cast<int>(i);
        last = static_cast<int>(i);
    }
}

// 3点円弧(直線+円弧、緩和曲線つきなら 直線+緩和+円弧+緩和+直線): 弧の範囲の弦を出す。
// 緩和曲線ありなら、緩和を含む全体スパン(TransitionSpan)と、円弧のみの弦(R表示なし)の2本。
// 範囲の始端は、その範囲の最初のサンプルの1つ前(前の区間の終端=境界サンプル)。
void AppendArcChords(const std::vector<AlignmentSegment>& curve,
                     const std::vector<TrackNodeSample>& spline, float radius,
                     std::vector<PlacePreview::ArcChord>& chords) {
    const bool hasTransition =
        std::any_of(curve.begin(), curve.end(), [](const AlignmentSegment& seg) {
            return seg.kind == AlignmentSegment::Kind::Transition;
        });
    int firstCurved = -1, lastCurved = -1;
    FindKindRange(spline, [](AlignmentSegment::Kind kind) { return kind != AlignmentSegment::Kind::Line; },
                  firstCurved, lastCurved);
    if (firstCurved < 0) return;
    const auto boundaryBefore = [](int firstIndex) {
        return static_cast<size_t>(std::max(firstIndex - 1, 0));
    };

    chords.push_back(MakeChord(spline, boundaryBefore(firstCurved), static_cast<size_t>(lastCurved),
                               radius,
                               hasTransition ? PlacePreview::ArcChord::Kind::TransitionSpan
                                             : PlacePreview::ArcChord::Kind::Arc));
    if (!hasTransition) return;

    // 円弧(Transitionを除く純粋な円弧)のみの区間を結ぶ弦。外側(TransitionSpan)のテキストと
    // 文字が重なるため、テキストは表示しない。
    int firstArc = -1, lastArc = -1;
    FindKindRange(spline, [](AlignmentSegment::Kind kind) { return kind == AlignmentSegment::Kind::Arc; },
                  firstArc, lastArc);
    if (firstArc < 0) return;
    PlacePreview::ArcChord arcOnly = MakeChord(spline, boundaryBefore(firstArc),
                                               static_cast<size_t>(lastArc), radius,
                                               PlacePreview::ArcChord::Kind::Arc);
    arcOnly.showRadiusText = false;
    chords.push_back(arcOnly);
}

// spline を折れ線(位置・区間の種類・up)として積む。
void AppendPolyline(const std::vector<TrackNodeSample>& spline, std::vector<Vec3>& polyline,
                    std::vector<AlignmentSegment::Kind>* kinds, std::vector<Vec3>* ups) {
    polyline.reserve(spline.size());
    for (const TrackNodeSample& sample : spline) {
        polyline.push_back(sample.pos);
        if (kinds != nullptr) kinds->push_back(sample.kind);  // 直線/緩和曲線/円弧の色分け用
        if (ups != nullptr) ups->push_back(sample.up);
    }
}

}  // namespace

PlacePreview TrackLayingSession::BuildPreview(const Editor& ed, float screenX, float screenY,
                                              const Mat4& viewProj, uint32_t width,
                                              uint32_t height) const {
    PlacePreview pv;
    if (!placing_) return pv;
    pv.active = true;
    pv.startPos = placeStartPos_;

    Vec3 hitPos;
    Guid snapNode;
    Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return pv;
    if (!ed.ResolvePlacementHit(ray, screenX, screenY, viewProj, width, height, hitPos, snapNode))
        return pv;
    return BuildPreviewAt(ed, hitPos, snapNode);
}

PlacePreview TrackLayingSession::BuildPreviewWorld(const Editor& ed, const Vec3& worldPos,
                                                   float snapRadius) const {
    const Guid snapNode = ed.FindNearestNodeWorld(worldPos, snapRadius);
    Vec3 hitPos = worldPos;
    if (snapNode != NilGuid()) hitPos = ed.graph.nodes.at(snapNode).pos;
    return BuildPreviewAt(ed, hitPos, snapNode);
}

PlacePreview TrackLayingSession::BuildPreviewAt(const Editor& ed, const Vec3& hitPos,
                                                const Guid& snapNode) const {
    PlacePreview pv;
    if (!placing_) return pv;
    pv.active = true;
    pv.startPos = placeStartPos_;

    if (!haveMidpoint_) {
        // 中間点待ち: 始点接線が既知ならレイ上へ射影した候補、未知なら自由点。
        pv.awaitingMidpoint = true;
        Vec3 mid;
        bool ok;
        if (haveStartTangent_) {
            ok = ProjectMidpointOnTangentRay(hitPos, mid);
        } else {
            mid = hitPos;
            Vec3 diff(hitPos.x - placeStartPos_.x, 0.0f, hitPos.z - placeStartPos_.z);
            ok = diff.Length() >= rs::core::kGeometryLengthEpsM;
        }
        if (!ok) {
            pv.polyline.push_back(placeStartPos_);
            return pv;
        }
        pv.polyline.push_back(placeStartPos_);
        pv.polyline.push_back(mid);
        return pv;
    }

    // 終点待ち: 中間点C確定済み。C->マウス位置(P1候補)で3点円弧を解く。
    pv.awaitingEndpoint = true;
    pv.midPos = midPos_;

    Vec3 startTangentUsed;
    if (!ResolveStartTangent(startTangentUsed)) {
        pv.polyline.push_back(placeStartPos_);
        pv.polyline.push_back(midPos_);
        return pv;
    }

    // 確定(CommitArc)と同一のソルバで解く。
    PlacementSolution sln;
    SolvePlacement(ed, placeStartPos_, startTangentUsed, midPos_, hitPos, snapNode, sln);
    pv.haveSolve = sln.haveSolveInfo;
    pv.solve = sln.solveInfo;
    pv.endSnappedToNode = sln.endSnappedToNode;
    pv.endSnapUnreachable = sln.endSnapUnreachable;

    const auto showUnsolved = [&]() {
        // 不成立: 補助線2本(P0->C, C->hitPos)を赤表示用に積む。
        pv.polyline.clear();
        pv.polylineKind.clear();
        pv.polylineUp.clear();
        pv.polyline.push_back(placeStartPos_);
        pv.polyline.push_back(sln.usedMid);
        pv.polyline.push_back(hitPos);
    };
    if (!sln.solved || sln.curve.empty()) {
        showUnsolved();
        return pv;
    }

    // 確定と同じ姿勢の計画。各 edge の形は計画の両端姿勢から BuildEdgeShape が作る。
    const Guid splitNode = pendingSplitEdge_ != NilGuid() ? rs::world::GenerateGuid() : NilGuid();
    const LaidTracksPlan plan =
        PlanLaidTracks(ed.graph, BuildLaidTracksInput(ed, sln, startTangentUsed, snapNode, splitNode));

    // 基準 curve(オフセット0)の形。オフセット0の track があればその track、無ければ(偶数本)
    // 表示用ガイド。
    const std::vector<float>& offsets = parallelOffsets_;
    const LaidEdgePlan* base = nullptr;
    if (plan.guide.has_value()) {
        base = &*plan.guide;
    } else {
        for (size_t i = 0; i < plan.tracks.size(); ++i) {
            const float offset = i < offsets.size() ? offsets[i] : 0.0f;
            if (offset == 0.0f) {
                base = &plan.tracks[i];
                break;
            }
        }
        if (base == nullptr && plan.tracks.size() == 1) base = &plan.tracks[0];
    }
    rs::world::EdgeShapeBuild baseShape;
    if (base != nullptr && base->valid) baseShape = BuildLaidEdgeShape(*base);
    if (!baseShape.ok) {
        showUnsolved();  // 基準の形を作れない: 確定も拒否される
        return pv;
    }
    AppendPolyline(baseShape.spline, pv.polyline, &pv.polylineKind, &pv.polylineUp);

    // 弧弦(R表示用)。
    const std::vector<TrackNodeSample>& spline = baseShape.spline;
    if (sln.isBiarc) {
        AppendBiarcChords(baseShape.curve, spline, pv.arcChords);
    } else if (pv.haveSolve && pv.solve.acceptable && !pv.solve.isStraightFallback) {
        AppendArcChords(baseShape.curve, spline, pv.solve.radius, pv.arcChords);
    }

    // ---- 複線(平行N本)プレビュー ----
    // 確定と同じく敷設開始時に確定した parallelOffsets_ を使う。オフセット0のtrackが無い
    // (偶数本)ときは、描画側が基準curveを描かない(referenceIsGuide)。
    if (plan.tracks.size() > 1) {
        pv.referenceIsGuide = plan.guide.has_value();
        for (size_t i = 0; i < plan.tracks.size(); ++i) {
            const float offset = i < offsets.size() ? offsets[i] : 0.0f;
            if (offset == 0.0f) continue;  // 基準curve自身がこのtrack
            rs::world::EdgeShapeBuild side;
            if (plan.tracks[i].valid) side = BuildLaidEdgeShape(plan.tracks[i]);
            if (!side.ok) continue;
            std::vector<Vec3> sidePoly;
            std::vector<AlignmentSegment::Kind> sideKinds;
            AppendPolyline(side.spline, sidePoly, &sideKinds, nullptr);
            pv.sidePolylines.push_back(std::move(sidePoly));
            pv.sidePolylineKinds.push_back(std::move(sideKinds));
        }
        // 確定と同じ判定。1本でも作れなければ確定も全体拒否される。
        pv.sideRejectReason = LaidTracksRejection(plan);
        pv.sideRejected = !pv.sideRejectReason.empty();
    }
    // 確定と同じ判定で敷設できる状態に戻ったら、前回の確定拒否の理由は消す
    // (理由をセットするCommitArc/CommitMultiTrackNodesと対。次の確定まで古い理由を出し続けない)。
    // 既存ノード起点の複線では終点をノードの明示選択で確定するため、カーソル位置のプレビューと
    // 確定の可否は結びつかない。その間は次のクリックで消す。
    const bool commitFollowsPreview = !(existingNodeStart_ && activeTrackCount_ > 1);
    if (commitFollowsPreview && !pv.sideRejected) commitRejectReason_.clear();
    return pv;
}

} // namespace rs::editor
