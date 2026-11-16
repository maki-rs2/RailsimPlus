//
// TrainDoorSide.h - 番線のOpenDoor(左右)を編成の各車両へ写す左右判定
//
// 番線側で基準フレームを1回だけ解決し、各車両へはワールド右手方向の一致で写す。
// 車両ごとのhead.dirを基準にすると、向きの揃っていないedgeを跨いで停車した編成で
// 開く側が食い違う。
//
#pragma once

#include "../../core/Math.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"

namespace rs::app {

// 番線のOpenDoor(左右)を各車両の_DOOR1/_DOOR2へ写すための基準。
struct DoorSideReference {
    bool valid = false;
    rs::core::Vec3 rightWorld;
};

// 基準フレームを1編成の停車につき1回だけ解決する。
DoorSideReference ResolveDoorSideReference(const rs::world::TrackGraph& graph,
                                           const rs::world::PlatformStopInfo& platform,
                                           const rs::world::TrackPosition& leadHead);

// 車両1両ぶんの左右入れ替え可否。
bool IsDoorSideFlipped(const rs::world::TrackGraph& graph,
                       const DoorSideReference& reference,
                       const rs::world::TrackPosition& carHead, bool carReversed);

} // namespace rs::app
