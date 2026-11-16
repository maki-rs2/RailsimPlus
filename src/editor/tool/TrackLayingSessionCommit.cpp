//
// TrackLayingSessionCommit.cpp - 敷設ツールの確定処理（グラフへのAddEdgeCommand投入、単線/複線）
//
// TrackLayingSession.cpp（クリック状態機械）から呼ばれる、単線/複線の確定コミット
// (CommitArc)を担う。複線×S字接続の一括確定(CommitMultiTrackNodes)は
// TrackLayingSessionCommitMulti.cpp へ分離(400行制約のため)。
//
#include "TrackLayingSession.h"

#include <algorithm>
#include <cmath>

#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "Editor.h"
#include "EditorPlacementSolution.h"
#include "EdgeSplitMergeCommand.h"
#include "TrackLayingCommit.h"
#include "../../world/alignment/EdgeShapeBuild.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;
using rs::world::TrackNode;

void TrackLayingSession::CommitArc(Editor& ed, const Vec3& p1, const Guid& snappedNode) {
    commitRejectReason_.clear();
    Vec3 startTangentUsed;
    if (!ResolveStartTangent(startTangentUsed)) return; // 始点接線が求まらない: 確定不可

    // プレビューと同一のソルバで解く。確定拒否時は敷設状態を維持。
    PlacementSolution sln;
    if (!SolvePlacement(ed, placeStartPos_, startTangentUsed, midPos_, p1, snappedNode, sln))
        return;

    std::unique_ptr<CompositeCommand> composite = std::make_unique<CompositeCommand>();
    // track0の始点node。分岐敷設では確定直前に作る分割ノード(確定が拒否されたときに
    // セッションの状態を書き換えないよう、メンバへは代入しない)。
    Guid splitNode = NilGuid();

    // スペース+edgeクリックでの分岐敷設開始。確定直前に元edgeをsで分割し、
    // 新設nodeをtrack0の始点として使う。確定直前に再検証する(他操作で分割不能に
    // なっていないか、二重防御。形を作れるかもここで確かめる)。
    if (pendingSplitEdge_ != NilGuid()) {
        auto splitEdgeIt = ed.graph.edges.find(pendingSplitEdge_);
        if (splitEdgeIt == ed.graph.edges.end() ||
            rs::world::CheckEdgeSplit(ed.graph, splitEdgeIt->second, pendingSplitS_) !=
                rs::world::EdgeSplitRefusal::None) {
            return;  // 拒否、敷設状態維持
        }
        std::unique_ptr<SplitEdgeCommand> splitCmd = std::make_unique<SplitEdgeCommand>(
            pendingSplitEdge_, pendingSplitS_, ed.manualOverlay);
        splitNode = splitCmd->NewNodeGuid();  // 以降のtrack0構築を既存node起点として扱う
        composite->Add(std::move(splitCmd));
    }

    // 姿勢の計画はプレビューと同じ関数。形を作れない edge が1本でもあれば確定拒否
    // (プレビューが不成立を示すのと同じ入力・同じ関数で事前確認する)。
    const LaidTracksPlan plan =
        PlanLaidTracks(ed.graph, BuildLaidTracksInput(ed, sln, startTangentUsed, snappedNode, splitNode));
    commitRejectReason_ = LaidTracksRejection(plan);
    if (!commitRejectReason_.empty()) return;  // 1本でも作れなければ全体拒否・敷設状態維持
    std::vector<Guid> trackEdges; // 中心線edge が平均元にする実レールedge群
    std::vector<Guid> newEndNodes;
    trackEdges.reserve(plan.tracks.size());
    for (const LaidEdgePlan& track : plan.tracks) {
        TrackEdge edge = track.edge;
        edge.guid = rs::world::GenerateGuid();
        rs::world::SetEdgeFolders(edge, ed.layingSettings.defaultTrackPlugins);
        trackEdges.push_back(edge.guid);
        newEndNodes.push_back(edge.nodeB);
        composite->Add(std::make_unique<AddEdgeCommand>(edge, track.nodeAIsNew, track.nodeA,
                                                         track.nodeBIsNew, track.nodeB));
    }

    // 分岐(ポイント)化の明示処理は不要。
    // 「その面に2本目のlinkが刺さった」時点で自動的に分岐になり、
    // activeSlot の既定値0が「先にあった link が開通したまま」を表すため、
    // 既存の直進経路は黙って避けられない。既存ポイントへの4本目(X字)追加も
    // 同じ規則でそのまま成立する。

    // ---- 複線中心線edge(複線一括構造物の配置基準) ----
    // N>=2 のとき、実レール群の平均オフセット位置に中心線edgeを1本追加する。
    // 複線一括仕様(プラグイン
    // trackNum>1)の橋脚/架線柱はこの中心線へ配置される。極端な小半径等で
    // 中心線のoffset curveが作れなかった場合(plan.hasCenter==false)のみ中心線を作らない
    // (実レール敷設は成功させる)。実体を持つ実ノード(AddEdgeCommand)で作るため、
    // 中心線編集で残存レールが追従できる。連続敷設で前回の中心線終端nodeを引き継ぐと、
    // 隣接する中心線edge同士が1つのjunction nodeを共有する(centerlineStartNode_)。
    Guid newCenterlineEndNode = NilGuid();
    if (plan.hasCenter) {
        TrackEdge center = plan.center.edge;
        ApplyCenterlineAttributes(center, static_cast<int>(plan.tracks.size()),
                                  ed.layingSettings.trackSpacing,
                                  ed.layingSettings.defaultTrackPlugins, trackEdges);
        composite->Add(std::make_unique<AddEdgeCommand>(center, plan.center.nodeAIsNew,
                                                         plan.center.nodeA, true, plan.center.nodeB));
        newCenterlineEndNode = center.nodeB;
    }

    if (!ed.ExecuteCommand(std::move(composite))) return;
    ed.NotePlaced();

    centerlineStartNode_ = newCenterlineEndNode; // N<2または非valid時はNilへ戻り鎖が途切れる

    // 敷設は終点から継続する（連続敷設）。終端接線を次の始点接線に引き継ぎ、
    // 平行線(track1..N-1)の終端nodeも次回の開始nodeとして引き継ぐ。
    placing_ = true;
    placeStartPos_ = sln.endPos;
    placeStartNode_ = newEndNodes[0];
    startTangent_ = sln.endTangent;
    haveStartTangent_ = true;
    haveMidpoint_ = false;
    parallelStartNodes_.assign(newEndNodes.begin() + 1, newEndNodes.end());
    pendingSplitEdge_ = NilGuid();
}

} // namespace rs::editor
