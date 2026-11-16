#pragma once

#include <optional>
#include <vector>

#include "TrackGraph.h"
#include "TravelDirectionFollow.h"

namespace rs::world {

// 信号を置くときの検証結果。None以外は置けない(または置いたままにできない)理由。
enum class SpeedSignalPlacementError {
    None,
    NoBlock,             // 位置を含む閉塞がない
    SameDirectionExists, // 担当閉塞に同じ向きの信号が既にある
    BlockFull,           // 担当閉塞の信号が上下の2個に達している
};

// 1閉塞に置ける信号の最大数(上り・下りで1個ずつ)。
constexpr size_t kMaxSpeedSignalsPerBlock = 2;

// 閉塞の境界となり、その閉塞の制限を受け持つのは主信号だけ。
constexpr bool IsBlockBoundarySignal(SpeedSignalKind kind) {
    return kind == SpeedSignalKind::Main;
}

// graph.speedSignalsの内容からedgeToSpeedSignalsとblockToSpeedSignals索引を作り直す。
void RebuildSpeedSignalIndices(TrackGraph& graph);

// 信号の制限速度[km/h]=最上段(stagesの最後)の速度。stagesが空ならnullopt(制限なし)。
// 速度制限の適用・表示色・パネルの現在値は、最上段の速度をすべてこの関数で引く。
std::optional<int> SignalTopSpeedKmh(const SpeedSignal& signal);

const SpeedSignal* FindSpeedSignal(const TrackGraph& graph, const Guid& guid);

// 信号の担当閉塞を返す。signal.blockの閉塞が無い、またはその閉塞が信号のedgeを含まないときはnullptr
// (表示専用の信号はblockがNilなので常にnullptr)。担当閉塞を引く処理はこの関数に集める。
const Block* OwnerBlock(const TrackGraph& graph, const SpeedSignal& signal);

// 置く信号の担当閉塞の決め方: edgeを含む閉塞のうちgraph.blocksで先に並ぶものを返す。無ければNil。
Guid DefaultOwnerBlockOnEdge(const TrackGraph& graph, const Guid& edge);

// 担当閉塞を持たない主信号(担当の閉塞が無い、または担当の閉塞が信号のedgeを含まない)を
// graph.speedSignalsから消し、索引を作り直す。消した数を返す。
// 閉塞からedgeを外す操作・閉塞の整理・レールの結合が共用する。Undo用の控えは呼び出し側が取る。
size_t PruneSignalsWithoutOwner(TrackGraph& graph);

// signalと同じ向きの進み方を、blockの中のedgeだけ、つながり(warpTableを渡したときは同じシーン内の
// ワープの先)に沿ってたどって印を付ける。着かないedgeは印に載らない。
// 信号を置けるかの検証と速度制限・矢印の向きは、向きの比較をすべてこの印で行う。
TravelDirectionMasks MarkSignalDirectionInBlock(const TrackGraph& graph,
                                                const WarpLinkTable* warpTable, size_t sceneIndex,
                                                const SpeedSignal& signal, const Block& block);

// 閉塞にある主信号(担当閉塞がその閉塞のもの)を返す。表示専用の信号は数えない。
std::vector<const SpeedSignal*> SpeedSignalsOfBlock(const TrackGraph& graph, const Block& block);

// 位置(edge, s)・向きdirectionに、担当閉塞blockGuidの信号を置けるかを検証する。ignoreGuidの信号は
// 無いものとして数える(位置・向きの変更で自分自身と衝突しないため)。追加コマンドの適用前と、
// 配置待ち中のホバー表示の両方がこれを使う。同じ向きかは、閉塞の中のつながりで決める
// (MarkSignalDirectionInBlock)。sceneIndexはgraphのシーン番号。
// blockGuidの閉塞が無い、またはedgeを含まないときはNoBlock(主信号以外は検査しない)。
SpeedSignalPlacementError ValidateSpeedSignalPlacement(const TrackGraph& graph, const Guid& edge,
                                                       double s, int direction,
                                                       const Guid& blockGuid,
                                                       const Guid& ignoreGuid,
                                                       const WarpLinkTable* warpTable = nullptr,
                                                       size_t sceneIndex = 0,
                                                       SpeedSignalKind kind = SpeedSignalKind::Main);

} // namespace rs::world
