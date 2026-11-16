//
// Rs2StationParentLookup.h - .rs2 の駅配置から、駅プラグインの Platform の ParentObject を引く。
//
// 駅プラグインの定義は既存の定義キャッシュ(LoadPluginDefinition)から読む。新しい解析経路は持たない。
//
#pragma once

#include <string>

#include "../../plugins/layout/Rs2PlatformParent.h"

namespace rs::app {

// installRootUtf8 配下の Station/<pluginName> を引く関数を返す。同じ駅プラグインは1回だけ調べる
// (返した関数の寿命のあいだ)。親を持つ番線が無い駅・プラグインが読めない駅は nullopt。
rs::legacy_layout::StationParentLookup MakeStationParentLookup(const std::string& installRootUtf8);

} // namespace rs::app
