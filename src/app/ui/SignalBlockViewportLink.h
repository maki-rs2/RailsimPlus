#pragma once

#include <optional>
#include <vector>

#include "../workspace/RailLabelHover.h"
#include "../../world/alignment/BlockConnectivity.h"

namespace rs::app {

// 入力側が毎フレーム更新し、オーバーレイとラベルの表示側が読む。
struct SignalBlockViewportLink {
    std::optional<workspace::RailLabelAnchor> hoveredRail;
    // オーバーレイで閉塞ごとに1回だけ計算し、ラベルも次のTickで読む。
    std::vector<rs::world::BlockConnectivity> connectivity;
};

} // namespace rs::app
