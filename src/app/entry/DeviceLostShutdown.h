//
// DeviceLostShutdown.h - デバイスロストを検出したあとの終了手順(順序だけを持つ純ロジック)
//
// 手順: (a) 理由をログへ出す → (b) 未保存のレイアウトがあればバックアップへ保存 →
// (c) エラーのメッセージボックスを出す → (d) アプリを終了する。デバイスの作り直しはしない。
// 各手順の中身(ログ・保存・表示・終了)は呼び出し側が渡す。
//
#pragma once

#include <functional>

namespace rs::app {

struct DeviceLostActions {
    // (a) 理由(GetDeviceRemovedReason とDRED)をログへ出す。
    std::function<void()> logReason;
    // 未保存のレイアウトがあり、バックアップを書いてよい状態か。
    std::function<bool()> shouldSaveBackup;
    // (b) バックアップへ保存する。成功したら true。
    std::function<bool()> saveBackup;
    // (c) エラーを表示する。backupAttempted は保存を試みたか、backupSaved はそれが成功したか。
    std::function<void(bool backupAttempted, bool backupSaved)> showError;
    // (d) アプリを終了する。
    std::function<void()> quit;
};

struct DeviceLostOutcome {
    bool backupAttempted = false;
    bool backupSaved = false;
};

// (a)(b)(c)(d)をこの順に実行する。(b)で例外が出ても(c)(d)へ進む。
DeviceLostOutcome RunDeviceLostShutdown(const DeviceLostActions& actions);

} // namespace rs::app
