//
// RmlUiFormationListView.h - 列車モードの左タブ下段「編成一覧」をデータモデル"workspace"へ結ぶ
//
// 値の読み書きはImGui版と同じFormationActionを通す。編成カードの表示(名前・車両の並び・両数)と選択、
// 新規編成、配置時の反転トグル、車両の絵の操作(cars_へ委譲)、カード単位の操作(削除ボタン・右クリックメニュー・名前のダブルクリック編集・
// 上下キーでの選択移動)を受け持つ。カードの対象は添字でなく編成のguid(16進文字列)で覚える
// (削除・複製・並びの変化で添字はずれるため)。RmlUiWorkspaceViewがメンバとして持ち、
// CreateDataModelの中でRegisterModelを呼んで同じデータモデルへ変数とイベントを足す
// (別のdata-modelは作らない)。
//
// 毎フレームの更新(Refresh)は、前回データモデルへ入れた値と比べて、変わった変数だけ
// DirtyVariableする(DirtyAllVariablesは呼ばない)。
//
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <RmlUi/Core/DataModelHandle.h>

#include "../workspace/ContextMenuCatalog.h"
#include "RmlUiFormationCarsView.h"

namespace Rml {
class Element;
class ElementDocument;
class Event;
} // namespace Rml

namespace rs::app {
struct FormationRefs;
class ToastQueue;
} // namespace rs::app

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

struct FormationListModel;
class RmlUiConfirmDialog;
class RmlUiContextMenu;
class RmlUiFormationImages;
class RmlUiTemplateSaveView;

// 右クリックメニュー(ContextMenuKind::FormationCard)の提供者も兼ねる。
class RmlUiFormationListView : public workspace::IContextMenuProvider {
public:
    RmlUiFormationListView();
    ~RmlUiFormationListView();
    RmlUiFormationListView(const RmlUiFormationListView&) = delete;
    RmlUiFormationListView& operator=(const RmlUiFormationListView&) = delete;

    // 編成の操作の対象と、ドラッグされたブラウザのカードからフォルダを引くためのブラウザを渡す
    // (どちらも呼び出し側が所有し、このビューより長く生きること)。
    void Attach(const FormationRefs& refs, plugin_browser::PluginBrowserService& browser, ToastQueue& queue);

    // カードの右クリックメニューの提供者として登録し、削除の確認に使うダイアログを受け取る。
    // 両方ともこのビューより長く生きること(RmlUiIntegrationが持つ)。
    void RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog, RmlUiTemplateSaveView& templateSave);

    // 上下キーでカードの選択を前後へ移す(端で止まる)。hoverはポインタの下の要素で、編成一覧の中でなければ
    // 何もしない。移したか、編成があって端で止まったときtrue(呼び出し側はキーを消費する)。
    // 入力欄・メニュー・ダイアログのキー占有の判定は呼び出し側で済ませておく。
    bool HandleArrowKey(int direction, Rml::Element* hover);
    bool HandleCarKeyDown(unsigned int virtualKey, Rml::Element* hover) { return cars_.HandleKeyDown(virtualKey, hover); }
    bool InsertPluginAfterFocused(int entryId) { return cars_.InsertPluginAfterFocused(entryId); }

    // IContextMenuProvider
    bool TargetExists(workspace::ContextMenuKind kind, const std::string& targetId) const override;
    bool IsItemEnabled(workspace::ContextMenuKind kind, std::string_view itemId, const std::string& targetId) const override;
    void OnItemChosen(workspace::ContextMenuKind kind, std::string_view itemId, const std::string& targetId) override;

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 毎フレーム呼ぶ。sectionVisibleは「左タブがブラウザで、プロファイルが列車のとき」。
    // panelVisibleは「プロファイルが列車のとき」(左タブをしまっても編成編集パネルの値は更新する。
    // 編成編集パネルを出すのは、さらにブラウザのページを出しているとき)。
    // 見えていないときは編成の値を読まず、セクションの表示フラグだけ更新する。Attach前は何もしない。
    // imagesは編成情報カードと共有する画像。呼び出し側が1フレームに1回だけBeginFrame/EndFrameで囲む
    // (panelVisibleがtrueのときは囲みの中で呼ぶ)。
    void Refresh(bool sectionVisible, bool panelVisible, RmlUiFormationImages& images);

private:
    // カードの削除ボタンとメニューの「この編成を削除」の共通の入口。確認ダイアログを出し、確定で削除する。
    void RequestRemove(const std::string& cardId);
    // 名前の編集。ダブルクリックで始め、Enter・blurで確定、Escで取り消す。
    void OnBeginRename(Rml::Event& event, const std::string& cardId);
    void OnCommitName(Rml::Event& event);
    void OnKeyName(Rml::Event& event);
    void EndEditing();
    // 入力欄が見えるようになってから、値を入れてフォーカスを移す(見えるようになるのは次のレイアウト後)。
    void FocusEditingInput();

    std::unique_ptr<FormationRefs> refs_;
    RmlUiConfirmDialog* confirmDialog_ = nullptr;
    RmlUiTemplateSaveView* templateSave_ = nullptr;
    Rml::ElementDocument* document_ = nullptr; // 名前編集の入力欄を探す文書(最初のダブルクリックで得る)
    bool pendingFocus_ = false;
    std::string editingInitialName_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<FormationListModel> model_;
    RmlUiFormationCarsView cars_; // 車両の絵の操作(ゴミ箱・反転・ドラッグ&ドロップ)
};

} // namespace rs::app::rmlui
