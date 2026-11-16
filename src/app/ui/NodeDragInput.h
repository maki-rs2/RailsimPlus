//
// NodeDragInput.h - 通常Selectモードの「押す→動かす→離す」ドラッグ(接線ハンドル・ノード本体)
//
// 押した瞬間に掴んだ対象をEditor::selectPressへ記録し(LatchSelectPress)、ドラッグの開始は
// その記録で決める。開始時のホバーや選択は見ない。
// ViewportInputModes.cppのHandleSelectModeInputだけが使う。
//
#pragma once

#include "../gpu/CameraMath.h"
#include "../input/InputSource.h"
#include "EditorViewport.h"

namespace rs::app {

// 左ボタンを押した瞬間に、掴んだ対象(接線ハンドル / ノード / なし)をEditor::selectPressへ記録する。
// 押すたびに前回の記録は捨てる。leftClickForEditorが偽(Shift押下など)なら対象なしのままにする。
void LatchSelectPress(rs::editor::Editor& editor, const rs::app::input::InputSource& input,
                      bool leftClickForEditor);

// 接線ハンドルのドラッグ(開始・更新・確定)。このフレームで処理したらtrueを返す。
bool UpdateTangentDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                       rs::app::input::PointerPos mouse);

// ノード本体のドラッグ(開始・更新・確定)。このフレームで処理したらtrueを返す。
bool UpdateNodeDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                    rs::app::input::PointerPos mouse);

} // namespace rs::app
