#include "PluginBrowserState.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/SearchTerms.h"
#include "../workspace/EnumIndex.h"

namespace rs::app::plugin_browser {
namespace {

using rs::platform_text::ContainsAllTerms;
using rs::platform_text::SplitSearchTerms;
using rs::platform_text::ToAsciiLower;

// お気に入りタブの表示名。
constexpr const char* kFavoritesTabLabel = "お気に入り";

// 検索の対象文字列(名前・作者・種別の表示名)。語が名前と作者にまたがって当たらないよう改行でつなぐ。
std::string SearchHaystack(const PluginCatalogEntry& entry) {
    return ToAsciiLower(entry.name + "\n" + entry.author + "\n" + GetBrowserKind(entry.kind).displayName);
}

// 並べ替えの表(BrowserSortCatalog)に従う順(UTF-8のバイト順)。同じ値はrelativeKeyの昇順。
bool EntryLess(const PluginCatalogEntry& a, const PluginCatalogEntry& b, const BrowserSortTraits& sort) {
    const std::string& left = sort.byAuthor ? a.author : a.name;
    const std::string& right = sort.byAuthor ? b.author : b.name;
    if (left != right) return sort.descending ? left > right : left < right;
    return a.relativeKey < b.relativeKey;
}

// 絞り込みの項目1つをエントリが満たすか。項目の判定はここだけに置く。
bool EntryMeetsFilter(BrowserFilter filter, EntryId id, const PluginCatalogEntry& entry,
                      const BrowserFilterContext& context) {
    switch (filter) {
    case BrowserFilter::InSet: return context.inSet(id, entry);
    case BrowserFilter::NotInSet: return !context.inSet(id, entry);
    case BrowserFilter::Duplicate: return !entry.shadowedFoldersUtf8.empty();
    case BrowserFilter::Signal: return entry.hasSignal;
    }
    return true;
}

// 選んだ項目をすべて満たすか(AND)。
bool EntryMeetsFilters(BrowserFilterSet filters, EntryId id, const PluginCatalogEntry& entry,
                       const BrowserFilterContext& context) {
    for (const BrowserFilterTraits& row : kBrowserFilters) {
        if ((filters & FilterBit(row.filter)) == 0) continue;
        if (!EntryMeetsFilter(row.filter, id, entry, context)) return false;
    }
    return true;
}

// 行の範囲[firstRow, lastRow)をカードの範囲へ直す。
CardRange RowsToCards(std::size_t firstRow, std::size_t lastRow, std::size_t columns, std::size_t total) {
    return {std::min(firstRow * columns, total), std::min(lastRow * columns, total)};
}

// 位置y(px)を含む行。tops[0, rows)のうちy以下で最後の上端の行(yが先頭より上なら0)。
std::size_t RowContaining(const std::vector<double>& tops, std::size_t rows, double y) {
    const std::vector<double>::const_iterator next = std::upper_bound(tops.begin(), tops.begin() + rows, y);
    const std::size_t count = static_cast<std::size_t>(next - tops.begin());
    return count > 0 ? count - 1 : 0;
}

// 位置y(px)より下に上端が来る最初の行(= yまでを覆う行の終わり)。最大でrows。
std::size_t RowEndCovering(const std::vector<double>& tops, std::size_t rows, double y) {
    const std::vector<double>::const_iterator at = std::lower_bound(tops.begin(), tops.end(), y);
    return std::min(static_cast<std::size_t>(at - tops.begin()), rows);
}

} // namespace

bool EntryMatchesKind(const PluginCatalogEntry& entry, BrowserKind tabKind) {
    // 信号用の種別は走査の単位ではなく、Stationの結果のうち信号つきのものを出す。
    if (GetBrowserKind(tabKind).signalOnly) return entry.hasSignal && entry.kind == BrowserKind::Station;
    return entry.kind == tabKind;
}

const char* PluginBrowserState::TabLabel(BrowserTab tab) {
    return tab.favorites ? kFavoritesTabLabel : GetBrowserKind(tab.kind).displayName;
}

std::vector<BrowserTab> PluginBrowserState::Tabs() const {
    std::vector<BrowserTab> tabs;
    const BrowserProfileTraits& traits = GetBrowserProfile(profile_);
    if (traits.tabCount == 0) return tabs;
    tabs.push_back(BrowserTab::Favorites());
    for (std::size_t i = 0; i < traits.tabCount; ++i) tabs.push_back(BrowserTab::OfKind(traits.tabs[i]));
    return tabs;
}

void PluginBrowserState::SetProfile(BrowserProfile profile) {
    if (profile == profile_) return;
    rememberedTabs_[rs::app::workspace::EnumIndex(profile_)] = tab_;
    profile_ = profile;
    const BrowserProfileTraits& traits = GetBrowserProfile(profile);
    const std::optional<BrowserTab>& remembered = rememberedTabs_[rs::app::workspace::EnumIndex(profile)];
    if (remembered.has_value()) {
        tab_ = *remembered;
    } else {
        tab_ = traits.tabCount > 0 ? BrowserTab::OfKind(traits.tabs[0]) : BrowserTab::Favorites();
    }
    dirty_ = true;
}

void PluginBrowserState::SelectTab(BrowserTab tab) {
    const std::vector<BrowserTab> tabs = Tabs();
    if (std::find(tabs.begin(), tabs.end(), tab) == tabs.end()) return;
    if (tab == tab_) return;
    tab_ = tab;
    dirty_ = true;
}

void PluginBrowserState::SetSearch(std::string search) {
    if (search == search_) return;
    search_ = std::move(search);
    dirty_ = true;
}

void PluginBrowserState::SetSortOrder(BrowserSortOrder order) {
    if (order == sortOrder_) return;
    sortOrder_ = order;
    dirty_ = true;
}

void PluginBrowserState::ToggleFilter(BrowserFilter filter) {
    const BrowserFilterSet bit = FilterBit(filter);
    if ((filters_ & bit) != 0) {
        filters_ &= ~bit;
    } else {
        filters_ = (filters_ & ~GetBrowserFilter(filter).exclusive) | bit;
    }
    dirty_ = true;
}

void PluginBrowserState::ClearFilters() {
    if (filters_ == 0) return;
    filters_ = 0;
    dirty_ = true;
}

bool PluginBrowserState::EntryInProfile(const PluginCatalogEntry& entry) const {
    const BrowserProfileTraits& traits = GetBrowserProfile(profile_);
    for (std::size_t i = 0; i < traits.tabCount; ++i) {
        if (EntryMatchesKind(entry, traits.tabs[i])) return true;
    }
    return false;
}

const std::vector<std::string>& PluginBrowserState::Path() const {
    static const std::vector<std::string> kNoPath;
    return tab_.favorites ? kNoPath : paths_[rs::app::workspace::EnumIndex(tab_.kind)];
}

std::vector<std::string>& PluginBrowserState::MutablePath() {
    return paths_[rs::app::workspace::EnumIndex(tab_.kind)];
}

std::vector<std::string> PluginBrowserState::PathLabels() const {
    std::vector<std::string> labels{TabLabel(tab_)};
    const std::vector<std::string>& path = Path();
    labels.insert(labels.end(), path.begin(), path.end());
    return labels;
}

void PluginBrowserState::EnterFolder(std::size_t folderIndex) {
    if (!showsHierarchy_ || folderIndex >= folders_.size()) return;
    MutablePath().push_back(folders_[folderIndex].name);
    dirty_ = true;
}

void PluginBrowserState::NavigateToDepth(std::size_t depth) {
    if (!showsHierarchy_ || depth >= Path().size()) return;
    MutablePath().resize(depth);
    dirty_ = true;
}

void PluginBrowserState::UpdateTreeIndex(const PluginCatalog& catalog, const rs::legacy_layout::PluginTreeSet* trees,
                                         std::uint64_t treesRevision) {
    IndexKey key;
    key.valid = true;
    key.kind = tab_.kind;
    key.revision = catalog.Revision();
    key.generation = catalog.Generation();
    key.treesRevision = treesRevision;
    key.hasTrees = trees != nullptr;
    if (key == indexKey_) return;
    indexKey_ = key;
    const rs::legacy_layout::PluginTreeDir* tree = trees != nullptr ? trees->Find(GetBrowserKind(tab_.kind).folderName) : nullptr;
    index_.Build(tree, tab_.kind, catalog);
}

void PluginBrowserState::CollectFlatResults(const PluginCatalog& catalog, const std::vector<std::string>& terms) {
    const std::vector<PluginCatalogEntry>& entries = catalog.Entries();
    for (EntryId id = 0; id < entries.size(); ++id) {
        const PluginCatalogEntry& entry = entries[id];
        const bool inTab = tab_.favorites
                               ? (EntryInProfile(entry) && favoriteKeys_.count(ToAsciiLower(entry.relativeKey)) > 0)
                               : EntryMatchesKind(entry, tab_.kind);
        if (!inTab) continue;
        if (!terms.empty() && !ContainsAllTerms(SearchHaystack(entry), terms)) continue;
        results_.push_back(id);
    }
}

bool PluginBrowserState::Refilter(const PluginCatalog& catalog, const std::vector<std::string>& favorites,
                                  const rs::legacy_layout::PluginTreeSet* trees, std::uint64_t treesRevision,
                                  const BrowserFilterContext& filterContext) {
    const bool favoritesChanged = favorites != lastFavorites_;
    if (!dirty_ && !favoritesChanged && catalog.Revision() == lastRevision_ &&
        catalog.Generation() == lastGeneration_ && treesRevision == lastTreesRevision_ &&
        filterContext.setRevision == lastSetRevision_) {
        return false;
    }
    dirty_ = false;
    lastSetRevision_ = filterContext.setRevision;
    lastRevision_ = catalog.Revision();
    lastGeneration_ = catalog.Generation();
    lastTreesRevision_ = treesRevision;
    if (favoritesChanged) {
        lastFavorites_ = favorites;
        favoriteKeys_.clear();
        for (const std::string& key : favorites) favoriteKeys_.insert(ToAsciiLower(key));
    }

    results_.clear();
    items_.clear();
    folders_.clear();
    showsHierarchy_ = false;
    if (GetBrowserProfile(profile_).tabCount == 0) return true; // タブが無いプロファイルは何も出さない

    const std::vector<std::string> terms = SplitSearchTerms(search_);
    if (!tab_.favorites) UpdateTreeIndex(catalog, trees, treesRevision);
    showsHierarchy_ = !tab_.favorites && terms.empty() && index_.HasTree();
    if (showsHierarchy_) {
        // 木の読み直しなどで今の階層が無くなっていたら最上位へ戻す。
        if (!index_.Contains(Path())) MutablePath().clear();
        folders_ = index_.FoldersAt(Path());
        results_ = index_.EntriesAt(Path());
    } else {
        CollectFlatResults(catalog, terms);
    }

    const std::vector<PluginCatalogEntry>& entries = catalog.Entries();
    // 絞り込み。セットを扱わないプロファイルでは、セットの項目は無いものとして扱う。
    const BrowserFilterSet activeFilters = UsableFilters(filters_, static_cast<bool>(filterContext.inSet));
    if (activeFilters != 0) {
        std::erase_if(results_, [&](EntryId id) {
            return !EntryMeetsFilters(activeFilters, id, entries[id], filterContext);
        });
    }
    const BrowserSortTraits& sort = GetBrowserSort(sortOrder_);
    std::sort(results_.begin(), results_.end(),
              [&entries, &sort](EntryId a, EntryId b) { return EntryLess(entries[a], entries[b], sort); });
    for (std::size_t i = 0; i < folders_.size(); ++i) items_.push_back(FolderItem(i));
    items_.insert(items_.end(), results_.begin(), results_.end());
    return true;
}

BrowserWindow ComputeBrowserWindow(const BrowserWindowInput& input) {
    BrowserWindow window;
    const bool measured = input.columns > 0 && !input.rowHeights.empty() &&
                          input.rowHeights.size() == BrowserRowCount(input.totalCount, input.columns);
    if (!measured) {
        // 未計測: 先頭から固定数だけ作る。空きは付けない。
        const std::size_t count = std::min(kUnmeasuredCardCount, input.totalCount);
        window.build = {0, count};
        window.visible = window.build;
        window.resident = window.build;
        return window;
    }

    const std::size_t rows = input.rowHeights.size();
    const std::vector<double> tops = ComputeRowTops(input.rowHeights);
    const double viewHeight = std::max(static_cast<double>(input.viewHeight), 0.0);
    const double maxScroll = std::max(0.0, tops[rows] - viewHeight);
    const double scrollTop = std::clamp(static_cast<double>(input.scrollTop), 0.0, maxScroll);

    // 見えている行[visibleFirst, visibleEnd)。1行は必ず含める。
    const std::size_t visibleFirst = std::min(RowContaining(tops, rows, scrollTop), rows - 1);
    const std::size_t visibleEnd = std::max(visibleFirst + 1, RowEndCovering(tops, rows, scrollTop + viewHeight));

    // 作る行は、見えている行の前後に1行ずつ足す。
    const std::size_t buildFirst = visibleFirst > 0 ? visibleFirst - 1 : 0;
    const std::size_t buildEnd = std::min(visibleEnd + 1, rows);
    window.build = RowsToCards(buildFirst, buildEnd, input.columns, input.totalCount);
    window.visible = RowsToCards(visibleFirst, visibleEnd, input.columns, input.totalCount);
    window.topSpace = static_cast<float>(tops[buildFirst]);
    window.bottomSpace = static_cast<float>(tops[rows] - tops[buildEnd]);

    // GPUに置く行は、見えている行の前後に1画面ぶん。
    const std::size_t residentFirst = RowContaining(tops, rows, scrollTop - viewHeight);
    const std::size_t residentEnd = RowEndCovering(tops, rows, scrollTop + 2.0 * viewHeight);
    window.resident = RowsToCards(std::min(residentFirst, visibleFirst), std::max(residentEnd, visibleEnd),
                                  input.columns, input.totalCount);
    return window;
}

std::vector<double> ComputeRowTops(std::span<const float> rowHeights) {
    std::vector<double> tops(rowHeights.size() + 1, 0.0);
    for (std::size_t i = 0; i < rowHeights.size(); ++i) tops[i + 1] = tops[i] + static_cast<double>(rowHeights[i]);
    return tops;
}

} // namespace rs::app::plugin_browser
