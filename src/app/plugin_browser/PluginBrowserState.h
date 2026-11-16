//
// PluginBrowserState.h - ブラウザの画面状態(タブ・検索語・絞り込み結果)と、見えている行だけ
// 作るための窓の範囲の計算(RmlUi非依存の純C++)
//
// メインスレッド専用。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "BrowserFilterCatalog.h"
#include "BrowserKindCatalog.h"
#include "BrowserProfileCatalog.h"
#include "BrowserSortCatalog.h"
#include "PluginCatalog.h"
#include "PluginTreeIndex.h"

namespace rs::app::plugin_browser {

// ---- グリッドの項目 ----
// グリッドの窓・行の高さ・画像の要求は項目の列(Items())の添字で動く。項目はエントリのEntryIdか、
// フォルダ(最上位ビットを立てた値)。印の判定と復元はここだけで行う。
inline constexpr EntryId kFolderItemFlag = EntryId{1} << (std::numeric_limits<EntryId>::digits - 1);

constexpr bool IsFolderItem(EntryId item) { return (item & kFolderItemFlag) != 0; }
// Folders()の添字をフォルダ項目にする。
constexpr EntryId FolderItem(std::size_t folderIndex) { return folderIndex | kFolderItemFlag; }
// フォルダ項目からFolders()の添字を取り出す。
constexpr std::size_t FolderIndexOf(EntryId item) { return item & ~kFolderItemFlag; }


// タブ1つ。お気に入りか、種別のどれか。
struct BrowserTab {
    bool favorites = false;
    BrowserKind kind = BrowserKind::Rail; // favoritesが偽のときだけ意味を持つ

    static BrowserTab Favorites() { return {true, BrowserKind::Rail}; }
    static BrowserTab OfKind(BrowserKind tabKind) { return {false, tabKind}; }
    bool operator==(const BrowserTab&) const = default;
};

// 絞り込みが外から受ける情報。セットへの所属はUI側が持つ覚え書きから引くため、関数で受ける。
struct BrowserFilterContext {
    std::function<bool(EntryId, const PluginCatalogEntry&)> inSet; // 空ならセットを扱わない(セットの項目は無視)
    std::uint64_t setRevision = 0; // セットの内容が変わるたびに増える版
};

class PluginBrowserState {
public:
    // モードが変わったときに呼ぶ。プロファイルごとに前回選んでいたタブを覚えていて戻す
    // (起動中だけ、保存しない)。初めてのプロファイルは最初の種別タブ。
    void SetProfile(BrowserProfile profile);
    BrowserProfile Profile() const { return profile_; }

    // 今のプロファイルにないタブは無視する。
    void SelectTab(BrowserTab tab);
    void SetSearch(std::string search);
    // 並べ替え。全タブ共通(起動中だけ、保存しない)。
    void SetSortOrder(BrowserSortOrder order);
    BrowserSortOrder SortOrder() const { return sortOrder_; }
    // 絞り込みの項目の入り切り(全タブ共通、起動中だけ保存しない)。同時に選べない相手は外す。
    void ToggleFilter(BrowserFilter filter);
    void ClearFilters();
    BrowserFilterSet Filters() const { return filters_; }

    // 今のプロファイルのタブの並び(お気に入りが先頭、そのあとに種別タブ)。プロファイルがNoneなら空。
    std::vector<BrowserTab> Tabs() const;
    BrowserTab CurrentTab() const { return tab_; }
    const std::string& Search() const { return search_; }

    // カタログのRevision/Generation・タブ・検索語・お気に入りの列・プロファイルのどれかが
    // 変わったときだけ絞り込みをやり直し、やり直したらtrueを返す。
    // favoritesはPluginFavoriteAction::Favorites()(relativeKeyの列)。
    // trees/treesRevisionはディレクトリ分けの木(無ければnullptr)とその版。版が変わったときも作り直す。
    // filterContextはセット所属の絞り込みに使う口(セットを扱わないプロファイルでは空のまま渡す)。
    bool Refilter(const PluginCatalog& catalog, const std::vector<std::string>& favorites,
                  const rs::legacy_layout::PluginTreeSet* trees = nullptr, std::uint64_t treesRevision = 0,
                  const BrowserFilterContext& filterContext = {});

    // 絞り込み結果(SortOrder()の順、同じ値はrelativeKeyの昇順)。階層を出しているときは今の階層の直下のエントリだけ。
    const std::vector<EntryId>& Results() const { return results_; }

    // グリッドの項目(先頭に今の階層のフォルダ、その後ろにResults())。
    const std::vector<EntryId>& Items() const { return items_; }

    // 階層を出すか(お気に入りタブでなく、検索語が空で、そのタブの木がある)。Refilter後の値。
    bool ShowsHierarchy() const { return showsHierarchy_; }
    // 今の階層のフォルダ(木のファイル順)。階層を出していないときは空。
    const std::vector<PluginTreeFolder>& Folders() const { return folders_; }
    // 今の階層(最上位からのフォルダ名の列)。種別タブごとに覚える(起動中だけ)。
    const std::vector<std::string>& Path() const;
    // パンくず用の名前(最上位はタブの表示名、その後ろにPath())。
    std::vector<std::string> PathLabels() const;
    // Folders()[folderIndex]の階層へ入る。範囲外や階層を出していないときは何もしない。
    void EnterFolder(std::size_t folderIndex);
    // 最上位から数えてdepth番目の階層へ戻る(0=最上位)。今より深い指定は何もしない。
    void NavigateToDepth(std::size_t depth);

    // タブ表示用の名前(お気に入りは専用の文、種別はBrowserKindCatalogの表示名)。
    static const char* TabLabel(BrowserTab tab);

private:
    // 今のプロファイルのどれかの種別タブに入るか。
    bool EntryInProfile(const PluginCatalogEntry& entry) const;
    // 階層を出さないときの結果(今のタブ全体から、検索語で絞る)をresults_へ入れる。
    void CollectFlatResults(const PluginCatalog& catalog, const std::vector<std::string>& terms);
    // 今の種別の索引を、入力が変わったときだけ作り直す。
    void UpdateTreeIndex(const PluginCatalog& catalog, const rs::legacy_layout::PluginTreeSet* trees,
                         std::uint64_t treesRevision);
    std::vector<std::string>& MutablePath();

    BrowserProfile profile_ = BrowserProfile::None;
    BrowserTab tab_ = BrowserTab::Favorites();
    // プロファイルごとの前回のタブ。
    std::array<std::optional<BrowserTab>, kBrowserProfileCount> rememberedTabs_{};
    std::string search_;
    BrowserSortOrder sortOrder_ = kDefaultBrowserSortOrder;
    BrowserFilterSet filters_ = 0;

    bool dirty_ = true; // タブ・検索語・プロファイルが変わった
    std::uint64_t lastSetRevision_ = 0;
    std::uint64_t lastRevision_ = 0;
    std::uint64_t lastGeneration_ = 0;
    std::vector<std::string> lastFavorites_;
    std::unordered_set<std::string> favoriteKeys_; // ASCII小文字化したrelativeKey
    std::vector<EntryId> results_;
    std::vector<EntryId> items_;

    // 階層。索引は作ったときの入力(種別・カタログの版・木の版・木の有無)を覚え、変わったときだけ作り直す。
    struct IndexKey {
        bool valid = false;
        BrowserKind kind = BrowserKind::Rail;
        std::uint64_t revision = 0;
        std::uint64_t generation = 0;
        std::uint64_t treesRevision = 0;
        bool hasTrees = false;
        bool operator==(const IndexKey&) const = default;
    };
    PluginTreeIndex index_;
    IndexKey indexKey_;
    std::uint64_t lastTreesRevision_ = 0;
    std::array<std::vector<std::string>, kBrowserKindCount> paths_{}; // 種別ごとの今の階層
    bool showsHierarchy_ = false;
    std::vector<PluginTreeFolder> folders_;
};

// エントリが種別タブtabKindに入るか(信号用の種別はStationのうち信号つきだけ)。
bool EntryMatchesKind(const PluginCatalogEntry& entry, BrowserKind tabKind);

// ---- 窓の範囲 ----

// 行の高さ・列数が未計測のとき、先頭から作るカードの数。
inline constexpr std::size_t kUnmeasuredCardCount = 24;

struct BrowserWindowInput {
    float scrollTop = 0.0f;   // px
    float viewHeight = 0.0f;  // px
    // 行ごとの高さ(px)。全行ぶん(行数 = ceil(totalCount / columns))を、未測の行は見積もりで埋めて渡す。
    // 長さが行数と合わないときは未計測として扱う。
    std::span<const float> rowHeights;
    std::size_t columns = 0;  // 1行のカード数。0なら未計測
    std::size_t totalCount = 0;
};

// 1行のカード数から行数を求める(columnsは1以上)。
inline std::size_t BrowserRowCount(std::size_t totalCount, std::size_t columns) {
    return (totalCount + columns - 1) / columns;
}

// 行の上端の位置(px)。tops[k] = 行kの上端、tops[行数] = 全体の高さ。窓の計算と、スクロールの補正が同じ値を使う。
std::vector<double> ComputeRowTops(std::span<const float> rowHeights);

// カードの範囲[first, last)。
struct CardRange {
    std::size_t first = 0;
    std::size_t last = 0;
    bool operator==(const CardRange&) const = default;
};

struct BrowserWindow {
    CardRange build;          // 要素を作るカード(見えている行の前後に1行ずつ足す)
    float topSpace = 0.0f;    // 上の空き高さ(px)
    float bottomSpace = 0.0f; // 下の空き高さ(px)
    CardRange visible;        // 見えている行のカード(画像の要求範囲)
    CardRange resident;       // GPUに置く範囲(見えている行の前後に1画面ぶん)
};

BrowserWindow ComputeBrowserWindow(const BrowserWindowInput& input);

} // namespace rs::app::plugin_browser
