//
// UpdateStartup.h - 更新の起動時の判定(マーカーに従う)と、最初のフレームでの成功の確認。Windows専用。
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "UpdateApplier.h"

namespace rs::app {

struct StartupMarkerOutcome {
    // 戻した実行ファイルを起動した(または戻しきれなかった)ので、呼び出し側はこのプロセスを終了する。
    bool exitProcess = false;
    // 前回の更新が失敗して前の版に戻っていた。
    bool rolledBackNotice = false;
};

// 起動の一番最初(GPU初期化より前)に、GUI起動のときだけ呼ぶ。マーカーの状態に従う。
// executablePath は main の最初に取ったパス(自分をリネームしたあとも、このパスを使う)。
StartupMarkerOutcome RunStartupMarkerCheck(const std::filesystem::path& executablePath, std::string_view currentVersion,
                                           const UpdateApplyOptions& options = {});

struct UpdateSuccess {
    std::string fromVersion;
    std::string toVersion;
};

// 最初のフレームを描いたあとに呼ぶ。マーカーが pending のときだけ、マーカーと計画にある
// `.bak` `.new` `.failed` を消して、成功した版を返す。消せないファイルがあっても失敗扱いにはしない。
std::optional<UpdateSuccess> ConfirmUpdateSucceeded(const std::filesystem::path& installDirectory);

// アプリの実行中に、更新まわりの結果と終了時の差し替えの入力を持つ。
struct UpdateSessionState {
    std::filesystem::path executablePath;
    bool rolledBackNotice = false;
    std::optional<UpdateSuccess> succeeded;
    // 終了後に差し替えるときの入力(restartForUpdate を立てる側が入れる)。
    std::filesystem::path stagingDirectory;
    std::string toVersion;
    bool firstFrameHandled = false;
    // 準備完了・失敗・キャンセルのお知らせを出した試行の番号(UpdateDownloader::Attempt)。
    // 同じ試行で二重に出さないために覚える(利用者が×で閉じたあとに出し直さない)。
    std::uint64_t readyNoticeAttempt = 0;
    std::uint64_t failedNoticeAttempt = 0;
    std::uint64_t cancelledNoticeAttempt = 0;
};

// 最初のフレームで1回だけ ConfirmUpdateSucceeded を呼ぶ。2回目以降の呼び出しと、無人実行では何もしない。
void ConfirmUpdateOnFirstFrame(UpdateSessionState& session, bool unattendedRun);

} // namespace rs::app
