//
// RmlUiGameSettingsView.h - ゲーム設定ウィンドウ(一般・描画・操作のタブ)を、データモデル"workspace"へ結ぶ。
//
// RmlUiMissingPluginsViewと同じ形で、RmlUiWorkspaceViewがメンバとして持ち、RegisterModelで変数と
// イベントを足す。値の書き込みはGameSettingsActionを通す(ImGui版と同じ呼び出し)。画面に出す文字と
// 選択肢はGameSettingsScreenModel.hの純粋関数が作り、ここは入れるだけにする。
// 変数名は重ね表示の名前(gameSettings)と衝突しないよう、すべて接頭辞gsを付ける。
//
// 音量スライダーは、ドラッグ中のchangeでは反映だけ行い、離したとき(dragend・mouseup・blur・keyup)
// にまとめて保存する。変更のない保存は行わない。レイアウト保存先のフォルダ選択ダイアログ(ShellPicker)の持ち主もここで、
// ダイアログのスレッドは、このビューが壊れるときに閉じて待つ。
//
// プラグインルートの管理ウィンドウは、RML側のopen_overlay('pluginRoots')で開く(ここは件数の表示だけ)。
// 重ね表示の開閉はWorkspaceShellが受け持つ。DOMには触れない(入力欄の値の読み書きは、イベントの
// 対象の要素だけ)。
//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiTheme.h"
#include "RmlUiInputSettingsView.h"
#include "../workspace/GameSettingsPresentation.h"
#include "../action/GameSettingsAction.h"
#include "../../platform/file/ShellPickerDialog.h"

namespace rs::app::rmlui {

using workspace::GameSettingsModel;

class RmlUiGameSettingsView {
public:
    explicit RmlUiGameSettingsView(ToastQueue& queue);
    ~RmlUiGameSettingsView();
    RmlUiGameSettingsView(const RmlUiGameSettingsView&) = delete;
    RmlUiGameSettingsView& operator=(const RmlUiGameSettingsView&) = delete;

    // 操作の対象を渡す。どれも呼び出し側が所有し、このビューより長く生きること。
    // ownerWindow はフォルダ選択ダイアログの親にするウィンドウ(HWND)。
    // themeSetはUIテーマの一覧(選択肢の元)。RmlUiIntegrationが持つものを渡す。
    void Attach(const GameSettingsRefs& refs, void* ownerWindow, const UiThemeSet& themeSet);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 毎フレーム呼ぶ。ダイアログが終わっていれば選ばれたフォルダを反映し、ウィンドウが見えているときだけ
    // 画面の値を更新する。framesPerSecondは平均のフレームレート。Attach前は何もしない。
    void Tick(bool windowVisible, float framesPerSecond);
    RmlUiInputSettingsView& InputSettings() { return inputSettings_; }

private:
    void RegisterAboutModel(Rml::DataModelConstructor& constructor);
    void UpdateAboutModel();
    void OnUpdateCheckEnabled(Rml::Event& event);
    void OnCheckUpdate();
    void OnOpenDownloadPage();
    void OnSelectTab(const Rml::VariantList& arguments);
    void OnSetLanguage(const Rml::String& key);
    void OnSetUiTheme(const Rml::VariantList& arguments);
    void OnVolumeCommit();
    void OnAudioDeviceChange(Rml::Event& event);
    void OnBrowseLayoutDirectory();
    void OnTimeScaleCommit(Rml::Event& event);
    void OnTimeScaleKey(Rml::Event& event);
    // 時刻の入力欄の文字を今のTimeScaleの表示に戻す。
    void ResetTimeScaleText(Rml::Element& element);
    void RefreshTexts();
    void RefreshAudioDevices();

    std::optional<GameSettingsRefs> refs_;
    void* ownerWindow_ = nullptr;
    const UiThemeSet* themeSet_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<GameSettingsModel> model_;
    rs::platform_file::ShellPicker picker_;

    // 画面の文言を作ったときの言語(変わったら文言と選択肢を作り直す)。
    int shownLanguage_ = -1;
    // 出力デバイスの列挙結果(最初に見えたときと「更新」で取り直す)と、選択肢の実体(添字は画面の選択位置)。
    std::vector<std::string> enumeratedDevices_;
    bool devicesFetched_ = false;
    std::vector<std::string> audioDeviceNames_;
    // 既定のフォルダ(表示用。実行中は変わらないのでAttachで1回だけ求める)。
    std::string defaultRootFolder_;
    std::string defaultLayoutFolder_;
    // 音量を反映してから保存していない間true。
    bool volumeUncommitted_ = false;
    bool timeScaleFocused_ = false;
    bool suppressTimeScaleBlurCommit_ = false;
    ToastQueue& queue_;
    workspace::GameSettingsPresentation presentation_;
    RmlUiInputSettingsView inputSettings_;
};

} // namespace rs::app::rmlui
