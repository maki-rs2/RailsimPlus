//
// EdgePick.h - レールを選ぶ操作(クリック・Shift+クリック・矩形)の結果と、その反映
//
// どのモード(レール・信号・ダイヤ)でも、選ぶ操作は同じ形のイベントになる(UpdateEdgePick、
// src/app/ui/EdgePickInput.h)。各モードはイベントを、自分の対象(エディタの選択、閉塞のレール、
// 番線のレール)へ反映するだけで、入力の判定は共有する。反映の規則はここの関数に1つだけ置く。
//
#pragma once

#include <vector>

#include "../../editor/tool/Editor.h"

namespace rs::app {

struct EdgePickEvent {
    // Click: 修飾なしのクリック(押した瞬間)。ShiftTap: Shift+クリック(ほとんど動かさずに離した時点)。
    // Box: 四角を描いて離した時点。
    enum class Kind { None, Click, ShiftTap, Box };
    Kind kind = Kind::None;
    // Click・ShiftTapは1本、Boxは四角にかかった実体のレール。
    std::vector<rs::world::Guid> edges;

    bool Any() const { return kind != Kind::None; }
};

// レールの一覧(閉塞・番線の範囲)へ反映した結果を返す。
// Click・ShiftTapは各レールの追加と除去の切り替え、Boxは含まれていないレールだけの追加。
std::vector<rs::world::Guid> ApplyEdgePickToList(std::vector<rs::world::Guid> current,
                                                 const EdgePickEvent& event);

// エディタのレール選択へ反映する。Clickは1本だけの選択に置き換え、ShiftTapは追加・除去、
// Boxは置き換える。
void ApplyEdgePickToSelection(rs::editor::Editor& editor, const EdgePickEvent& event);

} // namespace rs::app
