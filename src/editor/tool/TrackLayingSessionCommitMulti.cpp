//
// TrackLayingSessionCommitMulti.cpp - 複線×S字接続の一括確定処理
//
// TrackLayingSessionCommit.cpp から分離(400行制約のため)。終点ノード列
// (selectedEndNodes_)が activeTrackCount_ 個揃ったときに呼ばれる
// CommitMultiTrackNodesを担う。
//
#include "../../core/Epsilon.h"
#include "TrackLayingSession.h"

#include <algorithm>
#include <cmath>

#include "../../world/geometry/GeometrySolvers.h"
#include "Editor.h"
#include "EditorPlacementSolution.h"
#include "TrackLayingCommit.h"
#include "../../world/alignment/EdgeShapeBuild.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;
using rs::world::TrackNode;

namespace {

// 複線中心線edge(CommitArcと同じ、全trackの平均オフセット)を composite へ足す。
// 中心線の形を作れないときは reason を設定して false を返す(全体拒否)。
// track0(curve0)はここでは既存node間を独立に解いた1本にすぎず、他トラックとの
// 平行性(同心弧)は厳密には成り立たない(直線・単純弧では実質厳密、急な複合曲線
// 差異があれば近似)。CommitArcと同じ既知の近似であり新規の精度課題ではない。
bool AddCenterlineEdge(Editor& ed, int n, float meanOff,
                       const std::vector<rs::world::AlignmentSegment>& curve0,
                       const rs::world::TrackEdge::Refit& refit0,
                       const Vec3& startPos0, const Vec3& startDir0, const Guid& endNode0,
                       const Vec3& endTangent0, const std::vector<Guid>& trackEdges,
                       CompositeCommand& composite, std::string& reason) {
    const rs::world::OffsetCurveResult centerOc = rs::world::OffsetCurve(curve0, meanOff);
    if (!centerOc.valid) return true;

    const Vec3 up(0, 1, 0);
    Vec3 t0raw(startDir0.x, 0.0f, startDir0.z);
    Vec3 t0 = t0raw * (1.0f / t0raw.Length());
    Vec3 right0 = Vec3::Cross(up, t0).Normalized();
    Vec3 right1 = Vec3::Cross(up, endTangent0).Normalized();

    LaidEdgeInput input;
    input.a.pos = startPos0 + right0 * meanOff;
    input.a.tangent = t0;
    input.b.pos = ed.graph.nodes.at(endNode0).pos + right1 * meanOff;
    input.b.tangent = endTangent0;
    input.horizontalLength = rs::world::AlignmentTotalLength(centerOc.curve);
    input.refit = refit0;  // 単線の中心線と同じく、track0 の緩和曲線の指定を引き継ぐ
    const LaidEdgePlan plan = PlanLaidEdgePoses(ed.graph, input);
    if (!plan.valid || !BuildLaidEdgeShape(plan).ok) {  // 中心線も形を作れなければ全体拒否
        reason = "複線の中心線の形を作れません";
        return false;
    }

    TrackEdge center = plan.edge;
    ApplyCenterlineAttributes(center, n, ed.layingSettings.trackSpacing,
                              ed.layingSettings.defaultTrackPlugins, trackEdges);
    composite.Add(std::make_unique<AddEdgeCommand>(center, true, plan.nodeA, true, plan.nodeB));
    return true;
}

} // namespace

void TrackLayingSession::CommitMultiTrackNodes(Editor& ed) {
    int n = activeTrackCount_;
    if (int(selectedEndNodes_.size()) != n) return;
    commitRejectReason_.clear();
    // どの線がなぜ敷けないかを理由に残して全体を拒否する(何も敷設しない)。
    const auto reject = [this](int lineIndex, const char* why) {
        commitRejectReason_ = std::to_string(lineIndex + 1) + "本目の線: " + why;
    };

    // 始点列: [0]=placeStartNode_, i>=1: parallelStartNodes_[i-1]。全て既存node。
    std::vector<Guid> startNodes(static_cast<size_t>(n), NilGuid());
    startNodes[0] = placeStartNode_;
    for (int i = 1; i < n; ++i) {
        size_t idx = size_t(i - 1);
        startNodes[size_t(i)] = (idx < parallelStartNodes_.size()) ? parallelStartNodes_[idx]
                                                                    : NilGuid();
    }
    std::vector<Vec3> startPos(static_cast<size_t>(n)), startDir(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto it = ed.graph.nodes.find(startNodes[size_t(i)]);
        if (it == ed.graph.nodes.end()) {  // 始点nodeが無い: 敷設・選択状態維持で拒否
            reject(i, "始点のノードが見つかりません");
            return;
        }
        startPos[size_t(i)] = it->second.pos;
        startDir[size_t(i)] = it->second.tangent;
    }

    std::vector<PlacementSolution> slns(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        Vec3 p0 = startPos[size_t(i)];
        Vec3 t0raw(startDir[size_t(i)].x, 0.0f, startDir[size_t(i)].z);
        float t0Len = t0raw.Length();
        if (t0Len < rs::core::kDegenerateVectorEps) {
            reject(i, "始点の向きが決まりません");
            return;
        }
        Vec3 t0 = t0raw * (1.0f / t0Len);

        auto endIt = ed.graph.nodes.find(selectedEndNodes_[size_t(i)]);
        if (endIt == ed.graph.nodes.end()) {
            reject(i, "終点のノードが見つかりません");
            return;
        }
        Vec3 p1 = endIt->second.pos;
        Vec3 endDirRaw(endIt->second.tangent.x, 0.0f, endIt->second.tangent.z);
        float endDirLen = endDirRaw.Length();
        if (endDirLen < rs::core::kDegenerateVectorEps) {
            reject(i, "終点の向きが決まりません");
            return;
        }
        Vec3 endDirNorm = endDirRaw * (1.0f / endDirLen);

        if (!SolveEndNodeConnection(p0, t0, p1, endDirNorm, ed.layingSettings.CurrentTransitionSpec(),
                                    slns[size_t(i)])) {  // 1ペアでも失敗: 全体拒否(敷設・選択状態維持)
            reject(i, "始点と終点をなめらかに結べません");
            return;
        }
    }

    std::unique_ptr<CompositeCommand> composite = std::make_unique<CompositeCommand>();
    std::vector<Guid> trackEdges; // 中心線edge が平均元にする実レールedge群
    trackEdges.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        // 始点・終点とも既存ノード: ノード接線(ピッチ込み)をそのまま使う。端の符号は
        // 敷設時の始点・終点の接線(t0・解の endTangent)とノード接線の内積から決まる。
        LaidEdgeInput input;
        input.a.isNew = false;
        input.a.guid = startNodes[size_t(i)];
        input.a.tangent = startDir[size_t(i)];
        input.b.isNew = false;
        input.b.guid = selectedEndNodes_[size_t(i)];
        input.b.tangent = slns[size_t(i)].endTangent;
        input.refit = MakeRefitFromSolution(slns[size_t(i)], ed.layingSettings);
        const LaidEdgePlan plan = PlanLaidEdgePoses(ed.graph, input);
        if (!plan.valid || !BuildLaidEdgeShape(plan).ok) {  // 形を作れない: 全体拒否(敷設・選択状態維持)
            reject(i, "形を作れません");
            return;
        }

        TrackEdge edge = plan.edge;
        edge.guid = rs::world::GenerateGuid();
        rs::world::SetEdgeFolders(edge, ed.layingSettings.defaultTrackPlugins);
        trackEdges.push_back(edge.guid);
        composite->Add(std::make_unique<AddEdgeCommand>(edge, false, plan.nodeA, false, plan.nodeB));
    }

    // ---- 複線中心線edge ----
    if (n >= 2) {
        float sumOff = 0.0f;
        for (int i = 0; i < n; ++i)
            sumOff += (size_t(i) < parallelOffsets_.size()) ? parallelOffsets_[size_t(i)]
                                                              : 0.0f;
        const float meanOff = sumOff / static_cast<float>(n);
        if (!AddCenterlineEdge(ed, n, meanOff, slns[0].curve,
                               MakeRefitFromSolution(slns[0], ed.layingSettings), startPos[0],
                               startDir[0], selectedEndNodes_[0], slns[0].endTangent, trackEdges, *composite,
                               commitRejectReason_)) {
            return;
        }
    }

    if (!ed.ExecuteCommand(std::move(composite))) return;
    // 敷設が1区間終わったら、ノードの選択を外す(続きの敷設は placeStartNode_ が持つ)。
    ed.ClearSelectedNode();
    ed.NotePlaced();

    // 確定後: 終端到達なので敷設をリセットする。
    placing_ = false;
    haveMidpoint_ = false;
    haveStartTangent_ = false;
    selectedStartNodes_.clear();
    selectedEndNodes_.clear();
    parallelStartNodes_.clear();
    parallelOffsets_.clear();
    activeTrackCount_ = 1;
    existingNodeStart_ = false;
    placeStartNode_ = NilGuid();
}

} // namespace rs::editor
