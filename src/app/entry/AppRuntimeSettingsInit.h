//
// AppRuntimeSettingsInit.h - 起動時に言語・ユーザー設定・自動復元の初期状態を読み込む。
//
#pragma once

namespace rs::app {

struct AppRuntimeState;

void InitializeSettingsAndAutoRecovery(AppRuntimeState& state);

// 保存済みのウィンドウの設定(全画面表示)を、ウィンドウを見せる前に反映する。切り替えるUIが無い起動と、
// 解像度を固定したい検証用の起動では反映しない。
void ApplySavedWindowSettings(AppRuntimeState& state);

} // namespace rs::app
