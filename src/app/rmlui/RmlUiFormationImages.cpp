#include "RmlUiFormationImages.h"

#include <RmlUi/Core/Core.h>

#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_browser/PluginImageSource.h"

namespace rs::app::rmlui {

using rs::app::plugin_browser::EntryId;
using rs::app::plugin_browser::ImageKey;
using rs::app::plugin_browser::ThumbnailView;

void RmlUiFormationImages::ReleaseAll() {
    for (const std::pair<const ImageKey, std::string>& item : emitted_) Rml::ReleaseTexture(item.second);
    emitted_.clear();
}

void RmlUiFormationImages::BeginFrame(plugin_browser::PluginBrowserService& service) {
    service_ = &service;
    wanted_.clear();
    wantedSet_.clear();
    const plugin_browser::PluginCatalog& catalog = service.Catalog();
    // 世代が変わるとEntryIdの意味が変わる。更新があれば、まだ無かったフォルダが見つかるかもしれない。
    if (catalog.Generation() != generation_) {
        ReleaseAll();
        generation_ = catalog.Generation();
    }
    if (catalog.Revision() != revision_) {
        revision_ = catalog.Revision();
        entryByFolder_.clear();
    }
}

std::optional<EntryId> RmlUiFormationImages::EntryOf(const std::string& folderUtf8) {
    const std::unordered_map<std::string, std::optional<EntryId>>::const_iterator cached =
        entryByFolder_.find(folderUtf8);
    if (cached != entryByFolder_.end()) return cached->second;
    const plugin_browser::PluginCatalog& catalog = service_->Catalog();
    const plugin_browser::PluginCatalogEntry* entry = catalog.Find(folderUtf8);
    std::optional<EntryId> id;
    if (entry != nullptr) id = static_cast<EntryId>(entry - catalog.Entries().data());
    entryByFolder_[folderUtf8] = id;
    return id;
}

std::string RmlUiFormationImages::Want(const std::string& folderUtf8, ThumbnailView view) {
    if (service_ == nullptr) return {};
    const std::optional<EntryId> id = EntryOf(folderUtf8);
    if (!id.has_value()) return {}; // カタログに無い車両は車両名の箱のまま
    const ImageKey key(*id, view);
    if (wantedSet_.insert(key).second) wanted_.push_back(key);
    // アイコンだけのときは出さない(あとで差し替わると、ここでは古いテクスチャを解放できない)。
    if (!service_->Images().HasThumbnail(key)) return {};
    std::string& source = emitted_[key];
    if (source.empty()) {
        source = plugin_browser::MakeImageSourceName({generation_, key, plugin_browser::ImageSurface::FormationCard});
    }
    return source;
}

std::string RmlUiFormationImages::WantHeadIcon(const std::string& headFolderUtf8, bool headReversed) {
    return Want(headFolderUtf8, headReversed ? ThumbnailView::FrontFaceOpposite : ThumbnailView::FrontFace);
}

void RmlUiFormationImages::EndFrame() {
    if (service_ == nullptr) return;
    service_->Images().RequestViews(wanted_, service_->Catalog(), service_->ImageWorker());
    for (std::unordered_map<ImageKey, std::string>::iterator it = emitted_.begin(); it != emitted_.end();) {
        if (wantedSet_.count(it->first) == 0 || !service_->Images().HasThumbnail(it->first)) {
            Rml::ReleaseTexture(it->second);
            it = emitted_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace rs::app::rmlui
