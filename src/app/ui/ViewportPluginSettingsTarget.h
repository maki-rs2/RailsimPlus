//
// ViewportPluginSettingsTarget.h - プラグイン設定パネルの対象のクリック判定と赤い枠
//
// 「開くきっかけ」と「開いている対象の枠」のどちらも
// EditorViewportContext::pluginSettingsLinkの値だけで有効かどうかが決まる(表示モードでは分岐しない)。
// Editorのhovered*/selected*には書かない。
//
#pragma once

#include "../gpu/CameraMath.h"
#include "../input/InputTypes.h"
#include "EditorViewport.h"

namespace rs::app {

// ビューポートの個体(情景の個体・編成の車両)をクリックしたら、その個体のIDをlinkのclickedEntityIdへ書く。
// 当たらなければ何もしない。
void HandlePluginSettingsClick(const EditorViewportContext& ctx, const ViewProjResult& vpr,
                               rs::app::input::PointerPos mouse);

// パネルが開いている対象(linkのtargetEntityId)のまわりに、赤く点滅する枠を描く。
void DrawPluginSettingsTargetBox(const EditorViewportContext& ctx);

} // namespace rs::app
