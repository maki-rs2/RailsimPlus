#include "RmlUiGameSettingsView.h"

#include <RmlUi/Core/Element.h>
#include "RmlUiDataEventArgs.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiUpdatePhase.h"
#include "../action/GameSettingsScreenModel.h"

namespace rs::app::rmlui {
namespace {
constexpr const char* kTabKey = "gsTab";
// 選べるタブの名前(BuildGameSettingsTabChoicesのkey)。
bool IsTabKey(const Rml::String& key, const Rml::Vector<GameSettingsChoice>& tabs) {
    for (const GameSettingsChoice& tab : tabs) {
        if (tab.key == key) return true;
    }
    return false;
}

}

void RmlUiGameSettingsView::OnSelectTab(const Rml::VariantList& arguments) {
    const Rml::String key = FirstEventArgument<Rml::String>(arguments, Rml::String());
    if (!IsTabKey(key, model_->tabs)) return;
    timeScaleFocused_ = false; // タブを替えると入力欄は消える
    UpdateVariable(modelHandle_, kTabKey, model_->tab, key);
    inputSettings_.SetActive(key == "input");
}

void RmlUiGameSettingsView::OnSetLanguage(const Rml::String& key) {
    Language language = Language::Japanese;
    if (!refs_.has_value() || !FindLanguageByKey(key, language)) {
        return;
    }
    GameSettingsAction action(*refs_);
    if (action.ViewModel().language != language) action.SetLanguage(language);
}

void RmlUiGameSettingsView::OnSetUiTheme(const Rml::VariantList& arguments) {
    if (!refs_.has_value() || themeSet_ == nullptr) return;
    const Rml::String key = FirstEventArgument<Rml::String>(arguments, Rml::String());
    if (!themeSet_->themes.contains(key)) return;
    GameSettingsAction action(*refs_);
    if (ResolveUiThemeName(*themeSet_, action.ViewModel().uiTheme) != key) action.SetUiTheme(key);
}

void RmlUiGameSettingsView::OnVolumeCommit() {
    if (!refs_.has_value() || !volumeUncommitted_) return;
    volumeUncommitted_ = false;
    GameSettingsAction(*refs_).CommitAudioMasterVolume();
}

void RmlUiGameSettingsView::OnAudioDeviceChange(Rml::Event& event) {
    int index = 0;
    if (!refs_.has_value() || !ParseIntText(event.GetParameter<Rml::String>("value", Rml::String()), index)) return;
    if (index < 0 || static_cast<std::size_t>(index) >= audioDeviceNames_.size()) return;
    GameSettingsAction action(*refs_);
    const std::string& name = audioDeviceNames_[static_cast<std::size_t>(index)];
    // 値を書き戻しただけのchangeで保存しない。
    if (action.ViewModel().audioOutputDevice != name) action.SetAudioOutputDevice(name);
}

void RmlUiGameSettingsView::OnBrowseLayoutDirectory() {
    if (!refs_.has_value() || picker_.Open()) return;
    picker_.Start(ownerWindow_, refs_->localization.Text("game_settings.layout_save_picker_title"),
                  GameSettingsAction(*refs_).ViewModel().layoutSaveDirectory);
}

void RmlUiGameSettingsView::OnUpdateCheckEnabled(Rml::Event& event) {
    if (!refs_ || IsInsideContextUpdate()) return;
    const bool enabled = event.GetCurrentElement()->HasAttribute("checked");
    GameSettingsAction(*refs_).SetUpdateCheckEnabled(enabled);
}

void RmlUiGameSettingsView::OnCheckUpdate() {
    if (refs_) GameSettingsAction(*refs_).CheckForUpdates();
}

void RmlUiGameSettingsView::OnUpdateButton() {
    if (!refs_) return;
    GameSettingsAction action(*refs_);
    switch (updateButton_) {
    case UpdateButton::Update:
    case UpdateButton::Retry:
    case UpdateButton::OpenDownloadPage: action.DownloadUpdate(); break;
    case UpdateButton::Cancel: action.CancelUpdateDownload(); break;
    case UpdateButton::Restart: action.RestartForUpdate(); break;
    case UpdateButton::None: break;
    }
}

} // namespace rs::app::rmlui
