#include "LayoutDocumentFlow.h"

namespace rs::app::workspace {

LayoutFlowStep LayoutDocumentFlow::Reset() {
    stage_ = Stage::Idle;
    goal_ = Goal::None;
    savedPathUtf8_.clear();
    loadPathUtf8_.clear();
    overwritePathUtf8_.clear();
    quitForUpdate_ = false;
    return {};
}

LayoutFlowStep LayoutDocumentFlow::BeginSaveToSavedPathOrAsk() {
    if (savedPathUtf8_.empty()) {
        stage_ = Stage::SaveAs;
        return {LayoutFlowEffect::OpenSaveAs, {}, false};
    }
    stage_ = Stage::Saving;
    return {LayoutFlowEffect::Save, savedPathUtf8_, false};
}

LayoutFlowStep LayoutDocumentFlow::RunGoal(bool discardRecovery) {
    const Goal goal = goal_;
    const std::string loadPathUtf8 = loadPathUtf8_;
    const bool forUpdate = quitForUpdate_;
    Reset();
    if (goal == Goal::Quit) return {LayoutFlowEffect::Quit, {}, discardRecovery, forUpdate};
    if (goal == Goal::Load) return {LayoutFlowEffect::Load, loadPathUtf8, discardRecovery};
    return {};
}

LayoutFlowStep LayoutDocumentFlow::RequestSave(const std::string& savedPathUtf8) {
    if (stage_ != Stage::Idle) return {};
    savedPathUtf8_ = savedPathUtf8;
    return BeginSaveToSavedPathOrAsk();
}

LayoutFlowStep LayoutDocumentFlow::RequestSaveAs() {
    if (stage_ != Stage::Idle) return {};
    stage_ = Stage::SaveAs;
    return {LayoutFlowEffect::OpenSaveAs, {}, false};
}

LayoutFlowStep LayoutDocumentFlow::RequestLoad(const std::string& pathUtf8, bool dirty,
                                               const std::string& savedPathUtf8) {
    if (stage_ != Stage::Idle) return {};
    if (!dirty) return {LayoutFlowEffect::Load, pathUtf8, false};
    stage_ = Stage::Warning;
    goal_ = Goal::Load;
    loadPathUtf8_ = pathUtf8;
    savedPathUtf8_ = savedPathUtf8;
    return {LayoutFlowEffect::ShowUnsavedWarning, {}, false};
}

LayoutFlowStep LayoutDocumentFlow::RequestQuit(bool dirty, const std::string& savedPathUtf8, bool forUpdate) {
    if (stage_ != Stage::Idle) return {};
    quitForUpdate_ = forUpdate;
    if (!dirty) {
        // 誤って押したときに戻れるよう、保存するものがなくても終了の前に一度聞く。
        stage_ = Stage::QuitConfirm;
        return {LayoutFlowEffect::ConfirmQuit, {}, false};
    }
    stage_ = Stage::Warning;
    goal_ = Goal::Quit;
    savedPathUtf8_ = savedPathUtf8;
    return {LayoutFlowEffect::ShowUnsavedWarning, {}, false};
}

LayoutFlowStep LayoutDocumentFlow::OnUnsavedChoice(UnsavedChoice choice) {
    if (stage_ != Stage::Warning) return {};
    switch (choice) {
    case UnsavedChoice::Save: return BeginSaveToSavedPathOrAsk();
    case UnsavedChoice::Discard: return RunGoal(true);
    case UnsavedChoice::Cancel: return Reset();
    }
    return Reset();
}

LayoutFlowStep LayoutDocumentFlow::OnSaveAsName(const std::string& pathUtf8, bool exists) {
    if (stage_ != Stage::SaveAs) return {};
    if (exists) {
        stage_ = Stage::Overwrite;
        overwritePathUtf8_ = pathUtf8;
        return {LayoutFlowEffect::ConfirmOverwrite, pathUtf8, false};
    }
    stage_ = Stage::Saving;
    return {LayoutFlowEffect::Save, pathUtf8, false};
}

LayoutFlowStep LayoutDocumentFlow::OnSaveAsCancelled() {
    if (stage_ != Stage::SaveAs) return {};
    return Reset();
}

LayoutFlowStep LayoutDocumentFlow::OnOverwriteChoice(bool overwrite) {
    if (stage_ != Stage::Overwrite) return {};
    if (!overwrite) {
        // 名前を選び直せるよう、画面へ戻る。
        stage_ = Stage::SaveAs;
        return {};
    }
    stage_ = Stage::Saving;
    return {LayoutFlowEffect::Save, overwritePathUtf8_, false};
}

LayoutFlowStep LayoutDocumentFlow::OnQuitChoice(bool quit) {
    if (stage_ != Stage::QuitConfirm) return {};
    const bool forUpdate = quitForUpdate_;
    Reset();
    if (!quit) return {};
    return {LayoutFlowEffect::Quit, {}, false, forUpdate};
}

LayoutFlowStep LayoutDocumentFlow::OnSaveFinished(bool ok) {
    if (stage_ != Stage::Saving) return {};
    // 保存に失敗したら、そのあとの終了やロードも取りやめる。保存できたら復元データは保存の処理が消している。
    if (!ok) return Reset();
    return RunGoal(false);
}

} // namespace rs::app::workspace
