//
// ViewportRailMeshBuild.h - 敷設エディタビューポートのレール系メッシュ再構築
//
// Rail/Tie/Girderの連続押し出しメッシュ・Line(架線)ワイヤーの再構築を担う。
// g_editor.revision等の変化を検知したTickEditorViewportから呼ばれる。
//
#pragma once

#include "EditorViewport.h"

namespace rs::app {

// Rail/Tie/Girderの連続押し出しメッシュ + Line(架線)ワイヤーの再構築(g_editor.revision等が
// 変化した際にメインのメッシュパイプラインへ上げる1枚のmergedメッシュを作る)。
void RebuildExtrudedRailMesh(const EditorViewportContext& ctx);

// 架線1径間ぶん(ワイヤー+ハンガー)のメッシュ。実際の架線と同じ生成(AppendWireAndHangers)で、
// 配置プレビューの半透明表示に使う。baseA/baseBは接続点の位置、upA/upBは柱の上方向。
rs::render::Mesh BuildWireSpanMesh(const TrackPluginEntry& lineEntry, const rs::core::Vec3& baseA,
                                   const rs::core::Vec3& upA, const rs::core::Vec3& baseB,
                                   const rs::core::Vec3& upB, HangerModelCache& hangerModelCache);

} // namespace rs::app
