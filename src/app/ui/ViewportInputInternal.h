#pragma once

// HandleEditorInput()のモード分岐本体を複数TUへ分割するための内部専用ヘッダ。
// ViewportInput.h(EditorViewport.cpp等から広く参照される公開ヘッダ)には入れず、
// ViewportInput.cpp / ViewportInputModes.cpp の2TUだけがincludeする。

#include "../gpu/CameraMath.h"
#include "ViewportInput.h"

namespace rs::app {

// 円弧敷設モード: 左クリック確定・右クリック取消(ViewportInput.cppより分割)。
void HandlePlaceArcModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool leftClickForEditor);

// 区間分割モード: 境界ドラッグ・分割クリック・edge再選択(ViewportInput.cppより分割)。
bool HandleSectionSplitModeInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool leftClickForEditor);

// 配置物手動追加モード(複線中心線retarget含む、ViewportInput.cppより分割)。
void HandlePlacementAddModeInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool leftClickForEditor);

void HandleStationAnchorModeInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                  bool leftClickForEditor);

void HandleSceneryPlacementModeInput(const EditorViewportContext& ctx,
                                     const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                     bool leftClickForEditor);

// 架線編集モード: 接続点を2つ結ぶ接続と、径間のホバー・選択(ViewportWireEditInput.cpp)。
void HandleWireSpanModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool leftClickForEditor);

// 架線編集モードで選択中の径間1本を切る(Undo対象)。選択が無ければ何もせずfalse。
bool DeleteSelectedWireSpan(const EditorViewportContext& ctx);

// 通常Selectモード: ノードドラッグ + 配置物/node/edge選択(ViewportInput.cppより分割)。
void HandleSelectModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                           rs::app::input::PointerPos mouse, bool leftClickForEditor);

} // namespace rs::app
