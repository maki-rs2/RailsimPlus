//
// TrackLayingSession.cpp - 敷設ツールのクリック状態機械（HandleClickWorldImplArc等）
//
// TrackLayingSessionSolve.cpp から「解く/プレビュー」系を分離。こちらは
// 「クリックを受けて状態を進める」入力受付/状態遷移を担う。グラフへの確定
// コミット(AddEdgeCommand投入)はTrackLayingSessionCommit.cppへ分離。
//
#include "TrackLayingSession.h"

#include <algorithm>
#include <cmath>

#include "../../core/Epsilon.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "Editor.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;

namespace {

// 既存ノードから敷設を再開するとき、そのノードで新しい線が延びるべき「外向き」
// の水平接線を返す。node.tangent は常に所属edgeのA→B進行方向で格納されるため、
// 次数1の端点では link の end(0:nodeA/1:nodeB) を見て向きを解決する:
//   - end==1 (このノードがedgeの終点側) : dir は既に外向き -> +dir
//   - end==0 (このノードがedgeの始点側) : dir はedgeの内側向き -> 反転して -dir
// 次数0(孤立)や次数>=2(中間/分岐候補)では素の dir をそのまま返す(向き反転は
// 分岐編集フェーズで扱う)。戻り値は水平・正規化。dir が水平成分を持たなければ
// false。
bool ResolveOutwardTangentAtNode(const rs::world::TrackNode& node, Vec3& out) {
    Vec3 t(node.tangent.x, 0.0f, node.tangent.z);
    float len = t.Length();
    if (len < rs::core::kDegenerateVectorEps) return false;
    t = t * (1.0f / len);
    if (node.links.size() == 1 && node.links[0].end == 0) {
        t = t * -1.0f; // edgeの始点側端点 -> 外向きへ反転
    }
    out = t;
    return true;
}

} // namespace

// 中間点決定処理（HandleClickWorldImplArcの通常フロー、および既存ノード複線
// 開始で「地面クリックをそのまま中間点として処理」する経路の両方から呼ぶ）。
void TrackLayingSession::HandleMidpointClick(const Vec3& pos) {
    Vec3 mid;
    if (haveStartTangent_) {
        if (!ProjectMidpointOnTangentRay(pos, mid)) return; // s<0.5m: 無効クリック
        // 分岐を後ろ向きに伸ばすときは、始点接線を反転して以降のソルバへ前向きと同じ条件を渡す。
        const bool reversed = Vec3::Dot(mid - placeStartPos_, startTangent_) < 0.0f;
        if (reversed) startTangent_ = startTangent_ * -1.0f;
    } else {
        // 新規地面点開始: 中間点は自由。始点と同座標なら無効。
        Vec3 diff(pos.x - placeStartPos_.x, 0.0f, pos.z - placeStartPos_.z);
        if (diff.Length() < kMinMidpointDistanceM) return;
        mid = pos;
    }
    midPos_ = mid;
    haveMidpoint_ = true;
}

// 円弧ツール用クリック処理(RailSim2流3点指定)。
void TrackLayingSession::HandleClickWorldImplArc(Editor& ed, const Vec3& hitPos,
                                                 const Guid& snappedNode) {
    // 新しいクリックで、前回の拒否理由(Alt+クリック分割の拒否を含む)は消す。
    // このクリックで拒否されたら下で改めて立てる。
    startSelectionRejected_ = false;
    Vec3 pos = hitPos;
    bool hasSnap = (snappedNode != NilGuid());
    if (hasSnap) {
        auto it = ed.graph.nodes.find(snappedNode);
        if (it != ed.graph.nodes.end()) pos = it->second.pos;
    }

    if (!placing_) {
        if (hasSnap) {
            // 既存ノードへのスナップ: 選択列へトグル追加/解除。まだ敷設開始しない。
            auto sit = std::find(selectedStartNodes_.begin(), selectedStartNodes_.end(),
                                 snappedNode);
            if (sit != selectedStartNodes_.end()) {
                selectedStartNodes_.erase(sit);
            } else {
                selectedStartNodes_.push_back(snappedNode);
            }
            startSelectionRejected_ = false;
            return;
        }

        // 地面クリック。
        if (selectedStartNodes_.empty()) {
            // 新規点開始。
            placing_ = true;
            placeStartPos_ = pos;
            placeStartNode_ = NilGuid();
            haveMidpoint_ = false;
            haveStartTangent_ = false;

            activeTrackCount_ = TrackLayingSettings::ClampTrackCount(ed.layingSettings.trackCount);
            parallelOffsets_ = TrackLayingSettings::CenteredTrackOffsets(
                activeTrackCount_, ed.layingSettings.trackSpacing);
            parallelStartNodes_.clear();
            existingNodeStart_ = false;
            return;
        }

        // 既存ノード複数選択からの開始: 検証する。
        Guid node0Guid = selectedStartNodes_[0];
        auto it0 = ed.graph.nodes.find(node0Guid);
        if (it0 == ed.graph.nodes.end()) {
            startSelectionRejected_ = true;
            startSelectionRejectReason_ = "基準ノードが見つかりません";
            return;
        }
        Vec3 pos0 = it0->second.pos;
        // 基準ノードの外向き接線を解決する。始点側端点(nodeA)にスナップした
        // 場合は node.tangent が既存edgeの内側を向くため、外向きへ反転される
        // (これで敷設済みノードの始点側からも線を伸ばせる)。
        Vec3 t0;
        if (!ResolveOutwardTangentAtNode(it0->second, t0)) {
            startSelectionRejected_ = true;
            startSelectionRejectReason_ = "選択ノードの向き/前後位置が揃っていません";
            return;
        }
        Vec3 up(0, 1, 0);
        Vec3 right0 = Vec3::Cross(up, t0).Normalized();

        std::vector<float> offsets;
        offsets.reserve(selectedStartNodes_.size());
        offsets.push_back(0.0f); // index0 = 基準ノード自身
        bool ok = true;
        for (size_t i = 1; i < selectedStartNodes_.size(); ++i) {
            auto iti = ed.graph.nodes.find(selectedStartNodes_[i]);
            if (iti == ed.graph.nodes.end()) { ok = false; break; }
            Vec3 diRaw(iti->second.tangent.x, 0.0f, iti->second.tangent.z);
            float diLen = diRaw.Length();
            if (diLen <= rs::core::kDegenerateVectorEps) { ok = false; break; }
            Vec3 ti = diRaw * (1.0f / diLen);
            if (std::fabs(Vec3::Dot(ti, right0)) >= rs::core::kParallelTrackAlignmentEps) { ok = false; break; }
            Vec3 posDiff = iti->second.pos - pos0;
            Vec3 posDiffH(posDiff.x, 0.0f, posDiff.z);
            if (std::fabs(Vec3::Dot(posDiffH, t0)) >= 0.1f) { ok = false; break; }
            offsets.push_back(Vec3::Dot(posDiffH, right0));
        }

        if (!ok) {
            startSelectionRejected_ = true;
            startSelectionRejectReason_ = "選択ノードの向き/前後位置が揃っていません";
            return;
        }

        // 検証OK: 敷設開始。
        activeTrackCount_ = int(selectedStartNodes_.size());
        parallelOffsets_ = std::move(offsets);
        parallelStartNodes_.assign(selectedStartNodes_.begin() + 1, selectedStartNodes_.end());

        placeStartPos_ = pos0;
        placeStartNode_ = node0Guid;
        startTangent_ = t0;
        haveStartTangent_ = true;
        placing_ = true;
        haveMidpoint_ = false;
        selectedStartNodes_.clear();
        startSelectionRejected_ = false;
        existingNodeStart_ = true;

        // この地面クリックをそのまま中間点として処理する。
        HandleMidpointClick(pos);
        return;
    }

    if (!haveMidpoint_) {
        // クリック2: 中間点決定。
        HandleMidpointClick(pos);
        return;
    }

    // クリック3以降: 終点待ち。
    if (existingNodeStart_ && activeTrackCount_ > 1) {
        if (hasSnap) {
            commitRejectReason_.clear();
            // 終点ノード列へトグル蓄積(複線×S字接続)。
            auto eit = std::find(selectedEndNodes_.begin(), selectedEndNodes_.end(), snappedNode);
            if (eit != selectedEndNodes_.end()) {
                selectedEndNodes_.erase(eit);
            } else {
                selectedEndNodes_.push_back(snappedNode);
            }
            if (int(selectedEndNodes_.size()) == activeTrackCount_) {
                CommitMultiTrackNodes(ed); // 失敗時は選択・敷設状態を維持
            }
            return;
        }
        // 地面クリック: 前進継続(OffsetCurveベース、平行終点は新規node)。
        CommitArc(ed, pos, snappedNode);
        return;
    }

    // 単線 or 新規点開始複線。
    CommitArc(ed, pos, snappedNode);
}

void TrackLayingSession::OnClickWorld(Editor& ed, const Vec3& worldPos, float snapRadius) {
    Guid snapped = ed.FindNearestNodeWorld(worldPos, snapRadius);
    HandleClickWorldImplArc(ed, worldPos, snapped);
}

void TrackLayingSession::CancelPlacement(Editor& ed) {
    commitRejectReason_.clear();
    if (placing_ && haveMidpoint_) {
        // 終点待ち -> 中間点待ちへ1段階戻す。
        haveMidpoint_ = false;
        return;
    }
    // 中間点待ち(or直線ツール) -> 始点解除。敷設を終えるので、ノードの選択も外す。
    ed.ClearSelectedNode();
    placing_ = false;
    placeStartNode_ = NilGuid();
    haveStartTangent_ = false;
    haveMidpoint_ = false;
    parallelStartNodes_.clear();
    centerlineStartNode_ = NilGuid();
    selectedStartNodes_.clear();
    selectedEndNodes_.clear();
    parallelOffsets_.clear();
    activeTrackCount_ = 1;
    existingNodeStart_ = false;
    startSelectionRejected_ = false;
    pendingSplitEdge_ = NilGuid();
}

void TrackLayingSession::PruneInvalidReferences(const rs::world::TrackGraph& graph) {
    const auto missingNode = [&graph](const Guid& id) {
        return id != NilGuid() && !graph.nodes.contains(id);
    };
    if (missingNode(placeStartNode_) || missingNode(centerlineStartNode_) ||
        std::ranges::any_of(parallelStartNodes_, missingNode) ||
        std::ranges::any_of(selectedStartNodes_, missingNode) ||
        std::ranges::any_of(selectedEndNodes_, missingNode) ||
        (pendingSplitEdge_ != NilGuid() && !graph.edges.contains(pendingSplitEdge_))) {
        // 始点や本数の一部だけを残すと、次の確定で別の線を参照してしまう。
        *this = TrackLayingSession{};
    }
}

bool TrackLayingSession::IsBranchableEdge(const rs::world::TrackGraph& graph, const Guid& edge) {
    const std::unordered_map<Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(edge);
    if (edgeIt == graph.edges.end() || edgeIt->second.centerlineOnly) return false;
    for (const std::pair<const Guid, rs::world::TrackEdge>& kv : graph.edges) {
        const std::vector<Guid>& members = kv.second.trackEdges;
        if (std::find(members.begin(), members.end(), edge) != members.end()) return false;
    }
    return true;
}

bool TrackLayingSession::StartBranchFromEdge(Editor& ed, const Guid& edge, double s) {
    if (placing_) return false; // 敷設操作中は対象外(開始専用ジェスチャ)
    auto edgeIt = ed.graph.edges.find(edge);
    if (edgeIt == ed.graph.edges.end()) return false;
    // 複線中心線(centerlineOnly)自身、およびいずれかの中心線のメンバー実レールは
    // このジェスチャの対象外(複線中心線edge上でのスペース+クリックは対象外)。
    // CheckEdgeSplitは複線グループの同期分割に対応済みでMultiTrackInvolvedゲートを持たない(呼び出し側
    // Editor::SplitEdgeAtが同期分割を組み立てる前提)ため、ここで独立にガードする。
    if (!IsBranchableEdge(ed.graph, edge)) return false;
    const rs::world::EdgeSplitRefusal refusal =
        rs::world::CheckEdgeSplit(ed.graph, edgeIt->second, s);
    if (refusal != rs::world::EdgeSplitRefusal::None) {
        startSelectionRejected_ = true;
        startSelectionRejectReason_ = "この位置では分岐を開始できません(分割不可)";
        return false;
    }

    const rs::world::TrackFrame frame = rs::world::EvaluateAt(edgeIt->second, s, +1);

    placing_ = true;
    placeStartPos_ = frame.pos;
    placeStartNode_ = NilGuid();  // まだ新設していない(確定時にSplitEdgeCommandが発番)
    haveMidpoint_ = false;
    haveStartTangent_ = true;
    startTangent_ = frame.tangent;

    activeTrackCount_ = TrackLayingSettings::ClampTrackCount(ed.layingSettings.trackCount);
    parallelOffsets_ = TrackLayingSettings::CenteredTrackOffsets(
        activeTrackCount_, ed.layingSettings.trackSpacing);
    parallelStartNodes_.clear();
    existingNodeStart_ = false;
    startSelectionRejected_ = false;

    pendingSplitEdge_ = edge;
    pendingSplitS_ = s;
    return true;
}

} // namespace rs::editor
