#include "RmlUiPluginBrowserModel.h"

#include <utility>

#include <RmlUi/Core/DataStructHandle.h>

#include "RmlUiEnumKey.h"

namespace rs::app::rmlui {

namespace {

// お気に入りタブのキー。
constexpr const char* kFavoritesTabKey = "favorites";

} // namespace

void RegisterBrowserModelTypes(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<BrowserTabModel> handle = constructor.RegisterStruct<BrowserTabModel>()) {
        handle.RegisterMember("key", &BrowserTabModel::key);
        handle.RegisterMember("label", &BrowserTabModel::label);
        handle.RegisterMember("selected", &BrowserTabModel::selected);
    }
    if (Rml::StructHandle<BrowserFilterOptionModel> handle = constructor.RegisterStruct<BrowserFilterOptionModel>()) {
        handle.RegisterMember("key", &BrowserFilterOptionModel::key);
        handle.RegisterMember("label", &BrowserFilterOptionModel::label);
        handle.RegisterMember("selected", &BrowserFilterOptionModel::selected);
        handle.RegisterMember("available", &BrowserFilterOptionModel::available);
    }
    if (Rml::StructHandle<BrowserCardModel> handle = constructor.RegisterStruct<BrowserCardModel>()) {
        handle.RegisterMember("id", &BrowserCardModel::id);
        handle.RegisterMember("name", &BrowserCardModel::name);
        handle.RegisterMember("author", &BrowserCardModel::author);
        handle.RegisterMember("image", &BrowserCardModel::image);
        handle.RegisterMember("favorite", &BrowserCardModel::favorite);
        handle.RegisterMember("inSet", &BrowserCardModel::inSet);
        handle.RegisterMember("duplicate", &BrowserCardModel::duplicate);
        handle.RegisterMember("selected", &BrowserCardModel::selected);
    }
    if (Rml::StructHandle<BrowserFolderModel> handle = constructor.RegisterStruct<BrowserFolderModel>()) {
        handle.RegisterMember("index", &BrowserFolderModel::index);
        handle.RegisterMember("name", &BrowserFolderModel::name);
        handle.RegisterMember("count", &BrowserFolderModel::count);
    }
    if (Rml::StructHandle<BrowserCrumbModel> handle = constructor.RegisterStruct<BrowserCrumbModel>()) {
        handle.RegisterMember("depth", &BrowserCrumbModel::depth);
        handle.RegisterMember("label", &BrowserCrumbModel::label);
        handle.RegisterMember("current", &BrowserCrumbModel::current);
    }
    if (Rml::StructHandle<BrowserDetailModel> handle = constructor.RegisterStruct<BrowserDetailModel>()) {
        handle.RegisterMember("text", &BrowserDetailModel::text);
    }
    constructor.RegisterArray<Rml::Vector<BrowserTabModel>>();
    constructor.RegisterArray<Rml::Vector<BrowserFilterOptionModel>>();
    constructor.RegisterArray<Rml::Vector<BrowserCardModel>>();
    constructor.RegisterArray<Rml::Vector<BrowserFolderModel>>();
    constructor.RegisterArray<Rml::Vector<BrowserCrumbModel>>();
    constructor.RegisterArray<Rml::Vector<BrowserDetailModel>>();
}

Rml::String BrowserEntryDisplayName(const plugin_browser::PluginCatalogEntry& entry) {
    return entry.name.empty() ? entry.relativeKey : entry.name;
}

BrowserCardModel ToBrowserCardModel(plugin_browser::EntryId id, const plugin_browser::PluginCatalogEntry& entry,
                                    bool favorite, bool inSet, Rml::String image) {
    BrowserCardModel card;
    card.id = static_cast<int>(id);
    card.name = BrowserEntryDisplayName(entry);
    card.author = entry.author;
    card.image = std::move(image);
    card.favorite = favorite;
    card.inSet = inSet;
    card.duplicate = !entry.shadowedFoldersUtf8.empty();
    return card;
}

Rml::String BrowserTabKey(plugin_browser::BrowserTab tab) {
    return tab.favorites ? Rml::String(kFavoritesTabKey) : ToEnumKey(tab.kind);
}

std::optional<plugin_browser::BrowserTab> ParseBrowserTabKey(const Rml::String& key) {
    if (key == kFavoritesTabKey) return plugin_browser::BrowserTab::Favorites();
    const std::optional<plugin_browser::BrowserKind> kind = ParseEnumKey<plugin_browser::BrowserKind>(key);
    if (!kind.has_value()) return std::nullopt;
    return plugin_browser::BrowserTab::OfKind(*kind);
}

} // namespace rs::app::rmlui
