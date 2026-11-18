//
// TrackLayingCommit.h - 敷設の「姿勢の計画」(プレビューと確定が共通で使う)
//
// 敷設ソルバの出力(水平curve・終端接線・既存ノードの有無)から、各edgeの両端の3D姿勢
// (新規ノードの接線ピッチ=弦勾配、既存ノードはその接線のまま)を決める。確定
// (CommitArc/CommitMultiTrackNodes)とプレビュー(BuildPreview)は、ここで作った計画を
// そのまま使う。edge の形は計画の両端姿勢から BuildEdgeShape が作る単一の入口で、
// 確定では AddEdgeCommand::Apply が同じ入力で RebuildEdgeSpline を呼ぶ。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "../../world/alignment/AlignmentSegment.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/TrackEdge.h"
#include "../../world/alignment/TrackNode.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

// 計画する edge の1端の入力。
struct LaidEdgeEnd {
    bool isNew = true;  // true: 新規ノードを作る / false: 既存ノードにつなぐ
    rs::world::Guid guid = rs::world::NilGuid();  // 新規でNilなら計画が発番する
    rs::core::Vec3 pos;      // 新規ノードの位置
    rs::core::Vec3 tangent;  // この端での edge 進行方向の接線(水平でなくてよい)
    // 既存だが graph にまだ無いノード(分岐敷設で確定時に作られる分割ノード)の姿勢。
    // 無ければ pos/tangent から合成する。
    std::optional<rs::world::TrackNode> pendingNode;
};

struct LaidEdgeInput {
    LaidEdgeEnd a;
    LaidEdgeEnd b;
    float horizontalLength = 0.0f;  // 水平 curve の弧長(新規ノードの弦勾配の計算にだけ使う)
    rs::world::TrackEdge::Refit refit;  // available のときだけ edge.refit に入れる
};

// edge 1本分の姿勢の計画。edge は nodeA/nodeB・符号・refit だけを持つ(guid・プラグイン・
// 複線の属性は呼び出し側が足す)。
struct LaidEdgePlan {
    bool valid = false;  // false: 計画できなかった(水平長が得られない等)
    rs::world::TrackEdge edge;
    rs::world::TrackNode nodeA;  // 新規はテンプレート、既存は graph 上のコピー
    rs::world::TrackNode nodeB;
    bool nodeAIsNew = true;
    bool nodeBIsNew = true;
};

// 新規ノードの接線 = 水平接線に弦勾配のピッチを付けたもの(up は世界Y)。既存ノードへ
// つなぐ側はそのノードの接線(ピッチ込み)をそのまま使い、edge 端の符号は
// node.tangent と edge 進行方向の内積の符号で決める。
LaidEdgePlan PlanLaidEdgePoses(const rs::world::TrackGraph& graph, const LaidEdgeInput& input);

// 計画の両端姿勢から edge の形を作る(プレビューと確定前の事前確認が同じ入口)。
rs::world::EdgeShapeBuild BuildLaidEdgeShape(const LaidEdgePlan& plan);

// 敷設で作る edge 群(複線の実レール N 本と、N>=2 のときの中心線)の計画の入力。
struct LaidTracksInput {
    std::vector<rs::world::AlignmentSegment> curve;  // 基準 curve(オフセット0)
    rs::core::Vec3 startPos;
    rs::core::Vec3 startTangent;
    rs::core::Vec3 endPos;
    rs::core::Vec3 endTangent;
    std::vector<float> offsets;           // 各 track の横オフセット(size = 本数)
    std::vector<rs::world::Guid> startNodes;  // 各 track の始点ノード(size = 本数。Nilなら新規)
    std::optional<rs::world::TrackNode> pendingStartNode;  // startNodes[0] が graph に無いときの姿勢
    rs::world::Guid endNode0 = rs::world::NilGuid();  // track0 の終点(既存ノードにスナップ)
    rs::world::Guid centerStartNode = rs::world::NilGuid();  // 中心線の始点(連続敷設の引き継ぎ)
    rs::world::TrackEdge::Refit refit;    // 緩和曲線の後編集用の指定(available のときだけ使う)
};

struct LaidTracksPlan {
    std::vector<LaidEdgePlan> tracks;  // 実レール。valid=false なら offset を作れない(確定拒否)
    float meanOffset = 0.0f;
    bool hasCenter = false;            // N>=2 かつ中心線の水平長が得られた
    LaidEdgePlan center;
    // 基準 curve(オフセット0)に対応する track が無いとき(偶数本)の表示用ガイド。
    // 確定では使わない。
    std::optional<LaidEdgePlan> guide;
};

LaidTracksPlan PlanLaidTracks(const rs::world::TrackGraph& graph, const LaidTracksInput& input);

// 計画した全 edge(実レールと中心線)の形を作れるかを確かめる。作れれば空文字、作れない線が
// あれば、どの線かを含む理由を返す。確定とプレビューが同じ判定を使い、1本でも作れなければ
// 全体を拒否する。
std::string LaidTracksRejection(const LaidTracksPlan& plan);

// 計画した edge を複線中心線(centerlineOnly)edge にする属性(guid・本数・間隔・プラグイン・
// 平均元の実レール edge)を足す。curve/spline は持たない(形は AddEdgeCommand が作る)。
void ApplyCenterlineAttributes(rs::world::TrackEdge& edge, int trackNum, float trackSpacing,
                               const std::vector<std::string>& trackPlugins,
                               const std::vector<rs::world::Guid>& trackEdges);

}  // namespace rs::editor
