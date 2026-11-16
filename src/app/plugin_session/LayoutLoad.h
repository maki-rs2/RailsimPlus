#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "LayoutSession.h"
#include "../action/LayoutActionResult.h"
#include "Rs2TrainApplier.h"
#include "RslRuntimeState.h"
#include "../../world/scene/TrainRunInfoRegistrySerialize.h"
#include "../../world/scene/TrainTagRegistrySerialize.h"
#include "../../editor/tool/TrackLayingSettings.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"

namespace rs::editor { class Editor; }
namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class SceneRenderer;
class PluginSelector;
class CameraController;
struct TrainPlacementUiState;

// レイアウトの中身を、既定値で埋めた完全な状態で持つ。レイアウトに無い項目は既定値のままなので、
// 反映する側は「あるか」で分岐せず、全項目をそのまま適用すればよい(前のレイアウトの値が残らない)。
// ライブ状態(時計・太陽・編成・走行状態など)への反映は、デコード(.rsl・.rs2)では行わず
// ApplyNormalizedLayoutだけが確定後に行う。読込途中の失敗でライブ状態が汚染されないための境界。
struct NormalizedLayout {
    std::vector<LayoutSceneCache> scenes;
    size_t activeIndex = 0;
    rs::world::WarpLinkTable warpLinks;
    std::vector<rs::world::SceneEntity> cars;
    std::vector<TrainSpeedSeed> trainSpeedSeeds;
    std::vector<rs::world::TrainId> placedTrainIds;
    RslRuntimeState runtime;
    rs::world::TrainTagRegistrySnapshot trainTags;
    rs::world::TrainRunInfoRegistrySnapshot trainRunInfo;
    rs::editor::TrackLayingSettings layingSettings;
    rs::world::GameClock gameClock;
    rs::world::SolarOverrides solarOverrides;
    nlohmann::json formationDefsJson = nlohmann::json::array();
    nlohmann::json selectedFormationJson;
    std::vector<std::string> warnings;
    // 取り込み(.rs2)の時点で読めないと警告済みのレール系プラグインのフォルダ。
    // ApplyNormalizedLayoutは読み直さず、同じ警告を重ねて出さない。
    std::unordered_set<std::string> reportedTrackPluginFailures;
};

struct LayoutApplyRefs {
    LayoutSession& session;
    rs::editor::Editor& editor;
    rs::world::SceneRegistry& sceneRegistry;
    rs::world::GameClock& gameClock;
    rs::world::SolarOverrides& solarOverrides;
    SceneRenderer& sceneRenderer;
    CameraController& camera;
    PluginSelector& selector;
    TrainPlacementUiState* trainPlacementUi = nullptr;
    std::vector<ImportedWireSpan>* importedWireSpans = nullptr;
    // アクティブSceneをライブへ反映する直前に呼ばれる観測点。レール系プラグインの
    // 読込がその反映より先に完了することを検証するために使う。
    std::function<void()> beforeActiveSceneApply;
};

// firstIndex 以降の編成の定義をJSONにする(.rslの保存と、.rs2で取り込んだ編成の受け渡しで共用)。
nlohmann::json FormationsToJson(const PluginSelector& selector, size_t firstIndex = 0);
// firstIndex 以降の編成を取り除く(取り込みに失敗したとき、足した編成を戻す)。
void RemoveFormationsFrom(PluginSelector& selector, size_t firstIndex);
// レイアウトに保存する時刻と太陽の設定をJSONにする(LayoutTimeFromJsonの逆)。
nlohmann::json LayoutTimeToJson(const rs::world::GameClock& clock, const rs::world::SolarOverrides& overrides);
// 保存された時刻と太陽の設定を読む。欠けた項目と知らない名前は、渡された値のまま変えない。
void LayoutTimeFromJson(const nlohmann::json& json, rs::world::GameClock& clock,
                        rs::world::SolarOverrides& overrides);
// レイアウト反映前に、形式に依存しないライブ状態(配置物・走行状態)を破棄する。
void ResetLiveLayoutState(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer);
LayoutActionResult ApplyNormalizedLayout(NormalizedLayout&& layout, const LayoutApplyRefs& refs);

}  // namespace rs::app
