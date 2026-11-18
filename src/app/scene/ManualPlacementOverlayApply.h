//
// ManualPlacementOverlayApply.h - 手動オーバーレイを自動配置列へ後段適用する純関数
//
// 自動配置(CollectTrackPluginPlacements)の結果に対し、実体化済み個体を合流する。
// D3D12・GUIに依存しない純関数で
// Catch2で直接テストできる。overlay が空なら入力と完全に一致する(no-op)。
//
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "../../world/scene/ManualPlacementOverlay.h"
#include "TrackPluginPlacement.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// items(Pier/Pole 等の離散配置列)に overlay を適用した新しい列を返す。
//  - 実体化済み個体を同じ TrackPluginPlacementItem 表現へ変換して合流。
// overlay.empty() のときは items のコピーをそのまま返す。
std::vector<TrackPluginPlacementItem> ApplyOverlayToItems(
    const std::vector<TrackPluginPlacementItem>& items,
    const rs::world::ManualPlacementOverlay& overlay,
    const std::vector<TrackPluginEntry>& entries = {});

// オーバーレイの参照整合を掃除する。graph に存在しない edge を指す
// 実体化項目を落とし、
// pluginSections と実体化項目が重複する不正状態を解消するため graph を書き換える。
// 除去した参照の総数を返す(0=妥当)。
// 保存前・読込後に呼び、dangling ref が .rsl に出入りしないようにする。
// derivedPoleIds: overlay.polesに無くてもStructから導出される柱のGuid。
// 架線の端点としてはこれも有効な柱として扱う。
int SanitizeOverlay(rs::world::ManualPlacementOverlay& overlay,
                    rs::world::TrackGraph& graph,
                    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& derivedPoleIds = {});

} // namespace rs::app
