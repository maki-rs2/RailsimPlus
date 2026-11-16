#pragma once

#include <functional>
#include <unordered_set>

#include "../../sim/RouteReservation.h"
#include "../../sim/TrainRuntime.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/WarpLinkTable.h"

namespace rs::app {

using rs::world::TrainId;

// 1編成ぶんのポイントの予約。前方の進路が通るポイントを、先読みの範囲と、範囲に入った信号が
// 守る区間の終わりまでまとめて求め、取れたらポイントの開通先をその線へ切り替える。
// 手放すのは、先読みの範囲から外れ、かつ車体(編成長ぶん)がポイントの手前や上に残っていないポイント。
//
// 駅の停止目標が合流点より手前にあるあいだは、停止目標より先のポイントを求めない
// (停車中の列車が合流点を押さえ続けないようにする)。
//
// 戻り値は、この編成がまだ進路を取れていないポイントの区間を守る信号(速度の制限を0にする)。
// forwardSpanは先読みの範囲の走査結果で、slotOverrideはその走査に使った開通先の決定。
std::unordered_set<rs::world::Guid, rs::world::GuidHash> RunRouteReservationPhase(
    rs::sim::RouteReservation& ledger, const rs::sim::TrainRuntime& runtime,
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    const rs::world::ForwardScanSpan& forwardSpan, double seekAheadM,
    const rs::world::NodeSlotOverride* slotOverride,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
    double trainLengthM, double nowAbsSeconds);

} // namespace rs::app
