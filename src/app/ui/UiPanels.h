//
// UiPanels.h - ImGuiパネル描画関数
//
// 各パネルは操作対象への参照のみを受け取り、GUI描画と入力→編集の橋渡しを行う。
// デバイス/カメラ/プラグイン読込のオーケストレーションは main.cpp 側に残す。
//
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../render/mesh/Mesh.h"
#include "../camera/CameraControl.h"
#include "../action/LayoutPersistenceAction.h"
#include "../action/EdgePropertyAction.h"
#include "../action/EdgeSectionEditAction.h"
#include "../action/NodePropertyAction.h"
#include "../action/TrackLayingAction.h"
#include "../action/SceneEntitySwitchAction.h"
#include "../action/GameSettingsAction.h"
#include "../action/RenderSettingsAction.h"
#include "../action/PluginInfoAction.h"
#include "../action/ViewerSettingsAction.h"
#include "../camera/FlyCamera.h"
#include "../locale/Localization.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/Rs2SceneApplier.h"
#include "ImGuiPathPicker.h"
#include "ViewMode.h"
// 「編成」パネル(TrainPlacementUiState/DrawFormationListPanel)はFormationPanel.h/.cppにある。
// UiPanels.hをincludeする側から使えるよう、ここでもincludeする。
#include "FormationPanel.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class App;
class PluginSelector;
struct EditorViewportState;

// 「プラグイン」パネルの依存(現在読込中のプラグイン=PluginSession + 読込ログ)。
// エディタ/ビューワー両モード共通で使う。
struct PluginInfoPanelContext {
    PluginInfoAction& action;
    SceneEntitySwitchAction& sceneEntitySwitchAction;
    std::vector<std::string>& loadLog;
};

// 「プラグイン」パネル。PI情報(名前/type/作者)・カスタムスイッチ・
// 読込ログを描画する。パーツ個別選択UIは持たない(全パーツ同時表示が既定。
// 表示対象の切替はプラグイン選択系パネルの「表示」ボタンで行う)。
void DrawPluginInfoPanel(const PluginInfoPanelContext& ctx);

// 「ビューアー設定」ウィンドウの依存(プラグインビューアーモード専用)。
struct ViewerSettingsPanelContext {
    ViewerSettingsAction& action;
};

// 「ビューアー設定」ウィンドウ。ライト・ワイヤーフレーム・可動部の再生と速度・
// システムスイッチを描画する。
void DrawViewerSettingsPanel(const ViewerSettingsPanelContext& ctx);

// 「ゲーム設定」パネルの局所状態。
struct GameSettingsPanelState {
    // オーディオ出力デバイス列挙結果のキャッシュ(初回描画時+「更新」ボタン押下時のみ再列挙)。
    std::vector<std::string> cachedAudioOutputDevices;
    bool audioOutputDevicesFetched = false;
};

// 「ゲーム設定」パネルの依存。
struct GameSettingsPanelContext {
    GameSettingsAction& action;
    GameSettingsPanelState& state;
    ImGuiPathPicker& pathPicker;
};

// 「ゲーム設定」パネル。言語・音量・出力デバイス・プラグインルート(一覧表示と追加)・レイアウト保存先・fps・
// カメラを原点へ戻す操作・ゲーム内時刻表示とTimeScale編集・太陽位置(自転)モード選択を描画する。
void DrawGameSettingsPanel(const GameSettingsPanelContext& ctx);

// 「描画設定」パネルの依存。
struct RenderSettingsPanelContext {
    RenderSettingsAction& action;
    const Localization& localization;
};

// 「描画設定」パネル。描画距離・空と環境光・トーンマップ・シャドウ品質を描画する。
void DrawRenderSettingsPanel(const RenderSettingsPanelContext& ctx);

// 「敷設エディタ」パネル。ツール切替/Undo/Redo/角度スナップ/複線/緩和曲線
// 設定と、選択edge/nodeのプロパティ編集を描画する。敷設まわりの読み書きは
// TrackLayingAction を通す(直近フレームの敷設プレビューもActionが解釈する)。
// 下位セクション(区間分割編集・選択edge/nodeプロパティ)はそれぞれ専用のActionを通す。
// ワープ端点オーサリングUIは NodePropertyAction が担う。
struct EditorPanelContext {
    TrackLayingAction& action;
    EdgeSectionEditAction& sectionEditAction;
    EdgePropertyAction& edgeAction;
    NodePropertyAction& nodeAction;
    ImGuiPathPicker& pathPicker;
};

void DrawEditorPanel(const EditorPanelContext& ctx);

// 「レール系プラグイン」パネル(Rail/Tie/Girder/Pier/Line/Pole)。
// このリストは「読込済みプラグインの一覧」ではなく、「今から敷設するedge、
// または選択中のedgeに適用するプラグインの一覧」を表示・編集するUI。
// ・editor.selectedEdgeが未選択: Editor::layingSettings.defaultTrackPlugins(敷設テンプレート、
//   次に敷設確定したedgeへそのままコピーされる)を表示・編集する(Undo対象外、
//   グラフに属さないエディタ状態のため)。
// ・editor.selectedEdgeが選択中: TrackEdge::pluginSections(edge内区間分割、
//   区間の選択は「区間分割」パネル(EditorPanelSectionEdit)のSectionLayer::
//   PluginSet層)のうち、現在選択中の区間のpluginsを直接表示・編集する
//   (Editor::SetSelectedEdgeSectionFolders経由でUndo対応)。edge内での有効
//   範囲(start/end)の編集は「区間分割」パネル側(境界の分割/削除/移動)で行う。
// 「フォルダを追加…」でフォルダを選ぶと、まずPluginSelector::EnsureTrackPluginLoadedで
// 読込キャッシュへ確保してから現在の対象リストへfolderUtf8を足す(重複は
// 無視)。各行の「削除」は対象リストから外すだけで、読込キャッシュ自体は
// 消さない(他のedge/テンプレートが引き続き参照できるようにするため)。
// Pierのエントリには間隔[m](PI定義のPierInfo.Interval、手動編集不可)を表示のみ
// する(Rail/Tie/Girderは既存の連続押し出しの対象、Pole/Lineは対象外)。
// sceneRegistryはGPU常駐カウンタ(現在の常駐数)の表示にのみ使う。
// templateLastError は敷設テンプレート(RailwayPluginSet\*.txt)を選んで
// 対象リストへ一括追加する行専用のエラー表示(lastErrorとは別、TrainGroupTemplateApplier.h参照)。
void DrawTrackPluginListPanel(rs::editor::Editor& editor, PluginSelector& selector,
                             ImGuiPathPicker& pathPicker, std::string& lastError,
                             std::string& templateLastError,
                             const rs::world::SceneRegistry& sceneRegistry, EditorViewportState& state);

// 「情景プラグイン」パネルの依存(配置座標入力のUI状態。main.cppが所有する)。
// 位置(X/Y/Z)+yaw/pitch/roll(FreePlacementと同じ内因性yaw->pitch->roll順序)をパネル上部で共有入力し、「配置」
// ボタン押下時にFreePlacementへ変換する。この構造体→FreePlacementの変換点を
// 1箇所に集約し、ほかの入力手段も同じフィールドへ書き込めばよい形にする。値は次回配置時のデフォルトとして保持される。
struct SceneryPlacementUiState {
    rs::core::Vec3 position;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    bool continuousPlacement = false;
    bool snapToRail = false;
    bool stationAnchor = true;
    // 左右矢印キー1押下で回す角度[度]。
    float yawStepDeg = 15.0f;
};

// 「情景プラグイン」パネル(Station/Struct/Surface等、種別ごと単一選択)。
// sceneRegistryを渡すと各エントリに「配置」ボタンが出る。位置/yawはuiStateの
// 共有入力値を使う。
void DrawSingleSlotListPanel(rs::editor::Editor& editor, PluginSelector& selector,
                             ImGuiPathPicker& pathPicker, std::string& lastError,
                             rs::world::SceneRegistry& sceneRegistry, SceneryPlacementUiState& uiState);

// 「配置スイッチ」セクション。選択中のSceneEntityが対象で、プラグインの
// DefineSwitch宣言順の名前とその配置に適用されている現在値
// (SceneEntity::switchOptions)を一覧表示・編集する。編集するとSceneRegistry::
// SetSwitchOptionでswitchOptionsが書き換わり、既存のpark/revive・再バッチ機構が
// 自動で再ロード・再ベイクする。独立ウィンドウではなく「プラグイン」パネルの
// 中に描くため、Begin/Endは呼ばない。editor.HasSelectedSceneEntity()がfalseなら
// 何も描画しない。
struct SceneEntitySwitchContext {
    SceneEntitySwitchAction& action;
};

void DrawSceneEntitySwitchSection(const SceneEntitySwitchContext& ctx);

// 鑑賞中にビューポートのクリックで開く「プラグイン設定」ウィンドウ(RmlUi画面のプラグイン設定
// パネルに相当)。ctx.actionの対象のスイッチ一覧を独立したウィンドウに出す。×で閉じられたら
// trueを返す(呼び出し側が対象を空にする)。対象が見つからないときは何も描かずfalseを返す。
bool DrawSceneEntitySwitchWindow(const SceneEntitySwitchContext& ctx);

// 「表示切り替え」パネル。ラジオボタンでViewModeを選択する。常駐パネル。
void DrawViewModeSwitchPanel(ViewMode& mode);

// 「セーブデータ」パネルの依存。レイアウトの読込(CLIの--openlayoutと同じ
// LayoutPersistenceAction::LoadLayoutを呼ぶ)をUIから行えるようにする。保存は
// .rsl形式でSaveRslLayoutへ結線済み。
struct SaveDataPanelState {
    // 保存欄は拡張子なしのファイル名を受ける(.rslはAction側が補完する)。
    // 絶対パスを入れた場合はそのまま使い、保存先ディレクトリを無視する。
    char rslSaveNameBuf[512] = {};
    std::string lastError;
};

struct SaveDataPanelContext {
    LayoutPersistenceAction& action;
    const PluginSelector& selector;
    SaveDataPanelState& state;
    ImGuiPathPicker& pathPicker;
    std::vector<std::string>& log;
    const Localization& localization;
};

// 「セーブデータ」パネル。レイアウト読込(.rs2 / .rsl)、および.rsl形式での保存
// (全Sceneを保存する)を描画する。
void DrawSaveDataPanel(const SaveDataPanelContext& ctx);

// LayoutActionResultの失敗をユーザー向け文言へ変換する(SaveDataPanel.cppと
// AppRuntimeFrameUi.cppのTickPendingLayoutLoadが共有する。起動時CLI・ボタン
// どちらの読込結果も同じ1箇所で文言化するため、経路を分けない)。
std::string LayoutActionErrorMessage(const LayoutActionResult& result, const Localization& localization);

} // namespace rs::app
