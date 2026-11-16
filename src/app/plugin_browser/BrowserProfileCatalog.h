//
// BrowserProfileCatalog.h - 作業モードごとのブラウザの設定(種別タブの並び・カードの主操作・
// 情景の配置設定の有無)を1つの表にまとめる
//
// モードごとの違いはこの表だけで持つ。
//
#pragma once

#include <array>
#include <cstddef>
#include <iterator>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"
#include "BrowserKindCatalog.h"

namespace rs::app::plugin_browser {

enum class BrowserProfile { None, Rail, Scenery, Signal, Train };

inline constexpr std::size_t kBrowserProfileCount = 5;

// カードの主操作。
enum class BrowserCardAction {
    None,
    ToggleSet, // 敷設プラグインセットへ追加/外す
    Place,     // 配置プレビューを始める
    SelectSignal, // 信号プラグインとして選ぶ(信号を置く操作の前提。配置プレビューは始めない)
    DragToFormation, // カードのクリックでは何もせず、カードを編成へドラッグして落とす
};

// 最上段の「選択中」と、カードの強調が見るEditorのフォルダ。
enum class BrowserSelectedFolder {
    None,
    SceneryPlacement, // editor.sceneryPlacementFolder(配置プレビュー中だけ)
    SignalPlugin,     // editor.signalPluginFolder
};

// 1プロファイルの種別タブの最大数(今はRailの6種別)。
inline constexpr std::size_t kMaxProfileTabs = 6;

struct BrowserProfileTraits {
    BrowserProfile profile;
    std::array<BrowserKind, kMaxProfileTabs> tabs; // 先頭からtabCount個が有効
    std::size_t tabCount;
    BrowserCardAction cardAction;
    BrowserSelectedFolder selectedFolder;
    bool showPlacementOptions; // 情景の配置設定(連続配置・プラットフォームレール自動生成)を出すか
};

inline constexpr BrowserProfileTraits kBrowserProfiles[] = {
    {BrowserProfile::None, {}, 0, BrowserCardAction::None, BrowserSelectedFolder::None, false},
    {BrowserProfile::Rail,
     {BrowserKind::Rail, BrowserKind::Tie, BrowserKind::Girder, BrowserKind::Pier, BrowserKind::Line,
      BrowserKind::Pole},
     6, BrowserCardAction::ToggleSet, BrowserSelectedFolder::None, false},
    {BrowserProfile::Scenery, {BrowserKind::Station, BrowserKind::Struct}, 2, BrowserCardAction::Place,
     BrowserSelectedFolder::SceneryPlacement, true},
    {BrowserProfile::Signal, {BrowserKind::Signal}, 1, BrowserCardAction::SelectSignal,
     BrowserSelectedFolder::SignalPlugin, false},
    {BrowserProfile::Train, {BrowserKind::Train}, 1, BrowserCardAction::DragToFormation,
     BrowserSelectedFolder::None, false},
};

static_assert(std::size(kBrowserProfiles) == kBrowserProfileCount, "kBrowserProfilesの行数がBrowserProfileと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kBrowserProfiles, &BrowserProfileTraits::profile),
              "kBrowserProfilesの並びがBrowserProfileの列挙順と一致しない");

constexpr bool AreProfileTabCountsValid() {
    for (const BrowserProfileTraits& row : kBrowserProfiles) {
        if (row.tabCount > kMaxProfileTabs) return false;
    }
    return true;
}
static_assert(AreProfileTabCountsValid(), "tabCountがkMaxProfileTabsを超えている");

constexpr const BrowserProfileTraits& GetBrowserProfile(BrowserProfile profile) {
    return kBrowserProfiles[rs::app::workspace::EnumIndex(profile)];
}

} // namespace rs::app::plugin_browser
