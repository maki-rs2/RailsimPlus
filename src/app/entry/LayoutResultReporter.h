//
// LayoutResultReporter.h - 読込・新規の結果を、出どころによらず同じ経路で通知する。
//
#pragma once

namespace rs::app {

struct AppRuntimeState;
class LayoutDocumentController;

// LayoutPersistenceActionが終えた読込・新規の結果をすべて取り出し、1件ずつ次へ通知する。
//  - 読込ログと、セーブデータパネルのエラー表示(ImGui)
//  - ワークスペース(RmlUi)の失敗ダイアログ(controller経由)と警告のトースト
//  - 起動時に指定したレイアウトの失敗(終了コード)
// controllerはフレームループの中でだけ渡せる。nullptr(ループの前の同期読込)のときは、その通知を省く。
void ReportFinishedLayoutResults(AppRuntimeState& state, LayoutDocumentController* controller);

// 起動時の--openlayout指定を処理する。診断系CLIは完全同期、通常起動は予約経路に乗せる。
void OpenStartupLayout(AppRuntimeState& state, bool diagnosticsNeedPreloadedContent);

}  // namespace rs::app
