#include "LayoutResultReporter.h"

#include <format>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "AppRuntime.h"
#include "LayoutDocumentController.h"
#include "../action/LayoutPersistenceAction.h"
#include "../action/ToastQueue.h"
#include "../rmlui/RmlUiIntegration.h"
#include "../rmlui/RmlUiWorkspaceView.h"
#include "../ui/UiPanels.h"

namespace rs::app {
namespace {

void LogResult(AppRuntimeState& state, const LayoutActionResult& result, const std::string& errorMessage) {
    if (result.Ok()) {
        state.loadLog.push_back("[layout] 読込: nodes=" + std::to_string(result.nodeCount) +
                                " edges=" + std::to_string(result.edgeCount) +
                                " placements=" + std::to_string(result.placementCount) +
                                " scenes=" + std::to_string(result.sceneCount));
    } else {
        state.loadLog.push_back("[layout] 失敗: " + errorMessage);
        // ヘッドレス実行でも読めるようログへも流す。
        spdlog::error("[layout] load failed: {} ({})", errorMessage, result.diagnostic);
    }
    for (const std::string& warning : result.warnings) state.loadLog.push_back("[layout] " + warning);
}

// 警告は件数だけを1件のトーストにする。本文は読込ログに残る。
void PostWarningToast(AppRuntimeState& state, const LayoutActionResult& result) {
    if (result.warnings.empty()) return;
    rmlui::RmlUiWorkspaceView* const workspace = state.rmlUiIntegration.WorkspaceView();
    if (workspace == nullptr) return;
    const size_t count = result.warnings.size();
    workspace->Toasts().Post(ToastSeverity::Warning,
                             std::vformat(state.localization.Text("layout.load.warnings"),
                                          std::make_format_args(count)));
}

}  // namespace

void ReportFinishedLayoutResults(AppRuntimeState& state, LayoutDocumentController* controller) {
    for (const FinishedLayoutResult& finished : state.LayoutAction().TakeFinishedResults()) {
        const LayoutActionResult& result = finished.result;
        const std::string errorMessage = result.Ok() ? std::string{} : LayoutActionErrorMessage(result, state.localization);
        state.modePanels.SaveDataPanel().lastError = errorMessage;
        LogResult(state, result, errorMessage);
        PostWarningToast(state, result);
        if (controller != nullptr) controller->OnLayoutFinished(finished, state.localization);
        // 起動時に指定したレイアウトの結果は、最初に終えた1件。呼び出し側のスクリプトが気づけるよう
        // 失敗を終了コードへ残す。
        if (state.startupLayoutPending) {
            state.startupLayoutPending = false;
            state.startupLayoutFailed = !result.Ok();
        }
    }
}

// 起動時の--openlayout指定を処理する。診断系CLIは完全同期、通常起動は予約経路に乗せる。
void OpenStartupLayout(AppRuntimeState& state, bool diagnosticsNeedPreloadedContent) {
    state.startupLayoutPending = true;
    if (diagnosticsNeedPreloadedContent) {
        // 診断系CLI(--capture-png等)はフレーム0時点で読込済みであることが前提のため、
        // 予約経由にせず完全同期で読み込む。結果の通知は予約経路と同じ受け口で行う。
        state.LayoutAction().LoadLayout(state.cli.openLayoutPath);
        ReportFinishedLayoutResults(state, nullptr);
    } else {
        // 通常起動: ボタン経由の読込と同じ予約経路に乗せる(経路を分けない)。
        // 実行はRunFrameLoop側のTickPendingLayoutLoadが1フレーム目以降に行う。
        state.LayoutAction().RequestLoadLayout(state.cli.openLayoutPath);
    }
}

} // namespace rs::app
