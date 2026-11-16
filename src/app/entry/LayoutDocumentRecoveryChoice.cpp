// LayoutDocumentController のうち、起動時の復元の画面(候補の一覧・破棄の確認・通常起動とセーフモード)の処理。
#include "LayoutDocumentController.h"

#include <format>
#include <utility>

#include "../action/LayoutPersistenceAction.h"
#include "../action/RenderSettingsAction.h"
#include "../plugin_session/LayoutFileCatalog.h"
#include "../rmlui/RmlUiConfirmDialog.h"
#include "../rmlui/RmlUiIntegration.h"
#include "../settings/UserSettings.h"
#include "../workspace/WorkspaceShell.h"

namespace rs::app {

using workspace::OverlayKind;

namespace {

std::string RecoveryCandidateName(const AutoRecoveryCandidate& candidate, const Localization& localization) {
    if (!candidate.sourcePathUtf8.empty()) return LayoutNameFromPath(candidate.sourcePathUtf8);
    return LayoutDocumentController::NameOrUntitled(candidate.displayNameUtf8, localization);
}

} // namespace

void LayoutDocumentController::BeginRecoveryChoice(std::vector<AutoRecoveryCandidate> candidates,
                                                   std::string startupLayoutPath) {
    recoveryCandidates_ = std::move(candidates);
    startupLayoutPath_ = std::move(startupLayoutPath);
    recoveryChoicePending_ = true;
}

void LayoutDocumentController::TickRecoveryChoice(const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView& view = *services.rmlUi.WorkspaceView();
    if (std::exchange(recoveryChoicePending_, false)) {
        recoveryOpen_ = true;
        RefreshRecoveryCards(services);
        view.OpenOverlay(OverlayKind::Recovery);
    }
    if (!recoveryOpen_) return;

    if (std::optional<std::size_t> index = std::exchange(discardAnswer_, std::nullopt)) {
        if (*index < recoveryCandidates_.size()) {
            services.action.DiscardRecovery(recoveryCandidates_[*index].guid);
            recoveryCandidates_.erase(recoveryCandidates_.begin() + static_cast<std::ptrdiff_t>(*index));
            RefreshRecoveryCards(services);
        }
        if (recoveryCandidates_.empty()) {
            FinishRecoveryWithStart(false, services);
            return;
        }
    }

    const std::optional<rmlui::RecoveryCommand> command = view.Recovery().TakeCommand();
    if (!command.has_value()) return;
    switch (command->kind) {
    case rmlui::RecoveryCommand::Kind::Restore:
        if (command->index >= recoveryCandidates_.size()) break;
        restoringRecovery_ = true;
        services.action.RequestRestoreRecovery(recoveryCandidates_[command->index]);
        recoveryOpen_ = false;
        view.CloseOverlay(OverlayKind::Recovery);
        break;
    case rmlui::RecoveryCommand::Kind::Discard:
        if (command->index < recoveryCandidates_.size()) ShowDiscardRecoveryConfirm(command->index, services);
        break;
    case rmlui::RecoveryCommand::Kind::NormalStart: FinishRecoveryWithStart(false, services); break;
    case rmlui::RecoveryCommand::Kind::SafeMode: FinishRecoveryWithStart(true, services); break;
    }
}

void LayoutDocumentController::RefreshRecoveryCards(const LayoutDocumentServices& services) {
    std::vector<rmlui::RecoveryCardContent> cards;
    for (const AutoRecoveryCandidate& candidate : recoveryCandidates_) {
        cards.push_back({RecoveryCandidateName(candidate, services.localization),
                         FormatLayoutFileTime(candidate.writeTime)});
    }
    services.rmlUi.WorkspaceView()->Recovery().SetCards(cards);
}

void LayoutDocumentController::ShowDiscardRecoveryConfirm(std::size_t index, const LayoutDocumentServices& services) {
    rmlui::RmlUiConfirmDialog* const dialog = services.rmlUi.ConfirmDialog();
    if (dialog == nullptr) return;
    rmlui::ConfirmDialogContent content;
    const std::string name = RecoveryCandidateName(recoveryCandidates_[index], services.localization);
    content.message = std::vformat(services.localization.Text("layout.document.discard_recovery"),
                                   std::make_format_args(name));
    content.confirmLabel = services.localization.Text("layout.document.delete");
    content.cancelLabel = services.localization.Text("layout.document.cancel");
    content.danger = true;
    content.onConfirm = [this, index] { discardAnswer_ = index; };
    dialog->Open(std::move(content));
}

void LayoutDocumentController::FinishRecoveryWithStart(bool safeMode, const LayoutDocumentServices& services) {
    recoveryOpen_ = false;
    services.rmlUi.WorkspaceView()->CloseOverlay(OverlayKind::Recovery);
    if (safeMode) {
        ApplyRenderSettings(services.app, MakeSafeModeRenderSettings(services.userSettings));
        services.action.NewLayout();
    } else if (!startupLayoutPath_.empty()) {
        services.action.RequestLoadLayout(startupLayoutPath_);
    } else {
        services.action.NewLayout();
    }
}

} // namespace rs::app
