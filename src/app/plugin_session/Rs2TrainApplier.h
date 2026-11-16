//
// Rs2TrainApplier.h - .rs2 TrainGroupListの編成リスト登録+設置済み編成の配置
//
// 各TrainGroupをFormation(編成リスト)へ登録し、
// Enabled=yesの編成はTrainのPos/Dirを軌道へスナップしてシーンへ配置する。
//
#pragma once

#include <string>
#include <vector>

#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/layout/Rs2Types.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/scene/SceneEntity.h"
#include "RslRuntimeState.h"

namespace rs::world {
struct TrackGraph;
class TrainTagRegistry;
}

namespace rs::app {

using rs::world::TrainId;

struct TrainSpeedSeed {
    TrainId trainId;
    double targetKmh = 0.0;
    double currentKmh = 0.0;
};

class PluginSelector;

// 保存時点で駅に停まっていた編成(State=2/3)を停車状態で始めるための入力と出力先。
// 足元の番線の特定にワープ表が、停車時間の起点に読込直後のゲーム内時刻が要る。
// runtimeを渡さない(nullptr)と、速度だけを写す。
struct Rs2TrainRestoreContext {
    const rs::world::WarpLinkTable* warpLinks = nullptr;
    double nowAbsSeconds = 0.0;
    double timeScale = 1.0;  // ドアを閉じ始める先行時間(DoorClosingTime x TimeScale)に使う
    RslRuntimeState* runtime = nullptr;
};

struct Rs2TrainApplyResult {
    size_t formationsAdded = 0;  // 編成リストへ登録できた編成数
    size_t trainsPlaced = 0;    // 実際にシーンへ配置できた編成数
    size_t carsPlaced = 0;
    std::vector<std::string> warnings;
};

// layout.trainGroups の各TrainGroupについて、installRootUtf8配下のTrainPluginを
// 解決してPluginSelectorへFormationとして登録する(名前=TrainGroup.Name、空なら
// 自動採番に任せる)。group.enabled==trueの編成は、先頭車(trains[0])のPos/Dirを
// sceneGraphs/sceneAddressesの各Sceneへスナップし、最も近い/AddressMapで解決できた
// Sceneへ実際に配置する(rs::app::PlaceTrainRepeating)。
// sceneGraphs/sceneAddressesはSceneList内Sceneと同じ順・同じ件数で渡すこと。
// pendingTrainRoutinesも同じ添字で、配置後のTrainIdへ解決してTrackNode/PlatformStopInfoへ反映する。
// placedTrainIds/speedSeeds(いずれも省略可)は、配置できた編成のtrainIdと、
// レイアウトに保存されていた走行状態(目標速度/現在速度[km/h]、reverseの符号込み)の
// 出力先。ここで直接ランタイムやUI状態を触らず値で返すのはレイヤー分離のため:
// 本ファイルはRailsimPlusCore(RailsimPlusTestsからもリンクされる)に属し、反映先の
// SceneRendererはRailsimPlusにしか無い(GPU依存)。
// trainFormationLinks(省略可)には、配置した列車の配置元の編成を登録する(タグは空のまま)。
// restoreContext.runtimeを渡すと、停車中・発車直前だった編成の走行状態とダイヤ状態を
// そこへ追加する(.rslと同じ復元経路に流す前提)。
Rs2TrainApplyResult ApplyRs2TrainGroups(const rs::legacy_layout::Rs2Layout& layout,
                                        const std::string& installRootUtf8,
                                        PluginSelector& selector,
                                        const std::vector<rs::world::TrackGraph*>& sceneGraphs,
                                        const std::vector<rs::legacy_layout::AddressMap>& sceneAddresses,
                                        const std::vector<rs::legacy_layout::Rs2PendingTrainRoutines>& pendingTrainRoutines,
                                        std::vector<rs::world::SceneEntity>& cars,
                                        std::vector<TrainId>* placedTrainIds = nullptr,
                                        std::vector<TrainSpeedSeed>* speedSeeds = nullptr,
                                        rs::world::TrainTagRegistry* trainFormationLinks = nullptr,
                                        const Rs2TrainRestoreContext& restoreContext = {});

} // namespace rs::app
