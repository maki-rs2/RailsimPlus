//
// TrainPlacementHighlight.h - 編成を置こうとしている先頭位置の横棒
//
// TrainPlacementUiState::preview(TrainPlacementInputが計算済み)の先頭位置に、
// レールに直交する横棒を描く。置けるなら緑、他の編成と重なるなら赤。
// 編成の車両モデルの半透明表示は SceneRenderer::SetTrainPlacementPreview が受け持つ。
//
#pragma once

#include "EditorViewport.h"

namespace rs::app {

void DrawTrainPlacementHighlight(const EditorViewportContext& ctx);

} // namespace rs::app
