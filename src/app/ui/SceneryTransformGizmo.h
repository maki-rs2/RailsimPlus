#pragma once

#include "EditorViewport.h"

namespace rs::app {

// Gizmo上のドラッグを更新し、離した時点で1つのSceneEntityCommandに確定する。
bool HandleSceneryTransformGizmo(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                                 rs::app::input::PointerPos mouse);

// 選択した情景の中心へ移動軸と回転リングを描く。
void DrawSceneryTransformGizmo(const EditorViewportContext& ctx, const ViewProjResult& viewProj);

} // namespace rs::app
