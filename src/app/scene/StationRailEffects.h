//
// StationRailEffects.h - 駅のレール接続エフェクタ(ConnectRail / BranchRail / DisconnectRail)を毎フレーム適用する。
//
#pragma once

#include <vector>

#include "../../world/alignment/RailLinkEdit.h"
#include "../../world/scene/SceneRegistry.h"
#include "SceneSlotPool.h"

namespace rs::app {

// 駅の配置ごとに、発動条件が成り立つエフェクタを graph の重ね置き(railLinkOverlay)へ適用する。
// 各操作は冪等なので、条件が成り立つ間は毎フレーム呼んでよい。条件が成り立たなくなっても元へは戻さない
// (RailSim II は付け替えた状態を保つ)。静的グラフの編集で食い違った重ね置きは、適用の前に破棄する。
// inUse は列車が使用中のノードを答える(使用中のノードはつなぎ替えない)。何かを変えたら true。
bool ApplyStationRailEffects(rs::world::TrackGraph& graph, const rs::world::SceneRegistry& registry,
                             const std::vector<SceneSlotPool::Slot>& slots,
                             const rs::world::NodeInUseFn& inUse);

} // namespace rs::app
