//
// EditorPlacementSolution.h - 敷設ソルバ(Editor::SolvePlacement)の結果型
//
// editor/tool 内部専用のヘッダー。Editor.h 側は
// SolvePlacement の宣言で参照するだけなので前方宣言で足り、この定義を
// 必要とするのは実装(Editor.cpp / EditorPlacementCommit.cpp)だけ。
//
#pragma once

#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "TrackLayingSettings.h"
#include "../../world/alignment/AlignmentSegment.h"
#include "../../world/alignment/TrackEdge.h"

namespace rs::editor {

// 敷設ソルバ(SolvePlacement)の結果。プレビュー(BuildPreview)と確定(CommitArc)が
// 同一ロジックを共有するための中間表現。
struct PlacementSolution {
    bool solved = false;
    std::vector<rs::world::AlignmentSegment> curve;
    rs::core::Vec3 endTangent;             // 終端接線(次の始点接線に引き継ぐ)
    rs::core::Vec3 usedMid;                // 実際に使った中間点(未解決時は入力midのまま)
    rs::core::Vec3 endPos;                 // 最終終点(スナップ/角度スナップ反映済み)
    bool endSnappedToNode = false;         // 終点を既存nodeに合わせて2接線交点で解いたか
    bool endSnapUnreachable = false;       // 終点ノードスナップだが解が求まらない(確定拒否)
    bool isBiarc = false;                  // S字(双円弧)で解いたか
    rs::core::Vec3 biarcJoint;             // isBiarc時の接続点J
    bool haveSolveInfo = false;            // solveInfo が有効か(R/θ表示用)
    rs::world::ThreePointArcSolveResult solveInfo;    // R/θ等の表示用(biarc時は合成値)
};

// 始点(位置・接線)から、終点ノード(位置・正規化済み接線)までを結ぶ曲線を解く。
// 単一円弧(spec の緩和曲線つき、ノード接線の両向き候補)、だめなら biarc(S字)の順。
// 解けたら out の curve/endTangent/usedMid/isBiarc/solveInfo 等を埋めて true。
// 単線の終点ノードスナップと複線の一括敷設が共有する。
bool SolveEndNodeConnection(const rs::core::Vec3& startPos, const rs::core::Vec3& startTangent,
                            const rs::core::Vec3& endPos, const rs::core::Vec3& endDir,
                            const rs::world::TransitionSpec& spec, PlacementSolution& out);

// 解の結果から、緩和曲線の後編集用の指定(edge.refit)を作る。後編集の対象外なら available=false。
rs::world::TrackEdge::Refit MakeRefitFromSolution(const PlacementSolution& sln,
                                                  const TrackLayingSettings& settings);

} // namespace rs::editor
