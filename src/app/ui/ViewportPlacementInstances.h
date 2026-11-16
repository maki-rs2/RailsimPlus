//
// ViewportPlacementInstances.h - 敷設エディタビューポートの離散配置インスタンス反映
//
// 離散配置(Pier/Pole/Scenery)を自動配置+手動オーバーレイ適用後の一覧として求め、
// InstanceBatchManagerへ登録する。g_editor.revision等の変化を検知した
// TickEditorViewportから呼ばれる。
//
#pragma once

#include "EditorViewport.h"

namespace rs::app {

// 離散配置(Pier/Pole/Scenery)のGPUインスタンスバッチ反映(自動配置+手動オーバーレイ適用後の
// 一覧をInstanceBatchManagerへ登録し、placementCache/ピッキング用キャッシュも更新する)。
void RebuildDiscretePlacementInstances(const EditorViewportContext& ctx);

} // namespace rs::app
