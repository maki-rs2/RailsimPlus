//
// EdgePickInput.h - レールを選ぶ入力(クリック・Shift+クリック・矩形)の判定
//
// レール・信号・ダイヤのどのモードも、この1つの関数で入力をEdgePickEventへ変える。
// モードごとの違いは、イベントの反映先(EdgePick.hのApplyEdgePickTo*)だけにする。
// 矩形は空き地から左ドラッグで描く。Shift+左ドラッグはカメラの高さ調節なので、Shiftを押していない
// ときだけ使う。Shift+クリックはカメラと衝突しないよう、ほとんど動かさずに離した時点で判定する。
//
#pragma once

#include "../action/EdgePick.h"
#include "../gpu/CameraMath.h"
#include "../input/InputSource.h"
#include "EditorViewport.h"

namespace rs::app {

struct EdgePickInput {
    // マウスが乗っているレール(無ければNil)。Shift+クリックの対象になる。
    // 呼び出し側が、そのモードで選べない状況(ほかの操作を優先するときなど)ではNilを渡す。
    rs::world::Guid hoveredEdge = rs::world::NilGuid();
    // trueなら、修飾なしのクリックもClickとして返し、空き地を押した瞬間の判定も内部で行う
    // (クリックで即座に対象を決めるモード用)。falseならClickは返さず、空き地を押したかは
    // pressedOnEmptyで渡す(レールモードはノードや配置物を優先するため自分で判定する)。
    bool reportPlainClick = false;
    bool pressedOnEmpty = false;
};

// 毎フレーム呼ぶ。イベントが起きたフレームだけKindがNone以外になる。
EdgePickEvent UpdateEdgePick(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, const EdgePickInput& pick);

// ビューポート外・UIの上で離したときなど、入力処理が走らなかった場合に待ち状態を捨てる。
void CancelEdgePickIfReleased(const EditorViewportContext& ctx);

// 描いている最中の四角を画面に描く。
void DrawBoxSelect(const rs::editor::Editor& editor);

} // namespace rs::app
