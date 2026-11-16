#include "RmlUiGameSettingsView.h"

#include <filesystem>
#include <optional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiUpdatePhase.h"
#include "../action/GameSettingsScreenModel.h"
#include "../action/SettingsPlaceholderAction.h"
#include "../settings/UserSettings.h"
#include "../../platform/text/Encoding.h"
#include "../../world/time/SolarModeNames.h"

namespace rs::app::rmlui {

using workspace::GameSettingsModel;

namespace {

// 重ね表示の名前"gameSettings"(OverlayCatalog)と衝突しないよう、変数名は接頭辞gsを付ける。
constexpr const char* kTabKey = "gsTab";
constexpr const char* kTabsKey = "gsTabs";
constexpr const char* kTextKey = "gsText";
constexpr const char* kLanguageKey = "gsLanguage";
constexpr const char* kLanguagesKey = "gsLanguages";
constexpr const char* kThemeKey = "gsTheme";
constexpr const char* kThemesKey = "gsThemes";
constexpr const char* kVolumePercentKey = "gsVolumePercent";
constexpr const char* kTrainVolumePercentKey = "gsTrainVolumePercent";
constexpr const char* kEnvironmentVolumePercentKey = "gsEnvironmentVolumePercent";
constexpr const char* kEffectsVolumePercentKey = "gsEffectsVolumePercent";
constexpr const char* kAudioDeviceKey = "gsAudioDevice";
constexpr const char* kAudioDeviceLabelsKey = "gsAudioDeviceLabels";
constexpr const char* kPluginRootsKey = "gsPluginRoots";
constexpr const char* kLayoutSaveDirectoryKey = "gsLayoutSaveDirectory";
constexpr const char* kLayoutSaveDirectoryIsDefaultKey = "gsLayoutSaveDirectoryIsDefault";
constexpr const char* kPickingKey = "gsPicking";
constexpr const char* kFramerateKey = "gsFramerate";
constexpr const char* kDateTimeKey = "gsDateTime";
constexpr const char* kTimeScaleKey = "gsTimeScale";
constexpr const char* kEarthRotationKey = "gsEarthRotation";
constexpr const char* kEarthRotationsKey = "gsEarthRotations";

std::string FolderToUtf8(const std::filesystem::path& folder) {
    return rs::platform_text::WideToUtf8(folder.wstring());
}

} // namespace

RmlUiGameSettingsView::RmlUiGameSettingsView(ToastQueue& queue)
    : model_(std::make_unique<GameSettingsModel>()), queue_(queue),
      presentation_(workspace::MakeGameSettingsPlaceholder()), inputSettings_(queue) {}
RmlUiGameSettingsView::~RmlUiGameSettingsView() = default;

void RmlUiGameSettingsView::Attach(const GameSettingsRefs& refs, void* ownerWindow, const UiThemeSet& themeSet) {
    refs_.emplace(refs);
    inputSettings_.Attach(refs);
    ownerWindow_ = ownerWindow;
    themeSet_ = &themeSet;
    defaultRootFolder_ = FolderToUtf8(UserSettings::ExecutableDirectory());
    defaultLayoutFolder_ = FolderToUtf8(UserSettings::DefaultLayoutSaveDirectory());
}

void RmlUiGameSettingsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<GameSettingsChoice> handle = constructor.RegisterStruct<GameSettingsChoice>()) {
        handle.RegisterMember("key", &GameSettingsChoice::key);
        handle.RegisterMember("label", &GameSettingsChoice::label);
    }
    constructor.RegisterArray<Rml::Vector<GameSettingsChoice>>();
    constructor.Bind("gsFullscreen", &presentation_.fullscreen_);
    constructor.Bind("gsFullscreenLabel", &presentation_.label_);
    constructor.Bind("gsFullscreenNote", &presentation_.note_);
    constructor.Bind("gsFullscreenChoices", &presentation_.choices_);
    constructor.BindEventCallback("gs_fullscreen", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) {
        if (IsInsideContextUpdate() || !refs_.has_value()) return;
        GameSettingsAction(*refs_).SetFullscreen(event.GetCurrentElement()->HasAttribute("checked"));
    });
    inputSettings_.RegisterModel(constructor);
    RegisterAboutModel(constructor);
    if (Rml::StructHandle<GameSettingsTexts> handle = constructor.RegisterStruct<GameSettingsTexts>()) {
        handle.RegisterMember("title", &GameSettingsTexts::title);
        handle.RegisterMember("language", &GameSettingsTexts::language);
        handle.RegisterMember("theme", &GameSettingsTexts::theme);
        handle.RegisterMember("masterVolume", &GameSettingsTexts::masterVolume);
        handle.RegisterMember("trainVolume", &GameSettingsTexts::trainVolume);
        handle.RegisterMember("environmentVolume", &GameSettingsTexts::environmentVolume);
        handle.RegisterMember("outputDevice", &GameSettingsTexts::outputDevice);
        handle.RegisterMember("refreshDevices", &GameSettingsTexts::refreshDevices);
        handle.RegisterMember("pluginRoots", &GameSettingsTexts::pluginRoots);
        handle.RegisterMember("manageRoots", &GameSettingsTexts::manageRoots);
        handle.RegisterMember("layoutSaveDirectory", &GameSettingsTexts::layoutSaveDirectory);
        handle.RegisterMember("browse", &GameSettingsTexts::browse);
        handle.RegisterMember("resetToDefault", &GameSettingsTexts::resetToDefault);
        handle.RegisterMember("camera", &GameSettingsTexts::camera);
        handle.RegisterMember("resetCamera", &GameSettingsTexts::resetCamera);
        handle.RegisterMember("dateTime", &GameSettingsTexts::dateTime);
        handle.RegisterMember("timeScale", &GameSettingsTexts::timeScale);
        handle.RegisterMember("earthRotation", &GameSettingsTexts::earthRotation);
        handle.RegisterMember("fpsDisplay", &GameSettingsTexts::fpsDisplay);
        handle.RegisterMember("effectsVolume", &GameSettingsTexts::effectsVolume);
        handle.RegisterMember("version", &GameSettingsTexts::version);
        handle.RegisterMember("copyright", &GameSettingsTexts::copyright);
        handle.RegisterMember("openDownloadPage", &GameSettingsTexts::openDownloadPage);
        handle.RegisterMember("updateCheckEnabled", &GameSettingsTexts::updateCheckEnabled);
        handle.RegisterMember("checkNow", &GameSettingsTexts::checkNow);
    }
    constructor.Bind(kTabKey, &model_->tab);
    constructor.Bind(kTabsKey, &model_->tabs);
    constructor.Bind(kTextKey, &model_->texts);
    constructor.Bind(kLanguageKey, &model_->language);
    constructor.Bind(kLanguagesKey, &model_->languages);
    constructor.Bind(kThemeKey, &model_->theme);
    constructor.Bind(kThemesKey, &model_->themes);
    constructor.Bind(kVolumePercentKey, &model_->volumePercent);
    constructor.Bind(kTrainVolumePercentKey, &model_->trainVolumePercent);
    constructor.Bind(kEnvironmentVolumePercentKey, &model_->environmentVolumePercent);
    constructor.Bind(kEffectsVolumePercentKey, &model_->effectsVolumePercent);
    constructor.BindFunc("gsVolumeMinPercent", [](Rml::Variant& value) {
        value = UserSettingsData::kMinAudioVolumePercent;
    });
    constructor.BindFunc("gsVolumeMaxPercent", [](Rml::Variant& value) {
        value = UserSettingsData::kMaxAudioVolumePercent;
    });
    constructor.Bind(kAudioDeviceKey, &model_->audioDevice);
    constructor.Bind(kAudioDeviceLabelsKey, &model_->audioDeviceLabels);
    constructor.Bind(kPluginRootsKey, &model_->pluginRoots);
    constructor.Bind(kLayoutSaveDirectoryKey, &model_->layoutSaveDirectory);
    constructor.Bind(kLayoutSaveDirectoryIsDefaultKey, &model_->layoutSaveDirectoryIsDefault);
    constructor.Bind(kPickingKey, &model_->picking);
    constructor.Bind(kFramerateKey, &model_->framerate);
    constructor.Bind(kDateTimeKey, &model_->dateTime);
    constructor.Bind(kTimeScaleKey, &model_->timeScale);
    constructor.Bind(kEarthRotationKey, &model_->earthRotation);
    constructor.Bind(kEarthRotationsKey, &model_->earthRotations);

    constructor.BindEventCallback("gs_select_tab", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        OnSelectTab(a);
    });
    constructor.BindEventCallback("gs_set_language",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      if (IsInsideContextUpdate()) return; // 選択肢の作り直しで届くchangeは無視する
                                      OnSetLanguage(event.GetParameter<Rml::String>("value", Rml::String()));
                                  });
    constructor.BindEventCallback("gs_set_theme", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        OnSetUiTheme(a);
    });
    constructor.BindEventCallback("gs_volume_change", [this](Rml::DataModelHandle handle, Rml::Event& event,
                                                               const Rml::VariantList& arguments) {
        if (!refs_.has_value()) return;
        const Rml::String category = FirstEventArgument<Rml::String>(arguments, Rml::String());
        const float percent = event.GetParameter<float>("value", -1.0f);
        if (percent < 0.0f) return;
        GameSettingsAction action(*refs_);
        const GameSettingsViewModel viewModel = action.ViewModel();
        const float volume = PercentToVolume(percent);
        const int nextPercent = VolumeToPercent(volume);
        // 表示(スライダー横の数値)は、値を書き換えたうえで必ず再描画を依頼する。
        // 書き換えだけだと、Tickでの比較が一致して再描画されず、数値が古いまま残る。
        if (category == "overall") {
            if (VolumeToPercent(viewModel.audioMasterVolume) == nextPercent) return;
            action.SetAudioMasterVolume(volume);
            UpdateVariable(handle, kVolumePercentKey, model_->volumePercent, nextPercent);
        } else if (category == "train") {
            if (VolumeToPercent(viewModel.audioTrainVolume) == nextPercent) return;
            action.SetAudioTrainVolume(volume);
            UpdateVariable(handle, kTrainVolumePercentKey, model_->trainVolumePercent, nextPercent);
        } else if (category == "effects") {
            if (VolumeToPercent(viewModel.audioEffectsVolume) == nextPercent) return;
            action.SetAudioEffectsVolume(volume);
            UpdateVariable(handle, kEffectsVolumePercentKey, model_->effectsVolumePercent, nextPercent);
        } else {
            if (VolumeToPercent(viewModel.audioEnvironmentVolume) == nextPercent) return;
            action.SetAudioEnvironmentVolume(volume);
            UpdateVariable(handle, kEnvironmentVolumePercentKey, model_->environmentVolumePercent, nextPercent);
        }
        volumeUncommitted_ = true;
    });
    constructor.BindEventCallback("gs_volume_commit",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      OnVolumeCommit();
                                  });
    constructor.BindEventCallback("gs_audio_device_change",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnAudioDeviceChange(event);
                                  });
    constructor.BindEventCallback("gs_refresh_devices",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      devicesFetched_ = false;
                                  });
    constructor.BindEventCallback("gs_browse_layout_directory",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      OnBrowseLayoutDirectory();
                                  });
    constructor.BindEventCallback("gs_reset_layout_directory",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      if (refs_.has_value()) GameSettingsAction(*refs_).SetLayoutSaveDirectory("");
                                  });
    constructor.BindEventCallback("gs_set_earth_rotation",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
                                      OnSetEarthRotation(a);
                                  });
    constructor.BindEventCallback("gs_time_scale_focus",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      timeScaleFocused_ = true;
                                  });
    constructor.BindEventCallback("gs_time_scale_commit",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnTimeScaleCommit(event);
                                  });
    constructor.BindEventCallback("gs_time_scale_key",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnTimeScaleKey(event);
                                  });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiGameSettingsView::Tick(bool windowVisible, float framesPerSecond) {
    inputSettings_.SetActive(windowVisible && model_->tab == "input");
    if (!refs_.has_value() || !modelHandle_) return;
    GameSettingsAction action(*refs_);

    // ダイアログが終わっていれば、選ばれたフォルダを保存先にする(キャンセルは何もしない)。
    std::optional<std::string> picked;
    if (picker_.TryTake(picked) && picked.has_value()) action.SetLayoutSaveDirectory(*picked);
    UpdateVariable(modelHandle_, kPickingKey, model_->picking, picker_.Open());

    const GameSettingsViewModel viewModel = action.ViewModel();
    const Localization& localization = refs_->localization;
    const int language = static_cast<int>(viewModel.language);
    if (language != shownLanguage_) {
        shownLanguage_ = language;
        RefreshTexts();
    }

    if (!windowVisible) {
        // 離したイベントを受けずに閉じた場合も、反映済みの音量は保存する。入力欄の状態も残さない。
        if (volumeUncommitted_) OnVolumeCommit();
        timeScaleFocused_ = false;
        return;
    }

    if (!devicesFetched_) {
        devicesFetched_ = true;
        enumeratedDevices_ = action.EnumerateAudioOutputDevices();
    }
    UpdateAboutModel();

    UpdateVariable(modelHandle_, "gsFullscreen", presentation_.fullscreen_, std::string(viewModel.fullscreen ? "on" : "off"));
    UpdateVariable(modelHandle_, kLanguageKey, model_->language, Rml::String(LanguageCode(viewModel.language)));
    if (themeSet_ != nullptr) {
        UpdateVariable(modelHandle_, kThemeKey, model_->theme,
                       Rml::String(ResolveUiThemeName(*themeSet_, viewModel.uiTheme)));
    }
    UpdateVariable(modelHandle_, kVolumePercentKey, model_->volumePercent, VolumeToPercent(viewModel.audioMasterVolume));
    UpdateVariable(modelHandle_, kTrainVolumePercentKey, model_->trainVolumePercent, VolumeToPercent(viewModel.audioTrainVolume));
    UpdateVariable(modelHandle_, kEnvironmentVolumePercentKey, model_->environmentVolumePercent, VolumeToPercent(viewModel.audioEnvironmentVolume));
    UpdateVariable(modelHandle_, kEffectsVolumePercentKey, model_->effectsVolumePercent, VolumeToPercent(viewModel.audioEffectsVolume));

    audioDeviceNames_ = BuildAudioDeviceNames(enumeratedDevices_, viewModel.audioOutputDevice);
    UpdateVariable(modelHandle_, kAudioDeviceLabelsKey, model_->audioDeviceLabels,
                   Rml::Vector<Rml::String>(BuildAudioDeviceLabels(audioDeviceNames_, localization)));
    UpdateVariable(modelHandle_, kAudioDeviceKey, model_->audioDevice,
                   std::to_string(AudioDeviceSelectedIndex(audioDeviceNames_, viewModel.audioOutputDevice)));

    UpdateVariable(modelHandle_, kPluginRootsKey, model_->pluginRoots,
                   Rml::String(FormatPluginRootsSummary(viewModel.pluginRoots, defaultRootFolder_, localization)));
    const LayoutSaveDirectoryDisplay layoutDirectory =
        FormatLayoutSaveDirectory(viewModel.layoutSaveDirectory, defaultLayoutFolder_, localization);
    UpdateVariable(modelHandle_, kLayoutSaveDirectoryKey, model_->layoutSaveDirectory, Rml::String(layoutDirectory.text));
    UpdateVariable(modelHandle_, kLayoutSaveDirectoryIsDefaultKey, model_->layoutSaveDirectoryIsDefault,
                   layoutDirectory.isDefault);

    UpdateVariable(modelHandle_, kFramerateKey, model_->framerate, Rml::String(FormatFramerate(framesPerSecond)));
    UpdateVariable(modelHandle_, kDateTimeKey, model_->dateTime, Rml::String(FormatGameDateTime(viewModel, localization)));
    if (!timeScaleFocused_) { // 編集中の欄は上書きしない
        UpdateVariable(modelHandle_, kTimeScaleKey, model_->timeScale, Rml::String(FormatTimeScale(viewModel.timeScale)));
    }
    UpdateVariable(modelHandle_, kEarthRotationKey, model_->earthRotation,
                   Rml::String(rs::world::EarthRotationModeKey(
                       rs::world::EarthRotationModeFromIndex(viewModel.earthRotationMode))));
}

void RmlUiGameSettingsView::RefreshTexts() {
    const Localization& localization = refs_->localization;
    presentation_ = BuildGameSettingsPresentation(presentation_, localization);
    inputSettings_.RefreshTexts(localization);
    modelHandle_.DirtyVariable("gsFullscreenLabel");
    modelHandle_.DirtyVariable("gsFullscreenNote");
    modelHandle_.DirtyVariable("gsFullscreenChoices");
    UpdateVariable(modelHandle_, kTextKey, model_->texts, BuildGameSettingsTexts(localization));
    UpdateVariable(modelHandle_, kTabsKey, model_->tabs,
                   Rml::Vector<GameSettingsChoice>(BuildGameSettingsTabChoices(localization)));
    UpdateVariable(modelHandle_, kLanguagesKey, model_->languages,
                   Rml::Vector<GameSettingsChoice>(BuildLanguageChoices(localization)));
    if (themeSet_ != nullptr) {
        UpdateVariable(modelHandle_, kThemesKey, model_->themes,
                       Rml::Vector<GameSettingsChoice>(BuildUiThemeChoices(UiThemeNames(*themeSet_), localization)));
    }
    UpdateVariable(modelHandle_, kEarthRotationsKey, model_->earthRotations,
                   Rml::Vector<GameSettingsChoice>(BuildEarthRotationChoices(localization)));
}

} // namespace rs::app::rmlui
