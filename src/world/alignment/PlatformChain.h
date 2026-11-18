//
// PlatformChain.h - 進行方向に依存する番線チェーンの解決と停止目標計算
//
#pragma once

#include <vector>

#include "TrackForwardScan.h"
#include "PlatformStopInfo.h"

namespace rs::world {

struct PlatformStopInfo;

// 番線チェーンは「進行方向で見た番線」であり、番線そのものの内在的な属性ではない。
// 同じ番線でも反対側から進入すれば orderedEdges は逆順になり、始端は反対の端になる。
// 停止目標を「始端からの距離」で表す以上この向き依存は避けられないので、解決は必ず
// 走行中のTrackPositionを起点に行う(番線データだけから解決してはならない)。
struct PlatformChain {
    bool ok = false;                  // false=解決不能。呼び出し側は近似へフォールバックする
    std::vector<Guid> orderedEdges;   // 進行方向順に連続する番線edge
    double totalLength = 0.0;         // = platformLength [m]。orderedEdgesのEdgeLength総和
    // scanStart からチェーン始端までの弧長[m]。編成が既に番線内にいる場合、始端は
    // 後方にあるため負になる。
    double entryDistance = 0.0;
};

// scanStart から進行方向へ見た platform のチェーンを解決する。entryEdge は
// FindForwardEdge が返した最初の番線edge。長さはどこにもキャッシュせず毎回ライブな
// edge幾何から算出する(edge編集で長さが変わるため。TrackGraph外に持つとUndoが
// 守れない)。純関数でgraphを書き換えない。
PlatformChain ResolvePlatformChain(const TrackGraph& graph, const PlatformStopInfo& platform,
                                   TrackPosition scanStart, Guid entryEdge, double maxSearchM);

// 停止位置基準を RailSim II 停止目標式の正規化Offsetへ変換する。
// 番線チェーンは進行方向で解決されるため、前基準は編成先頭を進行方向側の番線端へ揃える
// 意味になる。同じ番線・同じ設定でも上り/下りで物理的な停止位置は鏡像になる(RailSim II と同じ
// 挙動)。オフセットmも進行方向前後で測る。
double ResolveStopOffset(StopReference kind, double platformLengthM, double trainLengthM);

// ダイヤの停止目標(scanStart からの弧長[m])。
//   StopTarget = 0.5 * (Offset + 1.0) * (platformLength + trainLength) + dist
// offset は [-1,+1] の正規化値(0=編成中央を番線中央へ、±1=端へ寄せる)。戻り値は
// 「チェーン始端から進行方向の物理的な先端までの距離」を scanStart 基準へ直したもの。
double PlatformStopTargetDistance(const PlatformChain& chain, double trainLengthM, double offset,
                                  double distM);

} // namespace rs::world
