// PlatformPassThrough.h - 番線の通過(DiaAction::Pass)判定と、その番線の在線ラッチ。
//
// 停止目標の設定は接近時(TrainRuntime::SeekStationStop)、要素のDequeueは到着時
// (DiaRuntime::Tick)に分かれているため、通過した番線は「到着」せずカーソルが進まない。
// RailSim IIはDequeueをAction判定より前に行う(通過でも消費する)ので、通過を決めた接近時に
// その場で1回だけ消費する必要がある。そのための「もう決めた」印がラッチである。
#pragma once

#include <functional>

#include "TrainOccupiedPath.h"

namespace rs::world {
struct PlatformStopInfo;
} // namespace rs::world

namespace rs::sim {

// 編成の車体が platformId の番線edge上にあるか。
//
// headの1点だけで判定してはならない。車体はheadから-head.dir側へ伸びる
// (TrainAssembly.cppのSolveTrainHeadsのChaseBackward)ので、逆走中はheadが編成の
// 後端になり、走行方向への前方探索が自分の車体を舐める。head1点で判定すると、まだ
// 番線に居座ったままの車体を距離0で再捕捉して停止し、解除しても即座に再捕捉する
// 無限ループになる(発車不能)。
//
// 車体範囲は占有edge列から採る。列はポイントの現在スロットに依存しないので、編成の
// 下で転換されても実際の車両位置から逸れない。列が使えないときだけ後方走査へ切り替える
// (この経路は転換で逸れ、判定が早く抜けうる)。走行方向ではなく車体の伸びる向きへ
// 走査すること。ここに逆走の分岐を足すと、上記の非対称をそのまま作り直すことになる。
// headのedge自身は単点で見る。長さ0の走査は範囲を1つも返さないため、trainLengthMが
// 0の呼び出しでも判定が空にならないようにする。
bool IsPlatformOccupied(const GraphForScene& graphForScene,
                        const rs::world::WarpLinkTable& warpTable, size_t sceneIndex,
                        const rs::world::TrackPosition& head,
                        const TrainOccupiedPath& occupiedPath, double trainLengthM,
                        const rs::world::Guid& platformId);

// 編成の足元にある、停車できる番線を返す(無ければNilGuid)。.rs2のように保存時点で
// すでに駅に停まっている編成の番線を特定するために使う。IsPlatformOccupiedで当たった
// 番線のうち、目印の中央が編成の中央にいちばん近いものを選ぶ。占有edge列を持たない
// 時点で呼ぶので、車体範囲は後方走査で求める。
rs::world::Guid FindStoppablePlatformUnderTrain(const GraphForScene& graphForScene,
                                                const rs::world::WarpLinkTable& warpTable,
                                                size_t sceneIndex,
                                                const rs::world::TrackPosition& head,
                                                double trainLengthM);

// 通過中番線のラッチを更新する。通過の判断は番線へ到達する前に下るため、車体がその
// 番線へ入るまでは解除しない(入る前に解除すると、次のtickで同じ番線を再び捕捉して
// ルーチン要素を二重に消費する)。入ったあと完全に出たらラッチを解除する。
void UpdatePassingPlatformLatch(const GraphForScene& graphForScene,
                                const rs::world::WarpLinkTable& warpTable, size_t sceneIndex,
                                const rs::world::TrackPosition& head,
                                const TrainOccupiedPath& occupiedPath, double trainLengthM,
                                rs::world::Guid& passingPlatform, bool& passingEntered);

// routineElementがPass指定ならラッチを張り、consumeRoutineで要素を1つ消費してtrueを
// 返す(呼び出し側は停止目標を作らずに抜ける)。Pass以外・要素なしならfalse。
bool BeginPlatformPassThrough(
    const rs::world::PlatformRoutineElement* routineElement,
    const rs::world::PlatformStopInfo& platform, rs::world::Guid& passingPlatform,
    bool& passingEntered,
    const std::function<void(const rs::world::PlatformStopInfo&)>& consumeRoutine);

} // namespace rs::sim
