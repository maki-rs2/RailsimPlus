//
// TrainPlacementInput.h - Trainモードのクリック配置入力
//
#pragma once

#include "../input/InputTypes.h"
#include "../gpu/CameraMath.h"
#include "EditorViewport.h"

namespace rs::app {

// Trainモードのクリック配置/選択/削除入力。マウス直下の実レールedge上の位置
// (edge, s)をホバーハイライト(Editor::hoveredEdge)として提示し、左クリックで
// その位置を先頭車として選択中の編成を配置する。
// trainHovered(=HandleTrainHoverInputの戻り値)がtrueのときは配置せず、
// クリックでその配置済み編成を選択(Editor::selectedSceneEntityId)する。
// 選択中はDeleteキーでその編成を丸ごと撤去する。
void HandleTrainPlacementInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool placementEnabled, bool trainHovered);

// 配置前の編成プレビュー(実際の車両モデルの半透明表示)をビューポートへ反映する。
// HandleTrainPlacementInputの直後に毎フレーム呼ぶ。preview.validが偽(Trainモード外・
// マウスがレール上にない・編成が空)のときは消す。モデルのロードとGPUアップロードは
// 編成の中身が変わったときだけ行う。
void SyncTrainPlacementPreview(const EditorViewportContext& ctx);

} // namespace rs::app
