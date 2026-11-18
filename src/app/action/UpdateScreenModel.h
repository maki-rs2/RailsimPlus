//
// UpdateScreenModel.h - 設定画面の「バージョン情報」に出す更新の状態の文字とボタンを決める純粋関数。
//
// 確認の結果(UpdateChecker)とダウンロードの状態(UpdateDownloader)から、状態の文字・補足・出すボタンを作る。
// 状態は持たず、通信にもファイルにも触れない。
//
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "../locale/Localization.h"
#include "ToastQueue.h"
#include "../update/UpdateChecker.h"
#include "../update/UpdateDownloader.h"

namespace rs::app {

// 画面に出すボタンは常に1つだけ(出さないときはNone)。
enum class UpdateButton { None, Update, Cancel, Restart, Retry, OpenDownloadPage };

struct UpdateScreenInput {
    UpdateCheckState check = UpdateCheckState::Unchecked;
    std::string latestVersion;  // 確認で見つかった新しい版
    InAppUpdateAvailability availability = InAppUpdateAvailability::NoInstallableAsset;
    UpdateDownloadState download = UpdateDownloadState::Idle;
    UpdateDownloadFailure failure = UpdateDownloadFailure::None;
    UpdateDownloadProgress progress;
    std::string preparedVersion;  // 準備ができた版(Readyのとき)
};

struct UpdateScreen {
    std::string status;
    // 書き込めないために手動で更新するときの補足。無ければ空。
    std::string note;
    UpdateButton button = UpdateButton::None;
};

UpdateScreen BuildUpdateScreen(const Localization& localization, const UpdateScreenInput& input);

// 確認の結果とダウンロードの状態から、画面の入力を作る。どちらも無ければ(nullptr)その分は初期値のまま。
UpdateScreenInput MakeUpdateScreenInput(const UpdateChecker* checker, const UpdateDownloader* downloader);

// 更新のトースト(キー1つに対して中身を入れ替える)の内容。
struct UpdateToast {
    ToastSeverity severity = ToastSeverity::Info;
    ToastStyle style = ToastStyle::Update;
    std::string text;
    UpdateButton button = UpdateButton::None;
    // ボタンの文言(ボタンが無ければ空)。
    std::string buttonLabel;
    // 時間で消えない。
    bool sticky = true;
    // ×を出すか。ダウンロード中・確認中・展開中は閉じられない。
    bool closable = true;
    // ボタンを押しても閉じないか(ダウンロードページを開くときだけ、押すと閉じる)。
    bool keepOpenOnAction = false;
};

// 状態に応じたトーストの内容。出すものが無いとき(確認前・最新の版・確認の失敗)は nullopt。
// Idle と Cancelled は、確認の結果が Available のときだけ「新しい版があります」の内容になる。
std::optional<UpdateToast> BuildUpdateToast(const Localization& localization, const UpdateScreenInput& input);

// 画面の分岐(RML)と、イベントの処理が共有するボタンの名前。
std::string_view UpdateButtonName(UpdateButton button);

} // namespace rs::app
