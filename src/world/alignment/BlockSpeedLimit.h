#pragma once

#include <functional>
#include <optional>

#include "IntervalList.h"
#include "TrackGraph.h"
#include "WarpLinkTable.h"

namespace rs::world {

// 信号1本が、ある列車に課す制限速度[km/h]を返す関数。nulloptなら制限なし。
// 現示は列車ごとに違う(自分が入った閉塞の信号では止まらない)ため、列車ごとに作って渡す。
// 空の関数を渡す呼び出しでは信号を無視する(エディタなど現示を持たない側)。
using SignalSpeedForTrainFn = std::function<std::optional<double>(const SpeedSignal&)>;

// edgeの速度制限[km/h](speedLimitKmh)と、そのedge上の閉塞にある信号の制限速度
// (signalSpeedが返す値)のうち小さい方を返す。
// 負は制限なし。alongEdgeDirは列車がedgeのs軸の向きに進むか。信号は進行方向と
// 同じ向きのものだけにかかる。向きは、信号から閉塞の中のedgeをつながりに沿ってたどって決める
// (warpTableを渡すと、同じシーン内のワープの先もたどる)。
// どの信号からもたどれないedgeは信号の制限をかけない。sceneIndexはgraphのシーン番号。
// 信号からたどれるedgeでは、進行方向と同じ向きの信号が無い向きの列車に速度0をかける。
// edgeが複数の閉塞に入っているときは、全閉塞の信号をまとめて1回だけ判定する(速度0になるのは、
// どの閉塞にも進行方向と同じ向きの信号が無いときだけ)。
// 信号がedgeの閉塞に無い、またはsignalSpeedが空ならedge.speedLimitKmhと同じ。
float EffectiveSpeedLimitKmh(const TrackGraph& graph, const Guid& edgeGuid,
                             const TrackEdge& edge, bool alongEdgeDir,
                             const SignalSpeedForTrainFn& signalSpeed,
                             const WarpLinkTable* warpTable = nullptr,
                             size_t sceneIndex = 0);

// signalの向きに進むとき、blockの中のedgeをs軸の向き(+1)に進むか逆(-1)に進むかを返す。
// 向きは、信号からつながりをたどって決める(warpTableを渡すと、同じシーン内のワープの先もたどる)。
// 閉塞が環になって同じedgeを両方向に通るedgeと、たどれないedgeは0を返す。
// 表示用: 速度制限(EffectiveSpeedLimitKmh)と同じ向きの決め方を、画面の矢印にも使う。
int SignalTravelDirectionOnEdge(const TrackGraph& graph, const SpeedSignal& signal, const Block& block,
                                const Guid& edgeGuid, const WarpLinkTable* warpTable = nullptr,
                                size_t sceneIndex = 0);

} // namespace rs::world
