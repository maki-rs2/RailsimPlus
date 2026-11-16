//
// TimeControlViewModel.h - ゲーム内時計から、時間操作の表示値を作る
//
#pragma once

#include "../workspace/TimeControlPresentation.h"
#include "../../world/time/GameClock.h"

namespace rs::app {

// 年月日・時分・倍率をclockの今の値にし、段の強調と日時の文字を作り直す。表示値が変わったときだけtrue。
// 入力欄の下書き(窓・倍率)には触れない。
bool FillTimeControlFromClock(workspace::TimeControlPresentation& control, const rs::world::GameClock& clock);

} // namespace rs::app
