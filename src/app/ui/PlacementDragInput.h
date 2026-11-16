//
// PlacementDragInput.h - 配置物(Pier/Pole)のホバー/選択/ドラッグ移動
//
// RailモードとSceneryモードの両方から呼ばれる
// (情景モードでもedgeリンクを保ったまま配置物を自由に動かせるよう、
// モード非依存の関数として独立させた)。
//
#pragma once

#include "../input/InputTypes.h"
#include "../gpu/CameraMath.h"
#include "EditorViewport.h"

namespace rs::app {

// 1フレーム分の配置物ホバー判定・クリック選択・ドラッグ移動(edge沿いs＋左右
// lateralOffsetMの2軸)を処理する。
// 戻り値: このフレームの左クリック(leftClickForEditor)が配置物選択で消費された
// (=配置物にヒットして選択した)か。呼び出し側はfalseのときだけ他の選択
// (node/edge選択、SceneEntity選択)へフォールバックする。配置物にヒットせず
// クリックした場合は選択を解除してfalseを返す(呼び出し側が重複してクリアする
// 必要はない)。ドラッグ中/ドラッグ開始判定など、クリック以外の処理も内部で
// 行うが、それらは常にfalseを返す(呼び出し側はleftClickForEditor==trueの
// ときだけこの戻り値を見ればよい)。
bool HandlePlacementPickAndDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                                rs::app::input::PointerPos mouse, bool leftClickForEditor);

} // namespace rs::app
