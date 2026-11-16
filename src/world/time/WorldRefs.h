//
// WorldRefs.h - GameClock/SolarOverridesなど「ワールド共有状態」への参照束。
//
// main.cppが所有するグローバル状態(g_gameClock/g_solarOverrides)をUIパネルの
// context構造体(ModePanelsContext/GameSettingsPanelContext)へ渡すための
// 集約点。新しい共有状態(天候・季節等)を追加する際は、このstructへ
// フィールドを1つ足すだけで両contextへ伝搬する。
//
#pragma once

#include "GameClock.h"
#include "SolarPosition.h"

namespace rs::world {

struct WorldRefs {
    GameClock& gameClock;
    SolarOverrides& solarOverrides;
};

} // namespace rs::world
