#pragma once

#include "EditorViewport.h"

namespace rs::app {

// 架線編集モード(接続点を2つ結ぶ方式)の表示。接続点の丸と、ホバー・選択中の径間の太線。
// モードが無効なら何もしない。
void DrawWireEditOverlay(const EditorViewportContext& ctx);

} // namespace rs::app
