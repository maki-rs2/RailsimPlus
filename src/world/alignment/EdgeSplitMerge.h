//
// EdgeSplitMerge.h - edge分割(SplitEdge)の純ロジック。graphを一切変更しない純関数群。
//
// Undo可能なgraph変更(node.links の張り替え・overlay remap を含む)は editor 層の
// SplitEdgeCommand(src/editor/tool/EdgeSplitMergeCommand.h)が担う。ここは形状保存の
// 判定と、first/second edge・middle node の値の組み立てのみを行う。
//
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "TrackGraph.h"
#include "WarpLinkTable.h"

namespace rs::world {

// CheckEdgeSplit の拒否理由。
enum class EdgeSplitRefusal {
    None,                        // 許可
    OutOfRange,                  // sが[kMinSpanLen, len-kMinSpanLen]の外、またはspline無し
    MultiTrackInvolved,          // centerlineOnly edge、またはいずれかの中心線のtrackEdges
    ShapeUnsolvable,             // 前半・後半の形を両端ノードの姿勢から作れない(ノードが無い場合を含む)
    HasSeam,                     // 論理継ぎ目(linkNodes)を持つedge。端の接続を壊さないため分割しない
};

// 分割位置 s の spline の姿勢(位置・ピッチ込みの接線・up・カント)を持つ中間ノード
// (links は空)。分割の組み立て(BuildEdgeSplit)と、確定前の事前確認(分岐敷設の
// プレビュー等)が同じ姿勢を使うための入口。
TrackNode MakeSplitMiddleNode(const TrackEdge& edge, double s, const Guid& guid);

// edge を s 地点で分割してよいかを判定する。中間ノードの姿勢と両端ノードの姿勢から
// 前半・後半の形(BuildEdgeShape)を実際に作れることまで確かめる(純関数、graphは変更しない)。
EdgeSplitRefusal CheckEdgeSplit(const TrackGraph& graph, const TrackEdge& edge, double s);

// edge分割の結果(graphへ挿入する前の完全な first/second edge と middle node)。
// middle.links は空のまま返す(呼び出し側のCommandがnode.linksの張り替えと合わせて
// 設定する)。
struct EdgeSplitResult {
    TrackEdge first;
    TrackEdge second;
    TrackNode middle;
};

// edge を s 地点で分割し、first/second/middle を組み立てる(純関数、graphは変更しない)。
// 前半・後半の curve/spline は空のまま返す(呼び出し側が RebuildEdgeSpline で作る)。
// 失敗(CheckEdgeSplit != None)時は nullopt。
// newEdgeGuid/newNodeGuid は呼び出し側が発番したものを渡す(Undo/Redoで再利用するため、
// この関数自身では発番しない)。
std::optional<EdgeSplitResult> BuildEdgeSplit(const TrackGraph& graph, const TrackEdge& edge,
                                               double s, const Guid& newEdgeGuid,
                                               const Guid& newNodeGuid);

// CheckEdgeMerge の拒否理由。
enum class EdgeMergeRefusal {
    None,                  // 許可
    NotDegree2,             // 共有nodeのlinks.size()!=2(またはnode/edgeが無い)
    IsBranchingNode,        // 共有nodeが分岐(IsPoint())
    NoCurve,                // いずれかのedgeのcurveが空
    ShapeUnsolvable,        // 結合後の形を遠端2ノードの姿勢から作れない
    CurvatureDiscontinuous, // 共有nodeで結合前後の曲率が不連続(閾値超過)
    MultiTrackInvolved,     // centerlineOnly edge、またはいずれかの中心線のtrackEdges
    LayerMismatch,          // layerが不一致
    CantDiscontinuous,      // 共有nodeのカントが結合後の全長線形補間から外れる
    WarpOnSharedNode,       // 共有nodeにワープリンクがある
    FormsLoop,              // 2本の遠端が同一node(結合すると自己ループになる)
    HasSeam,                // いずれかのedgeが論理継ぎ目(linkNodes)を持つ
};

// 共有node(2本のedgeの結節点)を結合してよいかを判定する(純関数、graphは変更しない)。
// warpLinksはnullable(nullptrならワープ判定をスキップする。世界層は現在の
// アクティブSceneインデックスを持っていないため、呼び出し側がWarpLinkTable::Links()を
// 全走査してnode一致で判定する)。
// allowLayerMismatch=trueの場合、layer不一致チェックをスキップする(異なるレイヤーの
// edge結合をUI確認ダイアログ経由で許可するためのフラグ)。
EdgeMergeRefusal CheckEdgeMerge(const TrackGraph& graph, const Guid& sharedNodeGuid,
                                 const WarpLinkTable* warpLinks,
                                 bool allowLayerMismatch = false);

// 結合結果。mergedはedgeFirst.guidを再利用する(remap範囲を半分にするため)。
// どちらのedgeが「edgeFirst」役(=guidを引き継ぐ側)になるかは、共有nodeの
// links[0]が参照するedgeとする(呼び出し側が迷わないよう固定)。
struct EdgeMergeResult {
    TrackEdge merged;             // guid = edgeFirst.guid
    Guid removedEdgeGuid;         // 消えるedge(旧edgeSecond)のguid
    Guid removedNodeGuid;         // 消える共有nodeのguid
    double lenFirst = 0.0;        // remap用: edgeSecond側のsに加算する量(結合前のedgeFirst理論長)
    double lenSecond = 0.0;       // remap用: 結合前のedgeSecond(後半役)の理論長
    // 速度制限と閉塞を引き継ぐ側。長いほうのレールが勝ち、同じ長さなら前半役(edgeFirst)。
    bool firstWins = true;
};

// edgeFirst/edgeSecondを共有nodeで結合する(純関数、graphは変更しない)。
// CheckEdgeMerge(graph, sharedNodeGuid, nullptr, allowLayerMismatch)!=None なら
// nullopt(ワープ判定は呼び出し側が別途CheckEdgeMergeへ実warpLinksを渡して事前に
// 確認する約束事、EdgeMergeCommand::applyを参照)。allowLayerMismatchの意味はCheckEdgeMerge
// と同じ。
std::optional<EdgeMergeResult> BuildEdgeMerge(const TrackGraph& graph,
                                               const Guid& sharedNodeGuid,
                                               bool allowLayerMismatch = false);

// MergeEdgeAt成功時、editor層より外側(ImportedWireSpan等)へ渡すremapの取り決め。
//
// 結合は前半役(keptEdge)・後半役(removedEdge)のそれぞれを独立に反転しうる
// (ComputeMergePlanのfrontReversed/backReversedは別々の判定)ため、s変換には
// 両側の「反転したか」と「結合前の理論長」が要る。sOffsetだけでは
// 反転ケースでアンカーがedgeの逆端へずれる。変換は必ず下の
// RemapMergedAnchor/UnremapMergedAnchorを通し、呼び出し側で式を書かないこと。
struct EdgeMergeRemap {
    Guid removedEdge;
    Guid keptEdge;
    double lenKept = 0.0;     // 結合前のkeptEdge(前半役)の理論長
    double lenRemoved = 0.0;  // 結合前のremovedEdge(後半役)の理論長
    bool keptReversed = false;
    bool removedReversed = false;
};

// 結合前のアンカー(edge, s)を結合後のedge座標へ写す。edgeがkept/removedのどちらでも
// ない場合は何もせずfalseを返す。成功時、edgeはkeptEdgeへ書き換わる。
// outReversedには「そのアンカーが乗っていた側が反転したか」を返す。反転側では
// 進行方向(frame.dir)が逆になるため、right方向のオフセット(lateralOffsetM等)を
// 持つアンカーは呼び出し側で符号を反転させること。
bool RemapMergedAnchor(const EdgeMergeRemap& remap, Guid& edge, double& s,
                       bool* outReversed = nullptr);

// RemapMergedAnchorの逆変換(結合のUndo用)。edgeがkeptEdgeでなければfalse。
// 境界(s == lenKept、共有nodeに乗っていたアンカー)はremoved側の先頭として扱う
// (幾何的には同一点なのでどちらでも見た目は同じだが、暗黙にすると再導出時に
// 食い違うため規約として固定する)。
bool UnremapMergedAnchor(const EdgeMergeRemap& remap, Guid& edge, double& s,
                         bool* outReversed = nullptr);

}  // namespace rs::world
