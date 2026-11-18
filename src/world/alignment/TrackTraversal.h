//
// TrackTraversal.h - 軌道走査カーネル
//
// TrackPosition（edge上の位置）と、それを距離 ds だけ前進させる Advance を
// 定義する。編成・台車・信号・在線など「軌道上の位置」を扱う全てがこの型を
// 使う。GUI 非依存・ヘッドレス数値検証可能な純関数群（rs::sim には依存しない）。
//
#pragma once

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../../core/Math.h"
#include "Guid.h"
#include "TrackNode.h"
#include "TrackPosition.h"

namespace rs::world {

struct TrackEdge;
struct TrackGraph;

// edge.spline を弧長 s で二分探索し、隣接2サンプルを線形内挿してフレームを返す。
// s は [0, edgeLength] にクランプされる。direction=-1 なら接線を反転して返す
// （up/cant は進行方向に依存しないため反転しない）。
// edge.spline が2点未満（縮退）の場合は既定のフレーム（原点・+Z接線）を返す。
// 親に追従するedge(IsParentDrivenEdge)は、休止姿勢のフレームへ edge.parentDelta を掛けた今のフレームを返す
// (弧長 s は剛体変換で変わらない)。それ以外のedgeは EvaluateAtRest と同じ。
TrackFrame EvaluateAt(const TrackEdge& edge, double s, int direction);

// parentDelta を掛けない、spline のままのフレーム。メッシュ・橋脚・架線柱など、
// 休止位置に固定する対象と、spline 上の位置の探索に使う。
TrackFrame EvaluateAtRest(const TrackEdge& edge, double s, int direction);

// 背向（trailing）でポイントへ進入した記録。進入した面の現在スロットをこの
// 進入スロットへ更新する（＝割り出し）ことで、後方走査が同じ経路を再導出できる。
// 適用は前進する head の走査（呼び出し側）の責務であり、Advance 自身はグラフを書き換えない（後方chase・敷設プレビュー・
// ピッキングからも呼ばれるため）。
struct TrailingPass {
    Guid node;
    int  side = 0;  // 進入した面
    int  slot = 0;  // その面での進入スロット
};

struct VisitedEdge {
    Guid edge = NilGuid();
    int direction = +1;
};

// Advance の結果。atEnd=true のとき pos は到達できた最終地点（行き止まり端
// または非開通ポイントで停止した端）を指し、consumed < ds になる。
struct AdvanceResult {
    TrackPosition pos;
    bool   atEnd = false;
    double consumed = 0.0;
    // 通過中に背向でポイントへ進入した記録（適用要否・適用先は呼び出し側の責務、
    // Advance 自身はグラフを書き換えない）。
    std::vector<TrailingPass> trailingPasses;
    std::vector<VisitedEdge> visitedEdges;
};

// start から弧長 ds（>=0）だけ前進させる。edge 端に達したら node を跨いで
// 次の edge へ載せ替える（複数 edge 跨ぎに対応）。
// links.size()==1（自分のみ）の node で行き止まりに達した場合、または
// ds を消費しきれない場合は atEnd=true で停止位置を返す。
// 前提: ds は非負（後退は direction を反転した TrackPosition を渡すことで表現する）。
// 純関数であり graph は一切書き換えない（結果の trailingPasses を適用するかは
// 呼び出し側が決める。後方chase・敷設プレビュー・ピッキングからも呼ばれるため、
// ここで状態を書き換えてはならない）。
AdvanceResult Advance(const TrackGraph& graph, TrackPosition start, double ds);

// 以下は Advance の内部実装だが、前方探索(TrackForwardScan.h の
// FindForwardEdge)が同じ分岐選択ロジックを共有するため公開する。
// 呼び出し側から直接使うことは想定しない。

// edge の弧長[m](spline末尾サンプルのs)。
double EdgeLength(const TrackEdge& edge);

// node での「来た edge を除いた次候補」選択。
// RailSim II の RailConnector と同じく走行は必ず一方の面から他方の面へ抜けるので、
// 進入した link の反対面(exitSide)の候補だけが行き先になる。候補が2本ある
// (=その面が分岐している)ときだけ現在開通スロットで選ぶ。
// これにより向かい/背向の場合分けは不要になる: 背向進入では出口面が1本しか
// 無いため常に通過でき、スプリングポイントの規約が自動的に成立する。
// enteredIndex には cameFromEdge に対応する links index を返す(見つからなければ
// -1)。呼び出し側(Advance)が「背向進入(=進入面が分岐)による割り出し」の記録に使う。
struct LinkSelection {
    std::optional<TrackNode::Link> next;
    int enteredIndex = -1;
};

// 戻り値 next=nullopt = 行き止まり(出口面に候補が無い、または来た edge がこの
// node に繋がっていない)。slotOverrideが非nullで、node.guidかつexitSideの
// エントリがあれば(std::nulloptでなければ)、node.activeSlotより優先してその値を使う
// (先読み専用の列車ごとの決定。共有activeSlotへは書き込まない)。
LinkSelection SelectNextLink(const TrackNode& node, const Guid& cameFromEdge,
                             const NodeSlotOverride* slotOverride = nullptr);

// SelectNextLink の結果が仮想edge(複線中心線ダミー等)を指す場合は、列車が
// 仮想edgeへ乗らないよう選択自体を無効化する(行き止まり扱い)。enteredIndex は
// 選択の成否によらずそのまま引き継ぐ(トレイリング判定は選択結果と独立のため)。
LinkSelection SelectNextRealLink(const TrackGraph& graph, const TrackNode& node,
                                 const Guid& cameFromEdge,
                                 const NodeSlotOverride* slotOverride = nullptr);

// 分岐ルーチンのhand(0=左/1=右、RailSim II の PointList.Point由来)を、node.exitSide面の
// リンク出現順(ordinal)へ変換する(RailSim II の CRailWay::MarchTrain中のCalcPoint直後の
// 反転と同じ規則)。
// 出口面のordinal 0/1それぞれが指す先のedgeについて、nodeから5m以上離れた
// 最初のspline点までのベクトルを「出ていく方向」とし、node基準のrightベクトル
// との内積で左右を判定する。候補が1本以下(分岐でない)場合は常に0を返す。
int ResolveHandToOrdinal(const TrackGraph& graph, const TrackNode& node, int exitSide, int hand);

} // namespace rs::world
