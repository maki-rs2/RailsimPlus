//
// WorkModeCatalog.h - 作業モードごとの表示名・下部バー文字・レイアウト(左右タブの中身/
// 敷設エディタの有無)を1つの表にまとめる。
//
// モード順序・左右タブの中身(左タブの中身としてのプラグインブラウザ、
// レールモードの敷設エディタ)を1箇所へ集約する。モードごとの違いはこの表だけで表し、ほかのコードにモード名での分岐を書かない。
//
#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <optional>

#include "../plugin_browser/BrowserProfileCatalog.h"
#include "../ui/ViewMode.h"
#include "../input/InputTypes.h"
#include "CatalogCheck.h"
#include "ScreenCatalog.h"
#include "WorkspaceShell.h"

namespace rs::app::workspace {

using rs::app::plugin_browser::BrowserProfile;

// 1モード分のレイアウト定義。表示名・下部バーのアイコン名は固定の文字列
// (assets/ui/workspace/workspace.rmlのdata-forが、アイコン名の".ic-"クラス(widgets.rcss)を付けて表示する)。
struct WorkModeLayout {
    WorkMode mode;
    const char* displayName;     // 上部バナーに出す名称
    const char* bottomBarIcon;   // 下部バーのアイコン名(widgets.rcssの.ic-mode-*の後半)
    // 左右タブの中身(NotchSideの並び=左,右。EnumIndex()で引く)。列車・レール・信号・情景の左タブは
    // プラグインブラウザ。
    std::array<TabContent, kNotchSideCount> tabs;
    bool hasLayingEditor;   // 敷設エディタ(敷設/編集の切替)
    // 左タブのプラグインブラウザの設定(種別タブの並び等。ブラウザでないモードはNone)。
    rs::app::plugin_browser::BrowserProfile browserProfile;
    rs::app::ViewMode viewMode; // このモードのとき編集ツール側へ渡す表示モード
    // ビューポートの個体をクリックするとプラグイン設定パネルを開くか。
    bool clickOpensPluginSettings;
    std::optional<rs::app::input::InputAction> shortcutAction;
    bool showInBottomBar;
};

// 表示順は次のとおり(レール・列車・信号・ダイヤ・
// 情景・地形編集)。下部バーのモードボタンはこの順にdata-forで生成する。
inline constexpr WorkModeLayout kWorkModeCatalog[] = {
    {WorkMode::Rail, "レール", "mode-rail", {TabContent::PluginBrowser, TabContent::None}, true, BrowserProfile::Rail,
     rs::app::ViewMode::Rail, false, rs::app::input::InputAction::SelectModeRail, true},
    {WorkMode::Train, "列車", "mode-train", {TabContent::PluginBrowser, TabContent::None}, false, BrowserProfile::Train, rs::app::ViewMode::Train,
     false, rs::app::input::InputAction::SelectModeTrain, true},
    {WorkMode::Signal, "信号", "mode-signal", {TabContent::PluginBrowser, TabContent::None}, false, BrowserProfile::Signal,
     rs::app::ViewMode::SignalBlock, false, rs::app::input::InputAction::SelectModeSignal, true},
    {WorkMode::Dia, "ダイヤ", "mode-dia", {TabContent::Panel, TabContent::None}, false, BrowserProfile::None, rs::app::ViewMode::StationDia,
     false, rs::app::input::InputAction::SelectModeDia, true},
    {WorkMode::Scenery, "情景", "mode-scenery", {TabContent::PluginBrowser, TabContent::None}, false, BrowserProfile::Scenery,
     rs::app::ViewMode::Scenery, false, rs::app::input::InputAction::SelectModeScenery, true},
    {WorkMode::Terrain, "地形編集", "mode-terrain", {TabContent::Panel, TabContent::None}, false, BrowserProfile::None, rs::app::ViewMode::Spectator,
     false, std::nullopt, false},
};

// 列挙と表の件数・順番が食い違ったらコンパイルエラーにする(下のGetWorkModeLayoutは添字で引くため)。
static_assert(std::size(kWorkModeCatalog) == kWorkModeCount, "kWorkModeCatalogの行数がWorkModeと一致しない");
static_assert(IsCatalogInEnumOrder(kWorkModeCatalog, &WorkModeLayout::mode), "kWorkModeCatalogの並びがWorkModeの列挙順と一致しない");

// modeに対応する表の行を返す。
constexpr const WorkModeLayout& GetWorkModeLayout(WorkMode mode) {
    return kWorkModeCatalog[EnumIndex(mode)];
}

// 画面状態から編集ツール側のViewModeを求める。作業モードのときはモード表の列を引くだけで、
// 鑑賞・環境画面・カメラ画面は編集ツールを止めるためSpectatorにする。運転台視点中も、
// 列車のホバー・選択などの編集操作を止めるためSpectatorにする。
inline rs::app::ViewMode ToViewMode(const WorkspaceSnapshot& snapshot) {
    if (!snapshot.InWorkMode() || snapshot.cabinView) return rs::app::ViewMode::Spectator;
    return GetWorkModeLayout(snapshot.workMode).viewMode;
}

// ビューポートの個体をクリックするとプラグイン設定パネルを開く画面状態か。作業モードのときは
// 作業モードの表の列、それ以外は画面の表(ScreenCatalog.h)の列を引く。ViewMode::Spectatorからは
// 判定しない(地形編集・環境・カメラもSpectatorになるため)。この判定はここだけで行う。
inline bool ClickOpensPluginSettings(const WorkspaceSnapshot& snapshot) {
    if (snapshot.InWorkMode()) return GetWorkModeLayout(snapshot.workMode).clickOpensPluginSettings;
    return GetScreenLayout(snapshot.screen).clickOpensPluginSettings;
}

// ImGui画面は作業モードの画面状態を持たないため、ViewMode表の操作可否を使う。
// 固定ワークスペースの画面との対応をViewModeだけから推測しない。
inline bool ClickOpensPluginSettings(rs::app::ViewMode viewMode) {
    return GetViewModeTraits(viewMode).clickOpensPluginSettings;
}

} // namespace rs::app::workspace
