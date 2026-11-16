#include "RmlUiPluginRootsView.h"

#include <optional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../action/PluginRootAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../settings/UserSettings.h"

namespace rs::app::rmlui {

// ルート1行。canUp/canDownは先頭・末尾の行で上へ・下へを無効にするための値。
struct PluginRootRowModel {
    Rml::String path;
    bool canUp = false;
    bool canDown = false;
    bool operator==(const PluginRootRowModel&) const = default;
};

// データモデルへ出す値の入れ物(定義はここだけ。変数名は下のk*Key)。
struct PluginRootsModel {
    Rml::Vector<PluginRootRowModel> rows;
    bool picking = false;
};

namespace {

// 重ね表示の名前"pluginRoots"(OverlayCatalog)と同じ名前の変数は作れないので、行の配列は別の名前にする。
constexpr const char* kRowsKey = "pluginRootRows";
constexpr const char* kPickingKey = "pluginRootPicking";

// フォルダ選択ダイアログのタイトル(仮。ローカライズ基盤より前)。
constexpr const char* kPickerTitle = "プラグインフォルダを選択";

// root_moveのdeltaとして受け付ける値(PluginRootAction::Moveの-1/+1と同じ)。
constexpr int kMoveUp = -1;
constexpr int kMoveDown = 1;

} // namespace

RmlUiPluginRootsView::RmlUiPluginRootsView() : model_(std::make_unique<PluginRootsModel>()) {}
RmlUiPluginRootsView::~RmlUiPluginRootsView() = default;

void RmlUiPluginRootsView::Attach(plugin_browser::PluginBrowserService& service, UserSettingsData& userSettings,
                                  void* ownerWindow) {
    service_ = &service;
    userSettings_ = &userSettings;
    ownerWindow_ = ownerWindow;
}

void RmlUiPluginRootsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<PluginRootRowModel> handle = constructor.RegisterStruct<PluginRootRowModel>()) {
        handle.RegisterMember("path", &PluginRootRowModel::path);
        handle.RegisterMember("canUp", &PluginRootRowModel::canUp);
        handle.RegisterMember("canDown", &PluginRootRowModel::canDown);
    }
    constructor.RegisterArray<Rml::Vector<PluginRootRowModel>>();
    constructor.Bind(kRowsKey, &model_->rows);
    constructor.Bind(kPickingKey, &model_->picking);

    constructor.BindEventCallback("root_add", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        OnAdd();
    });
    constructor.BindEventCallback("root_remove",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnRemove(arguments);
                                  });
    constructor.BindEventCallback("root_move",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnMove(arguments);
                                  });
    constructor.BindEventCallback("root_rescan", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        OnRescan();
    });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiPluginRootsView::Refresh() {
    if (userSettings_ == nullptr || !modelHandle_) return;
    PluginRootAction action(*userSettings_);

    // ダイアログが終わっていれば、選ばれたフォルダを足す(同じフォルダが既にあればAddが何もしない)。
    std::optional<std::string> picked;
    if (picker_.TryTake(picked) && picked.has_value()) action.Add(*picked);

    const std::vector<std::string>& roots = action.Roots();
    Rml::Vector<PluginRootRowModel> rows;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        rows.push_back({roots[i], i > 0, i + 1 < roots.size()});
    }
    UpdateVariable(modelHandle_, kRowsKey, model_->rows, rows);
    UpdateVariable(modelHandle_, kPickingKey, model_->picking, picker_.Open());
}

void RmlUiPluginRootsView::OnAdd() {
    picker_.Start(ownerWindow_, kPickerTitle); // 開いている間は何もしない(ボタンも無効にしてある)
}

void RmlUiPluginRootsView::OnRemove(const Rml::VariantList& arguments) {
    if (userSettings_ == nullptr) return;
    const int index = FirstEventArgument<int>(arguments, -1);
    if (index < 0) return;
    PluginRootAction(*userSettings_).Remove(static_cast<std::size_t>(index));
}

void RmlUiPluginRootsView::OnMove(const Rml::VariantList& arguments) {
    if (userSettings_ == nullptr) return;
    const int index = FirstEventArgument<int>(arguments, -1);
    const int delta = EventArgumentAt<int>(arguments, 1, 0);
    if (index < 0 || (delta != kMoveUp && delta != kMoveDown)) return;
    PluginRootAction(*userSettings_).Move(static_cast<std::size_t>(index), delta);
}

void RmlUiPluginRootsView::OnRescan() {
    if (service_ != nullptr) service_->Rescan();
}

} // namespace rs::app::rmlui
