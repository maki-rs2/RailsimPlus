//
// RmlUiPluginBrowserModel.h - プラグインブラウザがデータモデル"workspace"へ出す値の入れ物
//
// RmlUiPluginBrowserView.cpp(登録・更新)と、カードの組み立てなど同じ型を使う小さな.cppで共有する。
// 値は比較できるようにして、前回入れた値と変わったものだけDirtyVariableできるようにする。
//
#pragma once

#include <optional>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include "../plugin_browser/BrowserCardSizeCatalog.h"
#include "../plugin_browser/PluginBrowserState.h"

namespace rs::app::rmlui {

// タブ1つ(お気に入り+種別タブ)。keyはイベントselect_browser_tabの引数。
struct BrowserTabModel {
    Rml::String key;
    Rml::String label;
    bool selected = false;
    bool operator==(const BrowserTabModel&) const = default;
};

// 絞り込みの項目1つ。keyはイベントtoggle_browser_filterの引数。
struct BrowserFilterOptionModel {
    Rml::String key;
    Rml::String label;
    bool selected = false;
    bool available = true;
    bool operator==(const BrowserFilterOptionModel&) const = default;
};

// カード1枚。idはカタログのEntryId(イベントの引数)。imageは画像のソース名で、画像がまだ無ければ空
// (RML側が無地の箱を出す)。
struct BrowserCardModel {
    int id = 0;
    Rml::String name;
    Rml::String author;
    Rml::String image;
    bool favorite = false;
    bool inSet = false;     // レールの敷設プラグインセットに入っているか
    bool duplicate = false; // 別のルートに同じ相対パスのプラグインがある
    bool selected = false;  // 配置プレビュー中のプラグイン(最上段の「選択中」と同じもの)
    bool operator==(const BrowserCardModel&) const = default;
};

// フォルダのカード1枚。indexは今の階層のフォルダの添字(イベントenter_browser_folderの引数。
// カードのidとは別物)。countは「N件」の文字。
struct BrowserFolderModel {
    int index = 0;
    Rml::String name;
    Rml::String count;
    bool operator==(const BrowserFolderModel&) const = default;
};

// パンくずの1か所。depthは最上位からの深さ(イベントnavigate_browser_pathの引数)。
// currentは今いる階層で、押せない。
struct BrowserCrumbModel {
    int depth = 0;
    Rml::String label;
    bool current = false;
    bool operator==(const BrowserCrumbModel&) const = default;
};

// 重複の詳細の1行。
struct BrowserDetailModel {
    Rml::String text;
    bool operator==(const BrowserDetailModel&) const = default;
};

// データモデルへ出す値の入れ物(定義はここだけ。変数名は RmlUiPluginBrowserView.cpp のk*Key)。
struct PluginBrowserModel {
    Rml::Vector<BrowserTabModel> tabs;
    Rml::String search;
    // 並べ替え(何で並べるか・向き)。パネルの選択中の表示に使う。
    bool sortByAuthor = false;
    bool sortDescending = false;
    // 絞り込みの項目(表の順)。プロファイルで使えない項目はavailableが偽。
    Rml::Vector<BrowserFilterOptionModel> filterOptions;
    bool filterPanelOpen = false; // 漏斗のパネルを開いているか(画面だけの状態で、持ち主はこのモデル)
    bool filterActive = false;    // 並べ替えが初期値でない、または絞り込みを使っている(漏斗の目印)
    Rml::Vector<BrowserCardModel> cards;
    Rml::Vector<BrowserFolderModel> folders; // 窓の範囲にあるフォルダのカード
    Rml::Vector<BrowserCrumbModel> crumbs;   // 階層を出していないときは空(行ごと出さない)
    int backDepth = -1;         // 戻り先の深さ。最上位では-1で、戻るボタンの行を出さない
    Rml::String topSpace = "0px"; // 文書の読込前の値(空だとRCSSの構文エラーになる)
    Rml::String bottomSpace = "0px";
    Rml::String status;
    bool scanning = false;
    bool setButtons = false;
    bool placementOptions = false;
    bool continuousShown = false; // 連続配置の表示用の写し(持ち主はSceneryPlacementUiState)
    bool scenerySnapToRail = false;
    bool sceneryTransformShown = false;
    Rml::String sceneryX;
    Rml::String sceneryY;
    Rml::String sceneryZ;
    Rml::String sceneryYaw;
    Rml::String sceneryPitch;
    Rml::String sceneryRoll;
    Rml::Vector<BrowserDetailModel> detail;
    Rml::String cardSize = plugin_browser::GetBrowserCardSize(plugin_browser::kDefaultBrowserCardSize).key;
    Rml::String cardWidth = "0px";
    Rml::String thumbHeight = "0px";
    Rml::String nameFontSize = "0dp";   // 名前・作者の文字の大きさ(大きさの表の値)
    Rml::String authorFontSize = "0dp";
    Rml::String hoverName;   // マウスを乗せているカードの名前の全文(乗せていなければ空)
    Rml::String hoverDetail; // 同じ、作者と説明文
    Rml::String selectedName; // 最上段の「選択中」の名前(選択なしなら空)
};

// ---- モデルの組み立て(RmlUiPluginBrowserModel.cpp) ----

// 構造体と配列のデータモデル登録。
void RegisterBrowserModelTypes(Rml::DataModelConstructor& constructor);

// カードに出す名前。名前が空のときは相対パスで代える。
Rml::String BrowserEntryDisplayName(const plugin_browser::PluginCatalogEntry& entry);

BrowserCardModel ToBrowserCardModel(plugin_browser::EntryId id, const plugin_browser::PluginCatalogEntry& entry,
                                    bool favorite, bool inSet, Rml::String image);

// タブのキー(イベントselect_browser_tabの引数)。お気に入りは"favorites"、種別タブは列挙名の小文字。
Rml::String BrowserTabKey(plugin_browser::BrowserTab tab);
std::optional<plugin_browser::BrowserTab> ParseBrowserTabKey(const Rml::String& key);

} // namespace rs::app::rmlui
