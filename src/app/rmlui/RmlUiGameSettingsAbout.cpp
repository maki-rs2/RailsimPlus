#include "RmlUiGameSettingsView.h"

#include <format>
#include <RmlUi/Core/Element.h>
#include "RmlUiUpdatePhase.h"
#include "RmlUiUpdateVariable.h"
#include "../action/UpdateScreenModel.h"
#include "../update/UpdateChecker.h"
#include "../update/UpdateDownloader.h"

namespace rs::app::rmlui {

void RmlUiGameSettingsView::RegisterAboutModel(Rml::DataModelConstructor& constructor) {
    model_->appVersion = RS_APP_VERSION;
    constructor.Bind("gsAppVersion", &model_->appVersion);
    constructor.Bind("gsUpdateStatus", &model_->updateStatus);
    constructor.Bind("gsUpdateButton", &model_->updateButton);
    constructor.Bind("gsUpdateNote", &model_->updateNote);
    constructor.Bind("gsUpdateCheckEnabled", &model_->updateCheckEnabled);
    constructor.Bind("gsCanCheckUpdate", &model_->canCheckUpdate);
    constructor.BindEventCallback("gs_check_update", [this](Rml::DataModelHandle, Rml::Event&,
                                                            const Rml::VariantList&) { OnCheckUpdate(); });
    constructor.BindEventCallback("gs_update_button", [this](Rml::DataModelHandle, Rml::Event&,
                                                             const Rml::VariantList&) { OnUpdateButton(); });
    constructor.BindEventCallback("gs_update_check_enabled", [this](Rml::DataModelHandle, Rml::Event& event,
                                                                   const Rml::VariantList&) {
        OnUpdateCheckEnabled(event);
    });
}

void RmlUiGameSettingsView::UpdateAboutModel() {
    UpdateChecker* checker = refs_->updateChecker;
    const UpdateDownloader* downloader = refs_->updateDownloader;
    const UpdateScreenInput input = MakeUpdateScreenInput(checker, downloader);
    const UpdateScreen screen = BuildUpdateScreen(refs_->localization, input);
    updateButton_ = screen.button;
    UpdateVariable(modelHandle_, "gsUpdateStatus", model_->updateStatus, screen.status);
    UpdateVariable(modelHandle_, "gsUpdateNote", model_->updateNote, screen.note);
    UpdateVariable(modelHandle_, "gsUpdateButton", model_->updateButton, std::string(UpdateButtonName(screen.button)));
    UpdateVariable(modelHandle_, "gsCanCheckUpdate", model_->canCheckUpdate, checker != nullptr && checker->CanCheck());
    UpdateVariable(modelHandle_, "gsUpdateCheckEnabled", model_->updateCheckEnabled,
                   refs_->userSettings.updateCheckEnabled);
}

} // namespace rs::app::rmlui
