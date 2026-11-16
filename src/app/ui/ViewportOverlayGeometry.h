#pragma once

#include "../gpu/CameraMath.h"
#include "EditorViewport.h"

#include "../../core/DebugDraw.h"

namespace rs::app {
// ノードに関する線(輪・ガイド・架線の接続点の強調など)の太いスタイル。
rs::core::DebugStyle MakeNodeLineStyle(uint32_t color, float width);
// 注記の線・点の共通のスタイル。自身のレールのメッシュに埋まって薄くならないよう、深度テストはしない。
inline rs::core::DebugStyle MakeTopStyle(uint32_t color, float width) {
    rs::core::DebugStyle style;
    style.color = color;
    style.widthPx = width;
    style.depthMode = rs::core::DebugDepthMode::AlwaysOnTop;
    return style;
}
// 点から真下の基準面(Y=0)まで線を引き、足元の丸と高さの文字を描く(敷設・配置のプレビュー共通)。
void DrawHeightLine(const EditorViewportContext& ctx, const rs::core::Vec3& point);
void DrawTrackNodeGeometry(const EditorViewportContext& ctx);
void DrawArcToolPreviewGeometry(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                                float mouseX, float mouseY);
} // namespace rs::app
