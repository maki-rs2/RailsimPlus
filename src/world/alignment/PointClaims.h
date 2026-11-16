#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "TrackGraph.h"
#include "WarpForwardScan.h"

namespace rs::world {

// 前方の進路が通るポイント1か所の要求。faceはポイントの分岐している面で、slotはその面で
// 進路が通る線(面の中で何本目か)。分岐で入るとき(分岐していない面から入る)は進路が選ぶ線、
// 合流で入るとき(分岐している面から入る)は入ってくる線がslotになる。
struct PointClaim {
    size_t sceneIndex = 0;
    Guid node = NilGuid();
    int face = 0;
    int slot = 0;
    // 走査開始位置からポイントまでの距離[m]。
    double distanceM = 0.0;
    // 合流で入る(分岐している面から入る)要求。
    bool trailing = false;
    // 切り替えが要る(いまの開通先と違う)のに、隣の閉塞に他編成がいて切り替えられない。
    bool blockedByOccupancy = false;
};

// spanの進路が通るポイントを、近い順に返す。maxDistanceMを超える距離のポイントは含めない。
// ポイントの開通先はspanを作ったときの選択(slotOverrideまたは共有のactiveSlot)に従う。
// ワープをまたぐ境目と、分岐していないノードは含めない。
std::vector<PointClaim> CollectPointClaims(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    double maxDistanceM);

} // namespace rs::world
