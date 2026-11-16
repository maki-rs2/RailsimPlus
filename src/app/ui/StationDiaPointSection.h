//
// StationDiaPointSection.h - 駅・ダイヤパネルのポイント(分岐)一覧
//
#pragma once

#include "../action/StationDiaAction.h"

namespace rs::app {

// ポイントごとに、分岐している面の左右どちらを開通させるかを選ぶラジオを並べる。
void DrawStationDiaPointSection(StationDiaAction& action,
                                const std::vector<StationDiaPointRowViewModel>& points);

}  // namespace rs::app
