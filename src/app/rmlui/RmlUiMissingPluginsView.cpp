#include "RmlUiMissingPluginsView.h"

#include <utility>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../locale/Localization.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../settings/UserSettings.h"

namespace rs::app::rmlui {

// データモデルへ出す値の入れ物(定義はここだけ。変数名は下のk*Key)。
struct MissingPluginsModel {
    Rml::Vector<MissingPluginRowView> rows;
    MissingPluginTexts texts;
    int badgeCount = 0;
    bool picking = false;
};

namespace {

// 重ね表示の名前"missingPlugins"(OverlayCatalog)と同じ名前の変数は作れないので、行の配列は別の名前にする。
constexpr const char* kRowsKey = "missingPluginRows";
constexpr const char* kTextKey = "missingPluginText";
constexpr const char* kBadgeCountKey = "missingPluginBadgeCount";
constexpr const char* kPickingKey = "missingPluginPicking";

}  // namespace

RmlUiMissingPluginsView::RmlUiMissingPluginsView() : model_(std::make_unique<MissingPluginsModel>()) {}
RmlUiMissingPluginsView::~RmlUiMissingPluginsView() = default;

void RmlUiMissingPluginsView::Attach(const MissingPluginRefs& refs, UserSettingsData& userSettings, ToastQueue& toasts,
                                     plugin_browser::PluginBrowserService& browser, const Localization& localization,
                                     void* ownerWindow) {
    refs_.emplace(refs);
    userSettings_ = &userSettings;
    toasts_ = &toasts;
    browser_ = &browser;
    localization_ = &localization;
    ownerWindow_ = ownerWindow;
}

void RmlUiMissingPluginsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<MissingPluginRowView> handle = constructor.RegisterStruct<MissingPluginRowView>()) {
        handle.RegisterMember("name", &MissingPluginRowView::name);
        handle.RegisterMember("path", &MissingPluginRowView::path);
        handle.RegisterMember("kind", &MissingPluginRowView::kind);
        handle.RegisterMember("usage", &MissingPluginRowView::usage);
        handle.RegisterMember("deletable", &MissingPluginRowView::deletable);
    }
    constructor.RegisterArray<Rml::Vector<MissingPluginRowView>>();
    if (Rml::StructHandle<MissingPluginTexts> handle = constructor.RegisterStruct<MissingPluginTexts>()) {
        handle.RegisterMember("title", &MissingPluginTexts::title);
        handle.RegisterMember("badgeTitle", &MissingPluginTexts::badgeTitle);
        handle.RegisterMember("addRoot", &MissingPluginTexts::addRoot);
        handle.RegisterMember("rescan", &MissingPluginTexts::rescan);
        handle.RegisterMember("replace", &MissingPluginTexts::replace);
        handle.RegisterMember("remove", &MissingPluginTexts::remove);
    }
    constructor.Bind(kRowsKey, &model_->rows);
    constructor.Bind(kTextKey, &model_->texts);
    constructor.Bind(kBadgeCountKey, &model_->badgeCount);
    constructor.Bind(kPickingKey, &model_->picking);

    constructor.BindEventCallback("missing_replace",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnReplace(arguments);
                                  });
    constructor.BindEventCallback("missing_delete",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnDelete(arguments);
                                  });
    constructor.BindEventCallback("missing_rescan", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        OnRescan();
    });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiMissingPluginsView::Tick(const MissingPluginSignature& signature, double deltaSeconds) {
    if (!refs_.has_value() || !modelHandle_) return;

    if (std::optional<MissingPluginEntry> row = std::exchange(pendingDelete_, std::nullopt)) RunDelete(*row);

    // ダイアログが終わっていれば、押した時点の行を選ばれたフォルダへ差し替える(キャンセルは何もしない)。
    std::optional<std::string> picked;
    if (picker_.TryTake(picked)) {
        const std::optional<MissingPluginEntry> target = std::exchange(replaceTarget_, std::nullopt);
        if (picked.has_value() && target.has_value()) RunReplace(*target, *picked);
    }

    if (gate_.Update(signature, deltaSeconds)) {
        entries_ = MissingPluginAction(*refs_).Collect();
        rowsDirty_ = true;
    }

    const int language = static_cast<int>(localization_->CurrentLanguage());
    if (language != shownLanguage_) {
        shownLanguage_ = language;
        UpdateVariable(modelHandle_, kTextKey, model_->texts, BuildMissingPluginTexts(*localization_));
        rowsDirty_ = true;
    }
    if (rowsDirty_) {
        rowsDirty_ = false;
        UpdateVariable(modelHandle_, kRowsKey, model_->rows,
                       Rml::Vector<MissingPluginRowView>(BuildMissingPluginRows(entries_, *localization_)));
        UpdateVariable(modelHandle_, kBadgeCountKey, model_->badgeCount,
                       static_cast<int>(MissingPluginBadgeCount(entries_)));
    }

    UpdateVariable(modelHandle_, kPickingKey, model_->picking, picker_.Open());
}

void RmlUiMissingPluginsView::OnReplace(const Rml::VariantList& arguments) {
    if (!refs_.has_value() || picker_.Open()) return;
    const int index = FirstEventArgument<int>(arguments, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) return;
    const MissingPluginEntry& row = entries_[static_cast<std::size_t>(index)];
    const std::string startFolder =
        MissingPluginPickerStartFolder(row.site, userSettings_->pluginRoots, PluginExistsFn(PluginFolderExists));
    if (picker_.Start(ownerWindow_, localization_->Text("missing_plugin.picker_title"), startFolder)) {
        replaceTarget_ = row;
    }
}

void RmlUiMissingPluginsView::OnDelete(const Rml::VariantList& arguments) {
    const int index = FirstEventArgument<int>(arguments, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) return;
    // 確認の間に一覧が変わっても別の行を消さないよう、押した時点の行の写しで覚える。
    const MissingPluginEntry row = entries_[static_cast<std::size_t>(index)];
    if (confirmDialog_ == nullptr) {
        pendingDelete_ = row;
        return;
    }
    const std::string name = PluginFolderLeafName(row.folderUtf8);
    const std::string message = ExpandMissingPluginText(localization_->Text("missing_plugin.delete_confirm"),
                                                        row.usageCount, name.empty() ? row.folderUtf8 : name);
    confirmDialog_->Open(message, localization_->Text("missing_plugin.delete"), true,
                         [this, row]() { pendingDelete_ = row; });
}

void RmlUiMissingPluginsView::OnRescan() {
    if (browser_ != nullptr) browser_->Rescan();
    gate_.Force();
}

void RmlUiMissingPluginsView::PostNotice(const MissingPluginNotice& notice) {
    if (toasts_ == nullptr) return;
    toasts_->Post(notice.warning ? ToastSeverity::Warning : ToastSeverity::Info, notice.text);
}

void RmlUiMissingPluginsView::RunDelete(const MissingPluginEntry& row) {
    const MissingPluginReplaceResult result = MissingPluginAction(*refs_).Delete(row);
    const MissingPluginNotice notice = BuildMissingPluginNotice(result, true, *localization_);
    PostNotice(notice);
    gate_.Force();
}

void RmlUiMissingPluginsView::RunReplace(const MissingPluginEntry& row, const std::string& folderUtf8) {
    const MissingPluginReplaceResult result = MissingPluginAction(*refs_).Replace(row, folderUtf8);
    const MissingPluginNotice notice = BuildMissingPluginNotice(result, false, *localization_);
    PostNotice(notice);
    gate_.Force();
}

}  // namespace rs::app::rmlui
