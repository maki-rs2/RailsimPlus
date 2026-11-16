//
// RmlUiMissingPluginsView.h - 見つからないプラグインの警告バッジと一覧ウィンドウを、データモデル"workspace"へ結ぶ。
//
// RmlUiPluginRootsViewと同じ形で、RmlUiWorkspaceViewがメンバとして持ち、RegisterModelで変数とイベントを足す。
// 一覧はMissingPluginAction::Collectの結果で、毎フレームは作らない(MissingPluginRefreshGateが
// 変化を見張り、変わったときだけTickの中で作り直す)。表示用の値はMissingPluginViewModel.hの純粋関数が作り、
// ここは入れるだけにする。
// 「ルートを追加」は、ルート管理ウィンドウと同じイベント(root_add)を使い、フォルダ選択ダイアログと結果の
// 反映はRmlUiPluginRootsViewが受け持つ。ここが持つダイアログ(ShellPicker)は差し替え先の選択だけ。ダイアログのスレッドは、
// このビューが壊れるときに閉じて待つ。
//
// 重ね表示の開閉(toggle_overlay・Esc)はWorkspaceShellが受け持つ。DOMには触れない。
//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../action/MissingPluginAction.h"
#include "../action/MissingPluginRefreshGate.h"
#include "../action/MissingPluginViewModel.h"
#include "../action/ToastQueue.h"
#include "../../platform/file/ShellPickerDialog.h"

namespace rs::app {
class Localization;
struct UserSettingsData;
}  // namespace rs::app

namespace rs::app::plugin_browser {
class PluginBrowserService;
}  // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

struct MissingPluginsModel;
class RmlUiConfirmDialog;

class RmlUiMissingPluginsView {
public:
    RmlUiMissingPluginsView();
    ~RmlUiMissingPluginsView();
    RmlUiMissingPluginsView(const RmlUiMissingPluginsView&) = delete;
    RmlUiMissingPluginsView& operator=(const RmlUiMissingPluginsView&) = delete;

    // 操作の対象を渡す。どれも呼び出し側が所有し、このビューより長く生きること。
    // ownerWindow はフォルダ選択ダイアログの親にするウィンドウ(HWND)。
    void Attach(const MissingPluginRefs& refs, UserSettingsData& userSettings, ToastQueue& toasts,
                plugin_browser::PluginBrowserService& browser, const Localization& localization, void* ownerWindow);

    // 削除の確認に使う確認ダイアログを結ぶ。呼び出し側が所有し、このビューより長く生きること。
    void RegisterPopups(RmlUiConfirmDialog& dialog) { confirmDialog_ = &dialog; }

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 毎フレーム呼ぶ。signatureが変わっていれば一覧を作り直し、押された操作とダイアログの結果を実行する。
    // Attach前は何もしない。
    void Tick(const MissingPluginSignature& signature, double deltaSeconds);

    // 見つからないプラグインが1行も無いか(一覧が空になったらウィンドウを閉じるのに使う)。
    bool NothingMissing() const { return entries_.empty(); }

private:
    void OnReplace(const Rml::VariantList& arguments);
    void OnDelete(const Rml::VariantList& arguments);
    void OnRescan();
    void PostNotice(const MissingPluginNotice& notice);
    void RunDelete(const MissingPluginEntry& row);
    void RunReplace(const MissingPluginEntry& row, const std::string& folderUtf8);

    std::optional<MissingPluginRefs> refs_;
    UserSettingsData* userSettings_ = nullptr;
    ToastQueue* toasts_ = nullptr;
    plugin_browser::PluginBrowserService* browser_ = nullptr;
    const Localization* localization_ = nullptr;
    void* ownerWindow_ = nullptr;
    RmlUiConfirmDialog* confirmDialog_ = nullptr;

    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<MissingPluginsModel> model_;
    MissingPluginRefreshGate gate_;
    std::vector<MissingPluginEntry> entries_;
    // 画面の文言を作ったときの言語(変わったら文言と行を作り直す)。
    int shownLanguage_ = -1;
    bool rowsDirty_ = true;

    // 押された操作。行は押した時点の写しで持つ(ダイアログを開いている間に一覧が変わっても、別の行を触らない)。
    std::optional<MissingPluginEntry> pendingDelete_;
    std::optional<MissingPluginEntry> replaceTarget_;
    rs::platform_file::ShellPicker picker_;
};

}  // namespace rs::app::rmlui
