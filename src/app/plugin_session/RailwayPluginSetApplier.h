//
// RailwayPluginSetApplier.h - RailwayPluginSet\*.txt(敷設テンプレート)1本を
// パースし、Check=yesの各要素(Rail/Tie/Girder/Pier/Line/Pole)をPluginSelectorの
// レール系読込キャッシュへ確保する(app層グルー、Rs2SceneApplier.hと同じ位置づけ)。
//
// 敷設対象リスト(Editor::layingSettings.defaultTrackPlugins または選択中edgeのtrackPlugins)へ
// 実際に追加するかどうかはUI側(DrawTrackPluginListPanel)が既存の追加ロジック
// (重複排除+Undo対応のToggleSelectedEdgeTrackPlugin分岐)をresolvedFoldersへ
// 適用する形で行う。このApplierはEditorへ依存しない。
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

class PluginSelector;

// ApplyRailwayPluginSetFile の結果(診断表示用)。
struct RailwayPluginSetApplyResult {
    bool ok = false;
    std::string settingName;
    // Check=yesかつ読込に成功した要素の解決済み絶対パス(宣言順、Rail/Tie/Girder/
    // Pier/Line/Poleのうち存在するもののみ)。
    std::vector<std::string> resolvedFolders;
    std::vector<std::string> warnings;
};

// RailwayPluginSet\*.txt(setPathUtf8)をパースし、Check=yesの各要素を
// installRoot(=setPathUtf8の親の親、Rail/Tie/.../RailwayPluginSetの共通親)配下の
// <種別フォルダ>/<プラグイン名> へ解決した上で selector.EnsureTrackPluginLoaded で
// 読込キャッシュへ確保する。EnableCant/LiftRailSurface/MultiTrack/TrackNum/
// TrackInterval は適用しない(敷設パラメータへ反映しない)。
RailwayPluginSetApplyResult ApplyRailwayPluginSetFile(const std::string& setPathUtf8,
                                                       PluginSelector& selector,
                                                       const std::string& pluginRootUtf8 = {});

} // namespace rs::app
