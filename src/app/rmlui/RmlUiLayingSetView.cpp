#include "RmlUiLayingSetView.h"

#include <string>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiDragDrop.h"
#include "../action/ToastQueue.h"
#include "../action/TrackPluginSetAction.h"
#include "../action/TrackPluginSetText.h"
#include "../plugin_browser/LayingSetPreviewStore.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/RailTieHeights.h"
#include "../scene/ParsedPluginCache.h"
#include "../../editor/tool/Editor.h"

namespace rs::app::rmlui {

struct LayingSetModel {
    struct Item {
        Rml::String folder;
        Rml::String name;
        bool operator==(const Item&) const = default;
    };
    Rml::Vector<Item> items;
    Rml::String image;  // 合成プレビューのテクスチャ名("lsimg:<番号>")
    bool imageVisible = false;
};

namespace {

// ドラッグ元がプラグインブラウザのカード(data-entry-idを持つ要素)ならそのカタログの添字、でなければ-1。
int DraggedPluginEntryId(Rml::Event& event) {
    const Rml::Element* const dragged = DraggedElement(event);
    return dragged != nullptr ? dragged->GetAttribute<int>("data-entry-id", -1) : -1;
}

} // namespace

RmlUiLayingSetView::RmlUiLayingSetView(ToastQueue& queue) : queue_(queue), model_(std::make_unique<LayingSetModel>()) {}
RmlUiLayingSetView::~RmlUiLayingSetView() = default;

void RmlUiLayingSetView::Attach(rs::editor::Editor& editor, PluginSelector& selector,
                                plugin_browser::PluginBrowserService& browser) {
    editor_ = &editor;
    selector_ = &selector;
    browser_ = &browser;
}

void RmlUiLayingSetView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<LayingSetModel::Item> item = constructor.RegisterStruct<LayingSetModel::Item>()) {
        item.RegisterMember("folder", &LayingSetModel::Item::folder);
        item.RegisterMember("name", &LayingSetModel::Item::name);
    }
    constructor.RegisterArray<Rml::Vector<LayingSetModel::Item>>();
    constructor.Bind("layingSetItems", &model_->items);
    constructor.Bind("layingSetImage", &model_->image);
    constructor.Bind("layingSetImageVisible", &model_->imageVisible);
    constructor.BindEventCallback("laying_set_remove", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        OnRemove(args);
    });
    // プラグインブラウザのカードを段へドラッグして落とすとセットへ足す。落とせるカードが乗っている間だけ強調する。
    constructor.BindEventCallback("laying_set_drag_over", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
        if (DraggedPluginEntryId(event) >= 0) SetDropHighlight(event, true);
    });
    constructor.BindEventCallback("laying_set_drag_out", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
        SetDropHighlight(event, false);
    });
    constructor.BindEventCallback("laying_set_drop", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
        SetDropHighlight(event, false);
        OnDrop(DraggedPluginEntryId(event));
    });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiLayingSetView::Refresh() {
    if (!editor_ || !selector_ || !modelHandle_) return;
    Rml::Vector<LayingSetModel::Item> next;
    std::vector<std::string> folders;
    for (const TrackPluginSetItem& item : TrackPluginSetAction(*editor_, *selector_).ViewModel().items) {
        next.push_back({item.folderUtf8, item.loaded ? item.displayName : item.folderUtf8});
        folders.push_back(item.folderUtf8);
    }
    if (!(next == model_->items)) {
        model_->items = std::move(next);
        modelHandle_.DirtyVariable("layingSetItems");
    }
    RefreshImage(folders);
}

void RmlUiLayingSetView::RefreshImage(const std::vector<std::string>& folders) {
    if (!browser_) return;
    // 前のフレームで名前を差し替えた古いテクスチャは、画面が新しい名前へ切り替えたあとなので解放してよい。
    if (!pendingRelease_.empty()) {
        Rml::ReleaseTexture(pendingRelease_);
        pendingRelease_.clear();
    }

    plugin_browser::LayingSetPreviewStore& store = browser_->LayingPreview();
    const std::uint64_t generation = GetParsedPluginCache().Generation();
    // キーは定義ファイルの更新時刻を調べて作るので、毎フレームは作らない。
    if (!requested_ || folders != requestedFolders_ || generation != requestedGeneration_) {
        requested_ = true;
        requestedFolders_ = folders;
        requestedGeneration_ = generation;
        if (!folders.empty()) {
            plugin_browser::PluginImageRequest request;
            request.layingFolders = folders;
            request.layingHeights = ComputeRailTieHeights(folders, *selector_);
            store.Want(plugin_browser::MakeLayingPreviewKey(folders), browser_->ImageWorker(), std::move(request));
        }
    }

    const bool visible = !folders.empty() && store.Current() != nullptr;
    const Rml::String name = visible ? plugin_browser::MakeLayingImageName(store.ImageNumber()) : Rml::String();
    if (name != model_->image) {
        if (!model_->image.empty()) pendingRelease_ = model_->image;
        model_->image = name;
        modelHandle_.DirtyVariable("layingSetImage");
    }
    if (visible != model_->imageVisible) {
        model_->imageVisible = visible;
        modelHandle_.DirtyVariable("layingSetImageVisible");
    }
}

void RmlUiLayingSetView::OnDrop(int entryId) {
    if (!editor_ || !selector_ || !browser_ || entryId < 0) return;
    const std::vector<plugin_browser::PluginCatalogEntry>& entries = browser_->Catalog().Entries();
    if (static_cast<std::size_t>(entryId) >= entries.size()) return;
    const TrackPluginSetResult result = TrackPluginSetAction(*editor_, *selector_).Add(entries[static_cast<std::size_t>(entryId)].folderUtf8);
    const std::string text = TrackPluginSetErrorText(result);
    if (!text.empty()) queue_.Post(ToastSeverity::Warning, text);
    Refresh();
}

void RmlUiLayingSetView::OnRemove(const Rml::VariantList& arguments) {
    if (!editor_ || !selector_) return;
    const std::string folder = EventArgumentAt<Rml::String>(arguments, 0, Rml::String());
    const TrackPluginSetResult result = TrackPluginSetAction(*editor_, *selector_).Remove(folder);
    const std::string text = TrackPluginSetErrorText(result);
    if (!text.empty()) queue_.Post(ToastSeverity::Warning, text);
    Refresh();
}

}
