#include "RmlUiProtoView.h"

#include <algorithm>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <spdlog/spdlog.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiContextMenu.h"
#include "RmlUiDataEventArgs.h"

namespace rs::app::rmlui {

RmlUiProtoView::RmlUiProtoView(const UiThemeSet& themeSet) : themeSet_(themeSet), theme_(themeSet.baseTheme) {}

bool RmlUiProtoView::CreateDataModel(Rml::Context& context) {
    Rml::DataModelConstructor constructor = context.CreateDataModel("proto");
    if (!constructor) return false;
    context_ = &context;

    constructor.Bind("theme", &theme_);
    constructor.Bind("gallerySegment", &gallerySegment_);
    constructor.RegisterArray<std::vector<int>>();
    constructor.Bind("dragCards", &dragCards_);
    constructor.Bind("dropLog", &dropLog_);
    for (int i = 1; i <= 30; ++i) dragCards_.push_back(i);
    dropLog_ = "未ドロップ";

    constructor.RegisterArray<std::vector<std::string>>();
    constructor.Bind("contextCards", &contextCards_);
    constructor.Bind("contextItemBEnabled", &contextItemBEnabled_);
    constructor.Bind("contextLog", &contextLog_);
    contextCards_ = {"1", "2", "3"};
    contextLog_ = "未選択";
    constructor.Bind("confirmLog", &confirmLog_);
    confirmLog_ = "未実行";
    constructor.BindEventCallback(
        "open_confirm_demo", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OpenConfirmDemo(); });
    constructor.BindEventCallback(
        "remove_context_card", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { RemoveContextCard(); });
    constructor.BindEventCallback(
        "toggle_context_item_b", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { ToggleContextItemB(); });

    constructor.BindEventCallback(
        "select_theme", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!arguments.empty()) SelectTheme(FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback(
        "select_gallery_segment", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!arguments.empty()) SelectGallerySegment(FirstEventArgument<int>(arguments, 0));
        });
    constructor.BindEventCallback(
        "rebuild_drag_cards", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { RebuildDragCards(); });
    constructor.BindEventCallback(
        "drop_card", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { DropCard(event); });

    modelHandle_ = constructor.GetModelHandle();
    return true;
}

void RmlUiProtoView::SelectTheme(const std::string& themeName) {
    if (context_ == nullptr) return;
    if (themeSet_.themes.find(themeName) == themeSet_.themes.end()) {
        spdlog::warn("[rmlui] 未知のテーマ名です: {}", themeName);
        return;
    }
    ActivateUiTheme(*context_, themeSet_, themeName);
    theme_ = themeName;
    modelHandle_.DirtyVariable("theme");
}

void RmlUiProtoView::SelectGallerySegment(int index) {
    gallerySegment_ = index;
    modelHandle_.DirtyVariable("gallerySegment");
}

void RmlUiProtoView::RebuildDragCards() {
    std::reverse(dragCards_.begin(), dragCards_.end());
    modelHandle_.DirtyVariable("dragCards");
}

void RmlUiProtoView::RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog) {
    menu.RegisterProvider(workspace::ContextMenuKind::ProtoDemo, this);
    confirmDialog_ = &dialog;
}

void RmlUiProtoView::OpenConfirmDemo() {
    if (confirmDialog_ == nullptr) return;
    ++confirmCount_;
    const std::string label = "確認" + std::to_string(confirmCount_);
    confirmDialog_->Open("確認ダイアログの動作確認です。\n実行しますか?", "実行", true, [this, label]() {
        confirmLog_ = label + ": 実行が選ばれました";
        modelHandle_.DirtyVariable("confirmLog");
    });
    confirmLog_ = label + ": 開いています";
    modelHandle_.DirtyVariable("confirmLog");
}

bool RmlUiProtoView::TargetExists(workspace::ContextMenuKind, const std::string& targetId) const {
    return std::find(contextCards_.begin(), contextCards_.end(), targetId) != contextCards_.end();
}

bool RmlUiProtoView::IsItemEnabled(workspace::ContextMenuKind, std::string_view itemId, const std::string&) const {
    return itemId != "itemB" || contextItemBEnabled_;
}

void RmlUiProtoView::OnItemChosen(workspace::ContextMenuKind, std::string_view itemId, const std::string& targetId) {
    contextLog_ = "選択: " + std::string(itemId) + " / 対象: カード" + targetId;
    modelHandle_.DirtyVariable("contextLog");
}

void RmlUiProtoView::RemoveContextCard() {
    if (contextCards_.empty()) return;
    contextCards_.pop_back();
    modelHandle_.DirtyVariable("contextCards");
}

void RmlUiProtoView::ToggleContextItemB() {
    contextItemBEnabled_ = !contextItemBEnabled_;
    modelHandle_.DirtyVariable("contextItemBEnabled");
}

void RmlUiProtoView::DropCard(Rml::Event& event) {
    Rml::Element* dragged = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));
    const int cardId = dragged != nullptr ? dragged->GetAttribute<int>("card-id", -1) : -1;
    dropLog_ = "ドロップ: カード" + std::to_string(cardId);
    modelHandle_.DirtyVariable("dropLog");
}

} // namespace rs::app::rmlui
