//
//AutoCant.h - カント自動モード(TrackEdge::cantAuto)で edge に使うカントの解決
//
//ユーザーが決めるのは曲線部の最大カント(cantAutoMax)だけ。曲線部の符号は curve の曲がる
//向きから、つなぎ目の値は隣の edge が自動モードかどうかから、逓減率は調整値JSON
//(EditorGeometryTuning::autoCantTransitionRatioMeters)から決める。
//保存値 edge.cantA 等は書き換えない(手動へ戻したとき元の値が残る)。
//関数はグラフを読むだけの純関数で、RebuildAutoCantAround だけが spline を作り直す。
//
#pragma once

#include <vector>

#include "Alignment.h"
#include "TrackGraph.h"

namespace rs::world {

//自動モードで形を作るときに使うカント一式(TrackEdge の同名フィールドと同じ単位)。
struct AutoCantValues {
    float cantA = 0.0f;
    float cantB = 0.0f;
    float cantMid = 0.0f;
    float cantTransitionRatio = 0.0f;
};

//曲線部のプラトー(符号つき)。曲線の内側が高くなる向きにする。curve の最初の曲がる区間
//(Arc/Transition)の向きだけを使い、S字や複合曲線で向きが変わっても先頭の向きで一定にする。
//曲がる区間が無い(直線のみ)ときは 0。
float AutoCantPlateau(const std::vector<AlignmentSegment>& curve, float cantAutoMax);

//つなぎ目の値。自分と相手のプラトーが同符号なら絶対値の小さい方(符号つき)、
//どちらかが 0 か符号が逆なら 0。
float AutoCantJoint(float ownPlateau, float neighborPlateau);

//edge の端(0:nodeA / 1:nodeB)につながる相手の自動プラトーを、この edge の向きの符号で返す。
//相手が無い・次数3以上・相手が手動・自己ループのときは 0。
float NeighborAutoPlateau(const TrackGraph& graph, const TrackEdge& edge, int end);

//curve と両端の相手のプラトー(NeighborAutoPlateau の値)から一式を決める。
AutoCantValues ResolveAutoCant(const std::vector<AlignmentSegment>& curve, float cantAutoMax,
                               float neighborPlateauA, float neighborPlateauB);

//edge の現在有効なカント一式。自動モードなら ResolveAutoCant(edge.curve と graph の隣)、
//手動なら保存値そのまま。
AutoCantValues EffectiveEdgeCant(const TrackGraph& graph, const TrackEdge& edge);

//edges のうち自動モードのものと、それらの端ノードでつながる自動モードの edge の spline を
//作り直す。隣の curve や自動値が変わった後に、つなぎ目の値を追従させるために呼ぶ。
void RebuildAutoCantAround(TrackGraph& graph, const std::vector<Guid>& edges,
                           const AlignmentSampleOptions& opt = {});

//nodes につながる自動モードの edge の spline を作り直す。edge の追加・削除で
//つなぎ目の相手が変わったときに、残る側の edge へ使う。
void RebuildAutoCantAtNodes(TrackGraph& graph, const std::vector<Guid>& nodes,
                            const AlignmentSampleOptions& opt = {});

}  // namespace rs::world
