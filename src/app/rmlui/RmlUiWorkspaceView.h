//
// RmlUiWorkspaceView.h - WorkspaceShellの状態をworkspace.rmlのデータモデルへ反映し、
// data-event-clickをWorkspaceShellの操作へ変換する。
//
// WorkspaceShell自体(src/app/workspace/WorkspaceShell.h)はRmlUiに依存しない
// 純C++の状態機械であり、ここはそれとRmlUiのデータバインディング(`data-model`)を
// 結ぶ薄い変換層(ロジックはWorkspaceShell側)。DOMのid検索・クラスの手書き付け外しは
// 行わない(値の反映はすべてdata-if/data-class-*/data-style-*がRML側で行う)。
//
#pragma once

#include <functional>
#include <optional>
#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

#include "RmlUiFormationImages.h"
#include "RmlUiFormationListView.h"
#include "RmlUiTrainTabView.h"
#include "RmlUiSignalPanelView.h"
#include "RmlUiDiaPanelView.h"
#include "RmlUiEdgeEditView.h"
#include "RmlUiLayingSetView.h"
#include "RmlUiGameSettingsView.h"
#include "RmlUiHelpWindowView.h"
#include "RmlUiLayingEditorView.h"
#include "RmlUiLayoutFilesView.h"
#include "RmlUiLoadingView.h"
#include "RmlUiMissingPluginsView.h"
#include "RmlUiPluginBrowserView.h"
#include "RmlUiPluginRootsView.h"
#include "RmlUiPluginSettingsView.h"
#include "RmlUiRenderSettingsView.h"
#include "RmlUiRecoveryView.h"
#include "RmlUiSceneryYawStepView.h"
#include "RmlUiScenePanelView.h"
#include "RmlUiCameraScreenView.h"
#include "RmlUiTimeControlView.h"
#include "RmlUiTrainInfoCardView.h"
#include "RmlUiCabinHudView.h"
#include "RmlUiRailLabelView.h"
#include "RmlUiToastView.h"
#include "RmlUiTemplateSaveView.h"
#include "../action/ToastQueue.h"
#include "../input/EscapeRouting.h"
#include "../workspace/WorkspaceShell.h"

namespace Rml {
class Context;
class Element;
} // namespace Rml

namespace rs::app::rmlui {

// workspace.rmlの`data-model="workspace"`にバインドする表示専用の値の集まり。
// 定義と導出元はRmlUiWorkspaceView.cpp(FillFromSnapshot)。
struct WorkspaceViewModel;

// 主メニューで押された項目。処理はLayoutDocumentControllerが受け持つ。
enum class MainMenuCommand { None, Save, SaveAs, Load, Quit };
enum class ContextMenuKey;

class RmlUiWorkspaceView {
public:
    RmlUiWorkspaceView();
    ~RmlUiWorkspaceView();
    RmlUiWorkspaceView(const RmlUiWorkspaceView&) = delete;
    RmlUiWorkspaceView& operator=(const RmlUiWorkspaceView&) = delete;

    // Rml::Context::LoadDocument()より前に呼ぶこと。データモデルは文書を読み込む前に
    // 作る必要がある(RmlUiIntegration::Init参照)。起動直後(鑑賞)の値もここで入れる。失敗したらfalseを返す。
    bool CreateDataModel(Rml::Context& context);

    // 毎フレーム呼び、期限切れの除去と表示モデルの更新を行う。
    void Tick(double deltaSeconds);
    // 共通通知キュー。各機能は翻訳済み文言をここへ投稿する。
    rs::app::ToastQueue& Toasts() { return toastQueue_; }
    RmlUiScenePanelView& ScenePanel() { return scenePanel_; }
    RmlUiCameraScreenView& CameraScreen() { return cameraScreen_; }

    // Escの優先順位(WorkspaceShell::Back)を実行し、結果をデータモデルへ即時反映する。
    // toolOperationPendingは編集ツール側が判断した「取り消せる操作の途中か」。
    // ワークスペース表示時だけ呼ばれる想定。
    rs::app::workspace::BackResult Back(bool toolOperationPending);
    // モード切替はアイコン操作と同じWorkspaceShell::SelectMode経路を通す。
    void SelectMode(rs::app::workspace::WorkMode mode);
    // 鑑賞へ戻し、結果をデータモデルへ即時反映する。
    void ReturnToSpectate();
    // 運転台視点中かを画面状態へ知らせる(変わったときだけデータモデルへ反映する)。
    void SetCabinView(bool active);

    // 現在の画面状態(ViewMode・ToolModeの反映元)。
    const rs::app::workspace::WorkspaceSnapshot& Snapshot() const;
    // プラグイン設定パネルを開く要求・閉じる操作・今の対象(WorkspaceShellの同名メソッド)。
    // 開閉のあとデータモデルへ即時反映する。
    void OpenPluginSettings(const rs::world::Guid& targetEntityId);
    void ClosePluginSettings();
    // コードから重ね表示を開閉する。あとでデータモデルへ即時反映する。
    void OpenOverlay(rs::app::workspace::OverlayKind kind);
    void CloseOverlay(rs::app::workspace::OverlayKind kind);
    // 主メニューで押された項目を受け取る(押されていなければNone)。
    MainMenuCommand TakeMainMenuCommand();
    // レイアウトの一覧の画面(同じデータモデルへ変数とイベントを足している)。
    RmlUiLayoutFilesView& LayoutFiles() { return layoutFiles_; }
    // レイアウト読込中の表示(同じデータモデルへ変数を足している)。
    RmlUiLoadingView& Loading() { return loading_; }
    // 異常終了のあとの復元の画面(同じデータモデルへ変数とイベントを足している)。
    RmlUiRecoveryView& Recovery() { return recovery_; }
    const rs::world::Guid& PluginSettingsTarget() const;
    // 主メニューかゲーム設定が見えているか。見えている間はビューポートを操作させない。
    bool IsModalOverlayVisible() const;
    bool HandleListKey(ContextMenuKey key, Rml::Element* hover);
    void BindEscapeHandlers(input::EscapeHandlers& handlers) {
        if (IsModalOverlayVisible()) return;
        using input::EscapeTarget;
        using workspace::EnumIndex;
        handlers[EnumIndex(EscapeTarget::SignalPlacement)] = [this] { return railLabel_.CancelPendingPlacement(); };
        handlers[EnumIndex(EscapeTarget::TemplateSave)] = [this] { return templateSave_.CloseTransient(); };
        handlers[EnumIndex(EscapeTarget::TimeControl)] = [this] { return timeControl_.CloseTransient(); };
        handlers[EnumIndex(EscapeTarget::ScenePanel)] = [this] { return scenePanel_.CloseTransient(); };
        handlers[EnumIndex(EscapeTarget::DiaPanel)] = [this] { return diaPanel_.CloseTransient(); };
        handlers[EnumIndex(EscapeTarget::CameraScreen)] = [this] { return cameraScreen_.CloseTransient(); };
        handlers[EnumIndex(EscapeTarget::BrowserFilter)] = [this] { return pluginBrowser_.CloseFilterPanel(); };
    }
    bool HandleWorkspaceKeyDown(unsigned int virtualKey, Rml::Context& context, bool modalVisible);
    // 3D画面(UIの外)の左クリック(押してから離すまでほとんど動かなかったもの)。画面の状態に応じて受け取ったらtrue。
    bool HandleViewportClick(float x, float y) { return cameraScreen_.HandleViewportClick(x, y); }
    // カメラ画面のファインダーの枠。3Dをこの枠の中へ縮めて描く(カメラ画面のとき以外は値なし)。
    std::optional<workspace::CameraFinderRect> CameraFinder() const { return cameraScreen_.ActiveFinder(); }
    // 敷設エディタの敷設タブ(同じデータモデルへ変数とイベントを足している)。
    RmlUiLayingEditorView& LayingEditor() { return layingEditor_; }
    RmlUiEdgeEditView& EdgeEdit() { return edgeEdit_; }
    RmlUiDiaPanelView& DiaPanel() { return diaPanel_; }
    // 敷設エディタの敷設プラグインセットの段。
    RmlUiLayingSetView& LayingSet() { return layingSet_; }
    // 敷設・編成テンプレートの保存(名前入力と同名確認)。
    RmlUiTemplateSaveView& TemplateSave() { return templateSave_; }
    // プラグインブラウザ(左タブ。同じデータモデルへ変数とイベントを足している)。
    RmlUiPluginBrowserView& PluginBrowser() { return pluginBrowser_; }
    // ルート管理ウィンドウ(同じデータモデルへ変数とイベントを足している)。
    RmlUiPluginRootsView& PluginRoots() { return pluginRoots_; }
    // ゲーム設定ウィンドウ(一般・時間のタブ。同じデータモデルへ変数とイベントを足している)。
    RmlUiGameSettingsView& GameSettings() { return gameSettings_; }
    // ゲーム設定ウィンドウの描画タブ(同じデータモデルへ変数とイベントを足している)。
    RmlUiRenderSettingsView& RenderSettings() { return renderSettings_; }
    // 見つからないプラグインの警告バッジと一覧ウィンドウ(同じデータモデルへ変数とイベントを足している)。
    RmlUiMissingPluginsView& MissingPlugins() { return missingPlugins_; }
    // プラグイン設定パネルのスイッチ一覧(同じデータモデルへ変数とイベントを足している)。
    RmlUiPluginSettingsView& PluginSettings() { return pluginSettings_; }
    // 情景配置の矢印キーの回転刻みの入力欄(ブラウザの情景配置の設定にある。同じデータモデルへ足している)。
    RmlUiSceneryYawStepView& SceneryYawStep() { return sceneryYawStep_; }
    // 列車モードの左タブ下段の編成一覧(同じデータモデルへ変数とイベントを足している)。
    RmlUiFormationListView& FormationList() { return formationList_; }
    // 列車モードの左タブの階層(列車一覧 > ブラウザ)と編成テンプレートの分類。
    RmlUiTrainTabView& TrainTab() { return trainTab_; }
    // 信号モードの左タブ下段「信号の設定」パネル。
    RmlUiSignalPanelView& SignalPanel() { return signalPanel_; }
    // 下部バーの時間操作(ゲーム内時計の値を毎フレーム表示へ写す)。
    RmlUiTimeControlView& TimeControl() { return timeControl_; }
    // 列車モードの編成情報カード(先頭車の画面上に追従する。同じデータモデルへ変数とイベントを足している)。
    RmlUiTrainInfoCardView& TrainInfoCard() { return trainInfoCard_; }
    // 運転台視点中の右上パネルと戻るボタン。
    RmlUiCabinHudView& CabinHud() { return cabinHud_; }
    RmlUiRailLabelView& RailLabel() { return railLabel_; }
    // コンテキストメニューと確認ダイアログを、それを使う各ビューへ結ぶ。
    void RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog) {
        templateSave_.RegisterPopups(menu, dialog);
        formationList_.RegisterPopups(menu, dialog, templateSave_);
        trainInfoCard_.RegisterPopups(dialog);
        missingPlugins_.RegisterPopups(dialog);
        diaPanel_.RegisterPopups(dialog);
        scenePanel_.RegisterPopups(dialog);
        gameSettings_.InputSettings().RegisterPopups(dialog);
    }
    // 編成リストのカードと編成情報カードが共有する先頭車アイコン・車両の絵の画像。
    RmlUiFormationImages& FormationImages() { return formationImages_; }
    // モードボタンのヒント(ショートカットのキー)を引く割当表の元。呼び出し側が所有し、このビューより長く生きること。
    void AttachInput(const input::InputSource* input);

private:
    // WorkspaceShellの現在の状態からWorkspaceViewModelを作り直し、DataModelHandle::
    // DirtyAllVariables()で変更をRmlUiへ伝える。WorkspaceShellの操作メソッドを
    // 呼んだ直後に毎回呼ぶ(CreateDataModel内のイベントコールバック・Back()共通)。
    void RefreshViewModel();
    // WorkspaceShellの操作をdata-event-clickの名前へ結ぶ。actionにはイベント引数の先頭
    // (無ければ空文字列)を渡し、実行後にRefreshViewModel()を呼ぶ。
    void BindShellAction(Rml::DataModelConstructor& constructor, const Rml::String& name,
                         std::function<void(const Rml::String&)> action);
    // 引数が列挙のキー文字列(ToEnumKeyの小文字名)のイベント。列挙へ引いてactionへ渡し、未知の
    // 文字列は警告を出して無視する。定義はRmlUiWorkspaceView.cpp(そこでだけ使う)。
    template <typename E>
    void BindEnumShellAction(Rml::DataModelConstructor& constructor, const Rml::String& name,
                             std::function<void(E)> action);
    void BindShellActions(Rml::DataModelConstructor& constructor);

    rs::app::workspace::WorkspaceShell shell_;
    rs::app::ToastQueue toastQueue_;
    RmlUiToastView toastView_;
    RmlUiLayingEditorView layingEditor_;
    RmlUiEdgeEditView edgeEdit_;
    RmlUiLayingSetView layingSet_;
    RmlUiPluginBrowserView pluginBrowser_;
    RmlUiPluginRootsView pluginRoots_;
    RmlUiMissingPluginsView missingPlugins_;
    RmlUiGameSettingsView gameSettings_;
    RmlUiRenderSettingsView renderSettings_;
    RmlUiLayoutFilesView layoutFiles_;
    RmlUiRecoveryView recovery_;
    RmlUiLoadingView loading_;
    MainMenuCommand mainMenuCommand_ = MainMenuCommand::None;
    RmlUiPluginSettingsView pluginSettings_;
    RmlUiSceneryYawStepView sceneryYawStep_;
    RmlUiTemplateSaveView templateSave_;
    RmlUiFormationListView formationList_;
    RmlUiTrainTabView trainTab_;
    RmlUiSignalPanelView signalPanel_;
    RmlUiDiaPanelView diaPanel_;
    RmlUiScenePanelView scenePanel_;
    RmlUiCameraScreenView cameraScreen_;
    RmlUiTimeControlView timeControl_;
    RmlUiHelpWindowView helpWindow_;
    RmlUiTrainInfoCardView trainInfoCard_;
    RmlUiCabinHudView cabinHud_;
    RmlUiRailLabelView railLabel_;
    RmlUiFormationImages formationImages_;
    // 最新のスナップショット。RefreshViewModel()で更新し、Snapshot()/IsModalOverlayVisible()はこれを返す
    // (マウスメッセージのたびにShellから作り直さないため)。shell_の状態を変える経路は必ずRefreshViewModel()を呼ぶ。
    rs::app::workspace::WorkspaceSnapshot snapshot_;
    const input::InputSource* input_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<WorkspaceViewModel> viewModel_;
};

} // namespace rs::app::rmlui
