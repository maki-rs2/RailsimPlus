#include "RmlUiGameSettingsView.h"

#include <format>
#include <RmlUi/Core/Element.h>
#include "RmlUiUpdatePhase.h"
#include "RmlUiUpdateVariable.h"
#include "../update/UpdateChecker.h"

namespace rs::app::rmlui {

void RmlUiGameSettingsView::RegisterAboutModel(Rml::DataModelConstructor& constructor) {
    model_->appVersion = RS_APP_VERSION;
    constructor.Bind("gsAppVersion", &model_->appVersion);
    constructor.Bind("gsUpdateStatus", &model_->updateStatus);
    constructor.Bind("gsUpdateAvailable", &model_->updateAvailable);
    constructor.Bind("gsUpdateCheckEnabled", &model_->updateCheckEnabled);
    constructor.Bind("gsCanCheckUpdate", &model_->canCheckUpdate);
    constructor.BindEventCallback("gs_check_update", [this](Rml::DataModelHandle, Rml::Event&,
                                                            const Rml::VariantList&) { OnCheckUpdate(); });
    constructor.BindEventCallback("gs_open_download_page", [this](Rml::DataModelHandle, Rml::Event&,
                                                                  const Rml::VariantList&) { OnOpenDownloadPage(); });
    constructor.BindEventCallback("gs_update_check_enabled", [this](Rml::DataModelHandle, Rml::Event& event,
                                                                   const Rml::VariantList&) {
        OnUpdateCheckEnabled(event);
    });
}

void RmlUiGameSettingsView::UpdateAboutModel() {
    UpdateChecker* checker = refs_->updateChecker;
    const UpdateCheckState state = checker == nullptr ? UpdateCheckState::Unchecked : checker->State();
    std::string text = refs_->localization.Text(UpdateCheckStateTextKey(state));
    if (state == UpdateCheckState::Available) {
        text = std::vformat(text, std::make_format_args(checker->Release().version));
    }
    UpdateVariable(modelHandle_, "gsUpdateStatus", model_->updateStatus, text);
    UpdateVariable(modelHandle_, "gsUpdateAvailable", model_->updateAvailable, state == UpdateCheckState::Available);
    UpdateVariable(modelHandle_, "gsCanCheckUpdate", model_->canCheckUpdate, checker != nullptr && checker->CanCheck());
    UpdateVariable(modelHandle_, "gsUpdateCheckEnabled", model_->updateCheckEnabled,
                   refs_->userSettings.updateCheckEnabled);
}

} // namespace rs::app::rmlui
