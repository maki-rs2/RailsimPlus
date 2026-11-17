//
// FormationPanel.h - 「編成」パネルと「編成の車両」パネル(複数編成管理UX)
//
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/TrainId.h"
#include "ImGuiPathPicker.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

using rs::world::TrainId;

class FormationAction;
class PluginSelector;

// 「編成」パネルの依存(グルーピング配置のUI状態。ModePanelsが所有する)。
// placedGroupIdsは「配置」ボタンで発番されたtrainIdの履歴(目標速度UIの
// 対象一覧を作るために保持する。編成が削除されても本状態からは自動では消えない)。
// 配置前のハイライト用。TrainPlacementInputがフレームごとに1回計算し、
// ViewportOverlayが描くだけにする(置けるかの判定を二重に計算しない)。
struct TrainPlacementPreview {
    bool valid = false;
    // 他の編成と重なって置けない(WouldTrainOverlapの結果)。
    bool blocked = false;
    // 先頭の位置と向き(Shiftによる反転を反映済み)。
    rs::world::TrackPosition head;
    // 編成全長[m]。先頭から後方へこの長さが占める範囲(重なり判定に使う)。
    double lengthM = 0.0;
};

struct TrainPlacementUiState {
    bool reversedHead = false;
    TrainPlacementPreview preview;
    std::vector<TrainId> placedTrainIds;
    // 編成情報カードを広げている列車(常に1本まで)。無ければNilGuid。
    // 画面で列車を選んだとき・列車を置いたとき・「配置中」の編成カードを押したときに変わる。
    TrainId focusedTrainId = rs::world::NilGuid();
};

struct FormationPanelState {
    std::string lastError;
    std::string templateLastError;
    char newTagLabel[128] = {};
    // 停止距離の入力値は編成ごとに持つ。1つの値を共有すると、ある編成で入れた値が
    // 全編成の表示に化ける。
    std::unordered_map<TrainId, float, rs::world::GuidHash> stopDistanceInputs;
};

struct FormationPanelContext {
    FormationAction& action;
    FormationPanelState& state;
    // DrawAddRow に渡すためだけに保持する。
    PluginSelector& selector;
    ImGuiPathPicker& pathPicker;
};

// 「編成」パネル(複数編成(Formation)の作り分け+選択)と、選択中の
// 編成の車両編集を行う「編成の車両」パネル(存在するときのみ描画)。
// 実際の配置(先頭車の軌道位置を指定して選択中の編成
// (PluginSelector::SelectedFormationCars)を各車両自身の実長でSceneEntityとして
// 一括配置する処理)はビューポート側のTrainPlacementInput.hが担う。
// 本パネルは反転指定の入力と、配置済みグループの目標速度スライダーを提供する。
// templateLastError は編成テンプレート(TrainGroupTemplate\*.txt)を選んで
// 新規編成として追加する行専用のエラー表示(lastErrorとは別、TrainGroupTemplateApplier.h参照)。
void DrawFormationListPanel(const FormationPanelContext& ctx);

} // namespace rs::app
