//
// TrainPlacement.h - 編成(Train)のグルーピング配置
//
// 選択中の編成(PluginSelector::SelectedFormationCars、反転フラグ付き車両リスト)を、
// headから後方(chase)へ各車両自身の実長(FormationCar::frontLimit/tailLimit、
// AddPath時にdef.trainInfoからキャッシュ済み)で並べてSceneEntityとして配置する
// (rs::legacy_plugin::SolveTrainHeadsの異種車両対応版を使用)。
//
#pragma once

#include <vector>

#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/SceneEntity.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

using rs::world::TrainId;

// 先頭headから後方へ、各車両自身の実長で並べた各車の配置(軌道位置と反転のyaw)を返す。
// 実配置(BuildTrainRepeating)と配置前のプレビューが同じ位置になるよう、両方がこれを使う。
// 戻り値はcarsと同じ並び・同じ要素数(carsが空なら空)。
std::vector<rs::world::TrackPlacement> SolveCarPlacements(const std::vector<FormationCar>& cars,
                                                          const rs::world::TrackPosition& head,
                                                          const rs::world::TrackGraph& graph);

// 先頭headから全車両の実長ぶん後方へ辿れるか(編成が軌道の終端にはみ出さず収まるか)を返す。
// 収まらないと、SolveCarPlacementsは終端の1点へ後続の車両を重ねて置いてしまう。
// 1つのgraph上だけで判定する(warpの先へ跨ぐ位置は収まらない扱い)。
bool CarsFitOnTrack(const std::vector<FormationCar>& cars, const rs::world::TrackPosition& head,
                    const rs::world::TrackGraph& graph);

std::vector<rs::world::SceneEntity> BuildTrainRepeating(const std::vector<FormationCar>& cars,
                                                           const rs::world::TrackPosition& head,
                                                           const rs::world::TrackGraph& graph,
                                                           size_t sceneIndex,
                                                           TrainId* groupIdOut = nullptr);

// 戻り値: 配置した車両数(=cars.size()、0件なら0)。
// 各車両はSceneEntity{category=Train, reversed, trainId(共通), carOrder(先頭=0)}
// として追加され、reversedな車両はTrackPlacement::yawOffsetDeg=180が設定される。
// groupIdOutを渡すと発番した
// trainIdを書き込む(0両なら書き込まない。SceneRenderer::SetGroupTargetSpeed
// で走行させる際に使う)。
// sceneIndexには、graph/headが属するSceneの添字(=配置操作時のアクティブScene)を
// 渡す。SceneEntity::placementSceneIndexへそのまま入り、走行系がどのSceneのgraph
// でこの編成をtickすべきかの判断に使う。
int PlaceTrainRepeating(rs::world::SceneRegistry& registry, const std::vector<FormationCar>& cars,
                          const rs::world::TrackPosition& head,
                          const rs::world::TrackGraph& graph, size_t sceneIndex,
                          TrainId* groupIdOut = nullptr);

// 保存ファイル読込後など、外部から trainId を書き込んだ直後に呼ぶ。
// 次回の配置で使用するIDを、maxUsedGroupIdより大きい値へ単調増加させる。

} // namespace rs::app
