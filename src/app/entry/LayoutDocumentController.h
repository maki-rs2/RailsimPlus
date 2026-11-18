//
// LayoutDocumentController.h - 主メニューの保存・ロード・終了を、状態機械と画面と保存の処理へつなぐ。
//
// --rmlui-workspace のときだけ毎フレーム呼ぶ。判断はLayoutDocumentFlowにあり、ここはその指示どおりに
// ダイアログや一覧の画面を出し、LayoutPersistenceActionを呼ぶ。ダイアログの答えはウィンドウのメッセージの処理中にも
// 届くので、覚えておいて次のTickで処理する。
//
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../plugin_session/AutoRecovery.h"
#include "../workspace/LayoutDocumentFlow.h"

namespace rs::app {

class App;
class Localization;
class LayoutPersistenceAction;
class MainWindow;
struct FinishedLayoutResult;
struct LayoutActionResult;
struct UserSettingsData;

namespace rmlui {
class RmlUiIntegration;
class RmlUiWorkspaceView;
}

struct LayoutDocumentServices {
    rmlui::RmlUiIntegration& rmlUi;
    LayoutPersistenceAction& action;
    UserSettingsData& userSettings;
    MainWindow& mainWindow;
    const Localization& localization;
    App& app;
    // 更新のための終了が実際に行われる直前に呼ぶ(終了後の差し替えの入力を決める)。空なら何もしない。
    std::function<void()> prepareUpdateRestart;
};

class LayoutDocumentController {
public:
    void Tick(const LayoutDocumentServices& services);

    // ウィンドウの×(WM_CLOSE)。次のTickで終了の流れを始める。
    void RequestClose() { closeRequested_ = true; }
    // 更新を適用するための終了。未保存の確認などを通常の終了と同じように通し、終了が行われるときだけ
    // prepareUpdateRestart が呼ばれる。確認で取りやめられたら要求も消える。
    void RequestCloseForUpdate() {
        closeRequested_ = true;
        closeForUpdate_ = true;
    }
    // 起動時に、復元の画面を出す。選ぶまでレイアウトを開かない。startupLayoutPathは通常起動で開くレイアウト(無ければ空)。
    void BeginRecoveryChoice(std::vector<AutoRecoveryCandidate> candidates, std::string startupLayoutPath);
    // 読込・新規が終わったとき。失敗は出どころによらず知らせる。最近使ったファイルへの追加と復元の後始末は、
    // 一覧の画面や復元から始めた読込のときだけ行う。
    void OnLayoutFinished(const FinishedLayoutResult& finished, const Localization& localization);

    // 保存はサムネイルを撮ってから書く。描画の前に呼ぶ。保存を待っていれば、UIを除いた画面の読み戻しを要求する。
    // otherCaptureRequestedなら、ほかの読み戻しと重なるので次のフレームまで待つ。
    void BeforeRender(App& app, bool otherCaptureRequested);
    // BeforeRenderで読み戻しを要求したか。
    bool SaveCaptureRequested() const { return saveCaptureRequested_; }
    // 描画のあとに呼ぶ。読み戻しを要求していれば、サムネイルを作って保存する。
    void AfterRender(App& app, LayoutPersistenceAction& action, const Localization& localization);

    // 名前が空なら「無題」の文言にする(ダイアログと復元の画面で共通)。
    static std::string NameOrUntitled(const std::string& name, const Localization& localization);

private:
    void Apply(const workspace::LayoutFlowStep& step, const LayoutDocumentServices& services);
    // 読込の予約中・読込中か。この間に来た読込・保存の要求は無視する(読込ジョブの途中は Busy で断られる)。
    bool IsBusy(const LayoutDocumentServices& services) const;
    // 読込中の表示を出し入れする。読込は数秒メインのスレッドを止めるので、止める前に表示が一度画面に出ているよう、
    // 表示を出した次のTickで、読込の実行を許す(LayoutPersistenceAction::MarkPendingLoadIndicatorShown)。
    void UpdateLoadingDisplay(const LayoutDocumentServices& services);
    void HandleMenuCommand(const LayoutDocumentServices& services);
    void HandleFilesCommand(const LayoutDocumentServices& services);
    void OpenFilesScreen(bool saveAs, const LayoutDocumentServices& services);
    void ShowUnsavedWarning(const LayoutDocumentServices& services);
    void ShowOverwriteConfirm(const std::string& pathUtf8, const LayoutDocumentServices& services);
    void ShowQuitConfirm(const LayoutDocumentServices& services);
    void TickRecoveryChoice(const LayoutDocumentServices& services);
    void RefreshRecoveryCards(const LayoutDocumentServices& services);
    void ShowDiscardRecoveryConfirm(std::size_t index, const LayoutDocumentServices& services);
    // 復元の画面を閉じて、通常起動(safeModeならセーフモード)のレイアウトを開く。
    void FinishRecoveryWithStart(bool safeMode, const LayoutDocumentServices& services);

    workspace::LayoutDocumentFlow flow_;
    bool closeRequested_ = false;
    bool closeForUpdate_ = false;
    bool loadFromFilesScreen_ = false;
    // 一覧の画面を閉じてから読み込むため、次のTickまで待たせる読込。読込は数秒メインのスレッドを止め、
    // その間は画面が更新されないので、閉じる前に始めると一覧が残ったままクリックも受けてしまう。
    struct DeferredLoad {
        std::string pathUtf8; // 空なら新規レイアウト
        bool discardRecovery = false;
    };
    std::optional<DeferredLoad> deferredLoad_;
    bool loadingDisplayed_ = false; // 前のTickで読込中の表示を出したか
    std::optional<workspace::UnsavedChoice> unsavedAnswer_;
    std::optional<bool> overwriteAnswer_;
    std::optional<bool> quitAnswer_;
    std::string failureMessage_;
    // 待っている保存の保存先。空なら待っていない。
    std::string pendingSavePathUtf8_;
    bool saveCaptureRequested_ = false;
    std::optional<bool> saveFinished_;
    // 次のTickで最近使ったファイルへ足すパス。
    std::string recentToAddUtf8_;
    std::string loadingPathUtf8_;

    // 復元の画面。candidatesが空でなくても、recoveryOpen_が偽なら画面はもう閉じている。
    bool recoveryChoicePending_ = false;
    bool recoveryOpen_ = false;
    bool restoringRecovery_ = false;
    bool startAfterRestoreFailure_ = false;
    std::vector<AutoRecoveryCandidate> recoveryCandidates_;
    std::string startupLayoutPath_;
    std::optional<std::size_t> discardAnswer_;
};

} // namespace rs::app
