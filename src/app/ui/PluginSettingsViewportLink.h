//
// PluginSettingsViewportLink.h - プラグイン設定パネルとビューポートの受け渡し
//
#pragma once

#include <string>

#include "../../world/alignment/Guid.h"

namespace rs::app {

// ワークスペースのプラグイン設定パネルとビューポートの受け渡し。
// 書き込みはWorkspaceEditorBridge::Tickとビューポート(ViewportPluginSettingsTarget)の2者だけ。
struct PluginSettingsViewportLink {
    bool clickEnabled = false;   // in: このフレームのクリックでパネルを開けるか
    rs::world::Guid targetEntityId = rs::world::NilGuid();  // in: パネルが開いている対象(閉じていれば無効値)。赤い枠を描く
    rs::world::Guid clickedEntityId = rs::world::NilGuid(); // out: クリックで当たった個体(当たらなければ無効値のまま)
    bool clickedEmpty = false;   // out: クリックが何にも当たらなかった(パネルを閉じる合図)
};

} // namespace rs::app
