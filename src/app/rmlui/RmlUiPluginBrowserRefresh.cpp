#include "RmlUiPluginBrowserView.h"

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include "RmlUiBrowserHoverText.h"
#include "RmlUiPluginBrowserModel.h"
#include "RmlUiUpdateVariable.h"
#include "../action/PluginBrowserCardSizeAction.h"
#include "../action/PluginFavoriteAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../settings/UserSettings.h"

namespace rs::app::rmlui {
namespace {

using rs::app::plugin_browser::BrowserCardAction;
using rs::app::plugin_browser::BrowserProfileTraits;
using rs::app::plugin_browser::BrowserTab;
using rs::app::plugin_browser::BrowserWindow;
using rs::app::plugin_browser::EntryId;
using rs::app::plugin_browser::PluginBrowserState;
using rs::app::plugin_browser::PluginCatalogEntry;

constexpr const char* kSortByAuthorKey = "browserSortByAuthor";
constexpr const char* kSortDescendingKey = "browserSortDescending";
constexpr const char* kFilterOptionsKey = "browserFilterOptions";
constexpr const char* kFilterActiveKey = "browserFilterActive";
constexpr const char* kCardsKey = "browserCards";
constexpr const char* kTopSpaceKey = "browserTopSpace";
constexpr const char* kBottomSpaceKey = "browserBottomSpace";
constexpr const char* kStatusKey = "browserStatus";
constexpr const char* kScanningKey = "browserScanning";
constexpr const char* kDetailKey = "browserDetail";
constexpr const char* kCardWidthKey = "browserCardWidth";
constexpr const char* kThumbHeightKey = "browserThumbHeight";
constexpr const char* kNameFontSizeKey = "browserCardNameFontSize";
constexpr const char* kAuthorFontSizeKey = "browserCardAuthorFontSize";
constexpr const char* kHoverNameKey = "browserHoverName";
constexpr const char* kHoverDetailKey = "browserHoverDetail";
constexpr const char* kTabsKey = "browserTabs";
constexpr const char* kStatusNoRoots = "プラグインフォルダが未登録です(ゲーム設定から追加)";
constexpr const char* kStatusNoTabs = "このモードで出せるプラグインはありません";
constexpr const char* kStatusNoFavorites = "お気に入りがありません(カードの星で登録)";
constexpr const char* kStatusNoFavoritesMatch = "検索に一致するお気に入りがありません";
constexpr const char* kStatusNoFilterMatch = "絞り込みに一致するプラグインがありません(漏斗で解除)";
constexpr const char* kStatusNoMatch = "検索に一致するプラグインがありません(言葉を変えて探す)";
constexpr const char* kStatusEmptyFolder = "このフォルダにはプラグインがありません";
constexpr const char* kStatusEmptyKind = "この種別のプラグインが見つかりません";

std::string PxText(float pixels) { return std::format("{:.2f}px", pixels); }
std::string DpText(float dp) { return std::format("{:.1f}dp", dp); }

const char* EmptyReason(const PluginBrowserState& state) {
    if (plugin_browser::GetBrowserProfile(state.Profile()).tabCount == 0) return kStatusNoTabs;
    const bool searching = !state.Search().empty();
    const bool hasSetProfile = plugin_browser::GetBrowserProfile(state.Profile()).cardAction == BrowserCardAction::ToggleSet;
    if (plugin_browser::UsableFilters(state.Filters(), hasSetProfile) != 0) return kStatusNoFilterMatch;
    if (state.CurrentTab().favorites) return searching ? kStatusNoFavoritesMatch : kStatusNoFavorites;
    if (searching) return kStatusNoMatch;
    return state.ShowsHierarchy() && !state.Path().empty() ? kStatusEmptyFolder : kStatusEmptyKind;
}

} // namespace

void RmlUiPluginBrowserView::RefreshSortAndFilter(bool hasSetProfile) {
    const PluginBrowserState& state = service_->State();
    const plugin_browser::BrowserSortTraits& sort = plugin_browser::GetBrowserSort(state.SortOrder());
    UpdateVariable(modelHandle_, kSortByAuthorKey, model_->sortByAuthor, sort.byAuthor);
    UpdateVariable(modelHandle_, kSortDescendingKey, model_->sortDescending, sort.descending);
    Rml::Vector<BrowserFilterOptionModel> options;
    for (const plugin_browser::BrowserFilterTraits& filter : plugin_browser::kBrowserFilters) {
        options.push_back({filter.key, filter.label, (state.Filters() & plugin_browser::FilterBit(filter.filter)) != 0,
                           hasSetProfile || !filter.needsSetProfile});
    }
    UpdateVariable(modelHandle_, kFilterOptionsKey, model_->filterOptions, options);
    const bool active = state.SortOrder() != plugin_browser::kDefaultBrowserSortOrder ||
                        plugin_browser::UsableFilters(state.Filters(), hasSetProfile) != 0;
    UpdateVariable(modelHandle_, kFilterActiveKey, model_->filterActive, active);
}

void RmlUiPluginBrowserView::RefreshTabs() {
    const PluginBrowserState& state = service_->State();
    Rml::Vector<BrowserTabModel> tabs;
    for (const BrowserTab& tab : state.Tabs()) {
        tabs.push_back({BrowserTabKey(tab), PluginBrowserState::TabLabel(tab), tab == state.CurrentTab()});
    }
    UpdateVariable(modelHandle_, kTabsKey, model_->tabs, tabs);
}

void RmlUiPluginBrowserView::RefreshCards(Rml::ElementDocument& document) {
    const PluginBrowserState& state = service_->State();
    const BrowserProfileTraits& traits = plugin_browser::GetBrowserProfile(state.Profile());
    const std::vector<PluginCatalogEntry>& entries = service_->Catalog().Entries();
    const std::vector<EntryId>& items = state.Items();
    const plugin_browser::BrowserCardSizeTraits& sizeTraits =
        plugin_browser::GetBrowserCardSize(PluginBrowserCardSizeAction(*userSettings_).Size());
    const BrowserGridFrame frame = gridDriver_.Update(document, items, sizeTraits, scrollToTopPending_);
    scrollToTopPending_ = false;
    const BrowserWindow& window = frame.window;
    UpdateVariable(modelHandle_, kCardWidthKey, model_->cardWidth, PxText(frame.layout.cardWidth));
    UpdateVariable(modelHandle_, kThumbHeightKey, model_->thumbHeight, PxText(frame.layout.thumbHeight));
    UpdateVariable(modelHandle_, kNameFontSizeKey, model_->nameFontSize, DpText(sizeTraits.nameFontDp));
    UpdateVariable(modelHandle_, kAuthorFontSizeKey, model_->authorFontSize, DpText(sizeTraits.authorFontDp));
    images_.Update(*service_, items, window);
    const bool tracksSet = traits.cardAction == BrowserCardAction::ToggleSet;
    const PluginFavoriteAction favorites(*userSettings_);
    Rml::Vector<BrowserCardModel> cards;
    for (std::size_t i = window.build.first; i < window.build.last; ++i) {
        if (plugin_browser::IsFolderItem(items[i])) continue;
        const EntryId id = items[i];
        const PluginCatalogEntry& entry = entries[id];
        const bool inSet = tracksSet && membership_.Contains(id, entry);
        cards.push_back(ToBrowserCardModel(id, entry, favorites.IsFavorite(entry.relativeKey), inSet,
                                            images_.SourceFor(*service_, id)));
        cards.back().selected = !selected_.Folder().empty() && entry.folderUtf8 == selected_.Folder();
    }
    UpdateVariable(modelHandle_, kCardsKey, model_->cards, cards);
    folder_.Refresh(modelHandle_, *model_, window.build);
    UpdateVariable(modelHandle_, kTopSpaceKey, model_->topSpace, PxText(window.topSpace));
    UpdateVariable(modelHandle_, kBottomSpaceKey, model_->bottomSpace, PxText(window.bottomSpace));
}

void RmlUiPluginBrowserView::RefreshDetail() {
    Rml::Vector<BrowserDetailModel> detail;
    if (!detailRelativeKey_.empty()) {
        const plugin_browser::PluginCatalog& catalog = service_->Catalog();
        const std::optional<std::size_t> id = catalog.FindByRelativeKey(detailRelativeKey_);
        if (id.has_value()) {
            const PluginCatalogEntry& entry = catalog.Entries()[*id];
            detail.push_back({"採用: " + entry.folderUtf8});
            for (const std::string& shadowed : entry.shadowedFoldersUtf8) detail.push_back({"重複: " + shadowed});
        }
    }
    UpdateVariable(modelHandle_, kDetailKey, model_->detail, detail);
}

void RmlUiPluginBrowserView::RefreshHover() {
    BrowserHoverText text;
    if (hoveredEntry_.has_value() && *hoveredEntry_ < service_->Catalog().Entries().size())
        text = BuildBrowserHoverText(service_->Catalog().Entries()[*hoveredEntry_]);
    UpdateVariable(modelHandle_, kHoverNameKey, model_->hoverName, text.name);
    UpdateVariable(modelHandle_, kHoverDetailKey, model_->hoverDetail, text.detail);
}

void RmlUiPluginBrowserView::RefreshStatus() {
    Rml::String status;
    bool scanning = false;
    if (!operationError_.empty()) status = operationError_;
    else if (service_->Started() && service_->Roots().empty()) status = kStatusNoRoots;
    else if (service_->Scanning()) {
        scanning = true;
        status = std::format("読込中 {}件", service_->Catalog().Entries().size());
    }
    else if (service_->State().Items().empty()) status = EmptyReason(service_->State());
    UpdateVariable(modelHandle_, kStatusKey, model_->status, status);
    UpdateVariable(modelHandle_, kScanningKey, model_->scanning, scanning);
}

} // namespace rs::app::rmlui
