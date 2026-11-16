#pragma once

#include "../gpu/CameraMath.h"
#include "EditorViewport.h"

namespace rs::app {
void DrawEditorOverlay(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                       float mouseX, float mouseY);
void DrawSceneEntityOverlay(const EditorViewportContext& ctx, const ViewProjResult& viewProj);
} // namespace rs::app
