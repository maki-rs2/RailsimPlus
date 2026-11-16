//
// RmlUiPluginRootsView.h - ルート管理ウィンドウ(プラグインフォルダの追加・削除・並べ替え・再走査)を
// データモデル"workspace"へ結ぶ。
//
// RmlUiPluginBrowserViewと同じ形で、
// RmlUiWorkspaceViewがメンバとして持ち、RegisterModelで変数とイベントを足す。ルートの編集は
// PluginRootActionを通す。ルートを変えたあとの再走査は、PluginBrowserServiceが毎フレーム
// ルートを見比べて行う(ここから走査は呼ばない。再走査ボタンだけRescanを呼ぶ)。
// フォルダ選択ダイアログ(ShellPicker)の持ち主もここ。ダイアログのスレッドは、このビューが
// 壊れるときに閉じて待つ。
//
// 重ね表示の開閉(open_overlay/close_overlay・Esc)はWorkspaceShellが受け持ち、ここは値と操作だけを持つ。
// DOMには触れない。
//
#pragma once

#include <memory>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../../platform/file/ShellPickerDialog.h"

namespace rs::app {
struct UserSettingsData;
} // namespace rs::app

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

struct PluginRootsModel;

class RmlUiPluginRootsView {
public:
    RmlUiPluginRootsView();
    ~RmlUiPluginRootsView();
    RmlUiPluginRootsView(const RmlUiPluginRootsView&) = delete;
    RmlUiPluginRootsView& operator=(const RmlUiPluginRootsView&) = delete;

    // 操作の対象を渡す。どれも呼び出し側が所有し、このビューより長く生きること。
    // ownerWindow はフォルダ選択ダイアログの親にするウィンドウ(HWND)。
    void Attach(plugin_browser::PluginBrowserService& service, UserSettingsData& userSettings, void* ownerWindow);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // ウィンドウが見えているフレームと、DialogOpenのフレームに呼ぶ。Attach前は何もしない。
    void Refresh();
    // フォルダ選択ダイアログを開いているか(開いている間は、ウィンドウを閉じても結果を受け取るために
    // Refreshを呼ぶ)。
    bool DialogOpen() const { return picker_.Open(); }
    // 起動時にルートが空で自動的に開くとき、開く前に呼ぶ。ルートが1つ登録されるまで、
    // タイトルを最初の設定向けの文言にする。
    void SetFirstRun() { firstRun_ = true; }

private:
    void OnAdd();
    void OnRemove(const Rml::VariantList& arguments);
    void OnMove(const Rml::VariantList& arguments);
    void OnRescan();

    plugin_browser::PluginBrowserService* service_ = nullptr;
    UserSettingsData* userSettings_ = nullptr;
    void* ownerWindow_ = nullptr;
    bool firstRun_ = false;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<PluginRootsModel> model_;
    rs::platform_file::ShellPicker picker_;
};

} // namespace rs::app::rmlui
