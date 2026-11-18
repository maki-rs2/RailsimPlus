//
// NodeDragPlan.h - ノードドラッグ(位置・標高・接線ハンドル)の「姿勢の変更計画」
//
// ノードを動かす操作を、グラフを変えない純関数で計画する。計画は
//   1. 変更後のノード姿勢(動かすノード・複線中心線に追従するメンバー)
//   2. その姿勢で BuildEdgeShape が作る edge の形(事前確認とプレビューが同じ結果)
// を持つ。ok でなければ何も実行しない(確認で1本でも形を作れなければ拒否)。
// 確定は計画の nodes を MoveNodeCommand の CompositeCommand にして実行するだけで、
// edge の spline は MoveNodeCommand が RebuildEdgeSpline で作り直す(計画の spline と同じ入力)。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/AlignmentSegment.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

// 変更後のノード姿勢。
struct NodePoseChange {
    rs::world::Guid node = rs::world::NilGuid();
    rs::core::Vec3 pos;
    rs::core::Vec3 tangent;
    rs::core::Vec3 up;
    // true なら、接線が反対向きになっても、つながる edge 端の符号を反転しない(接線が edge 端の符号込みで
    // 進行方向を表すように、計画が決めてある)。終端ノードのドラッグが使う。
    bool keepEdgeSigns = false;
};

// BuildEdgeShape が作った edge の形(プレビューの折れ線にそのまま使う)。
struct EdgeShapePreview {
    rs::world::Guid edge = rs::world::NilGuid();
    std::vector<rs::world::TrackNodeSample> spline;
};

struct NodeDragPlan {
    bool ok = false;
    // 変更するノード。先頭が操作したノード、続いて中心線メンバー。
    std::vector<NodePoseChange> nodes;
    // 操作したノードに直接つながる edge の形(確定後の edge.spline と一致する)。
    // 中心線ノードの操作では中心線 edge だけ(メンバーの形は確認にだけ使う)。
    std::vector<EdgeShapePreview> edges;
    // 中心線から切り離すメンバー edge(個別に動かした複線の実レール)。
    std::vector<rs::world::Guid> detachEdges;
    std::string error;
};

// edgeGuid がいずれかの複線中心線(centerlineOnly)のメンバー edge か。
bool IsCenterlineMember(const rs::world::TrackGraph& graph, const rs::world::Guid& edgeGuid);

// 通常ノードを動かせるか: ノードがあり、中心線 edge につながらず、links が1以上。
bool CanMovePlainNode(const rs::world::TrackGraph& graph, const rs::world::Guid& nodeGuid);

// node を newPos(高さ込み)へ動かす計画。接線は次数2以上なら凍結、次数1(終端)は
// 相手端の進行方向に接して newPos を通る単一円弧の端の向き(水平)と、新しい弦勾配のピッチ。
// 隣接ノードの接線は動かさない。
NodeDragPlan PlanNodeMove(const rs::world::TrackGraph& graph, const rs::world::Guid& node,
                          const rs::core::Vec3& newPos);

// ノード標高(pos.y)の許容範囲[m]。ノード操作(ドラッグ・標高変更)の結果をこの範囲へ収める。
constexpr float kMinNodeElevationM = -2000.0f;
constexpr float kMaxNodeElevationM = 2000.0f;

// node の標高(pos.y)だけを elevation へ変える計画。接線・上方向は変えない。
// 複線中心線のノードなら、メンバーも同じ高さの差で追従させる(ドラッグ移動と同じ規則)。
// 孤立ノード(edgeが無い)は位置だけを変える。
NodeDragPlan PlanNodeElevation(const rs::world::TrackGraph& graph, const rs::world::Guid& node,
                               float elevation);

// node の接線を newTangent(単位ベクトル)へ直接設定する計画。位置は変えない。
NodeDragPlan PlanNodeTangent(const rs::world::TrackGraph& graph, const rs::world::Guid& node,
                               const rs::core::Vec3& newTangent);

} // namespace rs::editor
