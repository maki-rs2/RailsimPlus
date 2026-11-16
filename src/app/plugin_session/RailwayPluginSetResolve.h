//
// RailwayPluginSetResolve.h - RailwayPluginSet\*.txt(敷設テンプレート)1本のパースと、
// Check=yesの各要素のフォルダ解決だけを行う純粋な関数。PluginSelectorは触らない。
//
// 適用(ApplyRailwayPluginSetFile)と、適用せず内容だけ見る表示(サムネイル)の両方が使う。
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

struct RailwayPluginSetResolveResult {
    bool ok = false;  // パースでき、敷設テンプレートの形式だったか
    std::string settingName;
    // Check=yesで、フォルダ名を解決できた要素の絶対パス(Rail/Tie/Girder/Pier/Line/Poleの順)。
    // 読み込めるかは確かめない。
    std::vector<std::string> folders;
    std::vector<std::string> warnings;
};

// pluginRootUtf8が空なら、setPathUtf8の親の親をinstallRootとする。
RailwayPluginSetResolveResult ResolveRailwayPluginSetFile(const std::string& setPathUtf8,
                                                          const std::string& pluginRootUtf8 = {});

} // namespace rs::app
