#pragma once

namespace rs::app {
struct AppRuntimeState;
// 起動時に1回。無人実行でなければ、前回の更新の作業フォルダを捨てて、更新の確認を始める。
void StartUpdateCheck(AppRuntimeState& state);
void PollUpdateCheck(AppRuntimeState& state);
// 毎フレーム。ダウンロードの結果の取り込み・古い準備の破棄と、更新まわりのお知らせ(準備完了・失敗・更新後・ロールバック後)。
void PollUpdateDownload(AppRuntimeState& state);
// 更新のための終了が実際に行われる直前に呼ぶ。準備が済んでいれば、終了後の差し替えの入力を state へ入れる。
void PrepareUpdateRestart(AppRuntimeState& state);
} // namespace rs::app
